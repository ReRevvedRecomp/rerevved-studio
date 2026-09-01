#include "inspection_format.h"

#include <cstddef>
#include <iomanip>
#include <sstream>

namespace rerevved::studio
{

std::string FormatHeader(const FileInspection& inspection)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t index = 0; index < inspection.header_size; ++index)
    {
        if (index != 0)
            output << ' ';
        output << std::setw(2) << std::to_integer<unsigned>(inspection.header[index]);
    }
    return output.str();
}

std::string FormatByteSize(std::uintmax_t bytes)
{
    constexpr std::uintmax_t kibibyte = 1024;
    constexpr std::uintmax_t mebibyte = kibibyte * 1024;
    constexpr std::uintmax_t gibibyte = mebibyte * 1024;

    std::ostringstream output;
    output << std::fixed << std::setprecision(2);
    if (bytes >= gibibyte)
        output << static_cast<double>(bytes) / static_cast<double>(gibibyte) << " GiB";
    else if (bytes >= mebibyte)
        output << static_cast<double>(bytes) / static_cast<double>(mebibyte) << " MiB";
    else if (bytes >= kibibyte)
        output << static_cast<double>(bytes) / static_cast<double>(kibibyte) << " KiB";
    else
        output << bytes << " bytes";
    return output.str();
}

} // namespace rerevved::studio
