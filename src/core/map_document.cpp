#include "map_document.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::studio
{

std::string_view MapDocumentErrorMessage(MapDocumentError error)
{
    switch (error)
    {
        case MapDocumentError::truncated:
            return "The map record is truncated; the Xbox DLC profile requires 1088 bytes.";
        case MapDocumentError::unsupported_length:
            return "The map record length is unsupported; the Xbox DLC profile requires exactly 1088 bytes.";
    }
    return "The map record is invalid.";
}

std::expected<MapDocument, MapDocumentError>
ParseMapDocument(std::span<const std::byte> bytes)
{
    if (bytes.size() < kMapEnvelopeSize)
        return std::unexpected(MapDocumentError::truncated);
    if (bytes.size() > kMapEnvelopeSize)
        return std::unexpected(MapDocumentError::unsupported_length);

    MapDocument document{};
    std::ranges::copy(bytes.first<kMapCoreSize>(), document.core.begin());
    std::ranges::copy(bytes.last<kMapFooterSize>(), document.footer.begin());
    return document;
}

std::expected<MapDocument, std::string>
LoadMapDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return std::unexpected("Could not open map file for reading.");

    const auto end = input.tellg();
    if (end < 0)
        return std::unexpected("Could not read map file size.");
    const auto size = static_cast<std::uintmax_t>(end);
    if (size < kMapEnvelopeSize)
        return std::unexpected(std::string(MapDocumentErrorMessage(MapDocumentError::truncated)));
    if (size > kMapEnvelopeSize)
        return std::unexpected(
            std::string(MapDocumentErrorMessage(MapDocumentError::unsupported_length)));

    std::vector<std::byte> bytes(kMapEnvelopeSize);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
        return std::unexpected("Could not read map file data.");
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
        return std::unexpected("Map file ended before its reported size.");

    auto document = ParseMapDocument(bytes);
    if (!document)
        return std::unexpected(std::string(MapDocumentErrorMessage(document.error())));
    return std::move(*document);
}

} // namespace rerevved::studio
