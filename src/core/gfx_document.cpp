#include "gfx_document.h"

#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace rerevved::studio
{
namespace
{

constexpr std::uint16_t kEndTag           = 0;
constexpr std::uint16_t kExporterInfoTag  = 1000;
constexpr std::uint16_t kExternalImageTag = 1001;

bool CanRead(std::span<const std::byte> bytes, std::size_t offset, std::size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint16_t ReadU16Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint16_t>(bytes[offset]) |
           (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t ReadU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

class BitReader
{
public:
    BitReader(std::span<const std::byte> bytes, std::size_t byte_offset)
    : bytes_(bytes)
    , bit_offset_(byte_offset * 8)
    {
    }

    std::optional<std::uint32_t> ReadUnsigned(unsigned int bit_count)
    {
        if (bit_count > bytes_.size() * 8 - bit_offset_)
            return std::nullopt;

        std::uint32_t value = 0;
        for (unsigned int index = 0; index < bit_count; ++index)
        {
            const auto byte = std::to_integer<std::uint8_t>(bytes_[bit_offset_ / 8]);
            value           = (value << 1U) | ((byte >> (7U - (bit_offset_ % 8))) & 1U);
            ++bit_offset_;
        }
        return value;
    }

    std::optional<std::int32_t> ReadSigned(unsigned int bit_count)
    {
        const auto value = ReadUnsigned(bit_count);
        if (!value)
            return std::nullopt;
        if (bit_count == 0)
            return 0;
        if (bit_count < 32 && (*value & (1U << (bit_count - 1U))) != 0)
            return static_cast<std::int32_t>(*value | (~0U << bit_count));
        return static_cast<std::int32_t>(*value);
    }

    [[nodiscard]] std::size_t AlignedByteOffset() const
    {
        return (bit_offset_ + 7) / 8;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t                bit_offset_ = 0;
};

struct GfxTag
{
    std::uint16_t              code = 0;
    std::span<const std::byte> payload;
    std::size_t                next_offset = 0;
};

std::expected<GfxTag, GfxDocumentError>
ReadTag(std::span<const std::byte> bytes, std::size_t offset)
{
    if (!CanRead(bytes, offset, 2))
        return std::unexpected(GfxDocumentError::truncated);

    const auto header = ReadU16Le(bytes, offset);
    offset += 2;
    std::uint32_t length = header & 0x3FU;
    if (length == 0x3FU)
    {
        if (!CanRead(bytes, offset, 4))
            return std::unexpected(GfxDocumentError::truncated);
        length = ReadU32Le(bytes, offset);
        offset += 4;
    }
    if (!CanRead(bytes, offset, length))
        return std::unexpected(GfxDocumentError::truncated);

    return GfxTag{
        .code        = static_cast<std::uint16_t>(header >> 6U),
        .payload     = bytes.subspan(offset, length),
        .next_offset = offset + length,
    };
}

std::expected<std::vector<std::byte>, GfxDocumentError>
ReadByteString(std::span<const std::byte> bytes, std::size_t& offset)
{
    if (!CanRead(bytes, offset, 1))
        return std::unexpected(GfxDocumentError::truncated);
    const auto length = std::to_integer<std::uint8_t>(bytes[offset++]);
    if (!CanRead(bytes, offset, length))
        return std::unexpected(GfxDocumentError::truncated);

    std::vector<std::byte> value(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return value;
}

// GFX tag 1000 and the exporter 2.x gate are documented by the pinned
// Scaleform GFx v2 source. Only layout facts are used; no source code is copied.
// https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxLoaderImpl.cpp
std::expected<GfxExporterInfo, GfxDocumentError>
ParseExporterInfo(std::span<const std::byte> payload)
{
    if (!CanRead(payload, 0, 2))
        return std::unexpected(GfxDocumentError::truncated);

    GfxExporterInfo exporter{};
    exporter.version = ReadU16Le(payload, 0);
    if ((exporter.version & 0xFF00U) != 0x0200U)
        return std::unexpected(GfxDocumentError::unsupported_exporter_version);
    if (!CanRead(payload, 2, 6))
        return std::unexpected(GfxDocumentError::truncated);

    exporter.flags         = ReadU32Le(payload, 2);
    exporter.bitmap_format = ReadU16Le(payload, 6);
    std::size_t offset     = 8;
    auto        prefix     = ReadByteString(payload, offset);
    if (!prefix)
        return std::unexpected(prefix.error());
    auto swf_name = ReadByteString(payload, offset);
    if (!swf_name)
        return std::unexpected(swf_name.error());
    if (offset != payload.size())
        return std::unexpected(GfxDocumentError::invalid_tag_stream);

    exporter.prefix   = std::move(*prefix);
    exporter.swf_name = std::move(*swf_name);
    return exporter;
}

// GFX tag 1001 is documented by the same pinned source at:
// https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxTagLoaders.cpp
std::expected<GfxExternalImage, GfxDocumentError>
ParseExternalImage(std::span<const std::byte> payload)
{
    if (!CanRead(payload, 0, 8))
        return std::unexpected(GfxDocumentError::truncated);

    GfxExternalImage image{
        .character_id  = ReadU16Le(payload, 0),
        .bitmap_format = ReadU16Le(payload, 2),
        .target_width  = ReadU16Le(payload, 4),
        .target_height = ReadU16Le(payload, 6),
        .export_name   = {},
        .file_name     = {},
    };
    std::size_t offset      = 8;
    auto        export_name = ReadByteString(payload, offset);
    if (!export_name)
        return std::unexpected(export_name.error());
    auto file_name = ReadByteString(payload, offset);
    if (!file_name)
        return std::unexpected(file_name.error());
    if (offset != payload.size())
        return std::unexpected(GfxDocumentError::invalid_tag_stream);

    image.export_name = std::move(*export_name);
    image.file_name   = std::move(*file_name);
    return image;
}

} // namespace

std::string_view GfxDocumentErrorMessage(GfxDocumentError error)
{
    switch (error)
    {
        case GfxDocumentError::truncated:
            return "The GFX movie is truncated.";
        case GfxDocumentError::invalid_signature:
            return "The file does not have a valid GFX signature.";
        case GfxDocumentError::unsupported_compression:
            return "Compressed GFX movies are not supported.";
        case GfxDocumentError::length_mismatch:
            return "The GFX declared length does not match the file size.";
        case GfxDocumentError::missing_exporter_info:
            return "The GFX movie does not begin with exporter information.";
        case GfxDocumentError::unsupported_exporter_version:
            return "The GFX exporter version is not supported.";
        case GfxDocumentError::invalid_tag_stream:
            return "The GFX tag stream is invalid.";
    }
    return "The GFX movie is invalid.";
}

std::expected<GfxDocument, GfxDocumentError>
ParseGfxDocument(std::span<const std::byte> bytes)
{
    if (bytes.size() < 3)
        return std::unexpected(GfxDocumentError::truncated);
    const std::array signature{ bytes[0], bytes[1], bytes[2] };
    if (signature == std::array{ std::byte{ 'C' }, std::byte{ 'F' }, std::byte{ 'X' } })
        return std::unexpected(GfxDocumentError::unsupported_compression);
    if (signature != std::array{ std::byte{ 'G' }, std::byte{ 'F' }, std::byte{ 'X' } })
        return std::unexpected(GfxDocumentError::invalid_signature);
    if (bytes.size() < 8)
        return std::unexpected(GfxDocumentError::truncated);

    GfxDocument document{
        .file_version    = std::to_integer<std::uint8_t>(bytes[3]),
        .declared_length = ReadU32Le(bytes, 4),
        .frame           = {},
        .frame_rate_raw  = 0,
        .frame_count     = 0,
        .exporter        = {},
        .external_images = {},
    };
    if (document.declared_length != bytes.size())
        return std::unexpected(GfxDocumentError::length_mismatch);

    // RECT and tag record framing follow the Adobe SWF file format specification.
    // https://open-flash.github.io/mirrors/swf-spec-19.pdf
    BitReader  bits(bytes, 8);
    const auto coordinate_bits = bits.ReadUnsigned(5);
    if (!coordinate_bits)
        return std::unexpected(GfxDocumentError::truncated);
    const auto x_min = bits.ReadSigned(*coordinate_bits);
    const auto x_max = bits.ReadSigned(*coordinate_bits);
    const auto y_min = bits.ReadSigned(*coordinate_bits);
    const auto y_max = bits.ReadSigned(*coordinate_bits);
    if (!x_min || !x_max || !y_min || !y_max)
        return std::unexpected(GfxDocumentError::truncated);
    document.frame = GfxRect{
        .x_min_twips = *x_min,
        .x_max_twips = *x_max,
        .y_min_twips = *y_min,
        .y_max_twips = *y_max,
    };

    std::size_t offset = bits.AlignedByteOffset();
    if (!CanRead(bytes, offset, 4))
        return std::unexpected(GfxDocumentError::truncated);
    document.frame_rate_raw = ReadU16Le(bytes, offset);
    document.frame_count    = ReadU16Le(bytes, offset + 2);
    offset += 4;

    auto exporter_tag = ReadTag(bytes, offset);
    if (!exporter_tag)
        return std::unexpected(exporter_tag.error());
    if (exporter_tag->code != kExporterInfoTag)
        return std::unexpected(GfxDocumentError::missing_exporter_info);
    auto exporter = ParseExporterInfo(exporter_tag->payload);
    if (!exporter)
        return std::unexpected(exporter.error());
    document.exporter = std::move(*exporter);
    offset            = exporter_tag->next_offset;

    while (offset < bytes.size())
    {
        auto tag = ReadTag(bytes, offset);
        if (!tag)
            return std::unexpected(tag.error());
        if (tag->code == kEndTag)
        {
            if (!tag->payload.empty() || tag->next_offset != bytes.size())
                return std::unexpected(GfxDocumentError::invalid_tag_stream);
            return document;
        }
        if (tag->code == kExternalImageTag)
        {
            auto image = ParseExternalImage(tag->payload);
            if (!image)
                return std::unexpected(image.error());
            document.external_images.push_back(std::move(*image));
        }
        offset = tag->next_offset;
    }

    return std::unexpected(GfxDocumentError::invalid_tag_stream);
}

std::expected<GfxDocument, std::string>
LoadGfxDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return std::unexpected("Could not open GFX file for reading.");

    const auto end = input.tellg();
    if (end < 0)
        return std::unexpected("Could not read GFX file size.");
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("GFX file exceeds the supported size range.");

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
        return std::unexpected("Could not read GFX file data.");
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
        return std::unexpected("GFX file ended before its reported size.");

    auto document = ParseGfxDocument(bytes);
    if (!document)
        return std::unexpected(std::string(GfxDocumentErrorMessage(document.error())));
    return std::move(*document);
}

} // namespace rerevved::studio
