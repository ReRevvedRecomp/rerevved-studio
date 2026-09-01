#include "nif_inspector.h"
#include "nif_inspector_format.h"

#include <imgui.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace rerevved::studio
{
namespace
{

std::string FormatString(const NifDocument& document, std::uint32_t index)
{
    if (index == std::numeric_limits<std::uint32_t>::max())
        return "<none>";
    return FormatNifStringBytes(document.strings[index]);
}

void DrawReference(std::string_view label, std::uint32_t value)
{
    if (value == std::numeric_limits<std::uint32_t>::max())
        ImGui::Text("%.*s: none", static_cast<int>(label.size()), label.data());
    else
        ImGui::Text("%.*s: %u",
                    static_cast<int>(label.size()),
                    label.data(),
                    static_cast<unsigned int>(value));
}

void DrawReferences(std::string_view label, std::span<const std::uint32_t> values)
{
    ImGui::Text("%.*s: %zu", static_cast<int>(label.size()), label.data(), values.size());
    for (const auto value : values)
    {
        ImGui::SameLine();
        if (value == std::numeric_limits<std::uint32_t>::max())
            ImGui::TextUnformatted("none");
        else
            ImGui::Text("%u", static_cast<unsigned int>(value));
    }
}

void DrawAvObject(const NifDocument& document, const NifAvObject& object)
{
    ImGui::Text("Name: %s", FormatString(document, object.name_index).c_str());
    ImGui::Text("Flags: 0x%04X", static_cast<unsigned int>(object.flags));
    ImGui::Text("Translation: %.9g, %.9g, %.9g",
                static_cast<double>(object.translation[0]),
                static_cast<double>(object.translation[1]),
                static_cast<double>(object.translation[2]));
    ImGui::Text("Rotation: %.9g, %.9g, %.9g",
                static_cast<double>(object.rotation[0]),
                static_cast<double>(object.rotation[1]),
                static_cast<double>(object.rotation[2]));
    ImGui::Text("          %.9g, %.9g, %.9g",
                static_cast<double>(object.rotation[3]),
                static_cast<double>(object.rotation[4]),
                static_cast<double>(object.rotation[5]));
    ImGui::Text("          %.9g, %.9g, %.9g",
                static_cast<double>(object.rotation[6]),
                static_cast<double>(object.rotation[7]),
                static_cast<double>(object.rotation[8]));
    ImGui::Text("Scale: %.9g", static_cast<double>(object.scale));
    DrawReferences("Extra data", object.extra_data);
    DrawReference("Controller", object.controller);
    DrawReferences("Properties", object.properties);
    DrawReference("Collision object", object.collision_object);
}

void DrawWrapped(std::string_view text)
{
    const auto width = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
}

} // namespace

void DrawNifInspector(const NifDocument* document, std::string_view error)
{
    ImGui::SeparatorText("NIF container");
    if (!document)
    {
        if (error.empty())
            ImGui::TextWrapped("The NIF container could not be inspected.");
        else
            ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
        return;
    }

    ImGui::Text("Version: %u.%u.%u.%u",
                static_cast<unsigned int>((document->version >> 24U) & 0xFFU),
                static_cast<unsigned int>((document->version >> 16U) & 0xFFU),
                static_cast<unsigned int>((document->version >> 8U) & 0xFFU),
                static_cast<unsigned int>(document->version & 0xFFU));
    ImGui::TextUnformatted("Endian: big");
    ImGui::Text("User version: %u", static_cast<unsigned int>(document->user_version));
    ImGui::Text("Header size: %zu bytes", document->header_size);
    ImGui::Text("Blocks: %zu", document->blocks.size());
    ImGui::Text("Strings: %zu", document->strings.size());
    ImGui::Text("Groups: %zu", document->groups.size());
    ImGui::Text("Roots: %zu", document->roots.size());

    ImGui::SeparatorText("Block types");
    std::vector<std::size_t> block_counts(document->block_types.size());
    for (const auto& block : document->blocks)
        ++block_counts[block.type_index];
    for (std::size_t index = 0; index < document->block_types.size(); ++index)
    {
        ImGui::Text("%zu: %s (%zu)",
                    index,
                    FormatNifStringBytes(document->block_types[index]).c_str(),
                    block_counts[index]);
    }

    ImGui::SeparatorText("Scene objects");
    ImGui::Text("Nodes: %zu", document->nodes.size());
    for (const auto& node : document->nodes)
    {
        ImGui::PushID(static_cast<int>(node.block_index));
        if (ImGui::TreeNode(
                "node", "Block %u: NiNode", static_cast<unsigned int>(node.block_index)))
        {
            DrawAvObject(*document, node.object);
            DrawReferences("Children", node.children);
            DrawReferences("Effects", node.effects);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Text("Triangle shapes: %zu", document->tri_shapes.size());
    for (const auto& shape : document->tri_shapes)
    {
        ImGui::PushID(static_cast<int>(shape.block_index));
        if (ImGui::TreeNode(
                "shape", "Block %u: NiTriShape", static_cast<unsigned int>(shape.block_index)))
        {
            DrawAvObject(*document, shape.object);
            DrawReference("Data", shape.data);
            DrawReference("Skin instance", shape.skin_instance);
            ImGui::Text("Materials: %zu", shape.material.name_indices.size());
            for (std::size_t index = 0; index < shape.material.name_indices.size(); ++index)
            {
                ImGui::Text("%zu: %s, extra %d",
                            index,
                            FormatString(*document, shape.material.name_indices[index]).c_str(),
                            static_cast<int>(shape.material.extra_data[index]));
            }
            ImGui::Text("Active material: %d",
                        static_cast<int>(shape.material.active_material));
            ImGui::Text("Material needs update: %u",
                        static_cast<unsigned int>(shape.material.material_needs_update));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Text("Triangle data blocks: %zu", document->tri_shape_data.size());
    for (const auto& data : document->tri_shape_data)
    {
        ImGui::PushID(static_cast<int>(data.block_index));
        if (ImGui::TreeNode("geometry",
                            "Block %u: NiTriShapeData",
                            static_cast<unsigned int>(data.block_index)))
        {
            ImGui::Text("Group ID: %d", static_cast<int>(data.group_id));
            ImGui::Text("Vertices: %u", static_cast<unsigned int>(data.vertex_count));
            ImGui::Text("Keep flags: 0x%02X", static_cast<unsigned int>(data.keep_flags));
            ImGui::Text("Compress flags: 0x%02X",
                        static_cast<unsigned int>(data.compress_flags));
            ImGui::Text("Has vertices: 0x%02X",
                        static_cast<unsigned int>(data.has_vertices));
            ImGui::Text("Data flags: 0x%04X", static_cast<unsigned int>(data.data_flags));
            ImGui::Text("UV sets: %u",
                        static_cast<unsigned int>(data.data_flags & 0x003FU));
            ImGui::Text("Has normals: 0x%02X", static_cast<unsigned int>(data.has_normals));
            ImGui::Text("Has tangents: %s",
                        data.has_normals != 0 && (data.data_flags & 0x1000U) != 0 ? "yes" : "no");
            ImGui::Text("Bound center: %.9g, %.9g, %.9g",
                        static_cast<double>(data.bound_center[0]),
                        static_cast<double>(data.bound_center[1]),
                        static_cast<double>(data.bound_center[2]));
            ImGui::Text("Bound radius: %.9g", static_cast<double>(data.bound_radius));
            ImGui::Text("Has vertex colors: 0x%02X",
                        static_cast<unsigned int>(data.has_vertex_colors));
            ImGui::Text("Consistency flags: 0x%04X",
                        static_cast<unsigned int>(data.consistency_flags));
            DrawReference("Additional data", data.additional_data);
            ImGui::Text("Triangles: %u", static_cast<unsigned int>(data.triangle_count));
            ImGui::Text("Triangle points: %u",
                        static_cast<unsigned int>(data.triangle_point_count));
            ImGui::Text("Has triangles: 0x%02X",
                        static_cast<unsigned int>(data.has_triangles));
            ImGui::Text("Match groups: %u",
                        static_cast<unsigned int>(data.match_group_count));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Material properties");
    const auto material_text = FormatNifMaterialProperties(document->material_properties);
    if (material_text.empty())
        DrawWrapped("No NiMaterialProperty inventories are retained.");
    for (std::size_t index = 0; index < material_text.size(); ++index)
    {
        if (index != 0)
            ImGui::Separator();
        DrawWrapped(material_text[index].heading);
        for (const auto& field : material_text[index].fields)
            DrawWrapped(field);
    }

    ImGui::SeparatorText("Texturing properties");
    const auto texturing_text = FormatNifTexturingProperties(document->texturing_properties);
    if (texturing_text.empty())
        DrawWrapped("No NiTexturingProperty inventories are retained.");
    for (std::size_t index = 0; index < texturing_text.size(); ++index)
    {
        if (index != 0)
            ImGui::Separator();
        DrawWrapped(texturing_text[index].heading);
        for (const auto& field : texturing_text[index].fields)
            DrawWrapped(field);
    }

    ImGui::SeparatorText("Source textures");
    const auto source_text = FormatNifSourceTextures(*document);
    if (source_text.empty())
        DrawWrapped("No NiSourceTexture inventories are retained.");
    for (std::size_t index = 0; index < source_text.size(); ++index)
    {
        if (index != 0)
            ImGui::Separator();
        DrawWrapped(source_text[index].heading);
        for (const auto& field : source_text[index].fields)
            DrawWrapped(field);
    }
}

} // namespace rerevved::studio
