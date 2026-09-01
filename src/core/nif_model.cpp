#include "nif_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::studio
{
namespace
{

constexpr std::uint32_t kNullReference = std::numeric_limits<std::uint32_t>::max();

bool Equals(std::span<const std::byte> bytes, std::string_view value)
{
    return bytes.size() == value.size() &&
           std::ranges::equal(bytes, std::as_bytes(std::span(value.data(), value.size())));
}

bool BlockHasType(const NifDocument& document,
                  std::uint32_t      block_index,
                  std::string_view   expected_type)
{
    if (block_index >= document.blocks.size())
        return false;
    const auto type_index = document.blocks[block_index].type_index;
    return type_index < document.block_types.size() &&
           Equals(document.block_types[type_index], expected_type);
}

template <typename Value>
std::optional<std::size_t> FindParsedBlock(const std::vector<Value>& values,
                                           std::uint32_t             block_index)
{
    const auto found = std::ranges::find(values, block_index, &Value::block_index);
    if (found == values.end())
        return std::nullopt;
    return static_cast<std::size_t>(std::distance(values.begin(), found));
}

std::optional<NifModelError> ValidateGeometry(const NifTriShapeDataInventory& data)
{
    if (data.has_vertices == 0 || data.vertex_positions.empty() ||
        data.vertex_positions.size() != data.vertex_count || data.has_triangles == 0 ||
        data.triangles.empty() || data.triangles.size() != data.triangle_count ||
        (data.has_normals == 0 && !data.normal_vectors.empty()) ||
        (data.has_normals != 0 &&
         data.normal_vectors.size() != data.vertex_positions.size()))
        return NifModelError::inconsistent_geometry;

    for (const auto& triangle : data.triangles)
    {
        for (const auto selector : triangle)
        {
            if (selector >= data.vertex_positions.size())
                return NifModelError::triangle_selector_out_of_range;
        }
    }
    return std::nullopt;
}

std::array<double, 3> Rotate(const std::array<double, 9>& rotation,
                             const std::array<double, 3>& value)
{
    return {
        rotation[0] * value[0] + rotation[3] * value[1] + rotation[6] * value[2],
        rotation[1] * value[0] + rotation[4] * value[1] + rotation[7] * value[2],
        rotation[2] * value[0] + rotation[5] * value[1] + rotation[8] * value[2],
    };
}

bool IsFinite(const NifModelTransform& transform)
{
    return std::ranges::all_of(transform.translation, [](double value)
                               {
                                   return std::isfinite(value);
                               }) &&
           std::ranges::all_of(transform.rotation, [](double value)
                               {
                                   return std::isfinite(value);
                               }) &&
           std::isfinite(transform.scale);
}

std::expected<NifModelTransform, NifModelError> LocalTransform(const NifAvObject& object)
{
    NifModelTransform transform;
    std::ranges::copy(object.translation, transform.translation.begin());
    std::ranges::copy(object.rotation, transform.rotation.begin());
    transform.scale = object.scale;
    if (!IsFinite(transform))
        return std::unexpected(NifModelError::invalid_transform);
    return transform;
}

std::expected<NifModelTransform, NifModelError>
Compose(const NifModelTransform& parent, const NifModelTransform& local)
{
    NifModelTransform result;
    result.scale = parent.scale * local.scale;
    for (std::size_t column = 0; column < 3; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            result.rotation[column * 3 + row] =
                parent.rotation[row] * local.rotation[column * 3] +
                parent.rotation[3 + row] * local.rotation[column * 3 + 1] +
                parent.rotation[6 + row] * local.rotation[column * 3 + 2];
        }
    }

    const auto rotated_translation = Rotate(parent.rotation, local.translation);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        result.translation[axis] =
            parent.translation[axis] + parent.scale * rotated_translation[axis];
    }
    if (!IsFinite(result))
        return std::unexpected(NifModelError::invalid_transform);
    return result;
}

struct TraversalItem
{
    std::uint32_t block_index  = kNullReference;
    std::size_t   node_index   = 0;
    bool          leaving_node = false;
};

} // namespace

