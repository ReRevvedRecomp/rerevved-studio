#include "fpk_extraction.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>

namespace rerevved::studio
{

std::expected<void, FpkExtractionError>
ExtractFpkEntry(std::span<const std::byte>   source,
                const FpkIndex&              index,
                std::size_t                  selected_entry,
                const std::filesystem::path& destination)
{
    if (selected_entry >= index.entries.size())
        return std::unexpected(FpkExtractionError::invalid_selection);

    const auto& entry = index.entries[selected_entry];
    if (entry.size > std::numeric_limits<std::uint32_t>::max() - entry.offset)
        return std::unexpected(FpkExtractionError::invalid_entry_range);

    const auto offset = static_cast<std::size_t>(entry.offset);
    const auto size   = static_cast<std::size_t>(entry.size);
    if (offset > source.size() || size > source.size() - offset)
        return std::unexpected(FpkExtractionError::invalid_entry_range);

    std::ofstream output(destination, std::ios::binary | std::ios::noreplace);
    if (!output)
    {
        std::error_code error;
        const auto      destination_status = std::filesystem::symlink_status(destination, error);
        if (!error && destination_status.type() != std::filesystem::file_type::not_found)
            return std::unexpected(FpkExtractionError::destination_exists);
        return std::unexpected(FpkExtractionError::open_failed);
    }

    const auto payload = source.subspan(offset, size);
    output.write(reinterpret_cast<const char*>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));
    if (!output)
        return std::unexpected(FpkExtractionError::write_failed);

    output.close();
    if (output.fail())
        return std::unexpected(FpkExtractionError::finalization_failed);
    return {};
}

} // namespace rerevved::studio
