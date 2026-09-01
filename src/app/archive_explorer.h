#pragma once

#include "dds_viewer.h"
#include "fpk_document.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace rerevved::studio
{

// ImGuiListClipper reserves INT_MAX for an unknown item count.
inline constexpr std::size_t kMaximumArchiveNavigationEntries =
    static_cast<std::size_t>(std::numeric_limits<int>::max()) - 1;

inline std::string FormatBytes(std::span<const std::byte> bytes)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        if (index != 0)
            output << ' ';
        output << std::setw(2) << std::to_integer<unsigned int>(bytes[index]);
    }
    return output.str();
}

struct ExtractionModalLayout
{
    float content_width;
    float button_width;
    bool  stack_buttons;
};

[[nodiscard]] constexpr ExtractionModalLayout CalculateExtractionModalLayout(
    float work_width,
    float horizontal_window_padding,
    float item_spacing)
{
    const auto content_width =
        std::max(1.0F,
                 std::min(600.0F, work_width - horizontal_window_padding * 2.0F));
    const auto paired_button_width =
        std::min(120.0F, std::max(1.0F, (content_width - item_spacing) / 2.0F));
    const bool stack_buttons = paired_button_width < 120.0F;
    const auto button_width =
        stack_buttons ? std::min(120.0F, content_width) : paired_button_width;
    return { content_width, button_width, stack_buttons };
}

[[nodiscard]] constexpr float CalculateArchiveLabeledControlWidth(
    float available_width,
    float preferred_width,
    float label_width,
    float label_spacing)
{
    return std::min(
        preferred_width,
        std::max(1.0F, available_width - label_width - label_spacing));
}

struct ArchiveActionRowLayout
{
    float first_width;
    float second_width;
    bool  same_line;
};

[[nodiscard]] constexpr ArchiveActionRowLayout CalculateArchiveActionRowLayout(
    float available_width,
    float preferred_first_width,
    float first_label_width,
    float preferred_second_width,
    float item_spacing,
    float label_spacing)
{
    const auto first_width = CalculateArchiveLabeledControlWidth(
        available_width, preferred_first_width, first_label_width, label_spacing);
    const auto second_width =
        std::min(preferred_second_width, std::max(1.0F, available_width));
    const auto preferred_row_width = preferred_first_width + first_label_width +
                                     label_spacing + item_spacing + preferred_second_width;
    return { first_width, second_width, preferred_row_width <= available_width };
}

struct ArchiveExplorerState
{
    std::optional<std::size_t>      selected_entry;
    FpkEntryFormat                  selected_format = FpkEntryFormat::dds;
    std::optional<FpkEntryDocument> opened_document;
    std::string                     open_error;
    std::array<char, 2048>          extraction_path{};
    std::string                     extraction_result;
    std::string                     metadata_result;
    std::uint64_t                   requested_entry = 1;
    std::string                     navigation_error;
};

inline void CloseOpenedArchiveEntry(ArchiveExplorerState& state)
{
    state.opened_document.reset();
}

[[nodiscard]] inline bool SelectArchiveEntry(ArchiveExplorerState& state,
                                             std::size_t           entry_count,
                                             std::uint64_t         one_based_entry)
{
    if (entry_count == 0)
    {
        state.navigation_error = "This FPK archive has no entries.";
        return false;
    }
    if (one_based_entry == 0 || one_based_entry > entry_count)
    {
        state.navigation_error =
            "Enter an entry number from 1 to " + std::to_string(entry_count) + ".";
        return false;
    }

    const auto selected_entry = static_cast<std::size_t>(one_based_entry - 1);
    state.requested_entry     = one_based_entry;
    state.navigation_error.clear();
    if (state.selected_entry && *state.selected_entry == selected_entry)
        return true;

    state.selected_entry = selected_entry;
    state.opened_document.reset();
    state.open_error.clear();
    state.extraction_result.clear();
    state.metadata_result.clear();
    return true;
}

inline void SelectInitialArchiveEntry(ArchiveExplorerState& state,
                                      const FpkDocument&    document)
{
    if (state.selected_entry || document.index.entries.empty())
        return;
    (void)SelectArchiveEntry(state, document.index.entries.size(), 1);
}

void DrawArchiveInspector(const FpkDocument*    document,
                          ArchiveExplorerState& state,
                          std::string_view      error);

void DrawArchivePreview(const std::filesystem::path& archive_path,
                        const ArchiveExplorerState&  state,
                        DdsViewer&                   dds_viewer);

} // namespace rerevved::studio
