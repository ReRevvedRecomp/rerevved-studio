#include "nif_document.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <string_view>

namespace rerevved::studio
{
namespace
{

constexpr std::string_view kHeader  = "Gamebryo File Format, Version 20.3.0.9\n";
constexpr std::uint32_t    kVersion = 0x14030009;

bool CanRead(std::span<const std::byte> bytes, std::size_t offset, std::size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint16_t ReadU16Be(std::span<const std::byte> bytes, std::size_t offset)
{
    return (std::to_integer<std::uint16_t>(bytes[offset]) << 8U) |
           std::to_integer<std::uint16_t>(bytes[offset + 1]);
}

std::uint32_t ReadU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::uint32_t ReadU32Be(std::span<const std::byte> bytes, std::size_t offset)
{
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

bool CanReadArray(std::span<const std::byte> bytes,
                  std::size_t                offset,
                  std::uint32_t              count,
                  std::size_t                element_size)
{
    return offset <= bytes.size() && count <= (bytes.size() - offset) / element_size;
}

bool Equals(std::span<const std::byte> bytes, std::string_view value)
{
    return bytes.size() == value.size() &&
           std::ranges::equal(bytes, std::as_bytes(std::span(value.data(), value.size())));
}

bool ReadBlockU8(std::span<const std::byte> bytes, std::size_t& offset, std::uint8_t& value)
{
    if (!CanRead(bytes, offset, 1))
        return false;
    value = std::to_integer<std::uint8_t>(bytes[offset]);
    ++offset;
    return true;
}

bool ReadBlockU16(std::span<const std::byte> bytes, std::size_t& offset, std::uint16_t& value)
{
    if (!CanRead(bytes, offset, 2))
        return false;
    value = ReadU16Be(bytes, offset);
    offset += 2;
    return true;
}

bool ReadBlockU32(std::span<const std::byte> bytes, std::size_t& offset, std::uint32_t& value)
{
    if (!CanRead(bytes, offset, 4))
        return false;
    value = ReadU32Be(bytes, offset);
    offset += 4;
    return true;
}

bool ReadBlockF32(std::span<const std::byte> bytes, std::size_t& offset, float& value)
{
    std::uint32_t bits = 0;
    if (!ReadBlockU32(bytes, offset, bits))
        return false;
    value = std::bit_cast<float>(bits);
    return true;
}

bool SkipBlockArray(std::span<const std::byte> bytes,
                    std::size_t&               offset,
                    std::uint32_t              count,
                    std::size_t                element_size)
{
    if (!CanReadArray(bytes, offset, count, element_size))
        return false;
    offset += static_cast<std::size_t>(count) * element_size;
    return true;
}

bool IsValidReference(std::uint32_t value, std::size_t block_count)
{
    return value == std::numeric_limits<std::uint32_t>::max() || value < block_count;
}

bool IsValidStringIndex(std::uint32_t value, std::size_t string_count)
{
    return value == std::numeric_limits<std::uint32_t>::max() || value < string_count;
}

bool ReadReference(std::span<const std::byte> bytes,
                   std::size_t&               offset,
                   std::size_t                block_count,
                   std::uint32_t&             value)
{
    return ReadBlockU32(bytes, offset, value) && IsValidReference(value, block_count);
}

bool ReadReferences(std::span<const std::byte>  bytes,
                    std::size_t&                offset,
                    std::uint32_t               count,
                    std::size_t                 block_count,
                    std::vector<std::uint32_t>& values)
{
    if (!CanReadArray(bytes, offset, count, 4))
        return false;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t value = 0;
        if (!ReadReference(bytes, offset, block_count, value))
            return false;
        values.push_back(value);
    }
    return true;
}

bool ReadStringIndices(std::span<const std::byte>  bytes,
                       std::size_t&                offset,
                       std::uint32_t               count,
                       std::size_t                 string_count,
                       std::vector<std::uint32_t>& values)
{
    if (!CanReadArray(bytes, offset, count, 4))
        return false;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t value = 0;
        if (!ReadBlockU32(bytes, offset, value) || !IsValidStringIndex(value, string_count))
            return false;
        values.push_back(value);
    }
    return true;
}

bool ReadSignedValues(std::span<const std::byte> bytes,
                      std::size_t&               offset,
                      std::uint32_t              count,
                      std::vector<std::int32_t>& values)
{
    if (!CanReadArray(bytes, offset, count, 4))
        return false;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t value = 0;
        if (!ReadBlockU32(bytes, offset, value))
            return false;
        values.push_back(std::bit_cast<std::int32_t>(value));
    }
    return true;
}

bool ParseAvObject(std::span<const std::byte> bytes,
                   std::size_t&               offset,
                   std::size_t                block_count,
                   std::size_t                string_count,
                   NifAvObject&               object)
{
    std::uint32_t count = 0;
    if (!ReadBlockU32(bytes, offset, object.name_index) ||
        !IsValidStringIndex(object.name_index, string_count) ||
        !ReadBlockU32(bytes, offset, count) ||
        !ReadReferences(bytes, offset, count, block_count, object.extra_data) ||
        !ReadReference(bytes, offset, block_count, object.controller) ||
        !ReadBlockU16(bytes, offset, object.flags))
        return false;

    for (auto& value : object.translation)
    {
        if (!ReadBlockF32(bytes, offset, value))
            return false;
    }
    for (auto& value : object.rotation)
    {
        if (!ReadBlockF32(bytes, offset, value))
            return false;
    }
    if (!ReadBlockF32(bytes, offset, object.scale))
        return false;

    return ReadBlockU32(bytes, offset, count) &&
           ReadReferences(bytes, offset, count, block_count, object.properties) &&
           ReadReference(bytes, offset, block_count, object.collision_object);
}

bool ParseNode(std::span<const std::byte> bytes,
               std::uint32_t              block_index,
               std::size_t                block_count,
               std::size_t                string_count,
               NifNode&                   node)
{
    std::size_t offset  = 0;
    node.block_index    = block_index;
    std::uint32_t count = 0;
    return ParseAvObject(bytes, offset, block_count, string_count, node.object) &&
           ReadBlockU32(bytes, offset, count) &&
           ReadReferences(bytes, offset, count, block_count, node.children) &&
           ReadBlockU32(bytes, offset, count) &&
           ReadReferences(bytes, offset, count, block_count, node.effects) &&
           offset == bytes.size();
}

bool ParseTriShape(std::span<const std::byte> bytes,
                   std::uint32_t              block_index,
                   std::size_t                block_count,
                   std::size_t                string_count,
                   NifTriShape&               shape)
{
    std::size_t offset            = 0;
    shape.block_index             = block_index;
    std::uint32_t material_count  = 0;
    std::uint32_t active_material = 0;
    if (!ParseAvObject(bytes, offset, block_count, string_count, shape.object) ||
        !ReadReference(bytes, offset, block_count, shape.data) ||
        !ReadReference(bytes, offset, block_count, shape.skin_instance) ||
        !ReadBlockU32(bytes, offset, material_count) ||
        !ReadStringIndices(
            bytes, offset, material_count, string_count, shape.material.name_indices) ||
        !ReadSignedValues(bytes, offset, material_count, shape.material.extra_data) ||
        !ReadBlockU32(bytes, offset, active_material) ||
        !ReadBlockU8(bytes, offset, shape.material.material_needs_update) ||
        offset != bytes.size())
        return false;
    shape.material.active_material = std::bit_cast<std::int32_t>(active_material);
    return true;
}

bool ParseTriShapeData(std::span<const std::byte> bytes,
                       std::uint32_t              block_index,
                       std::size_t                block_count,
                       NifTriShapeDataInventory&  data)
{
    std::size_t offset = 0;
    data.block_index   = block_index;

    std::uint32_t group_id = 0;
    if (!ReadBlockU32(bytes, offset, group_id) ||
        !ReadBlockU16(bytes, offset, data.vertex_count) ||
        !ReadBlockU8(bytes, offset, data.keep_flags) ||
        !ReadBlockU8(bytes, offset, data.compress_flags) ||
        !ReadBlockU8(bytes, offset, data.has_vertices) ||
        (data.has_vertices != 0 && !SkipBlockArray(bytes, offset, data.vertex_count, 12)) ||
        !ReadBlockU16(bytes, offset, data.data_flags) ||
        !ReadBlockU8(bytes, offset, data.has_normals) ||
        (data.has_normals != 0 && !SkipBlockArray(bytes, offset, data.vertex_count, 12)))
        return false;
    data.group_id = std::bit_cast<std::int32_t>(group_id);

    if (data.has_normals != 0 && (data.data_flags & 0x1000U) != 0 &&
        (!SkipBlockArray(bytes, offset, data.vertex_count, 12) ||
         !SkipBlockArray(bytes, offset, data.vertex_count, 12)))
        return false;

    for (auto& value : data.bound_center)
    {
        if (!ReadBlockF32(bytes, offset, value))
            return false;
    }
    if (!ReadBlockF32(bytes, offset, data.bound_radius) ||
        !ReadBlockU8(bytes, offset, data.has_vertex_colors) ||
        (data.has_vertex_colors != 0 &&
         !SkipBlockArray(bytes, offset, data.vertex_count, 16)))
        return false;

    const auto uv_set_count = static_cast<std::uint32_t>(data.data_flags & 0x003FU);
    if (!SkipBlockArray(bytes,
                        offset,
                        static_cast<std::uint32_t>(data.vertex_count) * uv_set_count,
                        8) ||
        !ReadBlockU16(bytes, offset, data.consistency_flags) ||
        !ReadReference(bytes, offset, block_count, data.additional_data) ||
        !ReadBlockU16(bytes, offset, data.triangle_count) ||
        !ReadBlockU32(bytes, offset, data.triangle_point_count) ||
        !ReadBlockU8(bytes, offset, data.has_triangles) ||
        (data.has_triangles != 0 &&
         !SkipBlockArray(bytes, offset, data.triangle_count, 6)) ||
        !ReadBlockU16(bytes, offset, data.match_group_count))
        return false;

    for (std::uint16_t index = 0; index < data.match_group_count; ++index)
    {
        std::uint16_t vertex_count = 0;
        if (!ReadBlockU16(bytes, offset, vertex_count) ||
            !SkipBlockArray(bytes, offset, vertex_count, 2))
            return false;
    }
    return offset == bytes.size();
}

std::expected<std::vector<std::byte>, NifDocumentError>
ReadSizedBytes(std::span<const std::byte> bytes, std::size_t& offset)
{
    if (!CanRead(bytes, offset, 4))
        return std::unexpected(NifDocumentError::truncated);
    const auto size = static_cast<std::size_t>(ReadU32Be(bytes, offset));
    offset += 4;
    if (!CanRead(bytes, offset, size))
        return std::unexpected(NifDocumentError::truncated);
    std::vector<std::byte> result(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return result;
}

} // namespace

std::string_view NifDocumentErrorMessage(NifDocumentError error)
{
    switch (error)
    {
        case NifDocumentError::truncated:
            return "The NIF file is truncated.";
        case NifDocumentError::invalid_signature:
            return "The file does not have a valid Gamebryo NIF signature.";
        case NifDocumentError::unsupported_version:
            return "The NIF file version is not supported.";
        case NifDocumentError::unsupported_endian:
            return "Only the observed big-endian NIF layout is supported.";
        case NifDocumentError::unsupported_user_version:
            return "The NIF user version is not supported.";
        case NifDocumentError::invalid_layout:
            return "The NIF header, block table, or footer is inconsistent.";
    }
    return "The NIF file is invalid.";
}

std::expected<NifDocument, NifDocumentError>
ParseNifDocument(std::span<const std::byte> bytes)
{
    constexpr std::string_view kPrefix = "Gamebryo File Format, Version ";
    if (bytes.size() < kPrefix.size())
        return std::unexpected(NifDocumentError::truncated);
    if (!std::ranges::equal(bytes.first(kPrefix.size()),
                            std::as_bytes(std::span(kPrefix.data(), kPrefix.size()))))
        return std::unexpected(NifDocumentError::invalid_signature);
    if (bytes.size() < kHeader.size())
        return std::unexpected(NifDocumentError::truncated);
    if (!std::ranges::equal(bytes.first(kHeader.size()),
                            std::as_bytes(std::span(kHeader.data(), kHeader.size()))))
        return std::unexpected(NifDocumentError::unsupported_version);

    std::size_t offset = kHeader.size();
    if (!CanRead(bytes, offset, 13))
        return std::unexpected(NifDocumentError::truncated);

    NifDocument document{};
    document.version = ReadU32Le(bytes, offset);
    offset += 4;
    if (document.version != kVersion)
        return std::unexpected(NifDocumentError::unsupported_version);
    document.endian = std::to_integer<std::uint8_t>(bytes[offset]);
    ++offset;
    if (document.endian != 0)
        return std::unexpected(NifDocumentError::unsupported_endian);
    document.user_version = ReadU32Le(bytes, offset);
    offset += 4;
    if (document.user_version != 0)
        return std::unexpected(NifDocumentError::unsupported_user_version);
    const auto block_count = ReadU32Le(bytes, offset);
    offset += 4;

    // NifTools nif.xml defines this mixed-endian header: version, user version,
    // and block count are little-endian; later numeric fields follow Endian Type.
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1963-L1982
    if (!CanRead(bytes, offset, 2))
        return std::unexpected(NifDocumentError::truncated);
    const auto block_type_count = ReadU16Be(bytes, offset);
    offset += 2;
    if (!CanReadArray(bytes, offset, block_type_count, 4))
        return std::unexpected(NifDocumentError::truncated);
    document.block_types.reserve(block_type_count);
    for (std::uint16_t index = 0; index < block_type_count; ++index)
    {
        auto block_type = ReadSizedBytes(bytes, offset);
        if (!block_type)
            return std::unexpected(block_type.error());
        document.block_types.push_back(std::move(*block_type));
    }

    if (!CanReadArray(bytes, offset, block_count, 2))
        return std::unexpected(NifDocumentError::truncated);
    document.blocks.resize(block_count);
    for (auto& block : document.blocks)
    {
        block.type_index = ReadU16Be(bytes, offset);
        offset += 2;
        if (block.type_index >= document.block_types.size())
            return std::unexpected(NifDocumentError::invalid_layout);
    }
    if (!CanReadArray(bytes, offset, block_count, 4))
        return std::unexpected(NifDocumentError::truncated);
    for (auto& block : document.blocks)
    {
        block.size = ReadU32Be(bytes, offset);
        offset += 4;
    }

    if (!CanRead(bytes, offset, 8))
        return std::unexpected(NifDocumentError::truncated);
    const auto string_count = ReadU32Be(bytes, offset);
    offset += 4;
    document.max_string_length = ReadU32Be(bytes, offset);
    offset += 4;
    if (!CanReadArray(bytes, offset, string_count, 4))
        return std::unexpected(NifDocumentError::truncated);
    document.strings.reserve(string_count);
    for (std::uint32_t index = 0; index < string_count; ++index)
    {
        auto value = ReadSizedBytes(bytes, offset);
        if (!value)
            return std::unexpected(value.error());
        if (value->size() > document.max_string_length)
            return std::unexpected(NifDocumentError::invalid_layout);
        document.strings.push_back(std::move(*value));
    }

    if (!CanRead(bytes, offset, 4))
        return std::unexpected(NifDocumentError::truncated);
    const auto group_count = ReadU32Be(bytes, offset);
    offset += 4;
    if (!CanReadArray(bytes, offset, group_count, 4))
        return std::unexpected(NifDocumentError::truncated);
    document.groups.reserve(group_count);
    for (std::uint32_t index = 0; index < group_count; ++index)
    {
        document.groups.push_back(ReadU32Be(bytes, offset));
        offset += 4;
    }
    document.header_size = offset;

    for (auto& block : document.blocks)
    {
        block.offset = offset;
        if (!CanRead(bytes, offset, block.size))
            return std::unexpected(NifDocumentError::truncated);
        offset += block.size;
    }

    // The selected object layouts are the standard 20.3.0.9 inheritance chains.
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3359-L3367
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3440-L3494
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3840-L3869
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3876-L3925
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L4384-L4389
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5278-L5280
    // https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5282-L5289
    for (std::uint32_t block_index = 0; block_index < document.blocks.size(); ++block_index)
    {
        const auto& block      = document.blocks[block_index];
        const auto& block_type = document.block_types[block.type_index];
        const auto  payload    = bytes.subspan(block.offset, block.size);
        if (Equals(block_type, "NiNode"))
        {
            NifNode node{};
            if (!ParseNode(payload,
                           block_index,
                           document.blocks.size(),
                           document.strings.size(),
                           node))
                return std::unexpected(NifDocumentError::invalid_layout);
            document.nodes.push_back(std::move(node));
        }
        else if (Equals(block_type, "NiTriShape"))
        {
            NifTriShape shape{};
            if (!ParseTriShape(payload,
                               block_index,
                               document.blocks.size(),
                               document.strings.size(),
                               shape))
                return std::unexpected(NifDocumentError::invalid_layout);
            document.tri_shapes.push_back(std::move(shape));
        }
        else if (Equals(block_type, "NiTriShapeData"))
        {
            NifTriShapeDataInventory data{};
            if (!ParseTriShapeData(payload, block_index, document.blocks.size(), data))
                return std::unexpected(NifDocumentError::invalid_layout);
            document.tri_shape_data.push_back(std::move(data));
        }
    }

    if (!CanRead(bytes, offset, 4))
        return std::unexpected(NifDocumentError::truncated);
    const auto root_count = ReadU32Be(bytes, offset);
    offset += 4;
    if (!CanReadArray(bytes, offset, root_count, 4))
        return std::unexpected(NifDocumentError::truncated);
    document.roots.reserve(root_count);
    for (std::uint32_t index = 0; index < root_count; ++index)
    {
        const auto root = ReadU32Be(bytes, offset);
        offset += 4;
        if (root != std::numeric_limits<std::uint32_t>::max() && root >= document.blocks.size())
            return std::unexpected(NifDocumentError::invalid_layout);
        document.roots.push_back(root);
    }
    if (offset != bytes.size())
        return std::unexpected(NifDocumentError::invalid_layout);
    return document;
}

std::expected<NifDocument, std::string>
LoadNifDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return std::unexpected("Could not open NIF file for reading.");

    const auto end = input.tellg();
    if (end < 0)
        return std::unexpected("Could not read NIF file size.");
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("NIF file exceeds the supported size range.");

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
        return std::unexpected("Could not read NIF file data.");
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
        return std::unexpected("NIF file ended before its reported size.");

    auto document = ParseNifDocument(bytes);
    if (!document)
        return std::unexpected(std::string(NifDocumentErrorMessage(document.error())));
    return std::move(*document);
}

} // namespace rerevved::studio
