#include "archive_explorer.h"
#include "fpk_extraction.h"
#include "gfx_inspector.h"
#include "inspection_format.h"
#include "map_preview.h"
#include "mp3_preview.h"
#include "nif_inspector.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace rerevved::studio
{
namespace
{

constexpr std::array<const char*, 5> kFormatNames{
    "DDS texture", "GFX movie", "Gamebryo NIF", "MP3 audio", "Xbox DLC map"
};

std::string_view FormatName(FpkEntryFormat format)
{
    return kFormatNames[static_cast<std::size_t>(format)];
}

std::string ExtractionErrorMessage(FpkExtractionError error)
{
    switch (error)
    {
        case FpkExtractionError::invalid_selection:
        case FpkExtractionError::invalid_entry_range:
            std::unreachable();
        case FpkExtractionError::destination_exists:
            return "The extraction destination already exists and was not overwritten.";
        case FpkExtractionError::open_failed:
            return "The extraction destination could not be created.";
        case FpkExtractionError::write_failed:
            return "The selected entry could not be written completely.";
        case FpkExtractionError::finalization_failed:
            return "The extracted file could not be finalized.";
    }
    std::unreachable();
}

void DrawOpenedInspector(ArchiveExplorerState& state)
{
    if (!state.opened_document)
        return;

    const auto close_button_width = CalculateArchiveLabeledControlWidth(
        ImGui::GetContentRegionAvail().x,
        ImGui::CalcTextSize("Close opened entry").x +
            ImGui::GetStyle().FramePadding.x * 2.0F,
        0.0F,
        0.0F);
    if (ImGui::Button("Close opened entry", ImVec2(close_button_width, 0.0F)))
    {
        CloseOpenedArchiveEntry(state);
        return;
    }

    const auto& data = state.opened_document->data;
    if (const auto* gfx = std::get_if<GfxDocument>(&data))
        DrawGfxInspector(gfx, {});
    else if (const auto* nif = std::get_if<NifDocument>(&data))
        DrawNifInspector(nif, {});
    else
        ImGui::TextWrapped("Entry %zu is open as %s in Preview.",
                           state.opened_document->entry_index + 1,
                           FormatName(state.opened_document->format).data());
}

void DrawExtractionPopup(const FpkDocument& document, ArchiveExplorerState& state)
{
    if (!ImGui::BeginPopupModal(
            "Extract selected FPK entry", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const auto& style  = ImGui::GetStyle();
    const auto  layout = CalculateExtractionModalLayout(ImGui::GetMainViewport()->WorkSize.x,
                                                        style.WindowPadding.x,
                                                        style.ItemSpacing.x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + layout.content_width);
    ImGui::TextUnformatted("Enter a new destination path. Existing paths are never overwritten.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(layout.content_width);
    const bool submitted = ImGui::InputText("##fpk-extraction-path",
                                            state.extraction_path.data(),
                                            state.extraction_path.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    const bool extract_clicked =
        ImGui::Button("Extract", ImVec2(layout.button_width, 0.0F));
    if (submitted || extract_clicked)
    {
        const std::filesystem::path destination(state.extraction_path.data());
        auto                        result = ExtractFpkEntry(
            document.bytes, document.index, *state.selected_entry, destination);
        if (result)
        {
            state.extraction_result = "Extracted entry " +
                                      std::to_string(*state.selected_entry + 1) +
                                      " to a new destination.";
            state.extraction_path.fill('\0');
            ImGui::CloseCurrentPopup();
        }
        else
        {
            state.extraction_result = ExtractionErrorMessage(result.error());
        }
    }
    if (!layout.stack_buttons)
        ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(layout.button_width, 0.0F)))
        ImGui::CloseCurrentPopup();
    if (!state.extraction_result.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + layout.content_width);
        ImGui::TextUnformatted(state.extraction_result.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

} // namespace

void DrawArchiveInspector(const FpkDocument*    document,
                          ArchiveExplorerState& state,
                          std::string_view      error)
{
    ImGui::SeparatorText("FPK archive");
    if (!document)
    {
        ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
        return;
    }

    const auto& style        = ImGui::GetStyle();
    const auto  button_width = [&style](const char* label)
    {
        return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0F;
    };

    ImGui::Text("Version: %u", static_cast<unsigned int>(document->index.version));
    ImGui::Text("Entries: %zu", document->index.entries.size());
    ImGui::TextWrapped("Header unknown: %s",
                       FormatBytes(document->index.header_unknown).c_str());

    const auto entry_count = document->index.entries.size();
    const auto navigation_entry_count =
        std::min(entry_count, kMaximumArchiveNavigationEntries);
    bool scroll_to_selected_entry = false;
    if (entry_count > navigation_entry_count)
    {
        ImGui::TextWrapped("This archive exceeds the entry-list limit. Only entries 1 through "
                           "%zu can be selected.",
                           navigation_entry_count);
    }
    const auto entry_row = CalculateArchiveActionRowLayout(
        ImGui::GetContentRegionAvail().x,
        140.0F,
        ImGui::CalcTextSize("Entry number").x,
        button_width("Go to entry"),
        style.ItemSpacing.x,
        style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(entry_row.first_width);
    ImGui::BeginDisabled(navigation_entry_count == 0);
    const bool jump_submitted = ImGui::InputScalar("Entry number",
                                                   ImGuiDataType_U64,
                                                   &state.requested_entry,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
    if (entry_row.same_line)
        ImGui::SameLine();
    const bool jump_clicked =
        ImGui::Button("Go to entry", ImVec2(entry_row.second_width, 0.0F));
    ImGui::EndDisabled();
    if (jump_submitted || jump_clicked)
        scroll_to_selected_entry =
            SelectArchiveEntry(state, navigation_entry_count, state.requested_entry);

    const auto navigation_row = CalculateArchiveActionRowLayout(
        ImGui::GetContentRegionAvail().x,
        button_width("Previous"),
        0.0F,
        button_width("Next"),
        style.ItemSpacing.x,
        0.0F);
    const bool can_select_previous = state.selected_entry && *state.selected_entry > 0;
    ImGui::BeginDisabled(!can_select_previous);
    if (ImGui::Button("Previous", ImVec2(navigation_row.first_width, 0.0F)))
        scroll_to_selected_entry =
            SelectArchiveEntry(state, navigation_entry_count, *state.selected_entry);
    ImGui::EndDisabled();

    if (navigation_row.same_line)
        ImGui::SameLine();
    const bool can_select_next = state.selected_entry && navigation_entry_count > 0 &&
                                 *state.selected_entry < navigation_entry_count - 1;
    ImGui::BeginDisabled(!can_select_next);
    if (ImGui::Button("Next", ImVec2(navigation_row.second_width, 0.0F)))
        scroll_to_selected_entry =
            SelectArchiveEntry(state, navigation_entry_count, *state.selected_entry + 2);
    ImGui::EndDisabled();

    if (!state.navigation_error.empty())
        ImGui::TextWrapped("%s", state.navigation_error.c_str());

    if (ImGui::BeginListBox("##fpk-entries", ImVec2(-1.0F, 180.0F)))
    {
        const auto       list_entry_count = static_cast<int>(navigation_entry_count);
        ImGuiListClipper clipper;
        clipper.Begin(list_entry_count);
        if (scroll_to_selected_entry && state.selected_entry &&
            *state.selected_entry < static_cast<std::size_t>(list_entry_count))
        {
            clipper.IncludeItemByIndex(static_cast<int>(*state.selected_entry));
        }
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const auto index       = static_cast<std::size_t>(row);
                const auto label       = "Entry " + std::to_string(index + 1);
                const bool is_selected = state.selected_entry && *state.selected_entry == index;
                if (ImGui::Selectable(label.c_str(), is_selected))
                    (void)SelectArchiveEntry(state, navigation_entry_count, index + 1);
                if (scroll_to_selected_entry && is_selected)
                {
                    ImGui::SetScrollHereY(0.5F);
                    scroll_to_selected_entry = false;
                }
            }
        }
        ImGui::EndListBox();
    }

    if (entry_count == 0)
    {
        ImGui::TextUnformatted("This archive contains no entries.");
        return;
    }

    if (!state.selected_entry)
    {
        ImGui::TextUnformatted("Select a numbered entry to inspect it.");
        return;
    }

    const auto& entry = document->index.entries[*state.selected_entry];
    ImGui::SeparatorText("Selected entry");
    const auto metadata_width = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    const auto draw_wrapped   = [metadata_width](const char* text)
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + metadata_width);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
    };
    const auto entry_range = FormatFpkEntryRange(*state.selected_entry + 1, entry);
    draw_wrapped(entry_range.c_str());
    const auto copy_button_width = CalculateArchiveLabeledControlWidth(
        metadata_width, button_width("Copy byte range"), 0.0F, 0.0F);
    if (ImGui::Button("Copy byte range", ImVec2(copy_button_width, 0.0F)))
    {
        ImGui::SetClipboardText(entry_range.c_str());
        state.metadata_result = "Copied selected entry byte range.";
    }
    if (!state.metadata_result.empty())
        draw_wrapped(state.metadata_result.c_str());
    const auto unknown0 = "Unknown 0: " + FormatBytes(entry.unknown0);
    draw_wrapped(unknown0.c_str());
    const auto unknown1 = "Unknown 1: " + FormatBytes(entry.unknown1);
    draw_wrapped(unknown1.c_str());

    int selected_format = static_cast<int>(state.selected_format);
    ImGui::SetNextItemWidth(CalculateArchiveLabeledControlWidth(
        ImGui::GetContentRegionAvail().x,
        220.0F,
        ImGui::CalcTextSize("Open as").x,
        style.ItemInnerSpacing.x));
    if (ImGui::Combo("Open as",
                     &selected_format,
                     kFormatNames.data(),
                     static_cast<int>(kFormatNames.size())))
    {
        state.selected_format = static_cast<FpkEntryFormat>(selected_format);
        state.opened_document.reset();
        state.nif_preview.reset();
        state.open_error.clear();
    }
    const auto open_row = CalculateArchiveActionRowLayout(
        ImGui::GetContentRegionAvail().x,
        button_width("Open selected in memory"),
        0.0F,
        button_width("Extract selected..."),
        style.ItemSpacing.x,
        0.0F);
    if (ImGui::Button("Open selected in memory", ImVec2(open_row.first_width, 0.0F)))
        OpenSelectedArchiveEntryInMemory(*document, state);
    if (open_row.same_line)
        ImGui::SameLine();
    if (ImGui::Button("Extract selected...", ImVec2(open_row.second_width, 0.0F)))
    {
        state.extraction_result.clear();
        ImGui::OpenPopup("Extract selected FPK entry");
    }

    if (!state.open_error.empty())
        ImGui::TextWrapped("%s", state.open_error.c_str());
    if (!state.extraction_result.empty())
        ImGui::TextWrapped("%s", state.extraction_result.c_str());
    DrawOpenedInspector(state);
    DrawExtractionPopup(*document, state);
}

void DrawArchivePreview(const std::filesystem::path& archive_path,
                        ArchiveExplorerState&        state,
                        DdsViewer&                   dds_viewer)
{
    if (state.nif_preview)
    {
        const NifDocument* nif = nullptr;
        if (state.opened_document)
            nif = std::get_if<NifDocument>(&state.opened_document->data);
        dds_viewer.Clear();
        DrawNifPreview(nif,
                       state.nif_preview->model ? &*state.nif_preview->model : nullptr,
                       state.nif_preview->assembly_error,
                       state.open_error,
                       state.nif_preview->navigation);
        return;
    }

    if (!state.opened_document)
    {
        dds_viewer.Draw(nullptr, {}, "Open a supported FPK entry to preview it.");
        return;
    }

    const auto& data = state.opened_document->data;
    if (const auto* dds = std::get_if<DdsDocument>(&data))
    {
        const auto source_id = archive_path.string() + "#entry-" +
                               std::to_string(state.opened_document->entry_index + 1);
        dds_viewer.Draw(dds, source_id, {});
    }
    else if (const auto* mp3 = std::get_if<Mp3Document>(&data))
    {
        dds_viewer.Clear();
        DrawMp3Preview(mp3, {});
    }
    else if (const auto* map = std::get_if<MapDocument>(&data))
    {
        dds_viewer.Clear();
        DrawMapPreview(map, {});
    }
    else
    {
        dds_viewer.Draw(nullptr, {}, "The opened FPK entry is shown in Inspector.");
    }
}

} // namespace rerevved::studio
