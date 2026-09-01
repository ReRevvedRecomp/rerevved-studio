#include "gfx_inspector.h"
#include "inspection_format.h"

#include <imgui.h>

#include <cstdint>

namespace rerevved::studio
{

void DrawGfxInspector(const GfxDocument* document, std::string_view error)
{
    ImGui::SeparatorText("GFX movie");
    if (!document)
    {
        if (error.empty())
            ImGui::TextWrapped("The GFX movie could not be inspected.");
        else
            ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
        return;
    }

    const auto width_twips  = static_cast<std::int64_t>(document->frame.x_max_twips) -
                              document->frame.x_min_twips;
    const auto height_twips = static_cast<std::int64_t>(document->frame.y_max_twips) -
                              document->frame.y_min_twips;
    ImGui::Text("File version: %u", static_cast<unsigned int>(document->file_version));
    ImGui::Text("Declared length: %u bytes",
                static_cast<unsigned int>(document->declared_length));
    ImGui::Text("Frame X: %d to %d twips",
                document->frame.x_min_twips,
                document->frame.x_max_twips);
    ImGui::Text("Frame Y: %d to %d twips",
                document->frame.y_min_twips,
                document->frame.y_max_twips);
    ImGui::Text("Frame size: %.2f x %.2f pixels",
                static_cast<double>(width_twips) / 20.0,
                static_cast<double>(height_twips) / 20.0);
    ImGui::Text("Frame rate: %.2f (%u raw)",
                static_cast<double>(document->frame_rate_raw) / 256.0,
                static_cast<unsigned int>(document->frame_rate_raw));
    ImGui::Text("Frame count: %u", static_cast<unsigned int>(document->frame_count));

    ImGui::SeparatorText("Exporter");
    ImGui::Text("Version: %u.%u",
                static_cast<unsigned int>(document->exporter.version >> 8U),
                static_cast<unsigned int>(document->exporter.version & 0xFFU));
    ImGui::Text("Flags: 0x%08X", static_cast<unsigned int>(document->exporter.flags));
    ImGui::Text("Bitmap format: %u",
                static_cast<unsigned int>(document->exporter.bitmap_format));
    ImGui::Text("Prefix: %s", FormatGfxBytes(document->exporter.prefix).c_str());
    ImGui::Text("SWF name: %s", FormatGfxBytes(document->exporter.swf_name).c_str());

    ImGui::SeparatorText("External images");
    if (document->external_images.empty())
    {
        ImGui::TextUnformatted("None");
        return;
    }

    for (std::size_t index = 0; index < document->external_images.size(); ++index)
    {
        const auto& image = document->external_images[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::Text("Resource %zu", index + 1);
        ImGui::Text("Export name: %s", FormatGfxBytes(image.export_name).c_str());
        ImGui::Text("File name: %s", FormatGfxBytes(image.file_name).c_str());
        ImGui::Text("Character ID: %u", static_cast<unsigned int>(image.character_id));
        ImGui::Text("Bitmap format: %u", static_cast<unsigned int>(image.bitmap_format));
        ImGui::Text("Target size: %u x %u",
                    static_cast<unsigned int>(image.target_width),
                    static_cast<unsigned int>(image.target_height));
        ImGui::PopID();
    }
}

} // namespace rerevved::studio
