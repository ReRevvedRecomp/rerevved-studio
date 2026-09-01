#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace rerevved::studio
{

enum class DdsMetadataError
{
    truncated,
    invalid_signature,
    invalid_header_size,
    invalid_pixel_format_size,
};

struct DdsPixelFormat
{
    std::uint32_t            flags = 0;
    std::array<std::byte, 4> four_cc{};
    std::uint32_t            rgb_bit_count = 0;
    std::uint32_t            red_mask      = 0;
    std::uint32_t            green_mask    = 0;
    std::uint32_t            blue_mask     = 0;
    std::uint32_t            alpha_mask    = 0;
};

struct DdsDx10Header
{
    std::uint32_t dxgi_format        = 0;
    std::uint32_t resource_dimension = 0;
    std::uint32_t misc_flag          = 0;
    std::uint32_t array_size         = 0;
    std::uint32_t misc_flags2        = 0;
};

struct DdsMetadata
{
    std::uint32_t                flags                = 0;
    std::uint32_t                height               = 0;
    std::uint32_t                width                = 0;
    std::uint32_t                pitch_or_linear_size = 0;
    std::uint32_t                depth                = 0;
    std::uint32_t                mip_map_count        = 0;
    std::array<std::byte, 44>    reserved1{};
    DdsPixelFormat               pixel_format{};
    std::uint32_t                caps  = 0;
    std::uint32_t                caps2 = 0;
    std::array<std::byte, 8>     caps_reserved{};
    std::array<std::byte, 4>     reserved2{};
    std::optional<DdsDx10Header> dx10_header;
};

[[nodiscard]] std::expected<DdsMetadata, DdsMetadataError>
ParseDdsMetadata(std::span<const std::byte> bytes);

} // namespace rerevved::studio
