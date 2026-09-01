#include "map_preview.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace rerevved::studio
{
namespace
{

std::string FormatFooter(std::span<const std::byte> footer)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t offset = 0; offset < footer.size(); ++offset)
    {
        if (offset != 0)
            output << (offset % 16 == 0 ? '\n' : ' ');
        output << std::setw(2) << std::to_integer<unsigned int>(footer[offset]);
    }
    return output.str();
}

void DrawCore(const MapDocument& document)
{
    ImGui::TextUnformatted("Linear byte grid; player-facing orientation is unresolved.");
    const float  available = ImGui::GetContentRegionAvail().x;
    const float  cell_size = std::clamp(std::floor(available / static_cast<float>(kMapWidth)),
                                        4.0F,
                                        18.0F);
    const float  grid_size = cell_size * static_cast<float>(kMapWidth);
    const ImVec2 origin    = ImGui::GetCursorScreenPos();
    auto*        draw_list = ImGui::GetWindowDrawList();
    for (std::size_t row = 0; row < kMapHeight; ++row)
    {
        for (std::size_t column = 0; column < kMapWidth; ++column)
        {
            const auto   offset = row * kMapWidth + column;
            const auto   value  = std::to_integer<std::uint8_t>(document.core[offset]);
            const auto   shade  = static_cast<std::uint8_t>(20U + (value & 0x0FU) * 15U);
            const ImVec2 minimum(origin.x + static_cast<float>(column) * cell_size,
                                 origin.y + static_cast<float>(row) * cell_size);
            const ImVec2 maximum(minimum.x + cell_size, minimum.y + cell_size);
            draw_list->AddRectFilled(minimum, maximum, IM_COL32(shade, shade, shade, 255));
            draw_list->AddRect(minimum, maximum, IM_COL32(55, 55, 55, 255));
        }
    }

    ImGui::InvisibleButton("##map-core", ImVec2(grid_size, grid_size));
    if (!ImGui::IsItemHovered())
        return;

    const auto mouse  = ImGui::GetMousePos();
    const auto column = std::min(
        static_cast<std::size_t>((mouse.x - origin.x) / cell_size), kMapWidth - 1);
    const auto row = std::min(
        static_cast<std::size_t>((mouse.y - origin.y) / cell_size), kMapHeight - 1);
    const auto offset = row * kMapWidth + column;
    const auto value  = std::to_integer<std::uint8_t>(document.core[offset]);
    ImGui::BeginTooltip();
    ImGui::Text("File offset: %zu", offset);
    ImGui::Text("Raw byte: 0x%02X", static_cast<unsigned int>(value));
    ImGui::Text("Low nibble: 0x%X", static_cast<unsigned int>(value & 0x0FU));
    for (const auto flag : { 0x10U, 0x20U, 0x40U, 0x80U })
        ImGui::Text("0x%02X: %s", flag, (value & flag) != 0 ? "set" : "clear");
    ImGui::EndTooltip();
}

} // namespace

void DrawMapPreview(const MapDocument* document, std::string_view error)
{
    ImGui::Begin("Preview");
    if (!document)
    {
        if (error.empty())
            ImGui::TextWrapped("The Xbox DLC map record could not be inspected.");
        else
            ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Map core - 1024 bytes");
    DrawCore(*document);

    ImGui::SeparatorText("Footer - 64 bytes");
    const bool all_ff = std::ranges::all_of(document->footer,
                                            [](std::byte value)
                                            {
                                                return value == std::byte{ 0xFF };
                                            });
    ImGui::TextUnformatted(all_ff ? "Observed pattern: all 0xFF."
                                  : "Contains non-0xFF bytes; preserved without validation.");
    ImGui::TextUnformatted(FormatFooter(document->footer).c_str());
    ImGui::End();
}

} // namespace rerevved::studio
