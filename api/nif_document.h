#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
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

struct NifMaterialPropertyInventory
{
    std::uint32_t        block_index = 0;
    std::array<float, 3> ambient_color{};
    std::array<float, 3> diffuse_color{};
    std::array<float, 3> specular_color{};
    std::array<float, 3> emissive_color{};
    float                glossiness = 0.0F;
    float                alpha      = 0.0F;
};

struct NifTextureTransformInventory
{
    std::array<float, 2> translation{};
    std::array<float, 2> scale{};
    float                rotation         = 0.0F;
    std::uint32_t        transform_method = 0;
    std::array<float, 2> center{};
};

struct NifTexDescInventory
{
    std::uint32_t                               source                = UINT32_MAX;
    std::uint16_t                               flags                 = 0;
    std::uint8_t                                has_texture_transform = 0;
    std::optional<NifTextureTransformInventory> transform;
};

struct NifBumpTextureInventory
{
    float                luma_scale  = 0.0F;
    float                luma_offset = 0.0F;
    std::array<float, 4> bump_matrix{};
};

struct NifTextureSlotInventory
{
    std::uint8_t                           presence = 0;
    std::optional<NifTexDescInventory>     descriptor;
    std::optional<NifBumpTextureInventory> bump;
    std::optional<float>                   parallax_offset;
};

struct NifShaderTextureInventory
{
    std::uint8_t                       has_map = 0;
    std::optional<NifTexDescInventory> descriptor;
    std::optional<std::uint32_t>       map_id;
};

struct NifTexturingPropertyInventory
{
    std::uint32_t                          block_index   = 0;
    std::uint16_t                          flags         = 0;
    std::uint32_t                          texture_count = 0;
    std::array<NifTextureSlotInventory, 9> standard_slots;
    std::uint32_t                          shader_texture_count = 0;
    std::vector<NifShaderTextureInventory> shader_textures;
};

struct NifSourceTextureInventory
{
    std::uint32_t block_index         = 0;
    std::uint8_t  use_external        = 0;
    std::uint32_t file_name_index     = UINT32_MAX;
    std::uint32_t pixel_data          = UINT32_MAX;
    std::uint32_t pixel_layout        = 0;
    std::uint32_t use_mipmaps         = 0;
    std::uint32_t alpha_format        = 0;
    std::uint8_t  is_static           = 0;
    std::uint8_t  direct_render       = 0;
    std::uint8_t  persist_render_data = 0;

    [[nodiscard]] bool IsSupportedExternalSource() const noexcept
    {
        return use_external == 1 && file_name_index != UINT32_MAX && pixel_data == UINT32_MAX;
    }
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
    std::uint32_t                              version           = 0;
    std::uint8_t                               endian            = 0;
    std::uint32_t                              user_version      = 0;
    std::size_t                                header_size       = 0;
    std::uint32_t                              max_string_length = 0;
    std::vector<std::vector<std::byte>>        block_types;
    std::vector<NifBlock>                      blocks;
    std::vector<std::vector<std::byte>>        strings;
    std::vector<std::uint32_t>                 groups;
    std::vector<std::uint32_t>                 roots;
    std::vector<NifNode>                       nodes;
    std::vector<NifTriShape>                   tri_shapes;
    std::vector<NifTriShapeDataInventory>      tri_shape_data;
    std::vector<NifMaterialPropertyInventory>  material_properties;
    std::vector<NifTexturingPropertyInventory> texturing_properties;
    std::vector<NifSourceTextureInventory>     source_textures;
};

[[nodiscard]] std::string_view NifDocumentErrorMessage(NifDocumentError error);

[[nodiscard]] std::expected<NifDocument, NifDocumentError>
ParseNifDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<NifDocument, std::string>
LoadNifDocument(const std::filesystem::path& path);

} // namespace rerevved::studio
