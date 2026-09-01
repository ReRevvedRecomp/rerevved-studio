#pragma once

#include "fpk_index.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>

namespace rerevved::studio
{

enum class FpkExtractionError
{
    invalid_selection,
    invalid_entry_range,
    destination_exists,
    open_failed,
    write_failed,
    finalization_failed,
};

[[nodiscard]] std::expected<void, FpkExtractionError>
ExtractFpkEntry(std::span<const std::byte>   source,
                const FpkIndex&              index,
                std::size_t                  selected_entry,
                const std::filesystem::path& destination);

} // namespace rerevved::studio
