#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::studio
{

enum class NifDocumentError
{
    truncated,
    invalid_signature,
    unsupported_version,
    unsupported_endian,
    unsupported_user_version,
    invalid_layout,
};

struct NifBlock
{
    std::uint16_t type_index = 0;
    std::uint32_t size       = 0;
    std::size_t   offset     = 0;
};

struct NifAvObject
{
    std::uint32_t              name_index = UINT32_MAX;
    std::vector<std::uint32_t> extra_data;
    std::uint32_t              controller = UINT32_MAX;
    std::uint16_t              flags      = 0;
    std::array<float, 3>       translation{};
    std::array<float, 9>       rotation{};
    float                      scale = 1.0F;
    std::vector<std::uint32_t> properties;
    std::uint32_t              collision_object = UINT32_MAX;
};

struct NifNode
{
    std::uint32_t              block_index = 0;
    NifAvObject                object;
    std::vector<std::uint32_t> children;
    std::vector<std::uint32_t> effects;
};

struct NifMaterialData
{
    std::vector<std::uint32_t> name_indices;
    std::vector<std::int32_t>  extra_data;
    std::int32_t               active_material       = -1;
    std::uint8_t               material_needs_update = 0;
};

struct NifTriShape
{
    std::uint32_t   block_index = 0;
    NifAvObject     object;
    std::uint32_t   data          = UINT32_MAX;
    std::uint32_t   skin_instance = UINT32_MAX;
    NifMaterialData material;
};

struct NifTriShapeDataInventory
{
    std::uint32_t                             block_index    = 0;
    std::int32_t                              group_id       = 0;
    std::uint16_t                             vertex_count   = 0;
    std::uint8_t                              keep_flags     = 0;
    std::uint8_t                              compress_flags = 0;
    std::uint8_t                              has_vertices   = 0;
    std::vector<std::array<float, 3>>         vertex_positions;
    std::uint16_t                             data_flags  = 0;
    std::uint8_t                              has_normals = 0;
    std::vector<std::array<float, 3>>         normal_vectors;
    std::array<float, 3>                      bound_center{};
    float                                     bound_radius         = 0.0F;
    std::uint8_t                              has_vertex_colors    = 0;
    std::uint16_t                             consistency_flags    = 0;
    std::uint32_t                             additional_data      = UINT32_MAX;
    std::uint16_t                             triangle_count       = 0;
    std::uint32_t                             triangle_point_count = 0;
    std::uint8_t                              has_triangles        = 0;
    std::vector<std::array<std::uint16_t, 3>> triangles;
    std::uint16_t                             match_group_count = 0;
    std::vector<std::vector<std::uint16_t>>   normal_sharing_groups;
};

struct NifDocument
{
    std::uint32_t                         version           = 0;
    std::uint8_t                          endian            = 0;
    std::uint32_t                         user_version      = 0;
    std::size_t                           header_size       = 0;
    std::uint32_t                         max_string_length = 0;
    std::vector<std::vector<std::byte>>   block_types;
    std::vector<NifBlock>                 blocks;
    std::vector<std::vector<std::byte>>   strings;
    std::vector<std::uint32_t>            groups;
    std::vector<std::uint32_t>            roots;
    std::vector<NifNode>                  nodes;
    std::vector<NifTriShape>              tri_shapes;
    std::vector<NifTriShapeDataInventory> tri_shape_data;
};

[[nodiscard]] std::string_view NifDocumentErrorMessage(NifDocumentError error);

[[nodiscard]] std::expected<NifDocument, NifDocumentError>
ParseNifDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<NifDocument, std::string>
LoadNifDocument(const std::filesystem::path& path);

} // namespace rerevved::studio
