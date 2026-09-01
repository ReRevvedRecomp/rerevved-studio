#include "nif_inspector_format.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace rerevved::studio
{
namespace
{

std::string FormatColor(std::string_view label, const std::array<float, 3>& color)
{
    return std::string(label) + ": " + FormatNifInspectorFloat(color[0]) + ", " +
           FormatNifInspectorFloat(color[1]) + ", " + FormatNifInspectorFloat(color[2]);
}

std::string FormatRawByte(std::string_view label, std::uint8_t value)
{
    char hexadecimal[5]{};
    std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%02X", static_cast<unsigned int>(value));
    return std::string(label) + ": " + std::to_string(value) + " (" + hexadecimal + ")";
}

std::string FormatRawU32(std::string_view label, std::uint32_t value)
{
    char hexadecimal[11]{};
    std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%08X", value);
    return std::string(label) + ": " + std::to_string(value) + " (" + hexadecimal + ")";
}

std::string FormatRawU16Hex(std::string_view label, std::uint16_t value)
{
    char hexadecimal[7]{};
    std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%04X", static_cast<unsigned int>(value));
    return std::string(label) + ": " + hexadecimal;
}

std::string FormatReference(std::string_view label, std::uint32_t value)
{
    if (value == UINT32_MAX)
        return std::string(label) + ": none (0xFFFFFFFF)";
    return FormatRawU32(label, value);
}

std::string FormatFloatPair(std::string_view label, const std::array<float, 2>& values)
{
    return std::string(label) + ": " + FormatNifInspectorFloat(values[0]) + ", " +
           FormatNifInspectorFloat(values[1]);
}

void AppendTexDesc(std::vector<std::string>&  fields,
                   std::string_view           prefix,
                   const NifTexDescInventory& descriptor)
{
    const auto field_prefix = std::string(prefix) + " ";
    fields.push_back(FormatReference(field_prefix + "Source block reference", descriptor.source));
    fields.push_back(FormatRawU16Hex(field_prefix + "descriptor Flags", descriptor.flags));
    fields.push_back(
        FormatRawByte(field_prefix + "Has Texture Transform", descriptor.has_texture_transform));
    const bool has_transform = descriptor.has_texture_transform == 1 && descriptor.transform;
    fields.push_back(field_prefix + "Texture Transform: " +
                     (has_transform ? "present" : "absent"));
    if (!has_transform)
        return;

    fields.push_back(FormatFloatPair(field_prefix + "Translation X/Y",
                                     descriptor.transform->translation));
    fields.push_back(FormatFloatPair(field_prefix + "Scale X/Y", descriptor.transform->scale));
    fields.push_back(field_prefix + "Rotation: " +
                     FormatNifInspectorFloat(descriptor.transform->rotation));
    fields.push_back(FormatRawU32(field_prefix + "Transform Method",
                                  descriptor.transform->transform_method));
    fields.push_back(FormatFloatPair(field_prefix + "Center X/Y", descriptor.transform->center));
}

} // namespace

std::string FormatNifInspectorFloat(float value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-infinity" : "+infinity";
    if (value == 0.0F)
        return std::signbit(value) ? "-0" : "0";

    std::array<char, 64> buffer{};
    const auto           result = std::to_chars(buffer.data(),
                                                buffer.data() + buffer.size(),
                                                value,
                                                std::chars_format::general,
                                                std::numeric_limits<float>::max_digits10);
    return std::string(buffer.data(), result.ptr);
}

std::string FormatNifStringBytes(std::span<const std::byte> bytes)
{
    std::string result;
    for (const auto value : bytes)
    {
        const auto byte = std::to_integer<unsigned char>(value);
        if (byte >= 0x20 && byte <= 0x7E && byte != '\\')
        {
            result.push_back(static_cast<char>(byte));
            continue;
        }
        if (byte == '\\')
        {
            result += "\\\\";
            continue;
        }
        char escaped[5]{};
        std::snprintf(escaped, sizeof(escaped), "\\x%02X", byte);
        result += escaped;
    }
    return result;
}

std::vector<NifMaterialPropertyText>
FormatNifMaterialProperties(std::span<const NifMaterialPropertyInventory> materials)
{
    std::vector<NifMaterialPropertyText> result;
    result.reserve(materials.size());
    for (const auto& material : materials)
    {
        result.push_back({
            "NiMaterialProperty block " + std::to_string(material.block_index),
            {
                FormatColor("Ambient color", material.ambient_color),
                FormatColor("Diffuse color", material.diffuse_color),
                FormatColor("Specular color", material.specular_color),
                FormatColor("Emissive color", material.emissive_color),
                "Glossiness: " + FormatNifInspectorFloat(material.glossiness),
                "Alpha: " + FormatNifInspectorFloat(material.alpha),
            },
        });
    }
    return result;
}

std::vector<NifTexturingPropertyText>
FormatNifTexturingProperties(std::span<const NifTexturingPropertyInventory> properties)
{
    constexpr std::array<std::string_view, 9> kSlotNames{
        "Base", "Dark", "Detail", "Gloss", "Glow", "Bump", "Normal", "Parallax", "Decal 0"
    };

    std::vector<NifTexturingPropertyText> result;
    result.reserve(properties.size());
    for (const auto& property : properties)
    {
        NifTexturingPropertyText text{
            "NiTexturingProperty block " + std::to_string(property.block_index),
            {
                FormatRawU16Hex("Flags", property.flags),
                "Texture Count: " + std::to_string(property.texture_count),
            },
        };
        for (std::size_t slot_index = 0; slot_index < property.standard_slots.size();
             ++slot_index)
        {
            const auto& slot      = property.standard_slots[slot_index];
            const auto  slot_name = kSlotNames[slot_index];
            text.fields.push_back(FormatRawByte(std::string(slot_name) + " presence",
                                                slot.presence));
            const bool has_descriptor = slot.presence == 1 && slot.descriptor;
            text.fields.push_back(std::string(slot_name) + " descriptor: " +
                                  (has_descriptor ? "present" : "absent"));
            if (has_descriptor)
                AppendTexDesc(text.fields, slot_name, *slot.descriptor);
            if (slot.presence == 1 && slot.bump)
            {
                text.fields.push_back("Bump Luma Scale: " +
                                      FormatNifInspectorFloat(slot.bump->luma_scale));
                text.fields.push_back("Bump Luma Offset: " +
                                      FormatNifInspectorFloat(slot.bump->luma_offset));
                text.fields.push_back(
                    "Bump Matrix: " + FormatNifInspectorFloat(slot.bump->bump_matrix[0]) +
                    ", " + FormatNifInspectorFloat(slot.bump->bump_matrix[1]) + ", " +
                    FormatNifInspectorFloat(slot.bump->bump_matrix[2]) + ", " +
                    FormatNifInspectorFloat(slot.bump->bump_matrix[3]));
            }
            if (slot.presence == 1 && slot.parallax_offset)
                text.fields.push_back("Parallax Offset: " +
                                      FormatNifInspectorFloat(*slot.parallax_offset));
        }
        text.fields.push_back("Shader Texture Count: " +
                              std::to_string(property.shader_texture_count));
        for (std::size_t shader_index = 0; shader_index < property.shader_textures.size();
             ++shader_index)
        {
            const auto  prefix = "Shader " + std::to_string(shader_index + 1);
            const auto& shader = property.shader_textures[shader_index];
            text.fields.push_back(FormatRawByte(prefix + " Has Map", shader.has_map));
            const bool has_descriptor = shader.has_map == 1 && shader.descriptor;
            text.fields.push_back(prefix + " descriptor: " +
                                  (has_descriptor ? "present" : "absent"));
            if (has_descriptor)
                AppendTexDesc(text.fields, prefix, *shader.descriptor);
            if (shader.has_map == 1 && shader.map_id)
                text.fields.push_back(FormatRawU32(prefix + " Map ID", *shader.map_id));
        }
        result.push_back(std::move(text));
    }
    return result;
}

std::vector<NifSourceTextureText> FormatNifSourceTextures(const NifDocument& document)
{
    std::vector<NifSourceTextureText> result;
    result.reserve(document.source_textures.size());
    for (const auto& source : document.source_textures)
    {
        const bool           has_file_name = source.file_name_index != UINT32_MAX &&
                                             source.file_name_index < document.strings.size();
        NifSourceTextureText text{
            "NiSourceTexture block " + std::to_string(source.block_index),
            {
                FormatRawByte("Use External", source.use_external),
                FormatReference("File Name string index", source.file_name_index),
            },
        };
        if (has_file_name)
        {
            const auto escaped = FormatNifStringBytes(document.strings[source.file_name_index]);
            text.fields.push_back("File Name bytes: " +
                                  (escaped.empty() ? std::string("<empty>") : escaped));
        }
        text.fields.push_back(FormatReference("Pixel Data block reference", source.pixel_data));
        text.fields.push_back(FormatRawU32("Pixel Layout", source.pixel_layout));
        text.fields.push_back(FormatRawU32("Use Mipmaps", source.use_mipmaps));
        text.fields.push_back(FormatRawU32("Alpha Format", source.alpha_format));
        text.fields.push_back(FormatRawByte("Is Static", source.is_static));
        text.fields.push_back(FormatRawByte("Direct Render", source.direct_render));
        text.fields.push_back(FormatRawByte("Persist Render Data", source.persist_render_data));
        text.fields.push_back(has_file_name && source.IsSupportedExternalSource()
                                  ? "Supported external source"
                                  : "Unsupported source combination");
        result.push_back(std::move(text));
    }
    return result;
}

} // namespace rerevved::studio
