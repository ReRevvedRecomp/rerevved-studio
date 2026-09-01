#pragma once

#include "file_inspection.h"
#include "fpk_index.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace rerevved::studio
{

[[nodiscard]] std::string FormatHeader(const FileInspection& inspection);
[[nodiscard]] std::string FormatByteSize(std::uintmax_t bytes);
[[nodiscard]] std::string FormatFpkEntryRange(std::size_t     one_based_entry,
                                              const FpkEntry& entry);
[[nodiscard]] std::string FormatGfxBytes(std::span<const std::byte> bytes);

} // namespace rerevved::studio
