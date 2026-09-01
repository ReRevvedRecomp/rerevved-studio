#include "dds_metadata.h"

#include <algorithm>
#include <array>

namespace rerevved::studio
{
namespace
{

constexpr std::size_t              kBaseMetadataSize  = 128;
constexpr std::size_t              kDx10MetadataSize  = 148;
constexpr std::uint32_t            kDdsHeaderSize     = 124;
constexpr std::uint32_t            kPixelFormatSize   = 32;
constexpr std::uint32_t            kPixelFormatFourCc = 0x4;
constexpr std::array<std::byte, 4> kSignature{
    std::byte{ 'D' }, std::byte{ 'D' }, std::byte{ 'S' }, std::byte{ ' ' }
};
constexpr std::array<std::byte, 4> kDx10FourCc{
    std::byte{ 'D' }, std::byte{ 'X' }, std::byte{ '1' }, std::byte{ '0' }
};

std::uint32_t ReadU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

template <std::size_t Size>
std::array<std::byte, Size> ReadBytes(std::span<const std::byte> bytes, std::size_t offset)
{
    std::array<std::byte, Size> result{};
    std::ranges::copy(bytes.subspan(offset, Size), result.begin());
    return result;
}

} // namespace

std::expected<DdsMetadata, DdsMetadataError>
ParseDdsMetadata(std::span<const std::byte> bytes)
{
    if (bytes.size() < kBaseMetadataSize)
        return std::unexpected(DdsMetadataError::truncated);
    if (!std::ranges::equal(bytes.first<4>(), kSignature))
        return std::unexpected(DdsMetadataError::invalid_signature);
    if (ReadU32Le(bytes, 4) != kDdsHeaderSize)
        return std::unexpected(DdsMetadataError::invalid_header_size);
    if (ReadU32Le(bytes, 76) != kPixelFormatSize)
        return std::unexpected(DdsMetadataError::invalid_pixel_format_size);

    DdsMetadata metadata{};
    metadata.flags                      = ReadU32Le(bytes, 8);
    metadata.height                     = ReadU32Le(bytes, 12);
    metadata.width                      = ReadU32Le(bytes, 16);
    metadata.pitch_or_linear_size       = ReadU32Le(bytes, 20);
    metadata.depth                      = ReadU32Le(bytes, 24);
    metadata.mip_map_count              = ReadU32Le(bytes, 28);
    metadata.reserved1                  = ReadBytes<44>(bytes, 32);
    metadata.pixel_format.flags         = ReadU32Le(bytes, 80);
    metadata.pixel_format.four_cc       = ReadBytes<4>(bytes, 84);
    metadata.pixel_format.rgb_bit_count = ReadU32Le(bytes, 88);
    metadata.pixel_format.red_mask      = ReadU32Le(bytes, 92);
    metadata.pixel_format.green_mask    = ReadU32Le(bytes, 96);
    metadata.pixel_format.blue_mask     = ReadU32Le(bytes, 100);
    metadata.pixel_format.alpha_mask    = ReadU32Le(bytes, 104);
    metadata.caps                       = ReadU32Le(bytes, 108);
    metadata.caps2                      = ReadU32Le(bytes, 112);
    metadata.caps_reserved              = ReadBytes<8>(bytes, 116);
    metadata.reserved2                  = ReadBytes<4>(bytes, 124);

    const bool has_dx10_header =
        (metadata.pixel_format.flags & kPixelFormatFourCc) != 0 &&
        metadata.pixel_format.four_cc == kDx10FourCc;
    if (!has_dx10_header)
        return metadata;
    if (bytes.size() < kDx10MetadataSize)
        return std::unexpected(DdsMetadataError::truncated);

    metadata.dx10_header = DdsDx10Header{
        .dxgi_format        = ReadU32Le(bytes, 128),
        .resource_dimension = ReadU32Le(bytes, 132),
        .misc_flag          = ReadU32Le(bytes, 136),
        .array_size         = ReadU32Le(bytes, 140),
        .misc_flags2        = ReadU32Le(bytes, 144),
    };
    return metadata;
}

} // namespace rerevved::studio