std::string_view NifModelErrorMessage(NifModelError error)
{
    switch (error)
    {
        case NifModelError::scene_cycle:
            return "The NIF scene graph contains a cycle.";
        case NifModelError::wrong_data_block_type:
            return "A NiTriShape data reference does not target NiTriShapeData.";
        case NifModelError::inconsistent_geometry:
            return "The retained NIF geometry arrays are inconsistent.";
        case NifModelError::triangle_selector_out_of_range:
            return "A NIF triangle selector is outside the retained vertex positions.";
        case NifModelError::invalid_transform:
            return "A NIF scene transform is non-finite or unrepresentable.";
        case NifModelError::no_supported_meshes:
            return "The NIF scene contains no supported meshes.";
    }
    return "The NIF model could not be assembled.";
}

std::expected<std::array<double, 3>, NifModelError>
ApplyNifModelTransform(const NifModelTransform&    transform,
                       const std::array<float, 3>& position)
{
    const std::array<double, 3> source{ position[0], position[1], position[2] };
    const auto                  rotated = Rotate(transform.rotation, source);
    std::array<double, 3>       result{};
    for (std::size_t axis = 0; axis < result.size(); ++axis)
        result[axis] = transform.translation[axis] + transform.scale * rotated[axis];
    if (!std::ranges::all_of(result, [](double value)
                             {
                                 return std::isfinite(value);
                             }))
        return std::unexpected(NifModelError::invalid_transform);
    return result;
}

std::expected<NifModel, NifModelError> AssembleNifModel(const NifDocument& document)
{
    NifModel          model;
    std::vector<bool> active_nodes(document.nodes.size());

    for (std::size_t root_index = 0; root_index < document.roots.size(); ++root_index)
    {
        std::vector<std::size_t>   node_path;
        std::vector<TraversalItem> stack{ { document.roots[root_index], 0, false } };

        while (!stack.empty())
        {
            const auto item = stack.back();
            stack.pop_back();
            if (item.leaving_node)
            {
                active_nodes[item.node_index] = false;
                node_path.pop_back();
                continue;
            }
            if (item.block_index == kNullReference)
                continue;

            if (BlockHasType(document, item.block_index, "NiNode"))
            {
                const auto node_index = FindParsedBlock(document.nodes, item.block_index);
                if (!node_index)
                    continue;
                if (active_nodes[*node_index])
                    return std::unexpected(NifModelError::scene_cycle);

                active_nodes[*node_index] = true;
                node_path.push_back(*node_index);
                stack.push_back({ kNullReference, *node_index, true });
                const auto& children = document.nodes[*node_index].children;
                for (auto child = children.rbegin(); child != children.rend(); ++child)
                    stack.push_back({ *child, 0, false });
                continue;
            }

            if (!BlockHasType(document, item.block_index, "NiTriShape"))
                continue;
            const auto shape_index = FindParsedBlock(document.tri_shapes, item.block_index);
            if (!shape_index)
                continue;
            const auto& shape = document.tri_shapes[*shape_index];
            if (shape.data == kNullReference)
                continue;
            if (!BlockHasType(document, shape.data, "NiTriShapeData"))
                return std::unexpected(NifModelError::wrong_data_block_type);
            const auto data_index = FindParsedBlock(document.tri_shape_data, shape.data);
            if (!data_index)
                return std::unexpected(NifModelError::inconsistent_geometry);
            if (const auto error = ValidateGeometry(document.tri_shape_data[*data_index]))
                return std::unexpected(*error);

            NifModelMesh mesh;
            mesh.root_index           = root_index;
            mesh.node_path            = node_path;
            mesh.tri_shape_index      = *shape_index;
            mesh.tri_shape_data_index = *data_index;
            auto world_transform      = LocalTransform(shape.object);
            if (!world_transform)
                return std::unexpected(world_transform.error());
            for (auto node = node_path.rbegin(); node != node_path.rend(); ++node)
            {
                const auto parent_transform = LocalTransform(document.nodes[*node].object);
                if (!parent_transform)
                    return std::unexpected(parent_transform.error());
                world_transform = Compose(*parent_transform, *world_transform);
                if (!world_transform)
                    return std::unexpected(world_transform.error());
            }
            mesh.world_transform = *world_transform;
            for (const auto property : shape.object.properties)
            {
                if (property != kNullReference &&
                    BlockHasType(document, property, "NiMaterialProperty"))
                    mesh.material_property_blocks.push_back(property);
            }
            model.meshes.push_back(std::move(mesh));
        }
    }

    if (model.meshes.empty())
        return std::unexpected(NifModelError::no_supported_meshes);
    return model;
}

} // namespace rerevved::studio
