#pragma once

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

enum class GfxDocumentError
{
    truncated,
    invalid_signature,
    unsupported_compression,
    length_mismatch,
    missing_exporter_info,
    unsupported_exporter_version,
    invalid_tag_stream,
};

struct GfxRect
{
    std::int32_t x_min_twips = 0;
    std::int32_t x_max_twips = 0;
    std::int32_t y_min_twips = 0;
    std::int32_t y_max_twips = 0;
};

struct GfxExporterInfo
{
    std::uint16_t          version       = 0;
    std::uint32_t          flags         = 0;
    std::uint16_t          bitmap_format = 0;
    std::vector<std::byte> prefix;
    std::vector<std::byte> swf_name;
};

struct GfxExternalImage
{
    std::uint16_t          character_id  = 0;
    std::uint16_t          bitmap_format = 0;
    std::uint16_t          target_width  = 0;
    std::uint16_t          target_height = 0;
    std::vector<std::byte> export_name;
    std::vector<std::byte> file_name;
};

struct GfxDocument
{
    std::uint8_t                  file_version    = 0;
    std::uint32_t                 declared_length = 0;
    GfxRect                       frame;
    std::uint16_t                 frame_rate_raw = 0;
    std::uint16_t                 frame_count    = 0;
    GfxExporterInfo               exporter;
    std::vector<GfxExternalImage> external_images;
};

[[nodiscard]] std::string_view GfxDocumentErrorMessage(GfxDocumentError error);

[[nodiscard]] std::expected<GfxDocument, GfxDocumentError>
ParseGfxDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<GfxDocument, std::string>
LoadGfxDocument(const std::filesystem::path& path);

} // namespace rerevved::studio
