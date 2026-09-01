#pragma once

#include "dds_document.h"
#include "fpk_index.h"
#include "gfx_document.h"
#include "map_document.h"
#include "mp3_document.h"
#include "nif_document.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rerevved::studio
{

struct FpkDocument
{
    std::vector<std::byte> bytes;
    FpkIndex               index;
};

enum class FpkEntryFormat
{
    dds,
    gfx,
    nif,
    mp3,
    map,
};

using FpkEntryDocumentData =
    std::variant<DdsDocument, GfxDocument, NifDocument, Mp3Document, MapDocument>;

struct FpkEntryDocument
{
    std::size_t          entry_index = 0;
    FpkEntryFormat       format      = FpkEntryFormat::dds;
    FpkEntryDocumentData data;
};

[[nodiscard]] std::expected<FpkDocument, FpkIndexError>
ParseFpkDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<FpkDocument, std::string>
LoadFpkDocument(const std::filesystem::path& path);

[[nodiscard]] std::expected<FpkEntryDocument, std::string>
OpenFpkEntryDocument(const FpkDocument& document,
                     std::size_t        selected_entry,
                     FpkEntryFormat     format);

} // namespace rerevved::studio
