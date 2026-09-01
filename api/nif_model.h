#pragma once

#include "nif_document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace rerevved::studio
{

enum class NifModelError
{
    scene_cycle,
    wrong_data_block_type,
    inconsistent_geometry,
    triangle_selector_out_of_range,
    invalid_transform,
    no_supported_meshes,
};

struct NifModelTransform
{
    std::array<double, 3> translation{};
    std::array<double, 9> rotation{ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    double                scale = 1.0;
};

struct NifModelMesh
{
    std::size_t                root_index = 0;
    std::vector<std::size_t>   node_path;
    std::size_t                tri_shape_index      = 0;
    std::size_t                tri_shape_data_index = 0;
    std::vector<std::uint32_t> material_property_blocks;
    NifModelTransform          world_transform;
};

struct NifModel
{
    std::vector<NifModelMesh> meshes;
};

[[nodiscard]] std::string_view NifModelErrorMessage(NifModelError error);

[[nodiscard]] std::expected<std::array<double, 3>, NifModelError>
ApplyNifModelTransform(const NifModelTransform&    transform,
                       const std::array<float, 3>& position);

[[nodiscard]] std::expected<NifModel, NifModelError>
AssembleNifModel(const NifDocument& document);

} // namespace rerevved::studio
