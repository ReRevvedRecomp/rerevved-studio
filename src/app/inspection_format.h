#pragma once

#include "file_inspection.h"

#include <cstdint>
#include <string>

namespace rerevved::studio
{

[[nodiscard]] std::string FormatHeader(const FileInspection& inspection);
[[nodiscard]] std::string FormatByteSize(std::uintmax_t bytes);

} // namespace rerevved::studio
