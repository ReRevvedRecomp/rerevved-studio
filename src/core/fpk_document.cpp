#include "fpk_document.h"

#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace rerevved::studio
{
namespace
{

std::string_view IndexErrorMessage(FpkIndexError error)
{
    switch (error)
    {
        case FpkIndexError::truncated:
            return "The FPK archive table is truncated.";
        case FpkIndexError::invalid_signature:
            return "The file does not have a valid FPK signature.";
        case FpkIndexError::unsupported_version:
            return "The FPK archive version is unsupported.";
        case FpkIndexError::count_overflow:
            return "The FPK entry count exceeds the bounded archive table.";
        case FpkIndexError::entry_range_overflow:
            return "An FPK entry byte range overflows.";
        case FpkIndexError::entry_out_of_range:
            return "An FPK entry points outside the archive payload region.";
    }
    std::unreachable();
}

std::expected<std::span<const std::byte>, std::string>
EntryBytes(const FpkDocument& document, std::size_t selected_entry)
{
    if (selected_entry >= document.index.entries.size())
        return std::unexpected("The selected FPK entry does not exist.");

    const auto& entry = document.index.entries[selected_entry];
    if (entry.size > std::numeric_limits<std::uint32_t>::max() - entry.offset)
        return std::unexpected("The selected FPK entry range is invalid.");

    const auto offset = static_cast<std::size_t>(entry.offset);
    const auto size   = static_cast<std::size_t>(entry.size);
    if (offset > document.bytes.size() || size > document.bytes.size() - offset)
        return std::unexpected("The selected FPK entry range is invalid.");
    return std::span(document.bytes).subspan(offset, size);
}

template <typename Document, typename Error, typename ErrorMessage>
std::expected<FpkEntryDocument, std::string>
MakeEntryDocument(std::size_t                    selected_entry,
                  FpkEntryFormat                 format,
                  std::expected<Document, Error> parsed,
                  ErrorMessage                   error_message)
{
    if (!parsed)
        return std::unexpected(std::string(error_message(parsed.error())));
    return FpkEntryDocument{
        .entry_index = selected_entry,
        .format      = format,
        .data        = std::move(*parsed),
    };
}

} // namespace

std::expected<FpkDocument, FpkIndexError>
ParseFpkDocument(std::span<const std::byte> bytes)
{
    auto index = ParseFpkIndex(bytes);
    if (!index)
        return std::unexpected(index.error());
    return FpkDocument{
        .bytes = std::vector<std::byte>(bytes.begin(), bytes.end()),
        .index = std::move(*index),
    };
}

std::expected<FpkDocument, std::string>
LoadFpkDocument(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return std::unexpected("Could not open FPK file for reading.");

    const auto end = input.tellg();
    if (end < 0)
        return std::unexpected("Could not read FPK file size.");
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("The FPK file is too large to inspect on this platform.");

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
        return std::unexpected("Could not read FPK file data.");
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
        return std::unexpected("The FPK file ended before its reported size.");

    auto index = ParseFpkIndex(bytes);
    if (!index)
        return std::unexpected(std::string(IndexErrorMessage(index.error())));
    return FpkDocument{
        .bytes = std::move(bytes),
        .index = std::move(*index),
    };
}

std::expected<FpkEntryDocument, std::string>
OpenFpkEntryDocument(const FpkDocument& document,
                     std::size_t        selected_entry,
                     FpkEntryFormat     format)
{
    auto bytes = EntryBytes(document, selected_entry);
    if (!bytes)
        return std::unexpected(bytes.error());

    switch (format)
    {
        case FpkEntryFormat::dds:
            return MakeEntryDocument(
                selected_entry, format, ParseDdsDocument(*bytes), DdsDocumentErrorMessage);
        case FpkEntryFormat::gfx:
            return MakeEntryDocument(
                selected_entry, format, ParseGfxDocument(*bytes), GfxDocumentErrorMessage);
        case FpkEntryFormat::nif:
            return MakeEntryDocument(
                selected_entry, format, ParseNifDocument(*bytes), NifDocumentErrorMessage);
        case FpkEntryFormat::mp3:
            return MakeEntryDocument(
                selected_entry, format, ParseMp3Document(*bytes), Mp3DocumentErrorMessage);
        case FpkEntryFormat::map:
            return MakeEntryDocument(
                selected_entry, format, ParseMapDocument(*bytes), MapDocumentErrorMessage);
    }
    return std::unexpected("The selected FPK entry format is invalid.");
}

} // namespace rerevved::studio
