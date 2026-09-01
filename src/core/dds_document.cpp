#include "dds_document.h"

#include <bit>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace rerevved::studio
{
namespace
{

constexpr std::size_t   kPixelDataOffset        = 128;
constexpr std::uint32_t kPixelFormatAlphaPixels = 0x1;
constexpr std::uint32_t kPixelFormatRgb         = 0x40;
constexpr std::uint32_t kCaps2CubemapMask       = 0x0000FE00;
constexpr std::uint32_t kCaps2Volume            = 0x00200000;

bool IsEightBitMask(std::uint32_t mask)
{
    if (std::popcount(mask) != 8)
        return false;
    const auto shift = std::countr_zero(mask);
    return mask == (0xFFU << shift);
}

std::byte ReadChannel(std::uint32_t pixel, std::uint32_t mask)
{
    return static_cast<std::byte>((pixel & mask) >> std::countr_zero(mask));
}

std::uint32_t ReadU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::string_view MetadataErrorMessage(DdsMetadataError error)
{
    switch (error)
    {
        case DdsMetadataError::truncated:
            return "DDS metadata is truncated.";
        case DdsMetadataError::invalid_signature:
            return "The file does not have a valid DDS signature.";
        case DdsMetadataError::invalid_header_size:
            return "The DDS header size is invalid.";
        case DdsMetadataError::invalid_pixel_format_size:
            return "The DDS pixel format size is invalid.";
    }
    return "The DDS metadata is invalid.";
}

std::string_view PreviewErrorMessage(DdsPreviewError error)
{
    switch (error)
    {
        case DdsPreviewError::unsupported_format:
            return "This DDS encoding is not supported for preview.";
        case DdsPreviewError::invalid_dimensions:
            return "The DDS preview dimensions are invalid.";
        case DdsPreviewError::size_overflow:
            return "The DDS preview dimensions exceed the supported size range.";
        case DdsPreviewError::truncated_pixel_data:
            return "The DDS top-level pixel data is truncated.";
    }
    return "The DDS preview could not be decoded.";
}

} // namespace

std::string_view DdsDocumentErrorMessage(const DdsDocumentError& error)
{
    if (const auto* metadata_error = std::get_if<DdsMetadataError>(&error))
        return MetadataErrorMessage(*metadata_error);
    return PreviewErrorMessage(std::get<DdsPreviewError>(error));
}

std::expected<DdsDocument, DdsDocumentError>
ParseDdsDocument(std::span<const std::byte> bytes)
{
    auto metadata = ParseDdsMetadata(bytes);
    if (!metadata)
        return std::unexpected(DdsDocumentError{ metadata.error() });

    const auto& pixel_format = metadata->pixel_format;
    if (metadata->dx10_header ||
        (pixel_format.flags != kPixelFormatRgb &&
         pixel_format.flags != (kPixelFormatRgb | kPixelFormatAlphaPixels)) ||
        pixel_format.rgb_bit_count != 32 ||
        (metadata->caps2 & (kCaps2CubemapMask | kCaps2Volume)) != 0)
    {
        return std::unexpected(DdsDocumentError{ DdsPreviewError::unsupported_format });
    }

    const bool has_alpha = (pixel_format.flags & kPixelFormatAlphaPixels) != 0;
    if (!IsEightBitMask(pixel_format.red_mask) ||
        !IsEightBitMask(pixel_format.green_mask) ||
        !IsEightBitMask(pixel_format.blue_mask) ||
        (pixel_format.red_mask & pixel_format.green_mask) != 0 ||
        (pixel_format.red_mask & pixel_format.blue_mask) != 0 ||
        (pixel_format.green_mask & pixel_format.blue_mask) != 0 ||
        (has_alpha &&
         (!IsEightBitMask(pixel_format.alpha_mask) ||
          (pixel_format.alpha_mask & (pixel_format.red_mask | pixel_format.green_mask |
                                      pixel_format.blue_mask)) != 0)))
    {
        return std::unexpected(DdsDocumentError{ DdsPreviewError::unsupported_format });
    }

    if (metadata->width == 0 || metadata->height == 0)
        return std::unexpected(DdsDocumentError{ DdsPreviewError::invalid_dimensions });

    constexpr std::size_t bytes_per_pixel = 4;
    const auto            row_size        = static_cast<std::size_t>(metadata->width) * bytes_per_pixel;
    if (metadata->height > std::numeric_limits<std::size_t>::max() / row_size)
        return std::unexpected(DdsDocumentError{ DdsPreviewError::size_overflow });
    const auto pixel_data_size = static_cast<std::size_t>(metadata->height) * row_size;
    if (pixel_data_size > bytes.size() - kPixelDataOffset)
        return std::unexpected(DdsDocumentError{ DdsPreviewError::truncated_pixel_data });

    std::vector<std::byte> rgba8(pixel_data_size);
    const auto             pixel_data = bytes.subspan(kPixelDataOffset, pixel_data_size);
    for (std::size_t source_offset = 0, output_offset = 0;
         source_offset < pixel_data.size();
         source_offset += bytes_per_pixel, output_offset += bytes_per_pixel)
    {
        const auto pixel         = ReadU32Le(pixel_data, source_offset);
        rgba8[output_offset]     = ReadChannel(pixel, pixel_format.red_mask);
        rgba8[output_offset + 1] = ReadChannel(pixel, pixel_format.green_mask);
        rgba8[output_offset + 2] = ReadChannel(pixel, pixel_format.blue_mask);
        rgba8[output_offset + 3] =
            has_alpha ? ReadChannel(pixel, pixel_format.alpha_mask) : std::byte{ 0xFF };
    }

    return DdsDocument{
        .metadata = std::move(*metadata),
        .rgba8    = std::move(rgba8),
    };
}

std::expected<DdsDocument, std::string>
LoadDdsDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return std::unexpected("Could not open DDS file for reading.");

    const auto end = input.tellg();
    if (end < 0)
        return std::unexpected("Could not read DDS file size.");
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("DDS file exceeds the supported size range.");

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
        return std::unexpected("Could not read DDS file data.");
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
        return std::unexpected("DDS file ended before its reported size.");

    auto document = ParseDdsDocument(bytes);
    if (!document)
        return std::unexpected(std::string(DdsDocumentErrorMessage(document.error())));
    return std::move(*document);
}

} // namespace rerevved::studio
