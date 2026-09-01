#include "application.h"
#include "archive_explorer.h"
#include "dds_document.h"
#include "dds_metadata.h"
#include "file_inspection.h"
#include "file_kind.h"
#include "fpk_document.h"
#include "fpk_extraction.h"
#include "fpk_index.h"
#include "gfx_document.h"
#include "inspection_format.h"
#include "map_document.h"
#include "mp3_document.h"
#include "nif_document.h"
#include "nif_inspector_format.h"
#include "nif_model.h"
#include "nif_preview.h"
#include "synthetic_mp3.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace
{

int failures = 0;

void Expect(bool condition, std::string_view message)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void TestFileKinds()
{
    using rerevved::studio::ClassifyFile;
    using rerevved::studio::FileKind;

    Expect(ClassifyFile("Common0.FPK") == FileKind::fpk_archive,
           "FPK classification is case-insensitive");
    Expect(ClassifyFile("portrait.dds") == FileKind::dds_texture,
           "DDS classification");
    Expect(ClassifyFile("menu.GfX") == FileKind::gfx_movie,
           "GFx classification is case-insensitive");
    Expect(ClassifyFile("intro.bik") == FileKind::bik_video,
           "Bink classification");
    Expect(ClassifyFile("music.mp3") == FileKind::mp3_audio,
           "MP3 classification");
    Expect(ClassifyFile("terrain.NIF") == FileKind::nif_container,
           "NIF classification is case-insensitive");
    Expect(ClassifyFile("scenario.MaP") == FileKind::map_record,
           "map classification is case-insensitive");
    Expect(ClassifyFile("notes.txt") == FileKind::unknown,
           "unknown extension classification");
}

void TestDocumentErrorMessages()
{
    using namespace rerevved::studio;

    Expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsMetadataError::truncated }) ==
               "DDS metadata is truncated.",
           "DDS truncated-metadata message");
    Expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsMetadataError::invalid_signature }) ==
               "The file does not have a valid DDS signature.",
           "DDS invalid-signature message");
    Expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsMetadataError::invalid_header_size }) ==
               "The DDS header size is invalid.",
           "DDS invalid-header-size message");
    Expect(DdsDocumentErrorMessage(
               DdsDocumentError{ DdsMetadataError::invalid_pixel_format_size }) ==
               "The DDS pixel format size is invalid.",
           "DDS invalid-pixel-format-size message");
    Expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::unsupported_format }) ==
               "This DDS encoding is not supported for preview.",
           "DDS unsupported-preview message");
    Expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::invalid_dimensions }) ==
               "The DDS preview dimensions are invalid.",
           "DDS invalid-dimensions message");
    Expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::size_overflow }) ==
               "The DDS preview dimensions exceed the supported size range.",
           "DDS size-overflow message");
    Expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::truncated_pixel_data }) ==
               "The DDS top-level pixel data is truncated.",
           "DDS truncated-pixel-data message");

    Expect(GfxDocumentErrorMessage(GfxDocumentError::truncated) ==
               "The GFX movie is truncated.",
           "GFX truncated message");
    Expect(GfxDocumentErrorMessage(GfxDocumentError::invalid_signature) ==
               "The file does not have a valid GFX signature.",
           "GFX invalid-signature message");
    Expect(GfxDocumentErrorMessage(GfxDocumentError::unsupported_compression) ==
               "Compressed GFX movies are not supported.",
           "GFX unsupported-compression message");
    Expect(GfxDocumentErrorMessage(GfxDocumentError::length_mismatch) ==
               "The GFX declared length does not match the file size.",
           "GFX length-mismatch message");
    Expect(GfxDocumentErrorMessage(GfxDocumentError::missing_exporter_info) ==
               "The GFX movie does not begin with exporter information.",
           "GFX missing-exporter message");
    Expect(GfxDocumentErrorMessage(GfxDocumentError::unsupported_exporter_version) ==
               "The GFX exporter version is not supported.",
           "GFX unsupported-exporter message");
    Expect(GfxDocumentErrorMessage(GfxDocumentError::invalid_tag_stream) ==
               "The GFX tag stream is invalid.",
           "GFX invalid-tag-stream message");

    Expect(NifDocumentErrorMessage(NifDocumentError::truncated) ==
               "The NIF file is truncated.",
           "NIF truncated message");
    Expect(NifDocumentErrorMessage(NifDocumentError::invalid_signature) ==
               "The file does not have a valid Gamebryo NIF signature.",
           "NIF invalid-signature message");
    Expect(NifDocumentErrorMessage(NifDocumentError::unsupported_version) ==
               "The NIF file version is not supported.",
           "NIF unsupported-version message");
    Expect(NifDocumentErrorMessage(NifDocumentError::unsupported_endian) ==
               "Only the observed big-endian NIF layout is supported.",
           "NIF unsupported-endian message");
    Expect(NifDocumentErrorMessage(NifDocumentError::unsupported_user_version) ==
               "The NIF user version is not supported.",
           "NIF unsupported-user-version message");
    Expect(NifDocumentErrorMessage(NifDocumentError::invalid_layout) ==
               "The NIF header, block table, or footer is inconsistent.",
           "NIF invalid-layout message");

    Expect(Mp3DocumentErrorMessage(Mp3DocumentError::decode_failed) ==
               "The MP3 audio could not be decoded.",
           "MP3 decode-failure message");
    Expect(MapDocumentErrorMessage(MapDocumentError::truncated) ==
               "The map record is truncated; the Xbox DLC profile requires 1088 bytes.",
           "MAP truncated message");
    Expect(MapDocumentErrorMessage(MapDocumentError::unsupported_length) ==
               "The map record length is unsupported; the Xbox DLC profile requires exactly 1088 bytes.",
           "MAP unsupported-length message");
}

void TestArchiveNavigation()
{
    using rerevved::studio::ArchiveExplorerState;
    using rerevved::studio::kMaximumArchiveNavigationEntries;
    using rerevved::studio::SelectArchiveEntry;

    ArchiveExplorerState state;
    state.opened_document.emplace();
    state.open_error        = "old open error";
    state.extraction_result = "old extraction result";
    state.metadata_result   = "old metadata result";
    state.navigation_error  = "old navigation error";
    Expect(SelectArchiveEntry(state, 5, 3), "valid archive entry selection succeeds");
    Expect(state.selected_entry && *state.selected_entry == 2 && state.requested_entry == 3,
           "archive selection stores matching zero-based and displayed entry numbers");
    Expect(!state.opened_document && state.open_error.empty() &&
               state.extraction_result.empty() && state.metadata_result.empty() &&
               state.navigation_error.empty(),
           "changed archive selection clears entry-dependent results");

    state.opened_document.emplace();
    state.open_error        = "current open error";
    state.extraction_result = "current extraction result";
    state.metadata_result   = "current metadata result";
    state.navigation_error  = "old navigation error";
    Expect(SelectArchiveEntry(state, 5, 3), "current archive entry can be selected again");
    Expect(state.opened_document && state.open_error == "current open error" &&
               state.extraction_result == "current extraction result" &&
               state.metadata_result == "current metadata result" &&
               state.navigation_error.empty(),
           "reselecting the current archive entry preserves its results");

    state.requested_entry = 0;
    Expect(!SelectArchiveEntry(state, 5, state.requested_entry),
           "zero archive entry number is rejected");
    Expect(state.selected_entry && *state.selected_entry == 2 && state.opened_document &&
               state.metadata_result == "current metadata result" &&
               state.navigation_error == "Enter an entry number from 1 to 5.",
           "invalid archive jump preserves the current selection and reports its range");

    state.requested_entry = 6;
    Expect(!SelectArchiveEntry(state, 5, state.requested_entry),
           "archive entry number beyond the index is rejected");
    Expect(state.selected_entry && *state.selected_entry == 2 && state.opened_document &&
               state.navigation_error == "Enter an entry number from 1 to 5.",
           "out-of-range archive jump preserves the current selection");

    Expect(!SelectArchiveEntry(state, 0, 1), "empty archive entry selection is rejected");
    Expect(state.selected_entry && *state.selected_entry == 2 && state.opened_document &&
               state.navigation_error == "This FPK archive has no entries.",
           "empty archive navigation preserves the current selection and reports its state");

    ArchiveExplorerState boundary_state;
    Expect(SelectArchiveEntry(boundary_state, 5, 2) && boundary_state.selected_entry == 1,
           "archive navigation can select the second entry");
    Expect(SelectArchiveEntry(boundary_state, 5, *boundary_state.selected_entry) &&
               boundary_state.selected_entry == 0,
           "previous navigation reaches the first entry");
    Expect(SelectArchiveEntry(boundary_state, 5, 4) && boundary_state.selected_entry == 3,
           "archive navigation can select the penultimate entry");
    Expect(SelectArchiveEntry(boundary_state, 5, *boundary_state.selected_entry + 2) &&
               boundary_state.selected_entry == 4,
           "next navigation reaches the final entry");

    boundary_state.requested_entry = std::numeric_limits<std::uint64_t>::max();
    Expect(!SelectArchiveEntry(boundary_state, 5, boundary_state.requested_entry) &&
               boundary_state.selected_entry == 4 &&
               boundary_state.navigation_error == "Enter an entry number from 1 to 5.",
           "maximum unsigned archive jump is rejected without changing selection");

    const auto first_unlisted_entry =
        static_cast<std::uint64_t>(kMaximumArchiveNavigationEntries) + 1;
    Expect(kMaximumArchiveNavigationEntries ==
               static_cast<std::size_t>(std::numeric_limits<int>::max()) - 1,
           "archive navigation reserves the ImGui unknown-count sentinel");
    Expect(!SelectArchiveEntry(
               boundary_state, kMaximumArchiveNavigationEntries, first_unlisted_entry) &&
               boundary_state.selected_entry == 4,
           "entry beyond the signed ImGui list limit is rejected without changing selection");
}

void TestExtractionModalLayout()
{
    using rerevved::studio::CalculateExtractionModalLayout;

    const auto wide = CalculateExtractionModalLayout(1280.0F, 8.0F, 8.0F);
    Expect(wide.content_width == 600.0F && wide.button_width == 120.0F &&
               !wide.stack_buttons,
           "wide extraction modal uses preferred content and button widths");

    const auto exact_pair = CalculateExtractionModalLayout(264.0F, 8.0F, 8.0F);
    Expect(exact_pair.content_width == 248.0F && exact_pair.button_width == 120.0F &&
               !exact_pair.stack_buttons,
           "extraction buttons remain paired when both preferred widths fit");

    const auto narrow = CalculateExtractionModalLayout(263.0F, 8.0F, 8.0F);
    Expect(narrow.content_width == 247.0F && narrow.button_width == 120.0F &&
               narrow.stack_buttons,
           "extraction buttons stack before their preferred widths would shrink");

    const auto small = CalculateExtractionModalLayout(96.0F, 8.0F, 8.0F);
    Expect(small.content_width == 80.0F && small.button_width == 80.0F &&
               small.stack_buttons,
           "small extraction modal keeps stacked buttons within the work area");

    const auto minimal = CalculateExtractionModalLayout(8.0F, 8.0F, 8.0F);
    Expect(minimal.content_width == 1.0F && minimal.button_width == 1.0F &&
               minimal.stack_buttons,
           "minimal extraction modal retains reachable positive-width controls");
}

void TestArchiveActionRowLayout()
{
    using rerevved::studio::CalculateArchiveActionRowLayout;
    using rerevved::studio::CalculateArchiveLabeledControlWidth;

    const auto wide_entry =
        CalculateArchiveActionRowLayout(500.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    Expect(wide_entry.first_width == 140.0F && wide_entry.second_width == 90.0F &&
               wide_entry.same_line,
           "wide archive entry row retains preferred input and natural action widths");

    const auto exact_entry =
        CalculateArchiveActionRowLayout(322.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    Expect(exact_entry.first_width == 140.0F && exact_entry.second_width == 90.0F &&
               exact_entry.same_line,
           "exact-fit archive entry row remains on one line");

    const auto narrow_entry =
        CalculateArchiveActionRowLayout(321.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    Expect(narrow_entry.first_width == 140.0F && narrow_entry.second_width == 90.0F &&
               !narrow_entry.same_line,
           "archive entry action stacks just below the preferred fit");

    const auto minimal_entry =
        CalculateArchiveActionRowLayout(0.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    Expect(minimal_entry.first_width == 1.0F && minimal_entry.second_width == 1.0F &&
               !minimal_entry.same_line,
           "minimal archive entry row retains positive-width controls");

    const auto exact_navigation =
        CalculateArchiveActionRowLayout(128.0F, 70.0F, 0.0F, 50.0F, 8.0F, 0.0F);
    const auto narrow_navigation =
        CalculateArchiveActionRowLayout(127.0F, 70.0F, 0.0F, 50.0F, 8.0F, 0.0F);
    Expect(exact_navigation.same_line && !narrow_navigation.same_line,
           "previous and next retain exact-fit pairing and stack before clipping");

    Expect(CalculateArchiveLabeledControlWidth(304.0F, 220.0F, 80.0F, 4.0F) ==
                   220.0F &&
               CalculateArchiveLabeledControlWidth(303.0F, 220.0F, 80.0F, 4.0F) ==
                   219.0F &&
               CalculateArchiveLabeledControlWidth(0.0F, 220.0F, 80.0F, 4.0F) == 1.0F,
           "archive format selector prefers 220 pixels and stays positive when narrow");

    const auto exact_actions =
        CalculateArchiveActionRowLayout(328.0F, 180.0F, 0.0F, 140.0F, 8.0F, 0.0F);
    const auto narrow_actions =
        CalculateArchiveActionRowLayout(327.0F, 180.0F, 0.0F, 140.0F, 8.0F, 0.0F);
    const auto minimal_actions =
        CalculateArchiveActionRowLayout(0.0F, 180.0F, 0.0F, 140.0F, 8.0F, 0.0F);
    Expect(exact_actions.same_line && !narrow_actions.same_line &&
               minimal_actions.first_width == 1.0F &&
               minimal_actions.second_width == 1.0F && !minimal_actions.same_line,
           "open and extract actions pair exactly, stack when narrow, and remain reachable");
}

void TestArchiveMetadataLayout()
{
    using rerevved::studio::CalculateArchiveLabeledControlWidth;

    Expect(CalculateArchiveLabeledControlWidth(500.0F, 112.0F, 0.0F, 0.0F) ==
               112.0F,
           "wide archive metadata action retains its natural width");
    Expect(CalculateArchiveLabeledControlWidth(112.0F, 112.0F, 0.0F, 0.0F) ==
               112.0F,
           "exact-fit archive metadata action retains its natural width");
    Expect(CalculateArchiveLabeledControlWidth(111.0F, 112.0F, 0.0F, 0.0F) ==
               111.0F,
           "narrow archive metadata action is bounded to available width");
    Expect(CalculateArchiveLabeledControlWidth(0.0F, 112.0F, 0.0F, 0.0F) == 1.0F &&
               CalculateArchiveLabeledControlWidth(-20.0F, 112.0F, 0.0F, 0.0F) ==
                   1.0F,
           "non-positive archive metadata widths retain a positive action");
}

void TestAssetCloseSelection()
{
    using rerevved::studio::UpdateSelectionAfterAssetClose;

    std::optional<std::size_t> selection;
    Expect(!UpdateSelectionAfterAssetClose(0, selection) && !selection,
           "empty asset list preserves its empty selection");

    selection = 0;
    Expect(UpdateSelectionAfterAssetClose(1, selection) && !selection,
           "closing the only asset clears selection");

    selection = 0;
    Expect(UpdateSelectionAfterAssetClose(3, selection) && selection == 0,
           "closing the first asset selects the next asset at the same row");

    selection = 1;
    Expect(UpdateSelectionAfterAssetClose(3, selection) && selection == 1,
           "closing a middle asset selects the next asset at the same row");

    selection = 2;
    Expect(UpdateSelectionAfterAssetClose(3, selection) && selection == 1,
           "closing the final asset selects its previous neighbor");

    selection = 3;
    Expect(!UpdateSelectionAfterAssetClose(3, selection) && selection == 3,
           "an invalid close index preserves its selection");

    selection = std::numeric_limits<std::size_t>::max();
    Expect(!UpdateSelectionAfterAssetClose(3, selection) &&
               selection == std::numeric_limits<std::size_t>::max(),
           "a maximum close index preserves its selection");
}

void TestFpkEntryRangeFormatting()
{
    using rerevved::studio::FormatBytes;
    using rerevved::studio::FormatFpkEntryRange;
    using rerevved::studio::FpkEntry;

    const FpkEntry entry{
        .offset = 62,
        .size   = 5,
    };
    Expect(FormatFpkEntryRange(3, entry) ==
               "Entry: 3\nOffset: 62 (0x0000003E)\nSize: 5 bytes (0x00000005)\nEnd "
               "(exclusive): 67 (0x00000043)",
           "FPK entry range formats decimal and hexadecimal values");

    const FpkEntry maximum_range{
        .offset = 0xFFFFFFF0U,
        .size   = 0xFU,
    };
    Expect(FormatFpkEntryRange(1, maximum_range) ==
               "Entry: 1\nOffset: 4294967280 (0xFFFFFFF0)\nSize: 15 bytes (0x0000000F)\n"
               "End (exclusive): 4294967295 (0xFFFFFFFF)",
           "FPK entry range computes the exclusive end without overflow");

    const FpkEntry maximum_exclusive_end{
        .offset = std::numeric_limits<std::uint32_t>::max(),
        .size   = std::numeric_limits<std::uint32_t>::max(),
    };
    Expect(FormatFpkEntryRange(1, maximum_exclusive_end) ==
               "Entry: 1\nOffset: 4294967295 (0xFFFFFFFF)\nSize: 4294967295 bytes "
               "(0xFFFFFFFF)\nEnd (exclusive): 8589934590 (0x1FFFFFFFE)",
           "FPK entry range preserves the maximum promoted exclusive end");

    const FpkEntry empty_range{};
    Expect(FormatFpkEntryRange(1, empty_range) ==
               "Entry: 1\nOffset: 0 (0x00000000)\nSize: 0 bytes (0x00000000)\nEnd "
               "(exclusive): 0 (0x00000000)",
           "empty FPK entry range preserves its zero-width exclusive boundary");

    const std::array<std::byte, 1> zero_byte{ std::byte{ 0x00 } };
    Expect(FormatBytes(zero_byte) == "00", "archive zero byte uses two hexadecimal digits");

    const std::array<std::byte, 5> mixed_bytes{
        std::byte{ 0x00 },
        std::byte{ 0x0A },
        std::byte{ 0x7F },
        std::byte{ 0xA5 },
        std::byte{ 0xFF },
    };
    Expect(FormatBytes(mixed_bytes) == "00 0A 7F A5 FF",
           "archive bytes preserve order, uppercase tokens, and single spaces");
}

void TestInspection()
{
    const auto                         path = std::filesystem::temp_directory_path() /
                                              "rerevved-studio-core-test.dds";
    const std::array<unsigned char, 5> contents{ 0x44, 0x44, 0x53, 0x20, 0x7F };
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
    }

    const auto inspection = rerevved::studio::InspectFile(path);
    Expect(inspection.has_value(), "regular file inspection succeeds");
    if (inspection)
    {
        Expect(inspection->size == contents.size(), "file size is preserved");
        Expect(inspection->header_size == contents.size(), "short header size is preserved");
        Expect(rerevved::studio::FormatHeader(*inspection) == "44 44 53 20 7F",
               "header formatting");
        Expect(rerevved::studio::FormatByteSize(2048) == "2.00 KiB",
               "byte size formatting");
    }

    std::error_code ignored;
    const bool      removed = std::filesystem::remove(path, ignored);
    Expect(removed && !ignored, "synthetic inspection file is removed before failure tests");

    const auto missing = rerevved::studio::InspectFile(path);
    Expect(!missing && !missing.error().empty(),
           "missing file inspection reports a recoverable path error");

    const auto directory = rerevved::studio::InspectFile(std::filesystem::temp_directory_path());
    Expect(!directory && directory.error() == "Path is not a regular file.",
           "directory inspection reports the stable non-file error");
}

void AppendU32Le(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void AppendU16Le(std::vector<std::byte>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}

void AppendU16Be(std::vector<std::byte>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value >> 8U));
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
}

void AppendU32Be(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (unsigned int shift = 32; shift > 0; shift -= 8)
        bytes.push_back(static_cast<std::byte>((value >> (shift - 8U)) & 0xFFU));
}

void WriteU16Be(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset]     = static_cast<std::byte>(value >> 8U);
    bytes[offset + 1] = static_cast<std::byte>(value & 0xFFU);
}

void WriteU32Be(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned int shift = 32; shift > 0; shift -= 8)
        bytes[offset++] = static_cast<std::byte>((value >> (shift - 8U)) & 0xFFU);
}

void WriteU32Le(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

std::vector<std::byte> ByteString(std::string_view value)
{
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (const auto character : value)
        bytes.push_back(static_cast<std::byte>(character));
    return bytes;
}

void AppendLengthPrefixed(std::vector<std::byte>& bytes, std::span<const std::byte> value)
{
    bytes.push_back(static_cast<std::byte>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void AppendBits(std::vector<bool>& bits, std::uint32_t value, unsigned int bit_count)
{
    for (unsigned int remaining = bit_count; remaining > 0; --remaining)
        bits.push_back(((value >> (remaining - 1U)) & 1U) != 0);
}

void AppendRect(std::vector<std::byte>&            bytes,
                const std::array<std::int32_t, 4>& coordinates,
                unsigned int                       bit_count)
{
    std::vector<bool> bits;
    AppendBits(bits, bit_count, 5);
    const auto mask = (1U << bit_count) - 1U;
    for (const auto coordinate : coordinates)
        AppendBits(bits, static_cast<std::uint32_t>(coordinate) & mask, bit_count);
    while (bits.size() % 8 != 0)
        bits.push_back(false);

    for (std::size_t offset = 0; offset < bits.size(); offset += 8)
    {
        std::uint8_t value = 0;
        for (std::size_t bit = 0; bit < 8; ++bit)
            value = static_cast<std::uint8_t>((value << 1U) | (bits[offset + bit] ? 1U : 0U));
        bytes.push_back(static_cast<std::byte>(value));
    }
}

void AppendGfxTag(std::vector<std::byte>&    bytes,
                  std::uint16_t              code,
                  std::span<const std::byte> payload,
                  bool                       force_long = false)
{
    const bool long_form = force_long || payload.size() >= 0x3F;
    AppendU16Le(bytes,
                static_cast<std::uint16_t>((code << 6U) | (long_form ? 0x3FU : payload.size())));
    if (long_form)
        AppendU32Le(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

std::vector<std::byte> MakeExporterInfo(std::uint16_t version = 0x0204)
{
    std::vector<std::byte> payload;
    AppendU16Le(payload, version);
    AppendU32Le(payload, 5);
    AppendU16Le(payload, 2);
    AppendLengthPrefixed(payload, ByteString("ui/"));
    AppendLengthPrefixed(payload, ByteString("menu"));
    return payload;
}

std::vector<std::byte>
MakeExternalImage(std::span<const std::byte> export_name, std::span<const std::byte> file_name)
{
    std::vector<std::byte> payload;
    AppendU16Le(payload, 7);
    AppendU16Le(payload, 2);
    AppendU16Le(payload, 64);
    AppendU16Le(payload, 32);
    AppendLengthPrefixed(payload, export_name);
    AppendLengthPrefixed(payload, file_name);
    return payload;
}

std::vector<std::byte> BeginGfx()
{
    std::vector<std::byte> bytes{
        std::byte{ 'G' },
        std::byte{ 'F' },
        std::byte{ 'X' },
        std::byte{ 8 },
        std::byte{ 0 },
        std::byte{ 0 },
        std::byte{ 0 },
        std::byte{ 0 },
    };
    AppendRect(bytes, { -20, 1980, -40, 960 }, 12);
    AppendU16Le(bytes, 0x1800);
    AppendU16Le(bytes, 2);
    return bytes;
}

void UpdateGfxLength(std::vector<std::byte>& bytes)
{
    WriteU32Le(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
}

void FinishGfx(std::vector<std::byte>& bytes)
{
    AppendGfxTag(bytes, 0, {});
    UpdateGfxLength(bytes);
}

std::vector<std::byte> MakeGfx()
{
    auto bytes    = BeginGfx();
    auto exporter = MakeExporterInfo();
    AppendGfxTag(bytes, 1000, exporter);
    const std::array export_name{
        std::byte{ 'i' },
        std::byte{ 0 },
        std::byte{ 0xFF },
    };
    auto image = MakeExternalImage(export_name, ByteString("icon.dds"));
    AppendGfxTag(bytes, 1001, image, true);
    AppendGfxTag(bytes, 77, std::array{ std::byte{ 0xA5 } });
    AppendGfxTag(bytes, 1001, image);
    FinishGfx(bytes);
    return bytes;
}

void AppendNifSizedBytes(std::vector<std::byte>& bytes, std::span<const std::byte> value)
{
    AppendU32Be(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

struct SyntheticNif
{
    std::vector<std::byte> bytes;
    std::size_t            block_type_count_offset     = 0;
    std::size_t            first_type_index_offset     = 0;
    std::size_t            first_block_size_offset     = 0;
    std::size_t            string_count_offset         = 0;
    std::size_t            max_string_length_offset    = 0;
    std::size_t            group_count_offset          = 0;
    std::size_t            root_count_offset           = 0;
    std::size_t            root_index_offset           = 0;
    std::size_t            node_name_index_offset      = 0;
    std::size_t            node_extra_count_offset     = 0;
    std::size_t            node_child_count_offset     = 0;
    std::size_t            node_child_offset           = 0;
    std::size_t            shape_data_offset           = 0;
    std::size_t            shape_material_count_offset = 0;
};

void AppendNifAvObject(std::vector<std::byte>& bytes,
                       std::uint32_t           name_index,
                       std::uint16_t           flags,
                       std::uint32_t           property)
{
    AppendU32Be(bytes, name_index);
    AppendU32Be(bytes, 1);
    AppendU32Be(bytes, 2);
    AppendU32Be(bytes, 0xFFFFFFFF);
    AppendU16Be(bytes, flags);
    AppendU32Be(bytes, 0x3F800000);
    AppendU32Be(bytes, 0x40000000);
    AppendU32Be(bytes, 0x40400000);
    for (std::size_t index = 0; index < 9; ++index)
        AppendU32Be(bytes, index % 4 == 0 ? 0x3F800000 : 0);
    AppendU32Be(bytes, 0x40000000);
    AppendU32Be(bytes, 1);
    AppendU32Be(bytes, property);
    AppendU32Be(bytes, 0xFFFFFFFF);
}

SyntheticNif MakeNif()
{
    SyntheticNif result{};
    auto&        bytes  = result.bytes;
    const auto   header = ByteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    AppendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    AppendU32Le(bytes, 0);
    AppendU32Le(bytes, 3);

    result.block_type_count_offset = bytes.size();
    AppendU16Be(bytes, 3);
    AppendNifSizedBytes(bytes, ByteString("NiNode"));
    AppendNifSizedBytes(bytes, ByteString("NiTriShape"));
    AppendNifSizedBytes(bytes, ByteString("OpaqueBlock"));
    result.first_type_index_offset = bytes.size();
    AppendU16Be(bytes, 0);
    AppendU16Be(bytes, 1);
    AppendU16Be(bytes, 2);

    std::vector<std::byte> node;
    result.node_name_index_offset  = 0;
    result.node_extra_count_offset = 4;
    AppendNifAvObject(node, 0, 0x1234, 2);
    result.node_child_count_offset = node.size();
    AppendU32Be(node, 1);
    result.node_child_offset = node.size();
    AppendU32Be(node, 1);
    AppendU32Be(node, 0);

    std::vector<std::byte> shape;
    AppendNifAvObject(shape, 1, 0x5678, 2);
    result.shape_data_offset = shape.size();
    AppendU32Be(shape, 2);
    AppendU32Be(shape, 0xFFFFFFFF);
    result.shape_material_count_offset = shape.size();
    AppendU32Be(shape, 1);
    AppendU32Be(shape, 0);
    AppendU32Be(shape, 0xFFFFFFF9);
    AppendU32Be(shape, 0);
    shape.push_back(std::byte{ 1 });

    const std::array opaque{ std::byte{ 0xB1 }, std::byte{ 0xB2 }, std::byte{ 0xB3 } };
    result.first_block_size_offset = bytes.size();
    AppendU32Be(bytes, static_cast<std::uint32_t>(node.size()));
    AppendU32Be(bytes, static_cast<std::uint32_t>(shape.size()));
    AppendU32Be(bytes, static_cast<std::uint32_t>(opaque.size()));

    result.string_count_offset = bytes.size();
    AppendU32Be(bytes, 2);
    result.max_string_length_offset = bytes.size();
    AppendU32Be(bytes, 4);
    AppendNifSizedBytes(bytes, ByteString("Root"));
    const std::array raw_string{ std::byte{ 0 }, std::byte{ 0xFF } };
    AppendNifSizedBytes(bytes, raw_string);
    result.group_count_offset = bytes.size();
    AppendU32Be(bytes, 1);
    AppendU32Be(bytes, 3);

    result.node_name_index_offset += bytes.size();
    result.node_extra_count_offset += bytes.size();
    result.node_child_count_offset += bytes.size();
    result.node_child_offset += bytes.size();
    bytes.insert(bytes.end(), node.begin(), node.end());
    result.shape_data_offset += bytes.size();
    result.shape_material_count_offset += bytes.size();
    bytes.insert(bytes.end(), shape.begin(), shape.end());
    bytes.insert(bytes.end(), opaque.begin(), opaque.end());
    result.root_count_offset = bytes.size();
    AppendU32Be(bytes, 1);
    result.root_index_offset = bytes.size();
    AppendU32Be(bytes, 0);
    return result;
}

struct SyntheticGeometryNif
{
    std::vector<std::byte> bytes;
    std::size_t            block_size_offset               = 0;
    std::size_t            block_payload_offset            = 0;
    std::size_t            vertex_count_offset             = 0;
    std::size_t            vertex_positions_offset         = 0;
    std::size_t            data_flags_offset               = 0;
    std::size_t            has_normals_offset              = 0;
    std::size_t            normal_vectors_offset           = 0;
    std::size_t            additional_data_offset          = 0;
    std::size_t            triangle_count_offset           = 0;
    std::size_t            triangle_indices_offset         = 0;
    std::size_t            match_group_count_offset        = 0;
    std::size_t            match_group_vertex_count_offset = 0;
    std::size_t            match_group_selectors_offset    = 0;
};

SyntheticGeometryNif MakeGeometryNif(bool populated)
{
    SyntheticGeometryNif   result{};
    std::vector<std::byte> payload;
    AppendU32Be(payload, 0xFFFFFFF9);
    result.vertex_count_offset = payload.size();
    AppendU16Be(payload, populated ? 2 : 0);
    payload.push_back(std::byte{ 0xA1 });
    payload.push_back(std::byte{ 0xB2 });
    payload.push_back(populated ? std::byte{ 0xFF } : std::byte{ 0 });
    result.vertex_positions_offset = payload.size();
    if (populated)
        payload.insert(payload.end(), 24, std::byte{ 0 });
    result.data_flags_offset = payload.size();
    AppendU16Be(payload, populated ? 0x1001 : 0);
    result.has_normals_offset = payload.size();
    payload.push_back(populated ? std::byte{ 2 } : std::byte{ 0 });
    result.normal_vectors_offset = payload.size();
    if (populated)
    {
        payload.insert(payload.end(), 24, std::byte{ 0 });
        payload.insert(payload.end(), 24, std::byte{ 0 });
        payload.insert(payload.end(), 24, std::byte{ 0 });
    }
    AppendU32Be(payload, 0x3F800000);
    AppendU32Be(payload, 0x40000000);
    AppendU32Be(payload, 0x40400000);
    AppendU32Be(payload, 0xC0800000);
    payload.push_back(populated ? std::byte{ 0x80 } : std::byte{ 0 });
    if (populated)
        payload.insert(payload.end(), 32, std::byte{ 0 });
    if (populated)
        payload.insert(payload.end(), 16, std::byte{ 0 });
    AppendU16Be(payload, 0xBEEF);
    result.additional_data_offset = payload.size();
    AppendU32Be(payload, 0xFFFFFFFF);
    result.triangle_count_offset = payload.size();
    AppendU16Be(payload, populated ? 1 : 0);
    AppendU32Be(payload, 7);
    payload.push_back(populated ? std::byte{ 0x7F } : std::byte{ 0 });
    result.triangle_indices_offset = payload.size();
    if (populated)
    {
        AppendU16Be(payload, 0);
        AppendU16Be(payload, 1);
        AppendU16Be(payload, 0);
    }
    result.match_group_count_offset = payload.size();
    AppendU16Be(payload, populated ? 2 : 0);
    result.match_group_vertex_count_offset = payload.size();
    if (populated)
    {
        AppendU16Be(payload, 2);
        result.match_group_selectors_offset = payload.size();
        AppendU16Be(payload, 0);
        AppendU16Be(payload, 7);
        AppendU16Be(payload, 3);
        AppendU16Be(payload, 5);
        AppendU16Be(payload, 1);
        AppendU16Be(payload, 9);
    }

    auto&      bytes  = result.bytes;
    const auto header = ByteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    AppendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    AppendU32Le(bytes, 0);
    AppendU32Le(bytes, 1);
    AppendU16Be(bytes, 1);
    AppendNifSizedBytes(bytes, ByteString("NiTriShapeData"));
    AppendU16Be(bytes, 0);
    result.block_size_offset = bytes.size();
    AppendU32Be(bytes, static_cast<std::uint32_t>(payload.size()));
    AppendU32Be(bytes, 0);
    AppendU32Be(bytes, 0);
    AppendU32Be(bytes, 0);

    result.vertex_count_offset += bytes.size();
    result.vertex_positions_offset += bytes.size();
    result.data_flags_offset += bytes.size();
    result.has_normals_offset += bytes.size();
    result.normal_vectors_offset += bytes.size();
    result.additional_data_offset += bytes.size();
    result.triangle_count_offset += bytes.size();
    result.triangle_indices_offset += bytes.size();
    result.match_group_count_offset += bytes.size();
    result.match_group_vertex_count_offset += bytes.size();
    result.match_group_selectors_offset += bytes.size();
    result.block_payload_offset = bytes.size();
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    AppendU32Be(bytes, 1);
    AppendU32Be(bytes, 0);
    return result;
}

void AppendNifF32(std::vector<std::byte>& bytes, float value)
{
    AppendU32Be(bytes, std::bit_cast<std::uint32_t>(value));
}

void WriteNifF32(std::vector<std::byte>& bytes, std::size_t offset, float value)
{
    WriteU32Be(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void AppendNifVector3(std::vector<std::byte>& bytes, float x, float y, float z)
{
    AppendNifF32(bytes, x);
    AppendNifF32(bytes, y);
    AppendNifF32(bytes, z);
}

void AppendSyntheticObjectNet(std::vector<std::byte>& bytes, std::uint32_t name_index)
{
    AppendU32Be(bytes, name_index);
    AppendU32Be(bytes, 0);
    AppendU32Be(bytes, 0xFFFFFFFF);
}

std::size_t AppendSyntheticAvObject(std::vector<std::byte>&        bytes,
                                    std::uint32_t                  name_index,
                                    std::span<const std::uint32_t> properties)
{
    AppendSyntheticObjectNet(bytes, name_index);
    AppendU16Be(bytes, 0);
    AppendNifVector3(bytes, 0.0F, 0.0F, 0.0F);
    for (std::size_t index = 0; index < 9; ++index)
        AppendNifF32(bytes, index % 4 == 0 ? 1.0F : 0.0F);
    AppendNifF32(bytes, 1.0F);
    AppendU32Be(bytes, static_cast<std::uint32_t>(properties.size()));
    const auto property_reference_offset = bytes.size();
    for (const auto property : properties)
        AppendU32Be(bytes, property);
    AppendU32Be(bytes, 0xFFFFFFFF);
    return property_reference_offset;
}

struct SyntheticModelNif
{
    std::vector<std::byte> bytes;
    std::size_t            root_reference_offset     = 0;
    std::size_t            child_reference_offset    = 0;
    std::size_t            data_reference_offset     = 0;
    std::size_t            property_reference_offset = 0;
    std::size_t            triangle_indices_offset   = 0;
    std::size_t            node_transform_offset     = 0;
    std::size_t            shape_transform_offset    = 0;
};

SyntheticModelNif MakeSyntheticModelNif()
{
    SyntheticModelNif result{};

    std::vector<std::byte> node;
    result.node_transform_offset = 14;
    AppendSyntheticAvObject(node, 0, {});
    AppendU32Be(node, 1);
    result.child_reference_offset = node.size();
    AppendU32Be(node, 1);
    AppendU32Be(node, 0);

    std::vector<std::byte> shape;
    result.shape_transform_offset = 14;
    const std::array shape_properties{ std::uint32_t{ 3 } };
    result.property_reference_offset =
        AppendSyntheticAvObject(shape, 1, shape_properties);
    result.data_reference_offset = shape.size();
    AppendU32Be(shape, 2);
    AppendU32Be(shape, 0xFFFFFFFF);
    AppendU32Be(shape, 0);
    AppendU32Be(shape, 0xFFFFFFFF);
    shape.push_back(std::byte{ 0 });

    std::vector<std::byte> geometry;
    AppendU32Be(geometry, 0);
    AppendU16Be(geometry, 3);
    geometry.push_back(std::byte{ 0 });
    geometry.push_back(std::byte{ 0 });
    geometry.push_back(std::byte{ 1 });
    AppendNifVector3(geometry, 0.0F, 0.0F, 0.0F);
    AppendNifVector3(geometry, 1.0F, 0.0F, 0.0F);
    AppendNifVector3(geometry, 0.0F, 1.0F, 0.0F);
    AppendU16Be(geometry, 0);
    geometry.push_back(std::byte{ 1 });
    AppendNifVector3(geometry, 1.0F, 0.0F, 0.0F);
    AppendNifVector3(geometry, 0.0F, 1.0F, 0.0F);
    AppendNifVector3(geometry, 0.0F, 0.0F, 1.0F);
    AppendNifVector3(geometry, 0.5F, 0.5F, 0.0F);
    AppendNifF32(geometry, 1.0F);
    geometry.push_back(std::byte{ 0 });
    AppendU16Be(geometry, 0);
    AppendU32Be(geometry, 0xFFFFFFFF);
    AppendU16Be(geometry, 1);
    AppendU32Be(geometry, 3);
    geometry.push_back(std::byte{ 1 });
    result.triangle_indices_offset = geometry.size();
    AppendU16Be(geometry, 0);
    AppendU16Be(geometry, 1);
    AppendU16Be(geometry, 2);
    AppendU16Be(geometry, 0);

    std::vector<std::byte> material;
    AppendSyntheticObjectNet(material, 2);
    AppendNifVector3(material, 1.0F, 1.0F, 1.0F);
    AppendNifVector3(material, 1.0F, 1.0F, 1.0F);
    AppendNifVector3(material, 0.0F, 0.0F, 0.0F);
    AppendNifVector3(material, 0.0F, 0.0F, 0.0F);
    AppendNifF32(material, 1.0F);
    AppendNifF32(material, 1.0F);

    auto&      bytes  = result.bytes;
    const auto header = ByteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    AppendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    AppendU32Le(bytes, 0);
    AppendU32Le(bytes, 4);
    AppendU16Be(bytes, 4);
    AppendNifSizedBytes(bytes, ByteString("NiNode"));
    AppendNifSizedBytes(bytes, ByteString("NiTriShape"));
    AppendNifSizedBytes(bytes, ByteString("NiTriShapeData"));
    AppendNifSizedBytes(bytes, ByteString("NiMaterialProperty"));
    AppendU16Be(bytes, 0);
    AppendU16Be(bytes, 1);
    AppendU16Be(bytes, 2);
    AppendU16Be(bytes, 3);
    AppendU32Be(bytes, static_cast<std::uint32_t>(node.size()));
    AppendU32Be(bytes, static_cast<std::uint32_t>(shape.size()));
    AppendU32Be(bytes, static_cast<std::uint32_t>(geometry.size()));
    AppendU32Be(bytes, static_cast<std::uint32_t>(material.size()));
    AppendU32Be(bytes, 3);
    AppendU32Be(bytes, 17);
    AppendNifSizedBytes(bytes, ByteString("SyntheticRoot"));
    AppendNifSizedBytes(bytes, ByteString("SyntheticTriangle"));
    AppendNifSizedBytes(bytes, ByteString("SyntheticMaterial"));
    AppendU32Be(bytes, 0);

    result.child_reference_offset += bytes.size();
    result.node_transform_offset += bytes.size();
    bytes.insert(bytes.end(), node.begin(), node.end());
    result.property_reference_offset += bytes.size();
    result.data_reference_offset += bytes.size();
    result.shape_transform_offset += bytes.size();
    bytes.insert(bytes.end(), shape.begin(), shape.end());
    result.triangle_indices_offset += bytes.size();
    bytes.insert(bytes.end(), geometry.begin(), geometry.end());
    bytes.insert(bytes.end(), material.begin(), material.end());
    AppendU32Be(bytes, 1);
    result.root_reference_offset = bytes.size();
    AppendU32Be(bytes, 0);
    return result;
}

constexpr std::array<std::uint32_t, 14> kSyntheticMaterialBitsA{
    0x3FA00000,
    0xC0200000,
    0x40700000,
    0xC0900000,
    0x40A80000,
    0xC0D80000,
    0x00000000,
    0x80000000,
    0x7FC12345,
    0x7F800000,
    0xFF800000,
    0x00000001,
    0x40F00000,
    0xC1040000,
};

constexpr std::array<std::uint32_t, 14> kSyntheticMaterialBitsB{
    0x41200000,
    0x41300000,
    0x41400000,
    0x41500000,
    0x41600000,
    0x41700000,
    0x41800000,
    0x41880000,
    0x41900000,
    0x41980000,
    0x41A00000,
    0x41A80000,
    0x41B00000,
    0x41B80000,
};

void AppendSyntheticMaterialProperty(std::vector<std::byte>&              bytes,
                                     std::uint32_t                        name_index,
                                     const std::array<std::uint32_t, 14>& bits)
{
    AppendSyntheticObjectNet(bytes, name_index);
    for (const auto value : bits)
        AppendU32Be(bytes, value);
}

struct SyntheticMaterialNif
{
    std::vector<std::byte>     bytes;
    std::array<std::size_t, 2> block_size_offsets{};
    std::array<std::size_t, 2> payload_offsets{};
};

SyntheticMaterialNif MakeSyntheticMaterialNif()
{
    SyntheticMaterialNif                  result{};
    std::array<std::vector<std::byte>, 2> materials;
    AppendSyntheticMaterialProperty(materials[0], 0, kSyntheticMaterialBitsA);
    AppendSyntheticMaterialProperty(materials[1], 1, kSyntheticMaterialBitsB);

    auto&      bytes  = result.bytes;
    const auto header = ByteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    AppendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    AppendU32Le(bytes, 0);
    AppendU32Le(bytes, 2);
    AppendU16Be(bytes, 1);
    AppendNifSizedBytes(bytes, ByteString("NiMaterialProperty"));
    AppendU16Be(bytes, 0);
    AppendU16Be(bytes, 0);
    for (std::size_t index = 0; index < materials.size(); ++index)
    {
        result.block_size_offsets[index] = bytes.size();
        AppendU32Be(bytes, static_cast<std::uint32_t>(materials[index].size()));
    }
    AppendU32Be(bytes, 2);
    AppendU32Be(bytes, 18);
    AppendNifSizedBytes(bytes, ByteString("SyntheticMaterialA"));
    AppendNifSizedBytes(bytes, ByteString("SyntheticMaterialB"));
    AppendU32Be(bytes, 0);
    for (std::size_t index = 0; index < materials.size(); ++index)
    {
        result.payload_offsets[index] = bytes.size();
        bytes.insert(bytes.end(), materials[index].begin(), materials[index].end());
    }
    AppendU32Be(bytes, 0);
    return result;
}

std::size_t
AppendSyntheticTexDesc(std::vector<std::byte>&           bytes,
                       std::uint32_t                     source,
                       std::uint16_t                     flags,
                       std::uint8_t                      has_texture_transform,
                       std::span<const std::uint32_t, 8> transform_bits)
{
    const auto source_offset = bytes.size();
    AppendU32Be(bytes, source);
    AppendU16Be(bytes, flags);
    bytes.push_back(static_cast<std::byte>(has_texture_transform));
    if (has_texture_transform == 1)
    {
        for (const auto bits : transform_bits)
            AppendU32Be(bytes, bits);
    }
    return source_offset;
}

struct SyntheticTextureNif
{
    std::vector<std::byte>     bytes;
    std::array<std::size_t, 5> block_size_offsets{};
    std::array<std::size_t, 5> payload_offsets{};
    std::array<std::size_t, 5> payload_sizes{};
    std::size_t                first_property_name_offset       = 0;
    std::size_t                first_property_controller_offset = 0;
    std::size_t                texture_count_offset             = 0;
    std::array<std::size_t, 6> descriptor_source_offsets{};
    std::size_t                shader_count_offset = 0;
    std::array<std::size_t, 2> source_name_offsets{};
    std::array<std::size_t, 2> source_pixel_offsets{};
    std::array<std::size_t, 2> source_derived_offsets{};
};

SyntheticTextureNif MakeSyntheticTextureNif()
{
    constexpr std::array<std::uint32_t, 8> kTransformBitsA{
        0x00000000,
        0x80000000,
        0x7F800000,
        0xFF800000,
        0x7FC12345,
        0xA5A5A5A5,
        0x3F000000,
        0xBF000000,
    };
    constexpr std::array<std::uint32_t, 8> kTransformBitsB{
        0x3F800000,
        0x40000000,
        0x40400000,
        0x40800000,
        0x40A00000,
        0x12345678,
        0x40C00000,
        0x40E00000,
    };
    constexpr std::array<std::uint32_t, 6> kBumpBits{
        0x80000000,
        0x7F800000,
        0xFF800000,
        0x7FC54321,
        0x00000001,
        0xBF800000,
    };

    SyntheticTextureNif                   result{};
    std::array<std::vector<std::byte>, 5> payloads;

    auto& first_property              = payloads[0];
    result.first_property_name_offset = first_property.size();
    AppendSyntheticObjectNet(first_property, 0xFFFFFFFF);
    result.first_property_controller_offset = 8;
    AppendU16Be(first_property, 0xA5F3);
    result.texture_count_offset = first_property.size();
    AppendU32Be(first_property, 9);

    first_property.push_back(std::byte{ 1 });
    result.descriptor_source_offsets[0] =
        AppendSyntheticTexDesc(first_property, 2, 0xABCD, 1, kTransformBitsA);
    first_property.push_back(std::byte{ 2 });
    first_property.push_back(std::byte{ 0 });
    first_property.push_back(std::byte{ 0x7F });
    first_property.push_back(std::byte{ 1 });
    result.descriptor_source_offsets[1] =
        AppendSyntheticTexDesc(first_property, 3, 0x1020, 2, kTransformBitsA);
    first_property.push_back(std::byte{ 1 });
    result.descriptor_source_offsets[2] =
        AppendSyntheticTexDesc(first_property, 2, 0x3040, 0, kTransformBitsA);
    for (const auto bits : kBumpBits)
        AppendU32Be(first_property, bits);
    first_property.push_back(std::byte{ 0 });
    first_property.push_back(std::byte{ 1 });
    result.descriptor_source_offsets[3] =
        AppendSyntheticTexDesc(first_property, 0xFFFFFFFF, 0x5060, 0, kTransformBitsA);
    AppendU32Be(first_property, 0x7FC0BEEF);
    first_property.push_back(std::byte{ 0 });

    result.shader_count_offset = first_property.size();
    AppendU32Be(first_property, 3);
    first_property.push_back(std::byte{ 1 });
    result.descriptor_source_offsets[4] =
        AppendSyntheticTexDesc(first_property, 2, 0x7080, 0, kTransformBitsA);
    AppendU32Be(first_property, 0xDEADBEEF);
    first_property.push_back(std::byte{ 2 });
    first_property.push_back(std::byte{ 1 });
    result.descriptor_source_offsets[5] =
        AppendSyntheticTexDesc(first_property, 3, 0x90A0, 1, kTransformBitsB);
    AppendU32Be(first_property, 0xCAFEBABE);

    auto& second_property = payloads[1];
    AppendSyntheticObjectNet(second_property, 0xFFFFFFFF);
    AppendU16Be(second_property, 0x55AA);
    AppendU32Be(second_property, 9);
    for (const auto presence : { 0U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U })
        second_property.push_back(static_cast<std::byte>(presence));
    AppendU32Be(second_property, 0);

    auto append_source = [&](std::size_t   index,
                             std::uint8_t  use_external,
                             std::uint32_t file_name,
                             std::uint32_t pixel_data,
                             std::uint32_t base_value)
    {
        auto& source = payloads[index + 2];
        AppendSyntheticObjectNet(source, 0xFFFFFFFF);
        result.source_derived_offsets[index] = source.size();
        source.push_back(static_cast<std::byte>(use_external));
        result.source_name_offsets[index] = source.size();
        AppendU32Be(source, file_name);
        result.source_pixel_offsets[index] = source.size();
        AppendU32Be(source, pixel_data);
        AppendU32Be(source, base_value + 0);
        AppendU32Be(source, base_value + 1);
        AppendU32Be(source, base_value + 2);
        source.push_back(std::byte{ 2 });
        source.push_back(std::byte{ 3 });
        source.push_back(std::byte{ 4 });
    };
    append_source(0, 1, 0, 0xFFFFFFFF, 0x11111110);
    append_source(1, 0, 0xFFFFFFFF, 4, 0x22222220);
    payloads[4].push_back(std::byte{ 0xA5 });

    auto&      bytes  = result.bytes;
    const auto header = ByteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    AppendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    AppendU32Le(bytes, 0);
    AppendU32Le(bytes, static_cast<std::uint32_t>(payloads.size()));
    AppendU16Be(bytes, 3);
    AppendNifSizedBytes(bytes, ByteString("NiTexturingProperty"));
    AppendNifSizedBytes(bytes, ByteString("NiSourceTexture"));
    AppendNifSizedBytes(bytes, ByteString("OpaqueBlock"));
    for (const auto type : { 0U, 0U, 1U, 1U, 2U })
        AppendU16Be(bytes, static_cast<std::uint16_t>(type));
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        result.block_size_offsets[index] = bytes.size();
        result.payload_sizes[index]      = payloads[index].size();
        AppendU32Be(bytes, static_cast<std::uint32_t>(payloads[index].size()));
    }
    AppendU32Be(bytes, 1);
    AppendU32Be(bytes, 20);
    AppendNifSizedBytes(bytes, ByteString("SyntheticTexture.dds"));
    AppendU32Be(bytes, 0);

    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        result.payload_offsets[index] = bytes.size();
        bytes.insert(bytes.end(), payloads[index].begin(), payloads[index].end());
    }
    AppendU32Be(bytes, 0);

    const auto rebase = [&](std::size_t& value, std::size_t payload_index)
    {
        value += result.payload_offsets[payload_index];
    };
    rebase(result.first_property_name_offset, 0);
    rebase(result.first_property_controller_offset, 0);
    rebase(result.texture_count_offset, 0);
    for (auto& value : result.descriptor_source_offsets)
        rebase(value, 0);
    rebase(result.shader_count_offset, 0);
    for (std::size_t index = 0; index < result.source_name_offsets.size(); ++index)
    {
        rebase(result.source_name_offsets[index], index + 2);
        rebase(result.source_pixel_offsets[index], index + 2);
        rebase(result.source_derived_offsets[index], index + 2);
    }
    return result;
}

std::vector<std::byte> MakeDds(bool dx10)
{
    std::vector<std::byte> bytes(dx10 ? 148 : 128);
    bytes[0] = std::byte{ 'D' };
    bytes[1] = std::byte{ 'D' };
    bytes[2] = std::byte{ 'S' };
    bytes[3] = std::byte{ ' ' };
    WriteU32Le(bytes, 4, 124);
    WriteU32Le(bytes, 8, 0x0002100FU);
    WriteU32Le(bytes, 12, 64);
    WriteU32Le(bytes, 16, 128);
    WriteU32Le(bytes, 20, 512);
    WriteU32Le(bytes, 24, 4);
    WriteU32Le(bytes, 28, 3);
    bytes[32] = std::byte{ 0xA1 };
    WriteU32Le(bytes, 76, 32);
    WriteU32Le(bytes, 80, dx10 ? 0x4U : 0x41U);
    if (dx10)
    {
        bytes[84] = std::byte{ 'D' };
        bytes[85] = std::byte{ 'X' };
        bytes[86] = std::byte{ '1' };
        bytes[87] = std::byte{ '0' };
    }
    WriteU32Le(bytes, 88, 32);
    WriteU32Le(bytes, 92, 0x00FF0000U);
    WriteU32Le(bytes, 96, 0x0000FF00U);
    WriteU32Le(bytes, 100, 0x000000FFU);
    WriteU32Le(bytes, 104, 0xFF000000U);
    WriteU32Le(bytes, 108, 0x00401008U);
    WriteU32Le(bytes, 112, 0x00000200U);
    bytes[116] = std::byte{ 0xB2 };
    bytes[124] = std::byte{ 0xC3 };
    if (dx10)
    {
        WriteU32Le(bytes, 128, 28);
        WriteU32Le(bytes, 132, 3);
        WriteU32Le(bytes, 136, 4);
        WriteU32Le(bytes, 140, 2);
        WriteU32Le(bytes, 144, 1);
    }
    return bytes;
}

std::vector<std::byte> MakePreviewDds(std::uint32_t              width,
                                      std::uint32_t              height,
                                      std::uint32_t              red_mask,
                                      std::uint32_t              green_mask,
                                      std::uint32_t              blue_mask,
                                      std::uint32_t              alpha_mask,
                                      std::span<const std::byte> pixels)
{
    auto bytes = MakeDds(false);
    WriteU32Le(bytes, 12, height);
    WriteU32Le(bytes, 16, width);
    WriteU32Le(bytes, 20, width * 4);
    WriteU32Le(bytes, 80, alpha_mask == 0 ? 0x40U : 0x41U);
    WriteU32Le(bytes, 88, 32);
    WriteU32Le(bytes, 92, red_mask);
    WriteU32Le(bytes, 96, green_mask);
    WriteU32Le(bytes, 100, blue_mask);
    WriteU32Le(bytes, 104, alpha_mask);
    WriteU32Le(bytes, 112, 0);
    bytes.insert(bytes.end(), pixels.begin(), pixels.end());
    return bytes;
}

void TestDdsMetadata()
{
    using rerevved::studio::DdsMetadataError;
    using rerevved::studio::ParseDdsMetadata;

    const auto legacy   = MakeDds(false);
    const auto metadata = ParseDdsMetadata(legacy);
    Expect(metadata.has_value(), "legacy DDS metadata parsing succeeds");
    if (metadata)
    {
        Expect(metadata->width == 128 && metadata->height == 64,
               "DDS dimensions are preserved");
        Expect(metadata->flags == 0x0002100FU && metadata->pitch_or_linear_size == 512 &&
                   metadata->depth == 4 && metadata->mip_map_count == 3,
               "DDS base header values are preserved");
        Expect(metadata->reserved1[0] == std::byte{ 0xA1 } &&
                   metadata->caps_reserved[0] == std::byte{ 0xB2 } &&
                   metadata->reserved2[0] == std::byte{ 0xC3 },
               "DDS reserved bytes are preserved");
        Expect(metadata->pixel_format.flags == 0x41U &&
                   metadata->pixel_format.rgb_bit_count == 32 &&
                   metadata->pixel_format.red_mask == 0x00FF0000U &&
                   metadata->pixel_format.alpha_mask == 0xFF000000U,
               "DDS pixel format metadata is preserved");
        Expect(!metadata->dx10_header.has_value(),
               "legacy DDS metadata has no DX10 extension");
    }

    const auto dx10          = MakeDds(true);
    const auto dx10_metadata = ParseDdsMetadata(dx10);
    Expect(dx10_metadata.has_value() && dx10_metadata->dx10_header.has_value(),
           "DX10 DDS metadata parsing succeeds");
    if (dx10_metadata && dx10_metadata->dx10_header)
    {
        Expect(dx10_metadata->pixel_format.four_cc ==
                   std::array{ std::byte{ 'D' }, std::byte{ 'X' }, std::byte{ '1' }, std::byte{ '0' } },
               "DDS FourCC bytes are preserved");
        Expect(dx10_metadata->dx10_header->dxgi_format == 28 &&
                   dx10_metadata->dx10_header->resource_dimension == 3 &&
                   dx10_metadata->dx10_header->misc_flag == 4 &&
                   dx10_metadata->dx10_header->array_size == 2 &&
                   dx10_metadata->dx10_header->misc_flags2 == 1,
               "DDS DX10 extension values are preserved");
    }

    Expect(!ParseDdsMetadata(std::span(legacy).first(127)),
           "truncated DDS base metadata is rejected");

    auto invalid_signature      = legacy;
    invalid_signature[0]        = std::byte{ 0 };
    const auto signature_result = ParseDdsMetadata(invalid_signature);
    Expect(!signature_result &&
               signature_result.error() == DdsMetadataError::invalid_signature,
           "invalid DDS signature is rejected");

    auto invalid_header_size = legacy;
    WriteU32Le(invalid_header_size, 4, 123);
    const auto header_size_result = ParseDdsMetadata(invalid_header_size);
    Expect(!header_size_result &&
               header_size_result.error() == DdsMetadataError::invalid_header_size,
           "invalid DDS header size is rejected");

    auto invalid_pixel_format_size = legacy;
    WriteU32Le(invalid_pixel_format_size, 76, 31);
    const auto pixel_size_result = ParseDdsMetadata(invalid_pixel_format_size);
    Expect(!pixel_size_result &&
               pixel_size_result.error() == DdsMetadataError::invalid_pixel_format_size,
           "invalid DDS pixel format size is rejected");

    const auto truncated_dx10 = std::span(dx10).first(147);
    const auto dx10_result    = ParseDdsMetadata(truncated_dx10);
    Expect(!dx10_result && dx10_result.error() == DdsMetadataError::truncated,
           "truncated DDS DX10 extension is rejected");
}

void TestDdsDocument()
{
    using rerevved::studio::DdsMetadataError;
    using rerevved::studio::DdsPreviewError;
    using rerevved::studio::LoadDdsDocument;
    using rerevved::studio::ParseDdsDocument;

    const std::array bgra_pixels{
        std::byte{ 0x33 },
        std::byte{ 0x22 },
        std::byte{ 0x11 },
        std::byte{ 0x44 },
        std::byte{ 0xCC },
        std::byte{ 0xBB },
        std::byte{ 0xAA },
        std::byte{ 0xDD },
    };
    const auto bgra          = MakePreviewDds(2,
                                              1,
                                              0x00FF0000U,
                                              0x0000FF00U,
                                              0x000000FFU,
                                              0xFF000000U,
                                              bgra_pixels);
    const auto bgra_document = ParseDdsDocument(bgra);
    Expect(bgra_document.has_value(), "32-bit BGRA DDS preview decoding succeeds");
    if (bgra_document)
    {
        Expect(bgra_document->metadata.width == 2 && bgra_document->metadata.height == 1,
               "DDS preview dimensions come from parsed metadata");
        Expect(bgra_document->rgba8 ==
                   std::vector<std::byte>{
                       std::byte{ 0x11 },
                       std::byte{ 0x22 },
                       std::byte{ 0x33 },
                       std::byte{ 0x44 },
                       std::byte{ 0xAA },
                       std::byte{ 0xBB },
                       std::byte{ 0xCC },
                       std::byte{ 0xDD },
                   },
               "DDS channel masks produce RGBA8 preview pixels");
    }

    const std::array rgba_pixels{
        std::byte{ 0x10 },
        std::byte{ 0x20 },
        std::byte{ 0x30 },
        std::byte{ 0x7F },
    };
    const auto rgb          = MakePreviewDds(1,
                                             1,
                                             0x000000FFU,
                                             0x0000FF00U,
                                             0x00FF0000U,
                                             0,
                                             rgba_pixels);
    const auto rgb_document = ParseDdsDocument(rgb);
    Expect(rgb_document &&
               rgb_document->rgba8 ==
                   std::vector<std::byte>{
                       std::byte{ 0x10 },
                       std::byte{ 0x20 },
                       std::byte{ 0x30 },
                       std::byte{ 0xFF },
                   },
           "DDS RGB preview pixels receive opaque alpha");

    const auto truncated_metadata = ParseDdsDocument(std::span(bgra).first(127));
    Expect(!truncated_metadata &&
               std::get<DdsMetadataError>(truncated_metadata.error()) ==
                   DdsMetadataError::truncated,
           "DDS document preserves metadata truncation errors");

    const auto truncated_pixels = ParseDdsDocument(std::span(bgra).first(bgra.size() - 1));
    Expect(!truncated_pixels &&
               std::get<DdsPreviewError>(truncated_pixels.error()) ==
                   DdsPreviewError::truncated_pixel_data,
           "truncated DDS top-level pixels are rejected");

    auto       unsupported_dx10 = MakeDds(true);
    const auto dx10_result      = ParseDdsDocument(unsupported_dx10);
    Expect(!dx10_result &&
               std::get<DdsPreviewError>(dx10_result.error()) ==
                   DdsPreviewError::unsupported_format,
           "unsupported DX10 preview encoding is rejected");

    auto unsupported_masks = bgra;
    WriteU32Le(unsupported_masks, 92, 0x000003FFU);
    const auto mask_result = ParseDdsDocument(unsupported_masks);
    Expect(!mask_result &&
               std::get<DdsPreviewError>(mask_result.error()) ==
                   DdsPreviewError::unsupported_format,
           "unsupported DDS channel masks are rejected");

    auto ambiguous_flags = bgra;
    WriteU32Le(ambiguous_flags, 80, 0x00020041U);
    const auto flag_result = ParseDdsDocument(ambiguous_flags);
    Expect(!flag_result &&
               std::get<DdsPreviewError>(flag_result.error()) ==
                   DdsPreviewError::unsupported_format,
           "ambiguous DDS pixel format flags are rejected");

    auto cubemap = bgra;
    WriteU32Le(cubemap, 112, 0x00000200U);
    const auto cubemap_result = ParseDdsDocument(cubemap);
    Expect(!cubemap_result &&
               std::get<DdsPreviewError>(cubemap_result.error()) ==
                   DdsPreviewError::unsupported_format,
           "DDS cubemaps are rejected by the 2D preview");

    auto invalid_dimensions = bgra;
    WriteU32Le(invalid_dimensions, 16, 0);
    const auto dimension_result = ParseDdsDocument(invalid_dimensions);
    Expect(!dimension_result &&
               std::get<DdsPreviewError>(dimension_result.error()) ==
                   DdsPreviewError::invalid_dimensions,
           "zero DDS preview dimensions are rejected");

    auto overflowing_dimensions = bgra;
    WriteU32Le(overflowing_dimensions, 12, std::numeric_limits<std::uint32_t>::max());
    WriteU32Le(overflowing_dimensions, 16, std::numeric_limits<std::uint32_t>::max());
    const auto overflow_result = ParseDdsDocument(overflowing_dimensions);
    Expect(!overflow_result &&
               std::get<DdsPreviewError>(overflow_result.error()) ==
                   DdsPreviewError::size_overflow,
           "overflowing DDS preview dimensions are rejected");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-studio-dds-document-test.dds";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bgra.data()),
                     static_cast<std::streamsize>(bgra.size()));
    }
    const auto loaded_document = LoadDdsDocument(path);
    Expect(loaded_document && bgra_document &&
               loaded_document->rgba8 == bgra_document->rgba8,
           "DDS document loader preserves the synthetic decoded preview");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void TestGfxDocument()
{
    using rerevved::studio::GfxDocumentError;
    using rerevved::studio::LoadGfxDocument;
    using rerevved::studio::ParseGfxDocument;

    const auto bytes    = MakeGfx();
    const auto document = ParseGfxDocument(bytes);
    Expect(document.has_value(), "synthetic GFX v2 parsing succeeds");
    if (document)
    {
        Expect(document->file_version == 8 && document->declared_length == bytes.size(),
               "GFX file metadata is preserved");
        Expect(document->frame.x_min_twips == -20 &&
                   document->frame.x_max_twips == 1980 &&
                   document->frame.y_min_twips == -40 &&
                   document->frame.y_max_twips == 960,
               "signed GFX frame coordinates are preserved");
        Expect(document->frame_rate_raw == 0x1800 && document->frame_count == 2,
               "GFX frame rate and count are preserved");
        Expect(document->exporter.version == 0x0204 && document->exporter.flags == 5 &&
                   document->exporter.bitmap_format == 2 &&
                   document->exporter.prefix == ByteString("ui/") &&
                   document->exporter.swf_name == ByteString("menu"),
               "GFX exporter metadata is preserved");
        Expect(document->external_images.size() == 2,
               "long tags, unknown tags, and duplicate resources parse");
        if (document->external_images.size() == 2)
        {
            const auto& image = document->external_images[0];
            Expect(image.character_id == 7 && image.bitmap_format == 2 &&
                       image.target_width == 64 && image.target_height == 32,
                   "GFX external image numeric fields are preserved");
            Expect(image.export_name ==
                           std::vector<std::byte>{
                               std::byte{ 'i' },
                               std::byte{ 0 },
                               std::byte{ 0xFF },
                           } &&
                       image.file_name == ByteString("icon.dds") &&
                       document->external_images[1].export_name == image.export_name &&
                       document->external_images[1].file_name == image.file_name,
                   "GFX external names preserve bytes and source order");
        }
    }

    auto no_resources = BeginGfx();
    auto exporter     = MakeExporterInfo(0x02FF);
    AppendGfxTag(no_resources, 1000, exporter);
    FinishGfx(no_resources);
    const auto empty_document = ParseGfxDocument(no_resources);
    Expect(empty_document && empty_document->external_images.empty() &&
               empty_document->exporter.version == 0x02FF,
           "GFX 2.x without external resources succeeds");

    Expect(rerevved::studio::FormatGfxBytes(
               std::array{ std::byte{ 'A' }, std::byte{ '\\' }, std::byte{ 0 }, std::byte{ 0xFF } }) ==
               "A\\\\\\x00\\xFF",
           "GFX name bytes receive safe app-local formatting");

    const std::array short_input{ std::byte{ 'G' }, std::byte{ 'F' } };
    const auto       truncated = ParseGfxDocument(short_input);
    Expect(!truncated && truncated.error() == GfxDocumentError::truncated,
           "truncated GFX signature is rejected");

    const std::array invalid_signature{
        std::byte{ 'F' },
        std::byte{ 'W' },
        std::byte{ 'S' },
    };
    const auto signature_result = ParseGfxDocument(invalid_signature);
    Expect(!signature_result && signature_result.error() == GfxDocumentError::invalid_signature,
           "non-GFX signature is rejected");

    const std::array compressed_signature{
        std::byte{ 'C' },
        std::byte{ 'F' },
        std::byte{ 'X' },
    };
    const auto compressed_result = ParseGfxDocument(compressed_signature);
    Expect(!compressed_result &&
               compressed_result.error() == GfxDocumentError::unsupported_compression,
           "compressed GFX signature is reported as unsupported");

    auto mismatched_length = bytes;
    mismatched_length.push_back(std::byte{ 0 });
    const auto length_result = ParseGfxDocument(mismatched_length);
    Expect(!length_result && length_result.error() == GfxDocumentError::length_mismatch,
           "GFX declared length mismatch is rejected");

    auto missing_exporter = BeginGfx();
    FinishGfx(missing_exporter);
    const auto exporter_result = ParseGfxDocument(missing_exporter);
    Expect(!exporter_result &&
               exporter_result.error() == GfxDocumentError::missing_exporter_info,
           "missing first GFX exporter tag is rejected");

    auto unsupported_version = BeginGfx();
    exporter                 = MakeExporterInfo(0x0300);
    AppendGfxTag(unsupported_version, 1000, exporter);
    FinishGfx(unsupported_version);
    const auto version_result = ParseGfxDocument(unsupported_version);
    Expect(!version_result &&
               version_result.error() == GfxDocumentError::unsupported_exporter_version,
           "non-v2 GFX exporter is rejected");

    auto missing_end = BeginGfx();
    exporter         = MakeExporterInfo();
    AppendGfxTag(missing_end, 1000, exporter);
    UpdateGfxLength(missing_end);
    const auto end_result = ParseGfxDocument(missing_end);
    Expect(!end_result && end_result.error() == GfxDocumentError::invalid_tag_stream,
           "missing GFX End tag is rejected");

    auto malformed_end = BeginGfx();
    AppendGfxTag(malformed_end, 1000, exporter);
    AppendGfxTag(malformed_end, 0, std::array{ std::byte{ 0 } });
    UpdateGfxLength(malformed_end);
    Expect(!ParseGfxDocument(malformed_end), "non-empty GFX End tag is rejected");

    auto trailing_after_end = no_resources;
    trailing_after_end.push_back(std::byte{ 0 });
    UpdateGfxLength(trailing_after_end);
    Expect(!ParseGfxDocument(trailing_after_end), "bytes after GFX End tag are rejected");

    auto truncated_tag = BeginGfx();
    AppendGfxTag(truncated_tag, 1000, exporter);
    AppendU16Le(truncated_tag, static_cast<std::uint16_t>((77U << 6U) | 5U));
    truncated_tag.push_back(std::byte{ 0xA5 });
    UpdateGfxLength(truncated_tag);
    const auto tag_result = ParseGfxDocument(truncated_tag);
    Expect(!tag_result && tag_result.error() == GfxDocumentError::truncated,
           "out-of-range GFX tag payload is rejected");

    auto truncated_long_header = BeginGfx();
    AppendGfxTag(truncated_long_header, 1000, exporter);
    AppendU16Le(truncated_long_header, static_cast<std::uint16_t>((77U << 6U) | 0x3FU));
    UpdateGfxLength(truncated_long_header);
    const auto long_header_result = ParseGfxDocument(truncated_long_header);
    Expect(!long_header_result && long_header_result.error() == GfxDocumentError::truncated,
           "truncated GFX long tag header is rejected");

    auto truncated_rect = BeginGfx();
    truncated_rect.resize(9);
    UpdateGfxLength(truncated_rect);
    const auto rect_result = ParseGfxDocument(truncated_rect);
    Expect(!rect_result && rect_result.error() == GfxDocumentError::truncated,
           "truncated GFX frame rectangle is rejected");

    auto                   bad_exporter = BeginGfx();
    std::vector<std::byte> truncated_exporter;
    AppendU16Le(truncated_exporter, 0x0204);
    AppendU32Le(truncated_exporter, 0);
    AppendU16Le(truncated_exporter, 0);
    truncated_exporter.push_back(std::byte{ 3 });
    truncated_exporter.push_back(std::byte{ 'x' });
    AppendGfxTag(bad_exporter, 1000, truncated_exporter);
    FinishGfx(bad_exporter);
    const auto bad_exporter_result = ParseGfxDocument(bad_exporter);
    Expect(!bad_exporter_result && bad_exporter_result.error() == GfxDocumentError::truncated,
           "truncated GFX exporter string is rejected");

    auto bad_image = BeginGfx();
    AppendGfxTag(bad_image, 1000, exporter);
    std::vector<std::byte> truncated_image;
    AppendU16Le(truncated_image, 1);
    AppendU16Le(truncated_image, 2);
    AppendU16Le(truncated_image, 1);
    AppendU16Le(truncated_image, 1);
    truncated_image.push_back(std::byte{ 0 });
    truncated_image.push_back(std::byte{ 3 });
    truncated_image.push_back(std::byte{ 'x' });
    AppendGfxTag(bad_image, 1001, truncated_image);
    FinishGfx(bad_image);
    const auto bad_image_result = ParseGfxDocument(bad_image);
    Expect(!bad_image_result && bad_image_result.error() == GfxDocumentError::truncated,
           "truncated GFX external filename is rejected");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-studio-gfx-document-test.gfx";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto loaded_document = LoadGfxDocument(path);
    Expect(loaded_document && loaded_document->external_images.size() == 2,
           "GFX document loader preserves synthetic parse results");

    const auto invalid_path = std::filesystem::temp_directory_path() /
                              "rerevved-studio-invalid-gfx-document-test.gfx";
    {
        std::ofstream output(invalid_path, std::ios::binary);
        output.write("FWS", 3);
    }
    const auto invalid_document = LoadGfxDocument(invalid_path);
    Expect(!invalid_document &&
               invalid_document.error() == "The file does not have a valid GFX signature.",
           "GFX loader maps parse failures to app-facing messages");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(invalid_path, ignored);
}

void TestMapDocument()
{
    using rerevved::studio::kMapCoreSize;
    using rerevved::studio::kMapEnvelopeSize;
    using rerevved::studio::kMapFooterSize;
    using rerevved::studio::kMapHeight;
    using rerevved::studio::kMapWidth;
    using rerevved::studio::LoadMapDocument;
    using rerevved::studio::MapDocumentError;
    using rerevved::studio::ParseMapDocument;

    Expect(kMapWidth == 32 && kMapHeight == 32 && kMapCoreSize == 1024 &&
               kMapFooterSize == 64 && kMapEnvelopeSize == 1088,
           "Xbox DLC map envelope dimensions are fixed");

    std::array<std::byte, kMapEnvelopeSize> bytes{};
    for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
        bytes[offset] = static_cast<std::byte>((offset * 37U + 11U) & 0xFFU);
    std::ranges::fill(bytes.begin() + static_cast<std::ptrdiff_t>(kMapCoreSize),
                      bytes.end(),
                      std::byte{ 0xFF });

    const auto document = ParseMapDocument(bytes);
    Expect(document.has_value(), "synthetic 1088-byte map parsing succeeds");
    if (document)
    {
        Expect(std::ranges::equal(document->core, std::span(bytes).first(kMapCoreSize)),
               "map parser preserves the 1024-byte core");
        Expect(std::ranges::equal(document->footer,
                                  std::span(bytes).last(kMapFooterSize)),
               "map parser preserves the 64-byte footer");
    }

    auto unusual_footer              = bytes;
    unusual_footer[kMapCoreSize + 7] = std::byte{ 0x2A };
    const auto unusual_document      = ParseMapDocument(unusual_footer);
    Expect(unusual_document && unusual_document->footer[7] == std::byte{ 0x2A },
           "unproved map footer values remain observable");

    const auto truncated = ParseMapDocument(std::span(bytes).first(kMapEnvelopeSize - 1));
    Expect(!truncated && truncated.error() == MapDocumentError::truncated,
           "truncated map envelope is rejected");
    const auto empty = ParseMapDocument({});
    Expect(!empty && empty.error() == MapDocumentError::truncated,
           "empty map envelope is rejected");

    std::vector<std::byte> oversized(bytes.begin(), bytes.end());
    oversized.push_back(std::byte{ 0 });
    const auto unsupported = ParseMapDocument(oversized);
    Expect(!unsupported && unsupported.error() == MapDocumentError::unsupported_length,
           "map envelope with trailing data is rejected");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-studio-map-document-test.map";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto loaded = LoadMapDocument(path);
    Expect(loaded &&
               std::ranges::equal(loaded->core, std::span(bytes).first(kMapCoreSize)),
           "map loader preserves the synthetic document");

    const auto truncated_path = std::filesystem::temp_directory_path() /
                                "rerevved-studio-truncated-map-document-test.map";
    {
        std::ofstream output(truncated_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size() - 1));
    }
    const auto truncated_load = LoadMapDocument(truncated_path);
    Expect(!truncated_load &&
               truncated_load.error() ==
                   "The map record is truncated; the Xbox DLC profile requires 1088 bytes.",
           "map loader maps truncated input to an app-facing message");

    const auto oversized_path = std::filesystem::temp_directory_path() /
                                "rerevved-studio-oversized-map-document-test.map";
    {
        std::ofstream output(oversized_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(oversized.data()),
                     static_cast<std::streamsize>(oversized.size()));
    }
    const auto oversized_load = LoadMapDocument(oversized_path);
    Expect(!oversized_load &&
               oversized_load.error() ==
                   "The map record length is unsupported; the Xbox DLC profile requires exactly 1088 bytes.",
           "map loader maps unsupported length to an app-facing message");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(truncated_path, ignored);
    std::filesystem::remove(oversized_path, ignored);
}

void TestMp3Document()
{
    using rerevved::studio::LoadMp3Document;
    using rerevved::studio::Mp3DocumentError;
    using rerevved::studio::ParseMp3Document;
    using rerevved::studio::test::kSyntheticMp3;

    Expect(kSyntheticMp3.size() == 418, "synthetic MP3 byte count is preserved");
    const auto bytes =
        std::as_bytes(std::span(kSyntheticMp3.data(), kSyntheticMp3.size()));
    const auto document = ParseMp3Document(bytes);
    Expect(document.has_value(), "synthetic MP3 waveform decoding succeeds");
    if (document)
    {
        Expect(document->sample_rate == 11025 && document->channels == 1,
               "MP3 stream metadata is preserved");
        Expect(document->preview_frame_count > 0 && document->preview_complete,
               "short MP3 preview reaches the stream boundary");
        Expect(!document->waveform.empty(), "MP3 preview contains waveform points");
        Expect(std::ranges::all_of(document->waveform,
                                   [](float value)
                                   {
                                       return value >= -1.0F && value <= 1.0F;
                                   }),
               "MP3 waveform points stay in the decoded amplitude range");
    }

    const std::array<unsigned char, 2> truncated{ 0xFF, 0xE3 };
    const auto                         truncated_result = ParseMp3Document(std::as_bytes(std::span(truncated)));
    Expect(!truncated_result && truncated_result.error() == Mp3DocumentError::decode_failed,
           "truncated MP3 data reports a clean decode failure");
    const auto empty_result = ParseMp3Document({});
    Expect(!empty_result && empty_result.error() == Mp3DocumentError::decode_failed,
           "empty MP3 data reports a clean decode failure");
    const auto damaged_tail = ParseMp3Document(bytes.first(bytes.size() - 20));
    Expect(!damaged_tail && damaged_tail.error() == Mp3DocumentError::decode_failed,
           "MP3 data with a valid prefix and truncated final frame is rejected");

    std::vector<char> long_stream;
    for (int repetition = 0; repetition < 60; ++repetition)
        long_stream.insert(long_stream.end(), kSyntheticMp3.begin(), kSyntheticMp3.end());
    const auto bounded = ParseMp3Document(std::as_bytes(std::span(long_stream)));
    Expect(bounded && bounded->preview_frame_count == bounded->sample_rate * 10ULL &&
               !bounded->preview_complete && bounded->waveform.size() == 1000,
           "MP3 waveform decoding stops at the ten-second preview boundary");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-studio-mp3-document-test.mp3";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(kSyntheticMp3.data(), static_cast<std::streamsize>(kSyntheticMp3.size()));
    }
    const auto loaded = LoadMp3Document(path);
    Expect(loaded && loaded->sample_rate == 11025,
           "MP3 loader decodes the synthetic stream");

    const auto invalid_path = std::filesystem::temp_directory_path() /
                              "rerevved-studio-invalid-mp3-document-test.mp3";
    {
        std::ofstream output(invalid_path, std::ios::binary);
        output.write("not mp3", 7);
    }
    const auto invalid = LoadMp3Document(invalid_path);
    Expect(!invalid && invalid.error() == "The MP3 audio could not be decoded.",
           "MP3 loader maps decoder failures to an app-facing message");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(invalid_path, ignored);
}

void TestNifDocument()
{
    using rerevved::studio::LoadNifDocument;
    using rerevved::studio::NifDocumentError;
    using rerevved::studio::ParseNifDocument;

    const auto synthetic = MakeNif();
    const auto document  = ParseNifDocument(synthetic.bytes);
    Expect(document.has_value(), "synthetic big-endian NIF parsing succeeds");
    if (document)
    {
        Expect(document->version == 0x14030009 && document->endian == 0 &&
                   document->user_version == 0,
               "NIF version fields are preserved");
        Expect(document->block_types.size() == 3 &&
                   document->block_types[0] == ByteString("NiNode") &&
                   document->block_types[1] == ByteString("NiTriShape") &&
                   document->block_types[2] == ByteString("OpaqueBlock"),
               "NIF block type bytes are preserved");
        Expect(document->blocks.size() == 3 && document->blocks[0].type_index == 0 &&
                   document->blocks[0].size == 94 && document->blocks[1].type_index == 1 &&
                   document->blocks[1].size == 107 && document->blocks[2].size == 3,
               "NIF block inventory is preserved");
        Expect(document->blocks[0].offset == document->header_size &&
                   document->blocks[1].offset == document->header_size + 94 &&
                   document->blocks[2].offset == document->header_size + 201,
               "NIF block offsets are derived from declared sizes");
        Expect(document->strings.size() == 2 && document->strings[0] == ByteString("Root") &&
                   document->strings[1] ==
                       std::vector<std::byte>{ std::byte{ 0 }, std::byte{ 0xFF } } &&
                   document->max_string_length == 4,
               "NIF strings preserve non-text bytes and maximum length");
        Expect(document->groups == std::vector<std::uint32_t>{ 3 } &&
                   document->roots == std::vector<std::uint32_t>{ 0 },
               "NIF groups and roots are preserved");
        Expect(document->nodes.size() == 1 && document->nodes[0].block_index == 0 &&
                   document->nodes[0].object.name_index == 0 &&
                   document->nodes[0].object.extra_data == std::vector<std::uint32_t>{ 2 } &&
                   document->nodes[0].object.flags == 0x1234 &&
                   document->nodes[0].object.translation == std::array{ 1.0F, 2.0F, 3.0F } &&
                   document->nodes[0].object.scale == 2.0F &&
                   document->nodes[0].object.properties == std::vector<std::uint32_t>{ 2 } &&
                   document->nodes[0].children == std::vector<std::uint32_t>{ 1 } &&
                   document->nodes[0].effects.empty(),
               "standard NiNode metadata and references are preserved");
        Expect(document->tri_shapes.size() == 1 && document->tri_shapes[0].block_index == 1 &&
                   document->tri_shapes[0].object.name_index == 1 &&
                   document->tri_shapes[0].data == 2 &&
                   document->tri_shapes[0].skin_instance == 0xFFFFFFFF &&
                   document->tri_shapes[0].material.name_indices ==
                       std::vector<std::uint32_t>{ 0 } &&
                   document->tri_shapes[0].material.extra_data ==
                       std::vector<std::int32_t>{ -7 } &&
                   document->tri_shapes[0].material.active_material == 0 &&
                   document->tri_shapes[0].material.material_needs_update == 1,
               "standard NiTriShape metadata and material fields are preserved");
    }

    bool all_truncations_rejected = true;
    for (std::size_t size = 0; size < synthetic.bytes.size(); ++size)
        all_truncations_rejected =
            all_truncations_rejected && !ParseNifDocument(std::span(synthetic.bytes).first(size));
    Expect(all_truncations_rejected, "every truncated NIF prefix is rejected");

    auto invalid_signature      = synthetic.bytes;
    invalid_signature[0]        = std::byte{ 'N' };
    const auto signature_result = ParseNifDocument(invalid_signature);
    Expect(!signature_result && signature_result.error() == NifDocumentError::invalid_signature,
           "non-Gamebryo NIF signature is rejected");

    auto unsupported_text_version  = synthetic.bytes;
    unsupported_text_version[37]   = std::byte{ '8' };
    const auto text_version_result = ParseNifDocument(unsupported_text_version);
    Expect(!text_version_result &&
               text_version_result.error() == NifDocumentError::unsupported_version,
           "unsupported textual NIF version is rejected");

    auto unsupported_binary_version = synthetic.bytes;
    WriteU32Le(unsupported_binary_version, 39, 0x14020007);
    const auto binary_version_result = ParseNifDocument(unsupported_binary_version);
    Expect(!binary_version_result &&
               binary_version_result.error() == NifDocumentError::unsupported_version,
           "unsupported binary NIF version is rejected");

    auto unsupported_endian  = synthetic.bytes;
    unsupported_endian[43]   = std::byte{ 1 };
    const auto endian_result = ParseNifDocument(unsupported_endian);
    Expect(!endian_result && endian_result.error() == NifDocumentError::unsupported_endian,
           "unobserved little-endian NIF layout is rejected");

    auto unsupported_user_version = synthetic.bytes;
    WriteU32Le(unsupported_user_version, 44, 1);
    const auto user_version_result = ParseNifDocument(unsupported_user_version);
    Expect(!user_version_result &&
               user_version_result.error() == NifDocumentError::unsupported_user_version,
           "unobserved NIF user version is rejected");

    auto invalid_type_index = synthetic.bytes;
    WriteU16Be(invalid_type_index, synthetic.first_type_index_offset, 3);
    const auto type_index_result = ParseNifDocument(invalid_type_index);
    Expect(!type_index_result && type_index_result.error() == NifDocumentError::invalid_layout,
           "out-of-range NIF block type index is rejected");

    auto invalid_node_name = synthetic.bytes;
    WriteU32Be(invalid_node_name, synthetic.node_name_index_offset, 2);
    const auto node_name_result = ParseNifDocument(invalid_node_name);
    Expect(!node_name_result && node_name_result.error() == NifDocumentError::invalid_layout,
           "out-of-range NiNode name index is rejected");

    auto null_node_name = synthetic.bytes;
    WriteU32Be(null_node_name, synthetic.node_name_index_offset, 0xFFFFFFFF);
    const auto null_node_name_result = ParseNifDocument(null_node_name);
    Expect(null_node_name_result && null_node_name_result->nodes[0].object.name_index == 0xFFFFFFFF,
           "observed null NiNode name index is preserved");

    auto oversized_extra_data = synthetic.bytes;
    WriteU32Be(oversized_extra_data, synthetic.node_extra_count_offset, 0xFFFFFFFF);
    const auto extra_data_result = ParseNifDocument(oversized_extra_data);
    Expect(!extra_data_result && extra_data_result.error() == NifDocumentError::invalid_layout,
           "oversized NiNode extra-data list is rejected within its block");

    auto invalid_child = synthetic.bytes;
    WriteU32Be(invalid_child, synthetic.node_child_offset, 3);
    const auto child_result = ParseNifDocument(invalid_child);
    Expect(!child_result && child_result.error() == NifDocumentError::invalid_layout,
           "out-of-range NiNode child reference is rejected");

    auto invalid_shape_data = synthetic.bytes;
    WriteU32Be(invalid_shape_data, synthetic.shape_data_offset, 3);
    const auto shape_data_result = ParseNifDocument(invalid_shape_data);
    Expect(!shape_data_result && shape_data_result.error() == NifDocumentError::invalid_layout,
           "out-of-range NiTriShape data reference is rejected");

    auto oversized_materials = synthetic.bytes;
    WriteU32Be(oversized_materials, synthetic.shape_material_count_offset, 0xFFFFFFFF);
    const auto materials_result = ParseNifDocument(oversized_materials);
    Expect(!materials_result && materials_result.error() == NifDocumentError::invalid_layout,
           "oversized NiTriShape material list is rejected within its block");

    auto invalid_material_name = synthetic.bytes;
    WriteU32Be(invalid_material_name, synthetic.shape_material_count_offset + 4, 2);
    const auto material_name_result = ParseNifDocument(invalid_material_name);
    Expect(!material_name_result &&
               material_name_result.error() == NifDocumentError::invalid_layout,
           "out-of-range NiTriShape material name index is rejected");

    auto truncated_node_layout = synthetic.bytes;
    WriteU32Be(truncated_node_layout, synthetic.node_child_offset + 4, 1);
    const auto node_layout_result = ParseNifDocument(truncated_node_layout);
    Expect(!node_layout_result && node_layout_result.error() == NifDocumentError::invalid_layout,
           "NiNode arrays must fit exactly inside the declared block");

    auto oversized_block_count = synthetic.bytes;
    WriteU32Le(oversized_block_count, 48, 0xFFFFFFFF);
    const auto block_count_result = ParseNifDocument(oversized_block_count);
    Expect(!block_count_result && block_count_result.error() == NifDocumentError::truncated,
           "oversized NIF block count is rejected before allocation");

    auto oversized_type_count = synthetic.bytes;
    WriteU16Be(oversized_type_count, synthetic.block_type_count_offset, 0xFFFF);
    const auto type_count_result = ParseNifDocument(oversized_type_count);
    Expect(!type_count_result && type_count_result.error() == NifDocumentError::truncated,
           "oversized NIF block type count is rejected before allocation");

    const auto truncated_type = ParseNifDocument(std::span(synthetic.bytes).first(60));
    Expect(!truncated_type && truncated_type.error() == NifDocumentError::truncated,
           "truncated NIF type string is rejected");

    auto oversized_block = synthetic.bytes;
    WriteU32Be(oversized_block, synthetic.first_block_size_offset, 0xFFFFFFFF);
    const auto block_result = ParseNifDocument(oversized_block);
    Expect(!block_result && block_result.error() == NifDocumentError::truncated,
           "out-of-range NIF block payload is rejected");

    auto invalid_max_string = synthetic.bytes;
    WriteU32Be(invalid_max_string, synthetic.max_string_length_offset, 1);
    const auto string_result = ParseNifDocument(invalid_max_string);
    Expect(!string_result && string_result.error() == NifDocumentError::invalid_layout,
           "NIF string longer than declared maximum is rejected");

    auto oversized_string_count = synthetic.bytes;
    WriteU32Be(oversized_string_count, synthetic.string_count_offset, 0xFFFFFFFF);
    const auto string_count_result = ParseNifDocument(oversized_string_count);
    Expect(!string_count_result && string_count_result.error() == NifDocumentError::truncated,
           "oversized NIF string count is rejected before allocation");

    auto oversized_group_count = synthetic.bytes;
    WriteU32Be(oversized_group_count, synthetic.group_count_offset, 0xFFFFFFFF);
    const auto group_count_result = ParseNifDocument(oversized_group_count);
    Expect(!group_count_result && group_count_result.error() == NifDocumentError::truncated,
           "oversized NIF group count is rejected before allocation");

    auto truncated_footer = synthetic.bytes;
    truncated_footer.pop_back();
    const auto footer_result = ParseNifDocument(truncated_footer);
    Expect(!footer_result && footer_result.error() == NifDocumentError::truncated,
           "truncated NIF footer is rejected");

    auto oversized_root_count = synthetic.bytes;
    WriteU32Be(oversized_root_count, synthetic.root_count_offset, 0xFFFFFFFF);
    const auto root_count_result = ParseNifDocument(oversized_root_count);
    Expect(!root_count_result && root_count_result.error() == NifDocumentError::truncated,
           "oversized NIF root count is rejected before allocation");

    auto trailing_bytes = synthetic.bytes;
    trailing_bytes.push_back(std::byte{ 0 });
    const auto trailing_result = ParseNifDocument(trailing_bytes);
    Expect(!trailing_result && trailing_result.error() == NifDocumentError::invalid_layout,
           "bytes after the NIF footer are rejected");

    auto invalid_root = synthetic.bytes;
    WriteU32Be(invalid_root, synthetic.root_index_offset, 3);
    const auto root_result = ParseNifDocument(invalid_root);
    Expect(!root_result && root_result.error() == NifDocumentError::invalid_layout,
           "out-of-range NIF root reference is rejected");

    auto null_root = synthetic.bytes;
    WriteU32Be(null_root, synthetic.root_index_offset, 0xFFFFFFFF);
    const auto null_root_result = ParseNifDocument(null_root);
    Expect(null_root_result && null_root_result->roots[0] == 0xFFFFFFFF,
           "NIF null root reference is preserved");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-studio-nif-document-test.nif";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(synthetic.bytes.data()),
                     static_cast<std::streamsize>(synthetic.bytes.size()));
    }
    const auto loaded_document = LoadNifDocument(path);
    Expect(loaded_document && loaded_document->blocks.size() == 3,
           "NIF document loader preserves the synthetic block inventory");

    const auto invalid_path = std::filesystem::temp_directory_path() /
                              "rerevved-studio-invalid-nif-document-test.nif";
    {
        std::ofstream output(invalid_path, std::ios::binary);
        output.write("Not a NIF file", 14);
    }
    const auto invalid_document = LoadNifDocument(invalid_path);
    Expect(!invalid_document && invalid_document.error() == "The NIF file is truncated.",
           "NIF loader maps parse failures to app-facing messages");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(invalid_path, ignored);
}

void TestNifGeometryInventory()
{
    using rerevved::studio::NifDocumentError;
    using rerevved::studio::ParseNifDocument;

    const auto populated = MakeGeometryNif(true);
    const auto document  = ParseNifDocument(populated.bytes);
    Expect(document && document->tri_shape_data.size() == 1,
           "synthetic NiTriShapeData inventory parsing succeeds");
    if (document && document->tri_shape_data.size() == 1)
    {
        const auto& data = document->tri_shape_data[0];
        Expect(data.block_index == 0 && data.group_id == -7 && data.vertex_count == 2 &&
                   data.keep_flags == 0xA1 && data.compress_flags == 0xB2 &&
                   data.has_vertices == 0xFF && data.data_flags == 0x1001 &&
                   data.has_normals == 2,
               "NiTriShapeData vertex inventory and raw presence values are preserved");
        Expect(data.vertex_positions ==
                       std::vector<std::array<float, 3>>(2, std::array{ 0.0F, 0.0F, 0.0F }) &&
                   data.normal_vectors ==
                       std::vector<std::array<float, 3>>(2, std::array{ 0.0F, 0.0F, 0.0F }),
               "NiTriShapeData positions and normals are retained in source order");
        Expect(data.bound_center == std::array{ 1.0F, 2.0F, 3.0F } &&
                   data.bound_radius == -4.0F && data.has_vertex_colors == 0x80 &&
                   data.consistency_flags == 0xBEEF && data.additional_data == 0xFFFFFFFF,
               "NiTriShapeData bounds, flags, and additional-data reference are preserved");
        Expect(data.triangle_count == 1 && data.triangle_point_count == 7 &&
                   data.has_triangles == 0x7F && data.match_group_count == 2,
               "NiTriShapeData triangle inventory is preserved without semantic rejection");
        Expect(data.triangles ==
                       std::vector<std::array<std::uint16_t, 3>>{ { 0, 1, 0 } } &&
                   data.normal_sharing_groups ==
                       std::vector<std::vector<std::uint16_t>>{ { 0, 7 }, { 5, 1, 9 } },
               "triangle and above-vertex normal-sharing selectors remain ordered and exact");
    }

    const auto empty        = MakeGeometryNif(false);
    const auto empty_result = ParseNifDocument(empty.bytes);
    Expect(empty_result && empty_result->tri_shape_data.size() == 1 &&
               empty_result->tri_shape_data[0].vertex_count == 0 &&
               empty_result->tri_shape_data[0].triangle_count == 0 &&
               empty_result->tri_shape_data[0].match_group_count == 0 &&
               empty_result->tri_shape_data[0].vertex_positions.empty() &&
               empty_result->tri_shape_data[0].normal_vectors.empty() &&
               empty_result->tri_shape_data[0].triangles.empty() &&
               empty_result->tri_shape_data[0].normal_sharing_groups.empty(),
           "absent NiTriShapeData arrays produce empty retained containers");

    auto tangent_flag_without_normals = empty.bytes;
    WriteU16Be(tangent_flag_without_normals, empty.data_flags_offset, 0x1000);
    const auto tangent_flag_result = ParseNifDocument(tangent_flag_without_normals);
    Expect(tangent_flag_result && tangent_flag_result->tri_shape_data.size() == 1 &&
               tangent_flag_result->tri_shape_data[0].has_normals == 0 &&
               tangent_flag_result->tri_shape_data[0].normal_vectors.empty(),
           "NiTriShapeData tangent flag does not create retained arrays without normals");

    bool all_truncations_rejected = true;
    for (std::size_t size = 0; size < populated.bytes.size(); ++size)
        all_truncations_rejected =
            all_truncations_rejected && !ParseNifDocument(std::span(populated.bytes).first(size));
    Expect(all_truncations_rejected,
           "every truncated NiTriShapeData container prefix is rejected");

    for (const auto [array_offset, retained_bytes, label] :
         std::array{
             std::tuple{ populated.vertex_positions_offset, std::size_t{ 23 }, "positions" },
             std::tuple{ populated.normal_vectors_offset, std::size_t{ 23 }, "normals" },
             std::tuple{ populated.triangle_indices_offset, std::size_t{ 5 }, "triangles" },
             std::tuple{ populated.match_group_selectors_offset,
                         std::size_t{ 3 },
                         "normal-sharing selectors" },
         })
    {
        auto truncated_array = populated.bytes;
        WriteU32Be(truncated_array,
                   populated.block_size_offset,
                   static_cast<std::uint32_t>(array_offset - populated.block_payload_offset +
                                              retained_bytes));
        const auto result = ParseNifDocument(truncated_array);
        Expect(!result && result.error() == NifDocumentError::invalid_layout,
               std::string("truncated NiTriShapeData ") + label + " are rejected safely");
    }

    auto oversized_vertices = populated.bytes;
    WriteU16Be(oversized_vertices, populated.vertex_count_offset, 0xFFFF);
    const auto vertices_result = ParseNifDocument(oversized_vertices);
    Expect(!vertices_result && vertices_result.error() == NifDocumentError::invalid_layout,
           "oversized NiTriShapeData vertex arrays are rejected before retention allocation");

    auto oversized_normals = empty.bytes;
    WriteU16Be(oversized_normals, empty.vertex_count_offset, 0xFFFF);
    oversized_normals[empty.has_normals_offset] = std::byte{ 1 };
    const auto normals_result                   = ParseNifDocument(oversized_normals);
    Expect(!normals_result && normals_result.error() == NifDocumentError::invalid_layout,
           "oversized NiTriShapeData normal arrays are rejected before retention allocation");

    auto oversized_uv_sets = populated.bytes;
    WriteU16Be(oversized_uv_sets, populated.data_flags_offset, 0x103F);
    const auto uv_result = ParseNifDocument(oversized_uv_sets);
    Expect(!uv_result && uv_result.error() == NifDocumentError::invalid_layout,
           "oversized NiTriShapeData UV arrays are rejected within the block");

    auto invalid_additional_data = populated.bytes;
    WriteU32Be(invalid_additional_data, populated.additional_data_offset, 1);
    const auto additional_data_result = ParseNifDocument(invalid_additional_data);
    Expect(!additional_data_result &&
               additional_data_result.error() == NifDocumentError::invalid_layout,
           "out-of-range NiTriShapeData additional-data reference is rejected");

    auto oversized_triangles = populated.bytes;
    WriteU16Be(oversized_triangles, populated.triangle_count_offset, 0xFFFF);
    const auto triangles_result = ParseNifDocument(oversized_triangles);
    Expect(!triangles_result && triangles_result.error() == NifDocumentError::invalid_layout,
           "oversized NiTriShapeData triangle arrays are rejected before retention allocation");

    auto oversized_match_group = populated.bytes;
    WriteU16Be(
        oversized_match_group, populated.match_group_vertex_count_offset, 0xFFFF);
    const auto match_group_result = ParseNifDocument(oversized_match_group);
    Expect(!match_group_result &&
               match_group_result.error() == NifDocumentError::invalid_layout,
           "oversized normal-sharing selector arrays are rejected before retention allocation");

    auto trailing_match_group = populated.bytes;
    WriteU16Be(trailing_match_group, populated.match_group_count_offset, 0);
    const auto trailing_result = ParseNifDocument(trailing_match_group);
    Expect(!trailing_result && trailing_result.error() == NifDocumentError::invalid_layout,
           "NiTriShapeData must consume its declared block exactly");
}

void TestSyntheticModelFixture()
{
    using rerevved::studio::NifDocumentError;
    using rerevved::studio::ParseNifDocument;

    const auto synthetic = MakeSyntheticModelNif();
    Expect(synthetic.bytes == MakeSyntheticModelNif().bytes,
           "synthetic model fixture generation is deterministic");

    const auto document = ParseNifDocument(synthetic.bytes);
    Expect(document.has_value(), "complete synthetic model fixture parses successfully");
    if (document)
    {
        const std::vector<std::vector<std::byte>> expected_types{
            ByteString("NiNode"),
            ByteString("NiTriShape"),
            ByteString("NiTriShapeData"),
            ByteString("NiMaterialProperty"),
        };
        const std::vector<std::vector<std::byte>> expected_strings{
            ByteString("SyntheticRoot"),
            ByteString("SyntheticTriangle"),
            ByteString("SyntheticMaterial"),
        };
        Expect(document->version == 0x14030009 && document->endian == 0 &&
                   document->user_version == 0,
               "synthetic model fixture uses the supported Gamebryo profile");
        Expect(document->block_types == expected_types && document->blocks.size() == 4 &&
                   document->blocks[0].type_index == 0 &&
                   document->blocks[1].type_index == 1 &&
                   document->blocks[2].type_index == 2 &&
                   document->blocks[3].type_index == 3 &&
                   std::ranges::all_of(document->blocks, [&](const auto& block)
                                       {
                                           return block.type_index < document->block_types.size();
                                       }),
               "synthetic model block types and type indices are exact and in range");
        Expect(document->strings == expected_strings && document->max_string_length == 17 &&
                   document->groups.empty(),
               "synthetic model names and empty group table are exact");

        Expect(document->roots == std::vector<std::uint32_t>{ 0 } &&
                   document->blocks[document->roots[0]].type_index == 0 &&
                   document->nodes.size() == 1 && document->nodes[0].block_index == 0,
               "synthetic footer root selects the NiNode block");
        Expect(document->nodes[0].children == std::vector<std::uint32_t>{ 1 } &&
                   document->blocks[document->nodes[0].children[0]].type_index == 1,
               "synthetic NiNode child selects the NiTriShape block");
        Expect(document->nodes[0].object.properties.empty() &&
                   document->nodes[0].object.controller == 0xFFFFFFFF &&
                   document->nodes[0].object.translation ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->nodes[0].object.rotation ==
                       std::array{ 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F } &&
                   document->nodes[0].object.scale == 1.0F &&
                   document->nodes[0].effects.empty(),
               "synthetic root retains its identity local transform and empty references");

        Expect(document->tri_shapes.size() == 1 &&
                   document->tri_shapes[0].block_index == 1 &&
                   document->tri_shapes[0].data == 2 &&
                   document->blocks[document->tri_shapes[0].data].type_index == 2,
               "synthetic NiTriShape data reference selects NiTriShapeData");
        Expect(document->tri_shapes[0].object.properties ==
                       std::vector<std::uint32_t>{ 3 } &&
                   document->blocks[document->tri_shapes[0].object.properties[0]].type_index ==
                       3 &&
                   document->blocks[3].size == 68,
               "synthetic NiTriShape property selects the standard NiMaterialProperty block");
        Expect(document->material_properties.size() == 1 &&
                   document->material_properties[0].block_index == 3 &&
                   document->material_properties[0].ambient_color ==
                       std::array{ 1.0F, 1.0F, 1.0F } &&
                   document->material_properties[0].diffuse_color ==
                       std::array{ 1.0F, 1.0F, 1.0F } &&
                   document->material_properties[0].specular_color ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->material_properties[0].emissive_color ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->material_properties[0].glossiness == 1.0F &&
                   document->material_properties[0].alpha == 1.0F,
               "material-positive fixture retains the complete material payload");
        Expect(document->tri_shapes[0].object.controller == 0xFFFFFFFF &&
                   document->tri_shapes[0].object.translation ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->tri_shapes[0].object.rotation ==
                       std::array{ 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F } &&
                   document->tri_shapes[0].object.scale == 1.0F &&
                   document->tri_shapes[0].skin_instance == 0xFFFFFFFF &&
                   document->tri_shapes[0].material.name_indices.empty() &&
                   document->tri_shapes[0].material.active_material == -1,
               "synthetic shape has no animation, skinning, or material-name list");

        Expect(document->tri_shape_data.size() == 1 &&
                   document->tri_shape_data[0].block_index == 2 &&
                   document->tri_shape_data[0].vertex_count == 3 &&
                   document->tri_shape_data[0].has_vertices == 1 &&
                   document->tri_shape_data[0].data_flags == 0 &&
                   document->tri_shape_data[0].has_normals == 1 &&
                   document->tri_shape_data[0].has_vertex_colors == 0,
               "synthetic vertex and normal presence values are retained without UVs or colors");
        const std::vector<std::array<float, 3>> expected_positions{
            { 0.0F, 0.0F, 0.0F },
            { 1.0F, 0.0F, 0.0F },
            { 0.0F, 1.0F, 0.0F },
        };
        const std::vector<std::array<float, 3>> expected_normals{
            { 1.0F, 0.0F, 0.0F },
            { 0.0F, 1.0F, 0.0F },
            { 0.0F, 0.0F, 1.0F },
        };
        Expect(document->tri_shape_data[0].vertex_positions == expected_positions &&
                   document->tri_shape_data[0].normal_vectors == expected_normals,
               "synthetic vertex positions and normals retain their exact source order");
        Expect(document->tri_shape_data[0].triangle_count == 1 &&
                   document->tri_shape_data[0].triangle_point_count == 3 &&
                   document->tri_shape_data[0].has_triangles == 1 &&
                   document->tri_shape_data[0].match_group_count == 0,
               "synthetic triangle presence and counts are retained without match groups");
        Expect(document->tri_shape_data[0].triangles ==
                       std::vector<std::array<std::uint16_t, 3>>{ { 0, 1, 2 } } &&
                   document->tri_shape_data[0].normal_sharing_groups.empty(),
               "synthetic triangle selectors are retained and absent groups remain empty");
    }

    const std::array triangle_indices{
        std::byte{ 0 },
        std::byte{ 0 },
        std::byte{ 0 },
        std::byte{ 1 },
        std::byte{ 0 },
        std::byte{ 2 },
    };
    Expect(std::ranges::equal(
               std::span(synthetic.bytes).subspan(synthetic.triangle_indices_offset, 6),
               triangle_indices),
           "synthetic triangle indices are exactly 0, 1, and 2");

    auto                 serialized_transform = synthetic.bytes;
    constexpr std::array serialized_rotation{
        0.0F,
        1.0F,
        0.0F,
        -1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
    };
    WriteNifF32(serialized_transform, synthetic.shape_transform_offset, 4.0F);
    WriteNifF32(serialized_transform, synthetic.shape_transform_offset + 4, -5.0F);
    WriteNifF32(serialized_transform, synthetic.shape_transform_offset + 8, 6.0F);
    for (std::size_t index = 0; index < serialized_rotation.size(); ++index)
    {
        WriteNifF32(serialized_transform,
                    synthetic.shape_transform_offset + 12 + index * sizeof(float),
                    serialized_rotation[index]);
    }
    WriteNifF32(serialized_transform, synthetic.shape_transform_offset + 48, 2.5F);
    const auto transformed_document = ParseNifDocument(serialized_transform);
    Expect(transformed_document &&
               transformed_document->tri_shapes[0].object.translation ==
                   std::array{ 4.0F, -5.0F, 6.0F } &&
               transformed_document->tri_shapes[0].object.rotation ==
                   serialized_rotation &&
               transformed_document->tri_shapes[0].object.scale == 2.5F,
           "serialized shape translation, rotation, and scale retain exact f32 values");

    bool all_truncations_rejected = true;
    for (std::size_t size = 0; size < synthetic.bytes.size(); ++size)
        all_truncations_rejected = all_truncations_rejected &&
                                   !ParseNifDocument(std::span(synthetic.bytes).first(size));
    Expect(all_truncations_rejected,
           "every truncated synthetic model fixture prefix is rejected");
    Expect(!ParseNifDocument(std::span(synthetic.bytes).first(synthetic.node_transform_offset + 51)) &&
               !ParseNifDocument(std::span(synthetic.bytes).first(synthetic.shape_transform_offset + 51)),
           "truncated node and shape transform records fail through block-bounded parsing");

    for (const auto [offset, label] :
         std::array{
             std::pair{ synthetic.root_reference_offset, "root" },
             std::pair{ synthetic.child_reference_offset, "child" },
             std::pair{ synthetic.data_reference_offset, "data" },
             std::pair{ synthetic.property_reference_offset, "property" },
         })
    {
        auto corrupted = synthetic.bytes;
        WriteU32Be(corrupted, offset, 4);
        const auto result = ParseNifDocument(corrupted);
        Expect(!result && result.error() == NifDocumentError::invalid_layout,
               std::string("out-of-range synthetic ") + label + " reference is rejected");
    }
}

void TestNifMaterialPropertyInventory()
{
    using namespace rerevved::studio;

    const auto synthetic = MakeSyntheticMaterialNif();
    const auto parsed    = ParseNifDocument(synthetic.bytes);
    Expect(parsed && parsed->material_properties.size() == 2,
           "multiple synthetic material properties parse and retain in block order");
    if (parsed && parsed->material_properties.size() == 2)
    {
        const auto retained_bits = [](const NifMaterialPropertyInventory& material)
        {
            return std::array{
                std::bit_cast<std::uint32_t>(material.ambient_color[0]),
                std::bit_cast<std::uint32_t>(material.ambient_color[1]),
                std::bit_cast<std::uint32_t>(material.ambient_color[2]),
                std::bit_cast<std::uint32_t>(material.diffuse_color[0]),
                std::bit_cast<std::uint32_t>(material.diffuse_color[1]),
                std::bit_cast<std::uint32_t>(material.diffuse_color[2]),
                std::bit_cast<std::uint32_t>(material.specular_color[0]),
                std::bit_cast<std::uint32_t>(material.specular_color[1]),
                std::bit_cast<std::uint32_t>(material.specular_color[2]),
                std::bit_cast<std::uint32_t>(material.emissive_color[0]),
                std::bit_cast<std::uint32_t>(material.emissive_color[1]),
                std::bit_cast<std::uint32_t>(material.emissive_color[2]),
                std::bit_cast<std::uint32_t>(material.glossiness),
                std::bit_cast<std::uint32_t>(material.alpha),
            };
        };

        Expect(parsed->material_properties[0].block_index == 0 &&
                   parsed->material_properties[1].block_index == 1,
               "material inventory identity follows exact source block order");
        Expect(retained_bits(parsed->material_properties[0]) == kSyntheticMaterialBitsA,
               "ambient, diffuse, specular, emissive, glossiness, and alpha bits retain exact order");
        Expect(retained_bits(parsed->material_properties[1]) == kSyntheticMaterialBitsB,
               "a second material retains independent ordered f32 values");
        Expect(std::signbit(parsed->material_properties[0].specular_color[1]) &&
                   std::isnan(parsed->material_properties[0].specular_color[2]) &&
                   std::isinf(parsed->material_properties[0].emissive_color[0]) &&
                   std::isinf(parsed->material_properties[0].emissive_color[1]),
               "signed zero and representative non-finite values remain accepted and unchanged");
    }

    constexpr std::size_t kObjectNetSize        = 12;
    constexpr std::size_t kMaterialSize         = 68;
    const auto            parse_with_first_size = [&](std::size_t retained_size)
    {
        auto bytes = synthetic.bytes;
        WriteU32Be(bytes,
                   synthetic.block_size_offsets[0],
                   static_cast<std::uint32_t>(retained_size));
        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payload_offsets[0] + retained_size),
                    bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payload_offsets[0] + kMaterialSize));
        return ParseNifDocument(bytes);
    };

    bool inherited_truncations_rejected = true;
    for (const auto retained_size : std::array<std::size_t, 6>{ 0, 3, 4, 7, 8, 11 })
        inherited_truncations_rejected =
            inherited_truncations_rejected && !parse_with_first_size(retained_size);
    Expect(inherited_truncations_rejected,
           "truncated inherited NiObjectNET fields are rejected within the material block");

    bool derived_boundaries_rejected = true;
    for (const auto derived_size : std::array<std::size_t, 7>{ 0, 12, 24, 36, 48, 52, 55 })
        derived_boundaries_rejected =
            derived_boundaries_rejected &&
            !parse_with_first_size(kObjectNetSize + derived_size);
    Expect(derived_boundaries_rejected,
           "truncation at every material field boundary and within alpha is rejected");

    auto invalid_name = synthetic.bytes;
    WriteU32Be(invalid_name, synthetic.payload_offsets[0], 2);
    const auto invalid_name_result = ParseNifDocument(invalid_name);
    Expect(!invalid_name_result &&
               invalid_name_result.error() == NifDocumentError::invalid_layout,
           "out-of-range inherited material name index is rejected");

    auto invalid_controller = synthetic.bytes;
    WriteU32Be(invalid_controller, synthetic.payload_offsets[0] + 8, 2);
    const auto invalid_controller_result = ParseNifDocument(invalid_controller);
    Expect(!invalid_controller_result &&
               invalid_controller_result.error() == NifDocumentError::invalid_layout,
           "out-of-range inherited material controller reference is rejected");

    auto oversized_extra_data = synthetic.bytes;
    WriteU32Be(oversized_extra_data, synthetic.payload_offsets[0] + 4, 0xFFFFFFFF);
    const auto oversized_extra_data_result = ParseNifDocument(oversized_extra_data);
    Expect(!oversized_extra_data_result &&
               oversized_extra_data_result.error() == NifDocumentError::invalid_layout,
           "oversized inherited material extra-data count is rejected before allocation");

    auto trailing_payload = synthetic.bytes;
    trailing_payload.insert(
        trailing_payload.begin() +
            static_cast<std::ptrdiff_t>(synthetic.payload_offsets[0] + kMaterialSize),
        std::byte{ 0xA5 });
    WriteU32Be(trailing_payload,
               synthetic.block_size_offsets[0],
               static_cast<std::uint32_t>(kMaterialSize + 1));
    const auto trailing_payload_result = ParseNifDocument(trailing_payload);
    Expect(!trailing_payload_result &&
               trailing_payload_result.error() == NifDocumentError::invalid_layout,
           "extra NiMaterialProperty payload bytes are rejected by exact consumption");

    const auto model             = MakeSyntheticModelNif();
    const auto material_positive = ParseNifDocument(model.bytes);
    Expect(material_positive &&
               material_positive->tri_shapes[0].object.properties ==
                   std::vector<std::uint32_t>{ 3 } &&
               material_positive->material_properties.size() == 1 &&
               material_positive->material_properties[0].block_index == 3,
           "material retention does not alter the shape property reference");
}

void TestNifTextureSourceInventory()
{
    using namespace rerevved::studio;

    const auto synthetic = MakeSyntheticTextureNif();
    Expect(synthetic.bytes == MakeSyntheticTextureNif().bytes,
           "synthetic texture-source fixture generation is deterministic");
    const auto parsed = ParseNifDocument(synthetic.bytes);
    Expect(parsed && parsed->texturing_properties.size() == 2 &&
               parsed->source_textures.size() == 2,
           "multiple texturing properties and source textures retain source order");
    if (parsed && parsed->texturing_properties.size() == 2 &&
        parsed->source_textures.size() == 2)
    {
        const auto&                 first  = parsed->texturing_properties[0];
        const auto&                 second = parsed->texturing_properties[1];
        std::array<std::uint8_t, 9> first_presence{};
        std::array<std::uint8_t, 9> second_presence{};
        for (std::size_t index = 0; index < first.standard_slots.size(); ++index)
        {
            first_presence[index]  = first.standard_slots[index].presence;
            second_presence[index] = second.standard_slots[index].presence;
        }
        Expect(first.block_index == 0 && second.block_index == 1 &&
                   first.flags == 0xA5F3 && second.flags == 0x55AA &&
                   first.texture_count == 9 && second.texture_count == 9,
               "texturing property identity, raw flags, and count retain exact values");
        Expect(first_presence == std::array<std::uint8_t, 9>{ 1, 2, 0, 0x7F, 1, 1, 0, 1, 0 } &&
                   second_presence ==
                       std::array<std::uint8_t, 9>{ 0, 2, 3, 4, 5, 6, 7, 8, 9 },
               "all nine standard slot presence bytes retain fixed source order");
        Expect(!first.standard_slots[1].descriptor &&
                   !first.standard_slots[3].descriptor &&
                   std::ranges::none_of(second.standard_slots,
                                        [](const auto& slot)
                                        {
                                            return slot.descriptor.has_value();
                                        }),
               "standard slot bytes other than exact one consume no descriptor body");

        const auto& base = first.standard_slots[0];
        Expect(base.descriptor && base.descriptor->source == 2 &&
                   base.descriptor->flags == 0xABCD &&
                   base.descriptor->has_texture_transform == 1 &&
                   base.descriptor->transform,
               "Base slot retains its source, raw flags, and exact transform presence");
        if (base.descriptor && base.descriptor->transform)
        {
            const auto&      transform = *base.descriptor->transform;
            const std::array retained_bits{
                std::bit_cast<std::uint32_t>(transform.translation[0]),
                std::bit_cast<std::uint32_t>(transform.translation[1]),
                std::bit_cast<std::uint32_t>(transform.scale[0]),
                std::bit_cast<std::uint32_t>(transform.scale[1]),
                std::bit_cast<std::uint32_t>(transform.rotation),
                transform.transform_method,
                std::bit_cast<std::uint32_t>(transform.center[0]),
                std::bit_cast<std::uint32_t>(transform.center[1]),
            };
            Expect(retained_bits ==
                       std::array<std::uint32_t, 8>{ 0x00000000,
                                                     0x80000000,
                                                     0x7F800000,
                                                     0xFF800000,
                                                     0x7FC12345,
                                                     0xA5A5A5A5,
                                                     0x3F000000,
                                                     0xBF000000 },
                   "texture transform fields retain exact component order and f32 bits");
        }

        const auto& glow = first.standard_slots[4];
        Expect(glow.descriptor && glow.descriptor->source == 3 &&
                   glow.descriptor->has_texture_transform == 2 &&
                   !glow.descriptor->transform,
               "transform bytes other than exact one retain no transform body");

        const auto& bump = first.standard_slots[5];
        Expect(bump.descriptor && bump.bump && !bump.parallax_offset,
               "present Bump retains its descriptor and unconditional extension only");
        if (bump.bump)
        {
            const std::array retained_bits{
                std::bit_cast<std::uint32_t>(bump.bump->luma_scale),
                std::bit_cast<std::uint32_t>(bump.bump->luma_offset),
                std::bit_cast<std::uint32_t>(bump.bump->bump_matrix[0]),
                std::bit_cast<std::uint32_t>(bump.bump->bump_matrix[1]),
                std::bit_cast<std::uint32_t>(bump.bump->bump_matrix[2]),
                std::bit_cast<std::uint32_t>(bump.bump->bump_matrix[3]),
            };
            Expect(retained_bits ==
                       std::array<std::uint32_t, 6>{ 0x80000000,
                                                     0x7F800000,
                                                     0xFF800000,
                                                     0x7FC54321,
                                                     0x00000001,
                                                     0xBF800000 },
                   "Bump luma and matrix values retain exact order and f32 bits");
        }

        const auto& parallax = first.standard_slots[7];
        Expect(parallax.descriptor && parallax.descriptor->source == UINT32_MAX &&
                   parallax.parallax_offset &&
                   std::bit_cast<std::uint32_t>(*parallax.parallax_offset) == 0x7FC0BEEF,
               "Parallax retains a null source and exact unconditional offset bits");

        Expect(first.shader_texture_count == 3 && first.shader_textures.size() == 3 &&
                   second.shader_texture_count == 0 && second.shader_textures.empty(),
               "shader counts control exact ordered retained record sequences");
        if (first.shader_textures.size() == 3)
        {
            Expect(first.shader_textures[0].has_map == 1 &&
                       first.shader_textures[0].descriptor &&
                       first.shader_textures[0].map_id == 0xDEADBEEF &&
                       first.shader_textures[1].has_map == 2 &&
                       !first.shader_textures[1].descriptor &&
                       !first.shader_textures[1].map_id &&
                       first.shader_textures[2].has_map == 1 &&
                       first.shader_textures[2].descriptor &&
                       first.shader_textures[2].map_id == 0xCAFEBABE,
                   "shader presence, optional descriptors, and raw Map IDs retain source order");
            const auto& shader_transform = first.shader_textures[2].descriptor->transform;
            if (shader_transform)
            {
                const std::array retained_bits{
                    std::bit_cast<std::uint32_t>(shader_transform->translation[0]),
                    std::bit_cast<std::uint32_t>(shader_transform->translation[1]),
                    std::bit_cast<std::uint32_t>(shader_transform->scale[0]),
                    std::bit_cast<std::uint32_t>(shader_transform->scale[1]),
                    std::bit_cast<std::uint32_t>(shader_transform->rotation),
                    shader_transform->transform_method,
                    std::bit_cast<std::uint32_t>(shader_transform->center[0]),
                    std::bit_cast<std::uint32_t>(shader_transform->center[1]),
                };
                Expect(retained_bits ==
                           std::array<std::uint32_t, 8>{ 0x3F800000,
                                                         0x40000000,
                                                         0x40400000,
                                                         0x40800000,
                                                         0x40A00000,
                                                         0x12345678,
                                                         0x40C00000,
                                                         0x40E00000 },
                       "a shader descriptor retains an independent ordered transform");
            }
        }

        const auto& external = parsed->source_textures[0];
        const auto& opaque   = parsed->source_textures[1];
        Expect(external.block_index == 2 && external.use_external == 1 &&
                   external.file_name_index == 0 && external.pixel_data == UINT32_MAX &&
                   external.pixel_layout == 0x11111110 &&
                   external.use_mipmaps == 0x11111111 &&
                   external.alpha_format == 0x11111112 && external.is_static == 2 &&
                   external.direct_render == 3 && external.persist_render_data == 4,
               "external source retains its exact 24-byte ordered payload");
        Expect(external.IsSupportedExternalSource(),
               "only the exact validated external carrier combination is supported");
        Expect(opaque.block_index == 3 && opaque.use_external == 0 &&
                   opaque.file_name_index == UINT32_MAX && opaque.pixel_data == 4 &&
                   opaque.pixel_layout == 0x22222220 &&
                   opaque.use_mipmaps == 0x22222221 &&
                   opaque.alpha_format == 0x22222222 && !opaque.IsSupportedExternalSource(),
               "a structurally valid opaque pixel-data carrier remains unsupported");
        Expect(synthetic.payload_sizes[2] -
                           (synthetic.source_derived_offsets[0] - synthetic.payload_offsets[2]) ==
                       24 &&
                   synthetic.payload_sizes[3] -
                           (synthetic.source_derived_offsets[1] -
                            synthetic.payload_offsets[3]) ==
                       24,
               "each synthetic NiSourceTexture derived payload is exactly 24 bytes");
    }

    const auto parse_truncated_block = [&](std::size_t block_index, std::size_t retained_size)
    {
        auto bytes = synthetic.bytes;
        WriteU32Be(bytes,
                   synthetic.block_size_offsets[block_index],
                   static_cast<std::uint32_t>(retained_size));
        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payload_offsets[block_index] + retained_size),
                    bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payload_offsets[block_index] +
                                        synthetic.payload_sizes[block_index]));
        return ParseNifDocument(bytes);
    };

    bool property_truncations_rejected = true;
    for (std::size_t retained_size = 0; retained_size < synthetic.payload_sizes[0];
         ++retained_size)
        property_truncations_rejected =
            property_truncations_rejected && !parse_truncated_block(0, retained_size);
    Expect(property_truncations_rejected,
           "every truncated inherited, fixed, and conditional texturing-property prefix is rejected");

    bool source_truncations_rejected = true;
    for (std::size_t source_index = 0; source_index < 2; ++source_index)
    {
        for (std::size_t retained_size = 0;
             retained_size < synthetic.payload_sizes[source_index + 2];
             ++retained_size)
            source_truncations_rejected =
                source_truncations_rejected &&
                !parse_truncated_block(source_index + 2, retained_size);
    }
    Expect(source_truncations_rejected,
           "every truncated inherited and fixed NiSourceTexture prefix is rejected");

    auto wrong_texture_count = synthetic.bytes;
    WriteU32Be(wrong_texture_count, synthetic.texture_count_offset, 8);
    Expect(!ParseNifDocument(wrong_texture_count),
           "a texturing-property Texture Count other than nine is rejected");

    auto oversized_shader_count = synthetic.bytes;
    WriteU32Be(oversized_shader_count, synthetic.shader_count_offset, UINT32_MAX);
    Expect(!ParseNifDocument(oversized_shader_count),
           "an oversized shader count is rejected before allocation");

    bool all_invalid_descriptor_sources_rejected = true;
    bool all_null_descriptor_sources_retained    = true;
    for (const auto source_offset : synthetic.descriptor_source_offsets)
    {
        auto invalid_descriptor_source = synthetic.bytes;
        WriteU32Be(invalid_descriptor_source, source_offset, 5);
        all_invalid_descriptor_sources_rejected =
            all_invalid_descriptor_sources_rejected &&
            !ParseNifDocument(invalid_descriptor_source);

        auto null_descriptor_source = synthetic.bytes;
        WriteU32Be(null_descriptor_source, source_offset, UINT32_MAX);
        all_null_descriptor_sources_retained =
            all_null_descriptor_sources_retained &&
            ParseNifDocument(null_descriptor_source).has_value();
    }
    Expect(all_invalid_descriptor_sources_rejected,
           "out-of-range standard and shader descriptor source references are rejected");
    Expect(all_null_descriptor_sources_retained,
           "null standard and shader descriptor source references are retained");

    auto invalid_source_name = synthetic.bytes;
    WriteU32Be(invalid_source_name, synthetic.source_name_offsets[0], 1);
    Expect(!ParseNifDocument(invalid_source_name),
           "an out-of-range source filename index is rejected");
    auto null_source_name = synthetic.bytes;
    WriteU32Be(null_source_name, synthetic.source_name_offsets[0], UINT32_MAX);
    const auto null_name_result = ParseNifDocument(null_source_name);
    Expect(null_name_result && !null_name_result->source_textures[0].IsSupportedExternalSource(),
           "a null source filename remains structurally valid but unsupported");

    auto invalid_pixel_reference = synthetic.bytes;
    WriteU32Be(invalid_pixel_reference, synthetic.source_pixel_offsets[1], 5);
    Expect(!ParseNifDocument(invalid_pixel_reference),
           "an out-of-range source pixel-data reference is rejected");
    auto null_pixel_reference = synthetic.bytes;
    WriteU32Be(null_pixel_reference, synthetic.source_pixel_offsets[1], UINT32_MAX);
    Expect(ParseNifDocument(null_pixel_reference).has_value(),
           "a null source pixel-data reference is retained");

    auto external_with_pixel = synthetic.bytes;
    WriteU32Be(external_with_pixel, synthetic.source_pixel_offsets[0], 4);
    const auto external_with_pixel_result = ParseNifDocument(external_with_pixel);
    Expect(external_with_pixel_result &&
               !external_with_pixel_result->source_textures[0].IsSupportedExternalSource(),
           "Use External one with a non-null Pixel Data reference remains unsupported");

    auto unsupported_external_value                                 = synthetic.bytes;
    unsupported_external_value[synthetic.source_derived_offsets[0]] = std::byte{ 2 };
    const auto unsupported_external_result                          = ParseNifDocument(unsupported_external_value);
    Expect(unsupported_external_result &&
               unsupported_external_result->source_textures[0].use_external == 2 &&
               !unsupported_external_result->source_textures[0].IsSupportedExternalSource(),
           "a raw Use External byte other than exact one remains unsupported");

    auto invalid_property_name = synthetic.bytes;
    WriteU32Be(invalid_property_name, synthetic.first_property_name_offset, 1);
    Expect(!ParseNifDocument(invalid_property_name),
           "an invalid inherited texturing-property name index is rejected");
    auto invalid_property_controller = synthetic.bytes;
    WriteU32Be(invalid_property_controller, synthetic.first_property_controller_offset, 5);
    Expect(!ParseNifDocument(invalid_property_controller),
           "an invalid inherited texturing-property controller reference is rejected");
    auto oversized_property_extra_data = synthetic.bytes;
    WriteU32Be(oversized_property_extra_data,
               synthetic.first_property_name_offset + 4,
               UINT32_MAX);
    Expect(!ParseNifDocument(oversized_property_extra_data),
           "an oversized inherited texturing-property extra-data count is rejected");

    auto invalid_source_controller = synthetic.bytes;
    WriteU32Be(invalid_source_controller, synthetic.payload_offsets[2] + 8, 5);
    Expect(!ParseNifDocument(invalid_source_controller),
           "an invalid inherited source-texture controller reference is rejected");
    auto oversized_source_extra_data = synthetic.bytes;
    WriteU32Be(oversized_source_extra_data, synthetic.payload_offsets[2] + 4, UINT32_MAX);
    Expect(!ParseNifDocument(oversized_source_extra_data),
           "an oversized inherited source-texture extra-data count is rejected");

    auto trailing_property = synthetic.bytes;
    trailing_property.insert(
        trailing_property.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payload_offsets[0] +
                                        synthetic.payload_sizes[0]),
        std::byte{ 0xA5 });
    WriteU32Be(trailing_property,
               synthetic.block_size_offsets[0],
               static_cast<std::uint32_t>(synthetic.payload_sizes[0] + 1));
    Expect(!ParseNifDocument(trailing_property),
           "extra texturing-property payload bytes are rejected by exact consumption");

    auto trailing_source = synthetic.bytes;
    trailing_source.insert(
        trailing_source.begin() + static_cast<std::ptrdiff_t>(
                                      synthetic.payload_offsets[2] +
                                      synthetic.payload_sizes[2]),
        std::byte{ 0x5A });
    WriteU32Be(trailing_source,
               synthetic.block_size_offsets[2],
               static_cast<std::uint32_t>(synthetic.payload_sizes[2] + 1));
    Expect(!ParseNifDocument(trailing_source),
           "extra source-texture payload bytes are rejected by exact consumption");

    const auto model = ParseNifDocument(MakeSyntheticModelNif().bytes);
    Expect(model && model->texturing_properties.empty() && model->source_textures.empty() &&
               model->material_properties.size() == 1 && model->tri_shape_data.size() == 1,
           "texture-source retention leaves existing model and material parsing unchanged");
}

void TestNifMaterialInspectorPresentation()
{
    using namespace rerevved::studio;

    const auto parsed = ParseNifDocument(MakeSyntheticMaterialNif().bytes);
    Expect(parsed.has_value(), "synthetic materials parse for Inspector presentation");
    if (!parsed)
        return;

    const auto text = FormatNifMaterialProperties(parsed->material_properties);
    Expect(text.size() == 2 && text[0].heading == "NiMaterialProperty block 0" &&
               text[1].heading == "NiMaterialProperty block 1",
           "material Inspector presents every property in source block order");
    if (text.size() == 2)
    {
        Expect(text[0].fields ==
                   std::array<std::string, 6>{
                       "Ambient color: 1.25, -2.5, 3.75",
                       "Diffuse color: -4.5, 5.25, -6.75",
                       "Specular color: 0, -0, NaN",
                       "Emissive color: +infinity, -infinity, 1.40129846e-45",
                       "Glossiness: 7.5",
                       "Alpha: -8.25",
                   },
               "material Inspector preserves exact field, component, and special-value order");
        Expect(text[1].fields[0] == "Ambient color: 10, 11, 12" &&
                   text[1].fields[5] == "Alpha: 23",
               "a second material remains clearly independent and ordered");
    }

    for (const auto bits : std::array<std::uint32_t, 4>{
             0x3EAAAAAB, 0x3F800001, 0x00800001, 0x7F7FFFFF })
    {
        const auto value      = std::bit_cast<float>(bits);
        const auto value_text = FormatNifInspectorFloat(value);
        float      round_trip{};
        const auto result = std::from_chars(value_text.data(),
                                            value_text.data() + value_text.size(),
                                            round_trip,
                                            std::chars_format::general);
        Expect(result.ec == std::errc{} && result.ptr == value_text.data() + value_text.size() &&
                   std::bit_cast<std::uint32_t>(round_trip) == bits,
               "finite material text round-trips to the exact retained f32 bits");
    }
    Expect(FormatNifInspectorFloat(0.0F) == "0" &&
               FormatNifInspectorFloat(-0.0F) == "-0" &&
               FormatNifInspectorFloat(std::numeric_limits<float>::infinity()) == "+infinity" &&
               FormatNifInspectorFloat(-std::numeric_limits<float>::infinity()) == "-infinity" &&
               FormatNifInspectorFloat(std::numeric_limits<float>::quiet_NaN()) == "NaN",
           "material Inspector distinguishes signed zero and names infinities and NaN explicitly");

    const NifDocument empty;
    Expect(FormatNifMaterialProperties(empty.material_properties).empty(),
           "empty material inventory produces no property presentation sections");

    FpkEntryDocument embedded{ 3, FpkEntryFormat::nif, *parsed };
    const auto*      embedded_nif         = std::get_if<NifDocument>(&embedded.data);
    const auto       embedded_text        = embedded_nif
                                                ? FormatNifMaterialProperties(embedded_nif->material_properties)
                                                : std::vector<NifMaterialPropertyText>{};
    bool             presentation_matches = embedded_text.size() == text.size();
    for (std::size_t index = 0; presentation_matches && index < text.size(); ++index)
        presentation_matches = embedded_text[index].heading == text[index].heading &&
                               embedded_text[index].fields == text[index].fields;
    Expect(embedded_nif && presentation_matches,
           "direct and explicitly opened archive NIFs share identical material presentation");
}

void TestNifTextureSourceInspectorPresentation()
{
    using namespace rerevved::studio;

    const auto parsed = ParseNifDocument(MakeSyntheticTextureNif().bytes);
    Expect(parsed.has_value(), "synthetic texture sources parse for Inspector presentation");
    if (!parsed)
        return;

    const auto properties = FormatNifTexturingProperties(parsed->texturing_properties);
    Expect(properties.size() == 2 &&
               properties[0].heading == "NiTexturingProperty block 0" &&
               properties[1].heading == "NiTexturingProperty block 1",
           "texturing properties retain source block order in Inspector presentation");
    if (properties.size() == 2)
    {
        const std::vector<std::string> expected_first{
            "Flags: 0xA5F3",
            "Texture Count: 9",
            "Base presence: 1 (0x01)",
            "Base descriptor: present",
            "Base Source block reference: 2 (0x00000002)",
            "Base descriptor Flags: 0xABCD",
            "Base Has Texture Transform: 1 (0x01)",
            "Base Texture Transform: present",
            "Base Translation X/Y: 0, -0",
            "Base Scale X/Y: +infinity, -infinity",
            "Base Rotation: NaN",
            "Base Transform Method: 2779096485 (0xA5A5A5A5)",
            "Base Center X/Y: 0.5, -0.5",
            "Dark presence: 2 (0x02)",
            "Dark descriptor: absent",
            "Detail presence: 0 (0x00)",
            "Detail descriptor: absent",
            "Gloss presence: 127 (0x7F)",
            "Gloss descriptor: absent",
            "Glow presence: 1 (0x01)",
            "Glow descriptor: present",
            "Glow Source block reference: 3 (0x00000003)",
            "Glow descriptor Flags: 0x1020",
            "Glow Has Texture Transform: 2 (0x02)",
            "Glow Texture Transform: absent",
            "Bump presence: 1 (0x01)",
            "Bump descriptor: present",
            "Bump Source block reference: 2 (0x00000002)",
            "Bump descriptor Flags: 0x3040",
            "Bump Has Texture Transform: 0 (0x00)",
            "Bump Texture Transform: absent",
            "Bump Luma Scale: -0",
            "Bump Luma Offset: +infinity",
            "Bump Matrix: -infinity, NaN, 1.40129846e-45, -1",
            "Normal presence: 0 (0x00)",
            "Normal descriptor: absent",
            "Parallax presence: 1 (0x01)",
            "Parallax descriptor: present",
            "Parallax Source block reference: none (0xFFFFFFFF)",
            "Parallax descriptor Flags: 0x5060",
            "Parallax Has Texture Transform: 0 (0x00)",
            "Parallax Texture Transform: absent",
            "Parallax Offset: NaN",
            "Decal 0 presence: 0 (0x00)",
            "Decal 0 descriptor: absent",
            "Shader Texture Count: 3",
            "Shader 1 Has Map: 1 (0x01)",
            "Shader 1 descriptor: present",
            "Shader 1 Source block reference: 2 (0x00000002)",
            "Shader 1 descriptor Flags: 0x7080",
            "Shader 1 Has Texture Transform: 0 (0x00)",
            "Shader 1 Texture Transform: absent",
            "Shader 1 Map ID: 3735928559 (0xDEADBEEF)",
            "Shader 2 Has Map: 2 (0x02)",
            "Shader 2 descriptor: absent",
            "Shader 3 Has Map: 1 (0x01)",
            "Shader 3 descriptor: present",
            "Shader 3 Source block reference: 3 (0x00000003)",
            "Shader 3 descriptor Flags: 0x90A0",
            "Shader 3 Has Texture Transform: 1 (0x01)",
            "Shader 3 Texture Transform: present",
            "Shader 3 Translation X/Y: 1, 2",
            "Shader 3 Scale X/Y: 3, 4",
            "Shader 3 Rotation: 5",
            "Shader 3 Transform Method: 305419896 (0x12345678)",
            "Shader 3 Center X/Y: 6, 7",
            "Shader 3 Map ID: 3405691582 (0xCAFEBABE)",
        };
        Expect(properties[0].fields == expected_first,
               "texturing Inspector preserves exact slot, descriptor, extension, and shader order");
        Expect(properties[1].fields.size() == 21 &&
                   properties[1].fields[0] == "Flags: 0x55AA" &&
                   properties[1].fields[2] == "Base presence: 0 (0x00)" &&
                   properties[1].fields[4] == "Dark presence: 2 (0x02)" &&
                   properties[1].fields[18] == "Decal 0 presence: 9 (0x09)" &&
                   properties[1].fields[20] == "Shader Texture Count: 0",
               "absent descriptors retain raw presence bytes without presentation bodies");
    }

    const auto has_field = [](const std::vector<std::string>& fields, std::string_view value)
    {
        return std::ranges::find(fields, value) != fields.end();
    };
    const auto has_field_prefix = [](const std::vector<std::string>& fields,
                                     std::string_view                prefix)
    {
        return std::ranges::any_of(fields,
                                   [&](const auto& field)
                                   {
                                       return field.starts_with(prefix);
                                   });
    };

    auto non_selected_slot                                               = *parsed;
    non_selected_slot.texturing_properties[0].standard_slots[0].presence = 2;
    const auto non_selected_slot_text =
        FormatNifTexturingProperties(non_selected_slot.texturing_properties);
    Expect(has_field(non_selected_slot_text[0].fields, "Base descriptor: absent") &&
               !has_field_prefix(non_selected_slot_text[0].fields,
                                 "Base Source block reference"),
           "a descriptor body is presented only when its raw slot presence equals one");

    auto non_selected_transform = *parsed;
    non_selected_transform.texturing_properties[0]
        .standard_slots[0]
        .descriptor->has_texture_transform = 2;
    const auto non_selected_transform_text =
        FormatNifTexturingProperties(non_selected_transform.texturing_properties);
    Expect(has_field(non_selected_transform_text[0].fields,
                     "Base Texture Transform: absent") &&
               !has_field_prefix(non_selected_transform_text[0].fields,
                                 "Base Translation X/Y"),
           "transform values are presented only when the raw transform byte equals one");

    auto non_selected_shader                                               = *parsed;
    non_selected_shader.texturing_properties[0].shader_textures[0].has_map = 2;
    const auto non_selected_shader_text =
        FormatNifTexturingProperties(non_selected_shader.texturing_properties);
    Expect(has_field(non_selected_shader_text[0].fields, "Shader 1 descriptor: absent") &&
               !has_field_prefix(non_selected_shader_text[0].fields,
                                 "Shader 1 Source block reference") &&
               !has_field_prefix(non_selected_shader_text[0].fields, "Shader 1 Map ID"),
           "shader descriptor and Map ID presentation require raw Has Map equal to one");

    auto unresolved_source                                                         = *parsed;
    unresolved_source.texturing_properties[0].standard_slots[0].descriptor->source = 77;
    unresolved_source.source_textures.clear();
    const auto unresolved_source_text =
        FormatNifTexturingProperties(unresolved_source.texturing_properties);
    Expect(has_field(unresolved_source_text[0].fields,
                     "Base Source block reference: 77 (0x0000004D)"),
           "descriptor source references remain visible without a retained source inventory");

    const auto sources = FormatNifSourceTextures(*parsed);
    Expect(sources.size() == 2 && sources[0].heading == "NiSourceTexture block 2" &&
               sources[1].heading == "NiSourceTexture block 3",
           "source textures retain source block order in Inspector presentation");
    if (sources.size() == 2)
    {
        Expect(sources[0].fields ==
                   std::vector<std::string>{
                       "Use External: 1 (0x01)",
                       "File Name string index: 0 (0x00000000)",
                       "File Name bytes: SyntheticTexture.dds",
                       "Pixel Data block reference: none (0xFFFFFFFF)",
                       "Pixel Layout: 286331152 (0x11111110)",
                       "Use Mipmaps: 286331153 (0x11111111)",
                       "Alpha Format: 286331154 (0x11111112)",
                       "Is Static: 2 (0x02)",
                       "Direct Render: 3 (0x03)",
                       "Persist Render Data: 4 (0x04)",
                       "Supported external source",
                   },
               "supported external source presentation retains exact raw fields and metadata bytes");
        Expect(sources[1].fields ==
                   std::vector<std::string>{
                       "Use External: 0 (0x00)",
                       "File Name string index: none (0xFFFFFFFF)",
                       "Pixel Data block reference: 4 (0x00000004)",
                       "Pixel Layout: 572662304 (0x22222220)",
                       "Use Mipmaps: 572662305 (0x22222221)",
                       "Alpha Format: 572662306 (0x22222222)",
                       "Is Static: 2 (0x02)",
                       "Direct Render: 3 (0x03)",
                       "Persist Render Data: 4 (0x04)",
                       "Unsupported source combination",
                   },
               "unsupported source presentation keeps its opaque block reference visible");
    }

    const std::array printable{ std::byte{ 'A' }, std::byte{ 'z' }, std::byte{ ' ' } };
    const std::array with_nul{ std::byte{ 'A' }, std::byte{ 0 }, std::byte{ 'B' } };
    const std::array non_printable{ std::byte{ 0xFF }, std::byte{ '\\' }, std::byte{ 0x1F } };
    Expect(FormatNifStringBytes(printable) == "Az " && FormatNifStringBytes({}).empty() &&
               FormatNifStringBytes(with_nul) == "A\\x00B" &&
               FormatNifStringBytes(non_printable) == "\\xFF\\\\\\x1F",
           "retained string bytes use safe printable, empty, NUL, slash, and hex escaping");

    NifDocument escaped_document;
    escaped_document.strings = {
        {},
        { std::byte{ 'A' }, std::byte{ 0 }, std::byte{ 'B' } },
        { std::byte{ 0xFF }, std::byte{ '\\' } },
    };
    for (std::uint32_t index = 0; index < escaped_document.strings.size(); ++index)
    {
        NifSourceTextureInventory source{};
        source.block_index     = index;
        source.use_external    = 1;
        source.file_name_index = index;
        escaped_document.source_textures.push_back(source);
    }
    const auto escaped_sources = FormatNifSourceTextures(escaped_document);
    Expect(escaped_sources.size() == 3 &&
               escaped_sources[0].fields[2] == "File Name bytes: <empty>" &&
               escaped_sources[1].fields[2] == "File Name bytes: A\\x00B" &&
               escaped_sources[2].fields[2] == "File Name bytes: \\xFF\\\\" &&
               escaped_sources[0].fields.back() == "Supported external source",
           "source metadata displays empty, NUL-containing, and non-printable bytes without path interpretation");

    for (const auto mutation : std::array{ 0, 1, 2 })
    {
        auto unsupported = *parsed;
        if (mutation == 0)
            unsupported.source_textures[0].use_external = 0;
        else if (mutation == 1)
            unsupported.source_textures[0].file_name_index = UINT32_MAX;
        else
            unsupported.source_textures[0].pixel_data = 4;
        const auto unsupported_text = FormatNifSourceTextures(unsupported);
        Expect(!unsupported_text.empty() &&
                   unsupported_text[0].fields.back() == "Unsupported source combination",
               "each unsupported source-carrier combination receives the neutral classification");
    }

    auto invalid_name                               = *parsed;
    invalid_name.source_textures[0].file_name_index = 99;
    const auto invalid_name_text                    = FormatNifSourceTextures(invalid_name);
    Expect(!has_field_prefix(invalid_name_text[0].fields, "File Name bytes:") &&
               invalid_name_text[0].fields.back() == "Unsupported source combination",
           "an out-of-range retained filename index is not dereferenced or classified external");

    const NifDocument empty;
    Expect(FormatNifTexturingProperties(empty.texturing_properties).empty() &&
               FormatNifSourceTextures(empty).empty(),
           "empty texturing and source inventories produce neutral empty presentation inputs");

    FpkEntryDocument embedded{ 7, FpkEntryFormat::nif, *parsed };
    const auto*      embedded_nif        = std::get_if<NifDocument>(&embedded.data);
    const auto       embedded_properties = embedded_nif
                                               ? FormatNifTexturingProperties(
                                                     embedded_nif->texturing_properties)
                                               : std::vector<NifTexturingPropertyText>{};
    const auto       embedded_sources    = embedded_nif ? FormatNifSourceTextures(*embedded_nif)
                                                        : std::vector<NifSourceTextureText>{};
    const auto       same_text           = [](const auto& left, const auto& right)
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (left[index].heading != right[index].heading ||
                left[index].fields != right[index].fields)
                return false;
        }
        return true;
    };
    Expect(embedded_nif && same_text(properties, embedded_properties) &&
               same_text(sources, embedded_sources),
           "direct and explicitly opened archive NIFs share identical texture-source presentation");
}

void TestNifModelAssembly()
{
    using namespace rerevved::studio;

    Expect(NifModelErrorMessage(NifModelError::scene_cycle) ==
               "The NIF scene graph contains a cycle.",
           "NIF model cycle message");
    Expect(NifModelErrorMessage(NifModelError::wrong_data_block_type) ==
               "A NiTriShape data reference does not target NiTriShapeData.",
           "NIF model wrong-data-type message");
    Expect(NifModelErrorMessage(NifModelError::inconsistent_geometry) ==
               "The retained NIF geometry arrays are inconsistent.",
           "NIF model inconsistent-geometry message");
    Expect(NifModelErrorMessage(NifModelError::triangle_selector_out_of_range) ==
               "A NIF triangle selector is outside the retained vertex positions.",
           "NIF model triangle-selector message");
    Expect(NifModelErrorMessage(NifModelError::invalid_transform) ==
               "A NIF scene transform is non-finite or unrepresentable.",
           "NIF model transform message");
    Expect(NifModelErrorMessage(NifModelError::no_supported_meshes) ==
               "The NIF scene contains no supported meshes.",
           "NIF model no-mesh message");

    const auto synthetic = MakeSyntheticModelNif();
    const auto parsed    = ParseNifDocument(synthetic.bytes);
    Expect(parsed.has_value(), "synthetic model parses for model assembly");
    if (!parsed)
        return;

    const auto* positions_before = parsed->tri_shape_data[0].vertex_positions.data();
    const auto* normals_before   = parsed->tri_shape_data[0].normal_vectors.data();
    const auto* triangles_before = parsed->tri_shape_data[0].triangles.data();
    const auto  model            = AssembleNifModel(*parsed);
    Expect(model && model->meshes.size() == 1,
           "synthetic model assembles exactly one supported mesh");
    if (model && model->meshes.size() == 1)
    {
        const auto& mesh = model->meshes[0];
        Expect(mesh.root_index == 0 &&
                   mesh.node_path == std::vector<std::size_t>{ 0 } &&
                   mesh.tri_shape_index == 0 && mesh.tri_shape_data_index == 0 &&
                   mesh.material_property_blocks == std::vector<std::uint32_t>{ 3 },
               "assembled mesh resolves its root, node path, shape, data, and material");
        const auto& geometry = parsed->tri_shape_data[mesh.tri_shape_data_index];
        Expect(geometry.vertex_positions.size() == 3 && geometry.normal_vectors.size() == 3 &&
                   geometry.triangles ==
                       std::vector<std::array<std::uint16_t, 3>>{ { 0, 1, 2 } },
               "assembled mesh indices resolve the retained synthetic geometry");
        Expect(geometry.vertex_positions.data() == positions_before &&
                   geometry.normal_vectors.data() == normals_before &&
                   geometry.triangles.data() == triangles_before,
               "model assembly leaves document-owned geometry storage unchanged");
        const auto transformed = ApplyNifModelTransform(
            mesh.world_transform, geometry.vertex_positions[1]);
        Expect(transformed && *transformed == std::array{ 1.0, 0.0, 0.0 },
               "identity node and shape transforms preserve a retained position");
    }

    constexpr std::array rotate_z_positive{
        0.0F,
        1.0F,
        0.0F,
        -1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
    };
    auto shape_local                             = *parsed;
    shape_local.tri_shapes[0].object.translation = { 10.0F, 20.0F, 30.0F };
    shape_local.tri_shapes[0].object.rotation    = rotate_z_positive;
    shape_local.tri_shapes[0].object.scale       = 2.0F;
    const auto normals_before_transform          = shape_local.tri_shape_data[0].normal_vectors;
    const auto shape_local_model                 = AssembleNifModel(shape_local);
    const auto shape_local_position              = shape_local_model
                                                       ? ApplyNifModelTransform(
                                                             shape_local_model->meshes[0].world_transform,
                                                             { 1.0F, 0.0F, 0.0F })
                                                       : std::expected<std::array<double, 3>, NifModelError>(
                                                             std::unexpected(
                                                                 NifModelError::invalid_transform));
    Expect(shape_local_position &&
               *shape_local_position == std::array{ 10.0, 22.0, 30.0 } &&
               shape_local.tri_shape_data[0].normal_vectors == normals_before_transform,
           "shape-local translation, rotation, and scale affect positions without changing normals");

    auto parentless             = shape_local;
    parentless.roots            = { 1 };
    const auto parentless_model = AssembleNifModel(parentless);
    Expect(parentless_model && parentless_model->meshes.size() == 1 &&
               parentless_model->meshes[0].node_path.empty() &&
               ApplyNifModelTransform(parentless_model->meshes[0].world_transform,
                                      { 1.0F, 0.0F, 0.0F }) ==
                   std::expected<std::array<double, 3>, NifModelError>(
                       std::array{ 10.0, 22.0, 30.0 }),
           "a parentless rooted shape uses only its retained local transform");

    auto nested = *parsed;
    nested.blocks.push_back(NifBlock{ .type_index = 0 });
    nested.blocks.push_back(NifBlock{ .type_index = 1 });
    nested.blocks.push_back(NifBlock{ .type_index = 2 });
    auto nested_node        = nested.nodes[0];
    nested_node.block_index = 4;
    nested_node.children    = { 5 };
    nested.nodes.push_back(nested_node);
    auto nested_shape        = nested.tri_shapes[0];
    nested_shape.block_index = 5;
    nested_shape.data        = 6;
    nested.tri_shapes.push_back(nested_shape);
    auto nested_data        = nested.tri_shape_data[0];
    nested_data.block_index = 6;
    nested.tri_shape_data.push_back(nested_data);
    nested.nodes[0].children                = { 4, 1 };
    nested.nodes[0].object.translation      = { 10.0F, 0.0F, 0.0F };
    nested.nodes[0].object.scale            = 5.0F;
    nested.nodes[1].object.translation      = { 0.0F, 3.0F, 0.0F };
    nested.nodes[1].object.rotation         = rotate_z_positive;
    nested.nodes[1].object.scale            = 4.0F;
    nested.tri_shapes[1].object.translation = { 1.0F, 0.0F, 0.0F };
    nested.tri_shapes[1].object.scale       = 2.0F;
    const auto nested_model                 = AssembleNifModel(nested);
    Expect(nested_model && nested_model->meshes.size() == 2 &&
               nested_model->meshes[0].tri_shape_index == 1 &&
               nested_model->meshes[0].node_path == std::vector<std::size_t>{ 0, 1 } &&
               nested_model->meshes[1].tri_shape_index == 0 &&
               nested_model->meshes[1].node_path == std::vector<std::size_t>{ 0 },
           "nested nodes preserve source-order mesh discovery and node paths");
    if (nested_model && nested_model->meshes.size() == 2)
    {
        const auto& nested_transform = nested_model->meshes[0].world_transform;
        const auto  nested_position =
            ApplyNifModelTransform(nested_transform, { 1.0F, 0.0F, 0.0F });
        Expect(nested_transform.translation == std::array{ 10.0, 35.0, 0.0 } &&
                   nested_transform.rotation ==
                       std::array{ 0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0 } &&
                   nested_transform.scale == 40.0 && nested_position &&
                   *nested_position == std::array{ 10.0, 75.0, 0.0 },
               "nested transforms use proved parent-local scale, rotation, and translation order");

        std::vector<NifPreviewMeshView> nested_views;
        for (const auto& mesh : nested_model->meshes)
        {
            const auto& geometry = nested.tri_shape_data[mesh.tri_shape_data_index];
            nested_views.push_back(
                { geometry.vertex_positions, geometry.triangles, mesh.world_transform });
        }
        const auto nested_layout =
            CalculateNifWireframeLayout(nested_views, 240.0F, 160.0F);
        Expect(nested_layout && nested_layout->projection == NifProjection::xy &&
                   nested_layout->source_center == std::array{ -7.5, 37.5 },
               "sibling and nested meshes share one transformed Preview space");
    }

    auto null_child              = *parsed;
    null_child.nodes[0].children = { UINT32_MAX, 1 };
    const auto null_child_model  = AssembleNifModel(null_child);
    Expect(null_child_model && null_child_model->meshes.size() == 1,
           "null scene child references are ignored");

    auto material_order = *parsed;
    material_order.blocks.push_back(NifBlock{ .type_index = 3 });
    material_order.tri_shapes[0].object.properties = { 4, UINT32_MAX, 3 };
    const auto material_order_model                = AssembleNifModel(material_order);
    Expect(material_order_model &&
               material_order_model->meshes[0].material_property_blocks ==
                   std::vector<std::uint32_t>{ 4, 3 },
           "direct material properties preserve source order while nulls are ignored");

    auto self_cycle              = *parsed;
    self_cycle.nodes[0].children = { 0 };
    const auto self_cycle_result = AssembleNifModel(self_cycle);
    Expect(!self_cycle_result && self_cycle_result.error() == NifModelError::scene_cycle,
           "self-referencing scene node reports the exact cycle error");

    auto multi_cycle = *parsed;
    multi_cycle.blocks.push_back(NifBlock{ .type_index = 0 });
    auto second_node        = multi_cycle.nodes[0];
    second_node.block_index = 4;
    second_node.children    = { 0 };
    multi_cycle.nodes.push_back(second_node);
    multi_cycle.nodes[0].children = { 4 };
    const auto multi_cycle_result = AssembleNifModel(multi_cycle);
    Expect(!multi_cycle_result && multi_cycle_result.error() == NifModelError::scene_cycle,
           "multi-node scene cycle terminates with the exact cycle error");

    auto wrong_data               = *parsed;
    wrong_data.tri_shapes[0].data = 3;
    const auto wrong_data_result  = AssembleNifModel(wrong_data);
    Expect(!wrong_data_result &&
               wrong_data_result.error() == NifModelError::wrong_data_block_type,
           "valid wrong-type shape data reference fails model assembly");

    auto invalid_selector = synthetic.bytes;
    WriteU16Be(invalid_selector, synthetic.triangle_indices_offset + 4, 3);
    const auto invalid_selector_document = ParseNifDocument(invalid_selector);
    Expect(invalid_selector_document.has_value(),
           "out-of-range triangle selector remains structurally parseable");
    if (invalid_selector_document)
    {
        const auto invalid_selector_model = AssembleNifModel(*invalid_selector_document);
        Expect(!invalid_selector_model &&
                   invalid_selector_model.error() ==
                       NifModelError::triangle_selector_out_of_range,
               "out-of-range triangle selector fails at model assembly");
    }

    auto missing_positions = *parsed;
    missing_positions.tri_shape_data[0].vertex_positions.clear();
    const auto missing_positions_result = AssembleNifModel(missing_positions);
    Expect(!missing_positions_result &&
               missing_positions_result.error() == NifModelError::inconsistent_geometry,
           "missing retained positions fail model assembly");

    auto missing_triangles = *parsed;
    missing_triangles.tri_shape_data[0].triangles.clear();
    const auto missing_triangles_result = AssembleNifModel(missing_triangles);
    Expect(!missing_triangles_result &&
               missing_triangles_result.error() == NifModelError::inconsistent_geometry,
           "missing retained triangles fail model assembly");

    auto mismatched_normals = *parsed;
    mismatched_normals.tri_shape_data[0].normal_vectors.pop_back();
    const auto mismatched_normals_result = AssembleNifModel(mismatched_normals);
    Expect(!mismatched_normals_result &&
               mismatched_normals_result.error() == NifModelError::inconsistent_geometry,
           "mismatched retained normal count fails model assembly");

    auto no_normals                          = *parsed;
    no_normals.tri_shape_data[0].has_normals = 0;
    no_normals.tri_shape_data[0].normal_vectors.clear();
    const auto no_normals_result = AssembleNifModel(no_normals);
    Expect(no_normals_result && no_normals_result->meshes.size() == 1,
           "geometry without a retained normal array remains supported");

    auto unsupported = *parsed;
    unsupported.block_types.push_back(ByteString("UnsupportedSyntheticBlock"));
    unsupported.blocks.push_back(NifBlock{ .type_index = 4 });
    unsupported.nodes[0].children = { 4, 1 };
    const auto unsupported_result = AssembleNifModel(unsupported);
    Expect(unsupported_result && unsupported_result->meshes.size() == 1,
           "valid unsupported child block types are skipped");

    auto no_mesh              = *parsed;
    no_mesh.nodes[0].children = {};
    const auto no_mesh_result = AssembleNifModel(no_mesh);
    Expect(!no_mesh_result && no_mesh_result.error() == NifModelError::no_supported_meshes,
           "rooted scene without supported geometry reports the exact no-mesh error");

    auto non_finite_transform = *parsed;
    non_finite_transform.nodes[0].object.scale =
        std::numeric_limits<float>::infinity();
    const auto non_finite_transform_result = AssembleNifModel(non_finite_transform);
    Expect(!non_finite_transform_result &&
               non_finite_transform_result.error() == NifModelError::invalid_transform,
           "non-finite retained scene transforms fail model assembly exactly");
}

void TestNifWireframePreview()
{
    using namespace rerevved::studio;

    Expect(NifProjectionName(NifProjection::xy) == "Raw XY projection" &&
               NifProjectionName(NifProjection::xz) == "Raw XZ projection" &&
               NifProjectionName(NifProjection::yz) == "Raw YZ projection",
           "NIF preview names every raw-axis projection exactly");
    Expect(NifPreviewErrorMessage(NifPreviewError::non_finite_position) ==
                   "The NIF mesh contains a non-finite vertex position." &&
               NifPreviewErrorMessage(
                   NifPreviewError::unrepresentable_transformed_position) ==
                   "A transformed NIF vertex position is non-finite or unrepresentable." &&
               NifPreviewErrorMessage(NifPreviewError::empty_projected_bounds) ==
                   "The NIF mesh has no projected bounds to display." &&
               NifPreviewErrorMessage(NifPreviewError::degenerate_projection) ==
                   "The NIF mesh has a degenerate raw-axis projection." &&
               NifPreviewErrorMessage(NifPreviewError::unavailable_region) ==
                   "The NIF Preview region has no drawable area.",
           "NIF preview errors retain their exact messages");

    const auto synthetic = MakeSyntheticModelNif();
    const auto document  = ParseNifDocument(synthetic.bytes);
    Expect(document.has_value(), "synthetic NIF parses for wireframe preview");
    if (!document)
        return;
    const auto model = AssembleNifModel(*document);
    Expect(model && !model->meshes.empty(), "synthetic NIF assembles for wireframe preview");
    if (!model || model->meshes.empty())
        return;

    const auto&      geometry = document->tri_shape_data[model->meshes[0].tri_shape_data_index];
    const std::array mesh_view{ NifPreviewMeshView{ geometry.vertex_positions,
                                                    geometry.triangles,
                                                    model->meshes[0].world_transform } };
    const auto       layout =
        CalculateNifWireframeLayout(mesh_view, 240.0F, 160.0F);
    Expect(layout && layout->projection == NifProjection::xy,
           "synthetic positive mesh produces a fitted XY wireframe");
    if (layout)
    {
        const auto&      triangle = geometry.triangles[0];
        const std::array points{
            ProjectNifPosition(*layout, geometry.vertex_positions[triangle[0]]),
            ProjectNifPosition(*layout, geometry.vertex_positions[triangle[1]]),
            ProjectNifPosition(*layout, geometry.vertex_positions[triangle[2]]),
        };
        const std::array edges{
            std::pair{ points[0], points[1] },
            std::pair{ points[1], points[2] },
            std::pair{ points[2], points[0] },
        };
        const auto visible_edges = std::ranges::count_if(
            edges,
            [](const auto& edge)
            {
                return edge.first != edge.second;
            });
        Expect(visible_edges == 3,
               "three synthetic positions and one triangle produce three visible edges");
    }

    const std::array<std::array<std::uint16_t, 3>, 1> triangles{ { { 0, 1, 2 } } };
    const std::array                                  xy_positions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 4.0F, 0.0F, 0.0F },
        std::array{ 0.0F, 3.0F, 0.0F },
    };
    const std::array xz_positions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 4.0F, 0.0F, 0.0F },
        std::array{ 0.0F, 0.0F, 3.0F },
    };
    const std::array yz_positions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 0.0F, 4.0F, 0.0F },
        std::array{ 0.0F, 0.0F, 3.0F },
    };
    const auto xy_layout =
        CalculateNifWireframeLayout(xy_positions, triangles, 200.0F, 100.0F);
    const auto xz_layout =
        CalculateNifWireframeLayout(xz_positions, triangles, 200.0F, 100.0F);
    const auto yz_layout =
        CalculateNifWireframeLayout(yz_positions, triangles, 200.0F, 100.0F);
    Expect(xy_layout && xy_layout->projection == NifProjection::xy && xz_layout &&
               xz_layout->projection == NifProjection::xz && yz_layout &&
               yz_layout->projection == NifProjection::yz,
           "dominant projected bounds deterministically select XY, XZ, and YZ");

    const std::array translated_positions{
        std::array{ -10.0F, -20.0F, -3.0F },
        std::array{ -6.0F, -20.0F, -3.0F },
        std::array{ -10.0F, -18.0F, -3.0F },
    };
    const auto fitted =
        CalculateNifWireframeLayout(translated_positions, triangles, 200.0F, 120.0F);
    Expect(fitted.has_value(), "translated negative-coordinate geometry fits successfully");
    if (fitted)
    {
        std::array<float, 2> minimum{ 200.0F, 120.0F };
        std::array<float, 2> maximum{};
        for (const auto& position : translated_positions)
        {
            const auto projected = ProjectNifPosition(*fitted, position);
            for (std::size_t axis = 0; axis < projected.size(); ++axis)
            {
                minimum[axis] = std::min(minimum[axis], projected[axis]);
                maximum[axis] = std::max(maximum[axis], projected[axis]);
            }
        }
        const auto centered =
            std::abs((minimum[0] + maximum[0]) * 0.5F - 100.0F) < 0.001F &&
            std::abs((minimum[1] + maximum[1]) * 0.5F - 60.0F) < 0.001F;
        const auto aspect = (maximum[0] - minimum[0]) / (maximum[1] - minimum[1]);
        Expect(centered && std::abs(aspect - 2.0F) < 0.001F,
               "uniform fitting centers translated geometry and preserves aspect ratio");
        Expect(minimum[0] >= 0.0F && minimum[1] >= 0.0F && maximum[0] <= 200.0F &&
                   maximum[1] <= 120.0F,
               "fitted wireframe stays inside the requested Preview region");
    }

    auto non_finite  = xy_positions;
    non_finite[2][1] = std::numeric_limits<float>::infinity();
    const auto non_finite_result =
        CalculateNifWireframeLayout(non_finite, triangles, 200.0F, 100.0F);
    Expect(!non_finite_result &&
               non_finite_result.error() == NifPreviewError::non_finite_position,
           "non-finite NIF positions fail with the exact preview error");

    NifModelTransform unrepresentable_transform;
    unrepresentable_transform.scale = std::numeric_limits<double>::max();
    const std::array unrepresentable_view{
        NifPreviewMeshView{ xy_positions, triangles, unrepresentable_transform }
    };
    const auto unrepresentable =
        CalculateNifWireframeLayout(unrepresentable_view, 200.0F, 100.0F);
    Expect(!unrepresentable &&
               unrepresentable.error() ==
                   NifPreviewError::unrepresentable_transformed_position,
           "unrepresentable transformed positions fail with the exact preview error");

    NifModelTransform overflowing_area_transform;
    overflowing_area_transform.scale = 1.0e200;
    const std::array overflowing_area_view{
        NifPreviewMeshView{ xy_positions, triangles, overflowing_area_transform }
    };
    const auto overflowing_area =
        CalculateNifWireframeLayout(overflowing_area_view, 200.0F, 100.0F);
    Expect(!overflowing_area &&
               overflowing_area.error() ==
                   NifPreviewError::unrepresentable_transformed_position,
           "finite transformed coordinates with overflowing projected area fail exactly");

    const std::array collinear_positions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 1.0F, 1.0F, 0.0F },
        std::array{ 2.0F, 2.0F, 0.0F },
    };
    const auto collinear =
        CalculateNifWireframeLayout(collinear_positions, triangles, 200.0F, 100.0F);
    Expect(!collinear && collinear.error() == NifPreviewError::degenerate_projection,
           "collinear projected triangle fails safely");

    const std::array point_positions{
        std::array{ 1.0F, 1.0F, 1.0F },
        std::array{ 1.0F, 1.0F, 1.0F },
        std::array{ 1.0F, 1.0F, 1.0F },
    };
    const auto point =
        CalculateNifWireframeLayout(point_positions, triangles, 200.0F, 100.0F);
    Expect(!point && point.error() == NifPreviewError::empty_projected_bounds,
           "point-like projected bounds report the exact empty-bounds error");

    const std::span<const std::array<float, 3>> empty_positions;
    const auto                                  empty =
        CalculateNifWireframeLayout(empty_positions, triangles, 200.0F, 100.0F);
    Expect(!empty && empty.error() == NifPreviewError::empty_projected_bounds,
           "empty NIF positions report the exact projected-bounds error");
    const auto unavailable =
        CalculateNifWireframeLayout(xy_positions, triangles, 0.0F, 100.0F);
    Expect(!unavailable && unavailable.error() == NifPreviewError::unavailable_region,
           "non-positive Preview width fails without drawing outside its region");

    AssetDocument assembled_asset{};
    assembled_asset.nif       = *document;
    assembled_asset.nif_model = *model;
    AssetDocument assembly_failed_asset{};
    assembly_failed_asset.nif             = *document;
    assembly_failed_asset.nif_model_error = NifModelError::no_supported_meshes;
    AssetDocument parse_failed_asset{};
    parse_failed_asset.document_error =
        std::string(NifDocumentErrorMessage(NifDocumentError::truncated));
    Expect(assembly_failed_asset.nif && !assembly_failed_asset.nif_model &&
               assembly_failed_asset.nif_model_error &&
               assembly_failed_asset.document_error.empty() && !parse_failed_asset.nif &&
               !parse_failed_asset.nif_model_error && !parse_failed_asset.document_error.empty(),
           "NIF assembly errors remain distinct from parse errors and preserve Inspector data");

    std::vector<AssetDocument> assets;
    assets.push_back(std::move(assembled_asset));
    assets.push_back(std::move(assembly_failed_asset));
    std::size_t selected = 0;
    Expect(assets[selected].nif_model && !assets[selected].nif_model_error,
           "selected assembled NIF owns its preview model state");
    selected = 1;
    Expect(!assets[selected].nif_model && assets[selected].nif_model_error,
           "switching NIF documents cannot reuse another preview model");
    assets.erase(assets.begin());
    Expect(!assets[0].nif_model && assets[0].nif_model_error,
           "closing another asset does not retain its NIF preview state");
}

void TestNifWireframeNavigation()
{
    using namespace rerevved::studio;

    Expect(NifProjectionModeName(NifProjectionMode::automatic) == "Auto" &&
               NifProjectionModeName(NifProjectionMode::xy) == "XY" &&
               NifProjectionModeName(NifProjectionMode::xz) == "XZ" &&
               NifProjectionModeName(NifProjectionMode::yz) == "YZ",
           "NIF preview projection controls use exact neutral labels");

    const auto synthetic = MakeSyntheticModelNif();
    const auto parsed    = ParseNifDocument(synthetic.bytes);
    Expect(parsed.has_value(), "synthetic NIF parses for navigation tests");
    if (!parsed)
        return;

    auto multi_mesh = *parsed;
    multi_mesh.blocks.push_back(NifBlock{ .type_index = 1 });
    multi_mesh.blocks.push_back(NifBlock{ .type_index = 2 });
    auto second_shape               = multi_mesh.tri_shapes[0];
    second_shape.block_index        = 4;
    second_shape.data               = 5;
    second_shape.object.translation = { 10.0F, 5.0F, 0.0F };
    multi_mesh.tri_shapes.push_back(second_shape);
    auto second_data             = multi_mesh.tri_shape_data[0];
    second_data.block_index      = 5;
    second_data.vertex_positions = {
        { 0.0F, 0.0F, 0.0F },
        { 4.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 3.0F },
    };
    multi_mesh.tri_shape_data.push_back(second_data);
    multi_mesh.nodes[0].children = { 1, 4 };
    const auto model             = AssembleNifModel(multi_mesh);
    Expect(model && model->meshes.size() == 2,
           "multi-mesh synthetic NIF assembles every selectable mesh");
    if (!model || model->meshes.size() != 2)
        return;

    NifPreviewState navigation;
    Expect(navigation.selected_mesh == 0 &&
               navigation.projection == NifProjectionMode::automatic &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "new NIF documents default to the first mesh, Auto, and a fitted view");
    const std::array expected_projections{ NifProjection::xy, NifProjection::xz };
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        const auto selected = SelectNifPreviewMesh(
            navigation, model->meshes.size() + 1, index);
        const auto&      geometry = multi_mesh.tri_shape_data[model->meshes[navigation.selected_mesh].tri_shape_data_index];
        const std::array selected_view{
            NifPreviewMeshView{ geometry.vertex_positions,
                                geometry.triangles,
                                model->meshes[navigation.selected_mesh].world_transform }
        };
        const auto layout =
            CalculateNifWireframeLayout(selected_view, 240.0F, 160.0F);
        Expect(selected && navigation.selected_mesh == index && layout &&
                   layout->projection == expected_projections[index],
               "each selected mesh uses its own retained geometry");
    }

    std::vector<NifPreviewMeshView> all_views;
    for (const auto& mesh : model->meshes)
    {
        const auto& geometry = multi_mesh.tri_shape_data[mesh.tri_shape_data_index];
        all_views.push_back(
            { geometry.vertex_positions, geometry.triangles, mesh.world_transform });
    }
    const auto all_layout =
        CalculateNifWireframeLayout(all_views, 240.0F, 160.0F);
    const auto second_position = TransformNifPreviewPosition(
        all_views[1], all_views[1].positions[1]);
    Expect(all_layout && all_layout->projection == NifProjection::xy &&
               all_layout->source_center == std::array{ 7.0, 2.5 } &&
               second_position && *second_position == std::array{ 14.0, 5.0, 0.0 },
           "All meshes fits combined transformed bounds while individual meshes use their transforms");

    PanNifPreview(navigation, 2.0F, 3.0F);
    ZoomNifPreview(navigation, 1.0F);
    const auto selected_all = SelectNifPreviewMesh(
        navigation, model->meshes.size() + 1, model->meshes.size());
    Expect(selected_all && navigation.selected_mesh == model->meshes.size() &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "All meshes is an explicit fitted selection after every individual mesh");

    navigation.pan  = { 9.0F, -4.0F };
    navigation.zoom = 3.0;
    const auto invalid_selection =
        SelectNifPreviewMesh(navigation, model->meshes.size() + 1, 99);
    Expect(!invalid_selection && navigation.selected_mesh == 0 &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "invalid mesh selection deterministically recovers to the first fitted mesh");

    const std::array<std::array<float, 3>, 3> full_axis_positions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 4.0F, 1.0F, 2.0F },
        std::array{ 1.0F, 3.0F, 4.0F },
    };
    const std::array<std::array<std::uint16_t, 3>, 1> triangle{
        std::array<std::uint16_t, 3>{ 0, 1, 2 },
    };
    const auto automatic = CalculateNifWireframeLayout(full_axis_positions,
                                                       triangle,
                                                       240.0F,
                                                       160.0F,
                                                       NifProjectionMode::automatic);
    const auto xy        = CalculateNifWireframeLayout(
        full_axis_positions, triangle, 240.0F, 160.0F, NifProjectionMode::xy);
    const auto xz = CalculateNifWireframeLayout(
        full_axis_positions, triangle, 240.0F, 160.0F, NifProjectionMode::xz);
    const auto yz = CalculateNifWireframeLayout(
        full_axis_positions, triangle, 240.0F, 160.0F, NifProjectionMode::yz);
    Expect(automatic && automatic->projection == NifProjection::xz && xy &&
               xy->projection == NifProjection::xy && xz &&
               xz->projection == NifProjection::xz && yz &&
               yz->projection == NifProjection::yz,
           "Auto and explicit XY, XZ, and YZ projections are deterministic");

    if (xy)
    {
        const auto fitted_scale  = xy->scale;
        const auto fitted_center = xy->target_center;
        PanNifPreview(navigation, 12.0F, -7.0F);
        ZoomNifPreview(navigation, 1.0F);
        const auto viewed = ApplyNifPreviewView(*xy, navigation);
        Expect(std::abs(viewed.scale - fitted_scale * 1.2) < 0.001 &&
                   std::abs(viewed.target_center[0] - fitted_center[0] - 12.0) < 0.001 &&
                   std::abs(viewed.target_center[1] - fitted_center[1] + 7.0) < 0.001,
               "NIF preview pan and wheel zoom adjust only the fitted view");
        ResetNifPreviewView(navigation);
        const auto reset = ApplyNifPreviewView(*xy, navigation);
        Expect(reset.scale == fitted_scale && reset.target_center == fitted_center,
               "Fit/Reset restores the deterministic fitted view");
    }

    PanNifPreview(navigation, 5.0F, 6.0F);
    ZoomNifPreview(navigation, 2.0F);
    SelectNifPreviewProjection(navigation, NifProjectionMode::yz);
    Expect(navigation.projection == NifProjectionMode::yz &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "projection changes restore the fitted view");
    PanNifPreview(navigation, 3.0F, 4.0F);
    SelectNifPreviewProjection(navigation, NifProjectionMode::yz);
    Expect(navigation.pan == std::array{ 3.0F, 4.0F },
           "reselecting the active projection preserves navigation state");
    (void)SelectNifPreviewMesh(navigation, model->meshes.size() + 1, 1);
    Expect(navigation.selected_mesh == 1 &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "mesh changes restore the fitted view");

    AssetDocument first_asset{};
    first_asset.nif              = multi_mesh;
    first_asset.nif_model        = *model;
    first_asset.nif_preview      = navigation;
    first_asset.nif_preview.pan  = { 8.0F, 9.0F };
    first_asset.nif_preview.zoom = 2.0;
    AssetDocument second_asset{};
    second_asset.nif                       = multi_mesh;
    second_asset.nif_model                 = *model;
    second_asset.nif_preview.selected_mesh = 0;
    second_asset.nif_preview.projection    = NifProjectionMode::xy;
    second_asset.nif_preview.pan           = { -3.0F, 1.0F };
    second_asset.nif_preview.zoom          = 0.5;
    Expect(first_asset.nif_preview.selected_mesh == 1 &&
               first_asset.nif_preview.projection == NifProjectionMode::yz &&
               first_asset.nif_preview.pan == std::array{ 8.0F, 9.0F } &&
               first_asset.nif_preview.zoom == 2.0 &&
               second_asset.nif_preview.selected_mesh == 0 &&
               second_asset.nif_preview.projection == NifProjectionMode::xy &&
               second_asset.nif_preview.pan == std::array{ -3.0F, 1.0F },
           "open NIF documents retain independent mesh, projection, pan, and zoom state");
    std::vector<AssetDocument> assets;
    assets.push_back(std::move(first_asset));
    assets.push_back(std::move(second_asset));
    assets.erase(assets.begin());
    Expect(assets[0].nif_preview.selected_mesh == 0 &&
               assets[0].nif_preview.projection == NifProjectionMode::xy &&
               assets[0].nif_preview.pan == std::array{ -3.0F, 1.0F } &&
               assets[0].nif_preview.zoom == 0.5,
           "closing or switching NIF documents cannot retain another document's view state");

    const auto wide     = CalculateNifPreviewControlsLayout(500.0F, 8.0F, 72.0F);
    const auto exact    = CalculateNifPreviewControlsLayout(220.0F, 8.0F, 72.0F);
    const auto below    = CalculateNifPreviewControlsLayout(219.0F, 8.0F, 72.0F);
    const auto zero     = CalculateNifPreviewControlsLayout(0.0F, 8.0F, 72.0F);
    const auto negative = CalculateNifPreviewControlsLayout(-50.0F, 8.0F, 72.0F);
    Expect(wide.mesh_width == 320.0F && wide.projection_width == 140.0F &&
               wide.reset_width == 72.0F && wide.reset_same_line,
           "wide NIF Preview controls retain their preferred and natural widths");
    Expect(exact.projection_width == 140.0F && exact.reset_width == 72.0F &&
               exact.reset_same_line && !below.reset_same_line &&
               below.reset_width == 72.0F,
           "exact-fit controls remain inline and stack just before clipping");
    Expect(zero.mesh_width > 0.0F && zero.projection_width > 0.0F &&
               zero.reset_width > 0.0F && negative.mesh_width > 0.0F &&
               negative.projection_width > 0.0F && negative.reset_width > 0.0F,
           "non-positive Preview widths still produce reachable positive controls");
}

std::vector<std::byte> MakeFpk()
{
    constexpr std::size_t  table_end = 14 + 24 + 24;
    std::vector<std::byte> bytes;
    AppendU32Le(bytes, 6);
    bytes.insert(bytes.end(), { std::byte{ 'F' }, std::byte{ 'P' }, std::byte{ 'K' }, std::byte{ '_' }, std::byte{ 0x12 }, std::byte{ 0x34 } });
    AppendU32Le(bytes, 2);

    AppendU32Le(bytes, 3);
    bytes.insert(bytes.end(), 4, std::byte{ 0x41 });
    bytes.insert(bytes.end(), { std::byte{ 0x01 }, std::byte{ 0x02 }, std::byte{ 0x03 }, std::byte{ 0x04 }, std::byte{ 0x05 }, std::byte{ 0x06 }, std::byte{ 0x07 }, std::byte{ 0x08 } });
    AppendU32Le(bytes, 5);
    AppendU32Le(bytes, static_cast<std::uint32_t>(table_end));

    AppendU32Le(bytes, 4);
    bytes.insert(bytes.end(), 4, std::byte{ 0x42 });
    bytes.insert(bytes.end(), { std::byte{ 0x11 }, std::byte{ 0x12 }, std::byte{ 0x13 }, std::byte{ 0x14 }, std::byte{ 0x15 }, std::byte{ 0x16 }, std::byte{ 0x17 }, std::byte{ 0x18 } });
    AppendU32Le(bytes, 3);
    AppendU32Le(bytes, static_cast<std::uint32_t>(table_end + 5));

    bytes.insert(bytes.end(), { std::byte{ 0x31 }, std::byte{ 0x32 }, std::byte{ 0x33 }, std::byte{ 0x34 }, std::byte{ 0x35 }, std::byte{ 0x61 }, std::byte{ 0x62 }, std::byte{ 0x63 } });
    return bytes;
}

std::vector<std::byte>
MakeFpkWithPayloads(std::span<const std::vector<std::byte>> payloads)
{
    const auto             table_end = 14U + payloads.size() * 20U;
    std::vector<std::byte> bytes;
    AppendU32Le(bytes, 6);
    bytes.insert(bytes.end(),
                 { std::byte{ 'F' },
                   std::byte{ 'P' },
                   std::byte{ 'K' },
                   std::byte{ '_' },
                   std::byte{ 0x12 },
                   std::byte{ 0x34 } });
    AppendU32Le(bytes, static_cast<std::uint32_t>(payloads.size()));

    std::size_t payload_offset = table_end;
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        AppendU32Le(bytes, 0);
        AppendU32Le(bytes, static_cast<std::uint32_t>(0x1000U + index));
        AppendU32Le(bytes, static_cast<std::uint32_t>(0x2000U + index));
        AppendU32Le(bytes, static_cast<std::uint32_t>(payloads[index].size()));
        AppendU32Le(bytes, static_cast<std::uint32_t>(payload_offset));
        payload_offset += payloads[index].size();
    }
    for (const auto& payload : payloads)
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

void TestCloseOpenedArchiveEntry()
{
    using rerevved::studio::ArchiveExplorerState;
    using rerevved::studio::CloseOpenedArchiveEntry;
    using rerevved::studio::FpkEntryFormat;
    using rerevved::studio::OpenFpkEntryDocument;
    using rerevved::studio::ParseFpkDocument;

    const std::array payloads{ MakeGfx() };
    const auto       archive_bytes   = MakeFpkWithPayloads(payloads);
    const auto       first_document  = ParseFpkDocument(archive_bytes);
    const auto       second_document = ParseFpkDocument(archive_bytes);
    Expect(first_document && second_document,
           "independent synthetic archives for embedded close are valid");
    if (!first_document || !second_document)
        return;

    const auto first_open =
        OpenFpkEntryDocument(*first_document, 0, FpkEntryFormat::gfx);
    const auto second_open =
        OpenFpkEntryDocument(*second_document, 0, FpkEntryFormat::gfx);
    Expect(first_open && second_open,
           "independent synthetic embedded entries open before close");
    if (!first_open || !second_open)
        return;

    ArchiveExplorerState first_state;
    first_state.selected_entry    = 0;
    first_state.requested_entry   = 1;
    first_state.selected_format   = FpkEntryFormat::gfx;
    first_state.opened_document   = *first_open;
    first_state.open_error        = "retained open error";
    first_state.navigation_error  = "retained navigation error";
    first_state.metadata_result   = "retained metadata result";
    first_state.extraction_result = "retained extraction result";
    first_state.extraction_path.fill('X');

    ArchiveExplorerState second_state;
    second_state.selected_entry  = 0;
    second_state.requested_entry = 1;
    second_state.selected_format = FpkEntryFormat::gfx;
    second_state.opened_document = *second_open;

    const auto selected_entry_before     = first_state.selected_entry;
    const auto requested_entry_before    = first_state.requested_entry;
    const auto selected_format_before    = first_state.selected_format;
    const auto open_error_before         = first_state.open_error;
    const auto navigation_error_before   = first_state.navigation_error;
    const auto metadata_result_before    = first_state.metadata_result;
    const auto extraction_result_before  = first_state.extraction_result;
    const auto extraction_path_before    = first_state.extraction_path;
    const auto unrelated_state_preserved = [&]
    {
        return first_state.selected_entry == selected_entry_before &&
               first_state.requested_entry == requested_entry_before &&
               first_state.selected_format == selected_format_before &&
               first_state.open_error == open_error_before &&
               first_state.navigation_error == navigation_error_before &&
               first_state.metadata_result == metadata_result_before &&
               first_state.extraction_result == extraction_result_before &&
               first_state.extraction_path == extraction_path_before;
    };

    CloseOpenedArchiveEntry(first_state);
    Expect(!first_state.opened_document && !first_state.nif_preview,
           "closing an embedded entry releases its opened document and preview state");
    Expect(unrelated_state_preserved(),
           "closing an embedded entry preserves every unrelated archive field");
    Expect(first_document->bytes == archive_bytes && second_document->bytes == archive_bytes,
           "closing an embedded entry preserves each parent archive byte");
    Expect(second_state.opened_document && second_state.selected_entry == 0 &&
               second_state.selected_format == FpkEntryFormat::gfx,
           "closing one embedded entry leaves another archive state independent");

    CloseOpenedArchiveEntry(first_state);
    Expect(!first_state.opened_document && unrelated_state_preserved(),
           "closing with no opened embedded document is harmless");

    const auto reopened = OpenFpkEntryDocument(*first_document,
                                               *first_state.selected_entry,
                                               first_state.selected_format);
    Expect(reopened && reopened->entry_index == *first_state.selected_entry &&
               reopened->format == first_state.selected_format,
           "retained selection and explicit format reopen the same embedded entry");
}

void TestEmbeddedNifWireframePreview()
{
    using namespace rerevved::studio;

    auto synthetic = MakeSyntheticModelNif();
    WriteNifF32(synthetic.bytes, synthetic.shape_transform_offset, 10.0F);
    WriteNifF32(synthetic.bytes, synthetic.shape_transform_offset + 4, 5.0F);
    WriteNifF32(synthetic.bytes, synthetic.shape_transform_offset + 48, 2.0F);
    auto no_mesh = synthetic.bytes;
    WriteU32Be(no_mesh, synthetic.root_reference_offset, UINT32_MAX);
    const std::array<std::vector<std::byte>, 5> payloads{
        synthetic.bytes,
        synthetic.bytes,
        no_mesh,
        std::vector{ std::byte{ 'B' }, std::byte{ 'A' }, std::byte{ 'D' } },
        MakeGfx(),
    };
    const auto archive_bytes  = MakeFpkWithPayloads(payloads);
    const auto first_archive  = ParseFpkDocument(archive_bytes);
    const auto second_archive = ParseFpkDocument(archive_bytes);
    Expect(first_archive && second_archive,
           "independent synthetic archives parse for embedded NIF preview");
    if (!first_archive || !second_archive)
        return;

    ArchiveExplorerState first_state;
    (void)SelectArchiveEntry(first_state, payloads.size(), 1);
    first_state.selected_format   = FpkEntryFormat::nif;
    first_state.metadata_result   = "retained metadata";
    first_state.extraction_result = "retained extraction";
    first_state.navigation_error  = "retained navigation";
    first_state.extraction_path.fill('P');
    OpenSelectedArchiveEntryInMemory(*first_archive, first_state);
    Expect(first_state.opened_document && first_state.nif_preview &&
               first_state.nif_preview->model &&
               !first_state.nif_preview->assembly_error && first_state.open_error.empty(),
           "explicitly opened embedded NIF assembles a wireframe preview model");
    Expect(first_state.nif_preview &&
               first_state.nif_preview->navigation.selected_mesh == 0 &&
               first_state.nif_preview->navigation.projection ==
                   NifProjectionMode::automatic &&
               first_state.nif_preview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               first_state.nif_preview->navigation.zoom == 1.0,
           "new embedded NIF preview starts on the first mesh with fitted Auto state");
    if (first_state.nif_preview && first_state.nif_preview->model)
    {
        const auto& preview_document =
            std::get<NifDocument>(first_state.opened_document->data);
        const auto& preview_mesh = first_state.nif_preview->model->meshes[0];
        const auto& preview_geometry =
            preview_document.tri_shape_data[preview_mesh.tri_shape_data_index];
        const auto embedded_position = ApplyNifModelTransform(
            preview_mesh.world_transform, preview_geometry.vertex_positions[1]);
        Expect(embedded_position && *embedded_position == std::array{ 12.0, 5.0, 0.0 },
               "embedded NIF preview applies the retained shape transform");
        const auto selected_all = SelectNifPreviewMesh(
            first_state.nif_preview->navigation,
            first_state.nif_preview->model->meshes.size() + 1,
            first_state.nif_preview->model->meshes.size());
        Expect(selected_all && first_state.nif_preview->navigation.selected_mesh == 1,
               "embedded NIF Preview exposes the shared All meshes selection");
    }

    first_state.nif_preview->navigation.projection = NifProjectionMode::yz;
    first_state.nif_preview->navigation.pan        = { 7.0F, -3.0F };
    first_state.nif_preview->navigation.zoom       = 2.0;

    ArchiveExplorerState second_state;
    (void)SelectArchiveEntry(second_state, payloads.size(), 1);
    second_state.selected_format = FpkEntryFormat::nif;
    OpenSelectedArchiveEntryInMemory(*second_archive, second_state);
    Expect(second_state.nif_preview && second_state.nif_preview->model &&
               second_state.nif_preview->navigation.selected_mesh == 0 &&
               second_state.nif_preview->navigation.projection ==
                   NifProjectionMode::automatic &&
               second_state.nif_preview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               second_state.nif_preview->navigation.zoom == 1.0 &&
               first_state.nif_preview->navigation.selected_mesh == 1 &&
               first_state.nif_preview->navigation.projection == NifProjectionMode::yz &&
               first_state.nif_preview->navigation.pan == std::array{ 7.0F, -3.0F } &&
               first_state.nif_preview->navigation.zoom == 2.0,
           "separate archives retain independent embedded NIF preview state");

    const auto selected_before         = first_state.selected_entry;
    const auto requested_before        = first_state.requested_entry;
    const auto format_before           = first_state.selected_format;
    const auto metadata_before         = first_state.metadata_result;
    const auto extraction_before       = first_state.extraction_result;
    const auto extraction_path_before  = first_state.extraction_path;
    const auto navigation_error_before = first_state.navigation_error;
    const auto source_before           = first_archive->bytes;
    CloseOpenedArchiveEntry(first_state);
    Expect(!first_state.opened_document && !first_state.nif_preview &&
               first_state.selected_entry == selected_before &&
               first_state.requested_entry == requested_before &&
               first_state.selected_format == format_before &&
               first_state.metadata_result == metadata_before &&
               first_state.extraction_result == extraction_before &&
               first_state.extraction_path == extraction_path_before &&
               first_state.navigation_error == navigation_error_before &&
               first_archive->bytes == source_before,
           "closing embedded NIF clears only opened preview ownership and preserves archive state");
    Expect(second_state.opened_document && second_state.nif_preview &&
               second_state.nif_preview->model,
           "closing one archive preview leaves another archive fully independent");

    OpenSelectedArchiveEntryInMemory(*first_archive, first_state);
    Expect(first_state.opened_document && first_state.nif_preview &&
               first_state.nif_preview->model &&
               first_state.nif_preview->navigation.selected_mesh == 0 &&
               first_state.nif_preview->navigation.projection ==
                   NifProjectionMode::automatic &&
               first_state.nif_preview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               first_state.nif_preview->navigation.zoom == 1.0,
           "closed embedded NIF reopens immediately with deterministic preview state");

    first_state.nif_preview->navigation.projection = NifProjectionMode::xz;
    first_state.nif_preview->navigation.pan        = { 4.0F, 5.0F };
    first_state.nif_preview->navigation.zoom       = 3.0;
    (void)SelectArchiveEntry(first_state, payloads.size(), 2);
    Expect(!first_state.opened_document && !first_state.nif_preview &&
               first_state.selected_entry == 1,
           "selecting another archive entry removes the previous embedded preview state");
    OpenSelectedArchiveEntryInMemory(*first_archive, first_state);
    Expect(first_state.opened_document && first_state.nif_preview &&
               first_state.nif_preview->model &&
               first_state.nif_preview->navigation.selected_mesh == 0 &&
               first_state.nif_preview->navigation.projection ==
                   NifProjectionMode::automatic &&
               first_state.nif_preview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               first_state.nif_preview->navigation.zoom == 1.0,
           "opening a different embedded NIF initializes a fresh fitted Auto view");

    (void)SelectArchiveEntry(first_state, payloads.size(), 4);
    first_state.selected_format   = FpkEntryFormat::nif;
    first_state.metadata_result   = "parse metadata";
    first_state.extraction_result = "parse extraction";
    OpenSelectedArchiveEntryInMemory(*first_archive, first_state);
    Expect(!first_state.opened_document && first_state.nif_preview &&
               !first_state.nif_preview->model &&
               !first_state.nif_preview->assembly_error &&
               first_state.open_error == "The NIF file is truncated." &&
               first_state.metadata_result == "parse metadata" &&
               first_state.extraction_result == "parse extraction",
           "embedded NIF parse failure remains distinct and preserves Archive Explorer state");

    (void)SelectArchiveEntry(first_state, payloads.size(), 3);
    first_state.selected_format   = FpkEntryFormat::nif;
    first_state.metadata_result   = "assembly metadata";
    first_state.extraction_result = "assembly extraction";
    OpenSelectedArchiveEntryInMemory(*first_archive, first_state);
    Expect(first_state.opened_document && first_state.nif_preview &&
               !first_state.nif_preview->model &&
               first_state.nif_preview->assembly_error ==
                   NifModelError::no_supported_meshes &&
               first_state.open_error.empty() &&
               first_state.metadata_result == "assembly metadata" &&
               first_state.extraction_result == "assembly extraction",
           "embedded NIF assembly failure remains distinct from parse failure");

    ArchiveExplorerState explicit_gfx_state;
    (void)SelectArchiveEntry(explicit_gfx_state, payloads.size(), 1);
    explicit_gfx_state.selected_format = FpkEntryFormat::gfx;
    OpenSelectedArchiveEntryInMemory(*first_archive, explicit_gfx_state);
    Expect(!explicit_gfx_state.opened_document && !explicit_gfx_state.nif_preview &&
               explicit_gfx_state.open_error == "The file does not have a valid GFX signature.",
           "a selected NIF payload is not previewed without explicit NIF routing");

    ArchiveExplorerState valid_gfx_state;
    (void)SelectArchiveEntry(valid_gfx_state, payloads.size(), 5);
    valid_gfx_state.selected_format = FpkEntryFormat::gfx;
    OpenSelectedArchiveEntryInMemory(*first_archive, valid_gfx_state);
    Expect(valid_gfx_state.opened_document && !valid_gfx_state.nif_preview &&
               std::holds_alternative<GfxDocument>(valid_gfx_state.opened_document->data),
           "non-NIF embedded preview routing remains unchanged");

    AssetDocument direct_asset;
    const auto    direct_document = ParseNifDocument(synthetic.bytes);
    Expect(direct_document.has_value(), "direct NIF regression fixture parses");
    if (direct_document)
    {
        direct_asset.nif        = *direct_document;
        const auto direct_model = AssembleNifModel(*direct_document);
        if (direct_model)
            direct_asset.nif_model = *direct_model;
        direct_asset.nif_preview.projection = NifProjectionMode::xz;
        direct_asset.nif_preview.pan        = { 11.0F, 12.0F };
        direct_asset.nif_preview.zoom       = 1.5;
        CloseOpenedArchiveEntry(second_state);
        const auto direct_position = direct_asset.nif_model
                                         ? ApplyNifModelTransform(
                                               direct_asset.nif_model->meshes[0].world_transform,
                                               direct_asset.nif->tri_shape_data[0]
                                                   .vertex_positions[1])
                                         : std::expected<std::array<double, 3>, NifModelError>(
                                               std::unexpected(
                                                   NifModelError::invalid_transform));
        Expect(direct_asset.nif && direct_asset.nif_model && direct_position &&
                   *direct_position == std::array{ 12.0, 5.0, 0.0 } &&
                   direct_asset.nif_preview.projection == NifProjectionMode::xz &&
                   direct_asset.nif_preview.pan == std::array{ 11.0F, 12.0F } &&
                   direct_asset.nif_preview.zoom == 1.5,
               "direct and embedded NIFs share transformed preview behavior and isolated state");
    }

    Expect(first_archive->bytes == archive_bytes && second_archive->bytes == archive_bytes,
           "embedded NIF preview lifecycle preserves every retained archive byte");
}

void RemoveTestFile(const std::filesystem::path& path);

void TestInitialArchiveSelection()
{
    using rerevved::studio::ArchiveExplorerState;
    using rerevved::studio::AssetDocument;
    using rerevved::studio::ParseFpkDocument;
    using rerevved::studio::SelectArchiveEntry;
    using rerevved::studio::SelectInitialArchiveEntry;

    const std::array<std::vector<std::byte>, 1> one_payload{
        std::vector{ std::byte{ 0x10 }, std::byte{ 0x20 }, std::byte{ 0x30 } }
    };
    const auto one_bytes    = MakeFpkWithPayloads(one_payload);
    const auto one_document = ParseFpkDocument(one_bytes);
    Expect(one_document.has_value(), "one-entry synthetic FPK document is valid");

    ArchiveExplorerState one_state;
    if (one_document)
        SelectInitialArchiveEntry(one_state, *one_document);
    Expect(one_state.selected_entry && *one_state.selected_entry == 0 &&
               one_state.requested_entry == 1,
           "one-entry archive initially selects displayed entry 1");
    if (one_document && one_state.selected_entry)
    {
        const auto& entry = one_document->index.entries[*one_state.selected_entry];
        const auto  end   = static_cast<std::uint64_t>(entry.offset) + entry.size;
        Expect(entry.offset == 34 && entry.size == 3 && end == 37,
               "initial archive selection exposes the validated byte range");
    }
    Expect(!one_state.opened_document && one_state.open_error.empty() &&
               one_state.extraction_result.empty() && one_state.metadata_result.empty() &&
               std::ranges::all_of(one_state.extraction_path,
                                   [](char value)
                                   {
                                       return value == '\0';
                                   }),
           "initial archive selection has no embedded-open or extraction side effect");

    const std::array<std::vector<std::byte>, 3> multiple_payloads{
        std::vector{ std::byte{ 0x11 } },
        std::vector{ std::byte{ 0x21 }, std::byte{ 0x22 } },
        std::vector{ std::byte{ 0x31 }, std::byte{ 0x32 }, std::byte{ 0x33 } },
    };
    const auto multiple_bytes    = MakeFpkWithPayloads(multiple_payloads);
    const auto multiple_document = ParseFpkDocument(multiple_bytes);
    Expect(multiple_document.has_value(), "multi-entry synthetic FPK document is valid");

    AssetDocument first_asset{};
    if (multiple_document)
    {
        first_asset.fpk = *multiple_document;
        SelectInitialArchiveEntry(first_asset.archive, *first_asset.fpk);
    }
    auto& multiple_state = first_asset.archive;
    Expect(multiple_state.selected_entry && *multiple_state.selected_entry == 0 &&
               multiple_state.requested_entry == 1,
           "multi-entry archive initially selects displayed entry 1");

    const std::array<std::vector<std::byte>, 0> empty_payloads{};
    const auto                                  empty_bytes    = MakeFpkWithPayloads(empty_payloads);
    const auto                                  empty_document = ParseFpkDocument(empty_bytes);
    Expect(empty_document.has_value() && empty_document->index.entries.empty(),
           "empty synthetic FPK document is valid");
    ArchiveExplorerState empty_state;
    if (empty_document)
        SelectInitialArchiveEntry(empty_state, *empty_document);
    Expect(!empty_state.selected_entry && !empty_state.opened_document,
           "empty archive initialization retains no selection");

    const auto          failed_document = ParseFpkDocument(std::span(one_bytes).first(13));
    const AssetDocument failed_asset{};
    Expect(!failed_document && !failed_asset.fpk && !failed_asset.archive.selected_entry &&
               !failed_asset.archive.opened_document,
           "failed archive initialization creates no document, selection, or opened entry");

    if (!multiple_document)
        return;

    Expect(SelectArchiveEntry(multiple_state, multiple_document->index.entries.size(), 3),
           "user can change the initial archive selection");
    multiple_state.opened_document.emplace();
    multiple_state.extraction_result = "current extraction result";
    SelectInitialArchiveEntry(multiple_state, *multiple_document);
    Expect(multiple_state.selected_entry && *multiple_state.selected_entry == 2 &&
               multiple_state.requested_entry == 3 && multiple_state.opened_document &&
               multiple_state.extraction_result == "current extraction result",
           "reopening an initialized archive preserves later user state");

    const auto second_document = ParseFpkDocument(multiple_bytes);
    Expect(second_document.has_value(), "second synthetic FPK document is independently valid");
    AssetDocument second_asset{};
    if (second_document)
    {
        second_asset.fpk = *second_document;
        SelectInitialArchiveEntry(second_asset.archive, *second_asset.fpk);
    }
    auto& second_state = second_asset.archive;
    Expect(second_state.selected_entry && *second_state.selected_entry == 0 &&
               second_asset.fpk && !second_state.opened_document &&
               second_state.extraction_result.empty(),
           "a second archive owns independent initial state");
    Expect(SelectArchiveEntry(second_state, multiple_document->index.entries.size(), 2),
           "second archive selection can change independently");
    Expect(multiple_state.selected_entry && *multiple_state.selected_entry == 2 &&
               multiple_state.opened_document && second_state.selected_entry &&
               *second_state.selected_entry == 1 && !second_state.opened_document,
           "simultaneously opened archives retain independent selection and document state");
}

void TestFpkIndex()
{
    using rerevved::studio::FpkIndexError;
    using rerevved::studio::ParseFpkIndex;

    const auto bytes = MakeFpk();
    const auto index = ParseFpkIndex(bytes);
    Expect(index.has_value(), "FPK index parsing succeeds");
    if (index)
    {
        Expect(index->version == 6, "FPK version is preserved");
        Expect(index->header_unknown ==
                   std::array{ std::byte{ 0x12 }, std::byte{ 0x34 } },
               "FPK header unknown bytes are preserved");
        Expect(index->entries.size() == 2, "FPK entry count is preserved");
        Expect(index->entries[0].offset == 62 && index->entries[0].size == 5,
               "FPK entry byte range is preserved");
        Expect(index->entries[0].unknown0 ==
                   std::array{ std::byte{ 0x01 }, std::byte{ 0x02 }, std::byte{ 0x03 }, std::byte{ 0x04 } },
               "FPK entry unknown bytes are preserved");
        Expect(index->entries[0].unknown1 ==
                   std::array{ std::byte{ 0x05 }, std::byte{ 0x06 }, std::byte{ 0x07 }, std::byte{ 0x08 } },
               "FPK second entry unknown bytes are preserved");
    }

    Expect(!ParseFpkIndex(std::span(bytes).first(13)), "truncated FPK header is rejected");

    auto oversized_count = bytes;
    WriteU32Le(oversized_count, 10, std::numeric_limits<std::uint32_t>::max());
    Expect(!ParseFpkIndex(oversized_count),
           "FPK count arithmetic exceeding the bounded table is rejected");

    auto truncated_record = bytes;
    WriteU32Le(truncated_record, 14, std::numeric_limits<std::uint32_t>::max());
    Expect(!ParseFpkIndex(truncated_record), "truncated variable FPK record is rejected");

    auto out_of_range = bytes;
    WriteU32Le(out_of_range, 34, static_cast<std::uint32_t>(bytes.size() + 1));
    const auto out_of_range_result = ParseFpkIndex(out_of_range);
    Expect(!out_of_range_result && out_of_range_result.error() == FpkIndexError::entry_out_of_range,
           "out-of-range FPK entry is rejected");

    auto range_overflow = bytes;
    WriteU32Le(range_overflow, 30, 32);
    WriteU32Le(range_overflow, 34, 0xFFFFFFF0U);
    const auto range_overflow_result = ParseFpkIndex(range_overflow);
    Expect(!range_overflow_result &&
               range_overflow_result.error() == FpkIndexError::entry_range_overflow,
           "FPK offset and size overflow is rejected");
}

void TestFpkDocument()
{
    using rerevved::studio::DdsDocument;
    using rerevved::studio::FpkEntryFormat;
    using rerevved::studio::GfxDocument;
    using rerevved::studio::LoadFpkDocument;
    using rerevved::studio::MapDocument;
    using rerevved::studio::Mp3Document;
    using rerevved::studio::NifDocument;
    using rerevved::studio::OpenFpkEntryDocument;
    using rerevved::studio::ParseFpkDocument;
    using rerevved::studio::test::kSyntheticMp3;

    const std::array dds_pixels{
        std::byte{ 0x10 }, std::byte{ 0x20 }, std::byte{ 0x30 }, std::byte{ 0xFF }
    };
    auto       dds = MakePreviewDds(1,
                                    1,
                                    0x00FF0000U,
                                    0x0000FF00U,
                                    0x000000FFU,
                                    0xFF000000U,
                                    dds_pixels);
    auto       gfx = MakeGfx();
    auto       nif = MakeNif().bytes;
    const auto mp3_bytes =
        std::as_bytes(std::span(kSyntheticMp3.data(), kSyntheticMp3.size()));
    std::vector<std::byte> mp3(mp3_bytes.begin(), mp3_bytes.end());
    std::vector<std::byte> map(rerevved::studio::kMapEnvelopeSize);
    std::ranges::fill(map.begin() +
                          static_cast<std::ptrdiff_t>(rerevved::studio::kMapCoreSize),
                      map.end(),
                      std::byte{ 0xFF });

    const std::array payloads{
        std::move(dds), std::move(gfx), std::move(nif), std::move(mp3), std::move(map)
    };
    const auto archive_bytes = MakeFpkWithPayloads(payloads);
    const auto source_before = archive_bytes;
    auto       document      = ParseFpkDocument(archive_bytes);
    Expect(document.has_value(), "FPK document retains a validated synthetic archive");
    Expect(archive_bytes == source_before, "FPK document parsing leaves source bytes unchanged");
    if (!document)
        return;

    Expect(document->bytes == archive_bytes, "FPK document retains exact validated source bytes");
    Expect(document->index.entries.size() == payloads.size(),
           "FPK document retains the bounded entry index");

    const std::array formats{
        FpkEntryFormat::dds,
        FpkEntryFormat::gfx,
        FpkEntryFormat::nif,
        FpkEntryFormat::mp3,
        FpkEntryFormat::map,
    };
    for (std::size_t index = 0; index < formats.size(); ++index)
    {
        const auto opened = OpenFpkEntryDocument(*document, index, formats[index]);
        Expect(opened.has_value(), "supported FPK entry opens through a bounded span parser");
        if (!opened)
            continue;
        Expect(opened->entry_index == index && opened->format == formats[index],
               "opened FPK entry preserves selection and explicit format");
        const bool has_expected_type =
            (index == 0 && std::holds_alternative<DdsDocument>(opened->data)) ||
            (index == 1 && std::holds_alternative<GfxDocument>(opened->data)) ||
            (index == 2 && std::holds_alternative<NifDocument>(opened->data)) ||
            (index == 3 && std::holds_alternative<Mp3Document>(opened->data)) ||
            (index == 4 && std::holds_alternative<MapDocument>(opened->data));
        Expect(has_expected_type, "FPK entry router returns the explicitly selected document type");
    }
    Expect(document->bytes == archive_bytes,
           "in-memory FPK entry routing leaves retained archive bytes unchanged");

    const auto invalid_selection =
        OpenFpkEntryDocument(*document, document->index.entries.size(), FpkEntryFormat::dds);
    Expect(!invalid_selection &&
               invalid_selection.error() == "The selected FPK entry does not exist.",
           "invalid in-memory FPK selection reports its exact failure");

    auto invalid_range                    = *document;
    invalid_range.index.entries[0].offset = std::numeric_limits<std::uint32_t>::max();
    invalid_range.index.entries[0].size   = 2;
    const auto range_result               = OpenFpkEntryDocument(invalid_range, 0, FpkEntryFormat::dds);
    Expect(!range_result &&
               range_result.error() == "The selected FPK entry range is invalid.",
           "in-memory FPK routing reports its revalidated entry range failure");

    const std::array malformed_payloads{
        std::vector{ std::byte{ 'B' }, std::byte{ 'A' }, std::byte{ 'D' } }
    };
    const auto malformed_bytes   = MakeFpkWithPayloads(malformed_payloads);
    const auto malformed_archive = ParseFpkDocument(malformed_bytes);
    Expect(malformed_archive.has_value(), "synthetic malformed FPK payload remains bounded");
    if (malformed_archive)
    {
        const std::array expected_errors{
            std::string_view{ "DDS metadata is truncated." },
            std::string_view{ "The file does not have a valid GFX signature." },
            std::string_view{ "The NIF file is truncated." },
            std::string_view{ "The MP3 audio could not be decoded." },
            std::string_view{
                "The map record is truncated; the Xbox DLC profile requires 1088 bytes." },
        };
        for (std::size_t index = 0; index < formats.size(); ++index)
        {
            const auto malformed = OpenFpkEntryDocument(*malformed_archive, 0, formats[index]);
            Expect(!malformed && malformed.error() == expected_errors[index],
                   "embedded FPK parser preserves its exact malformed-content failure");
        }
        Expect(malformed_archive->bytes == malformed_bytes,
               "failed embedded parsers leave malformed FPK source bytes unchanged");
    }

    const auto invalid_format = OpenFpkEntryDocument(
        *document, 0, static_cast<FpkEntryFormat>(formats.size()));
    Expect(!invalid_format &&
               invalid_format.error() == "The selected FPK entry format is invalid.",
           "invalid embedded FPK format reports a bounded failure");
    Expect(document->bytes == archive_bytes,
           "failed in-memory FPK entry routing leaves retained archive bytes unchanged");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-studio-fpk-document-test.fpk";
    RemoveTestFile(path);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(archive_bytes.data()),
                     static_cast<std::streamsize>(archive_bytes.size()));
    }
    const auto loaded = LoadFpkDocument(path);
    Expect(loaded && loaded->bytes == archive_bytes &&
               loaded->index.entries.size() == payloads.size(),
           "FPK document loader retains validated bytes and index");
    RemoveTestFile(path);

    const auto expect_load_error = [&](std::span<const std::byte> bytes,
                                       std::string_view           expected,
                                       std::string_view           message)
    {
        RemoveTestFile(path);
        {
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        const auto invalid = LoadFpkDocument(path);
        Expect(!invalid && invalid.error() == expected, message);
        RemoveTestFile(path);
    };

    expect_load_error(std::span(archive_bytes).first(13),
                      "The FPK archive table is truncated.",
                      "FPK loader reports a clean truncated-table failure");

    auto invalid_signature = archive_bytes;
    invalid_signature[4]   = std::byte{ 'X' };
    expect_load_error(invalid_signature,
                      "The file does not have a valid FPK signature.",
                      "FPK loader reports a clean invalid-signature failure");

    auto unsupported_version = archive_bytes;
    WriteU32Le(unsupported_version, 0, 5);
    expect_load_error(unsupported_version,
                      "The FPK archive version is unsupported.",
                      "FPK loader reports a clean unsupported-version failure");

    auto overflowing_range = archive_bytes;
    WriteU32Le(overflowing_range, 26, 32);
    WriteU32Le(overflowing_range, 30, 0xFFFFFFF0U);
    expect_load_error(overflowing_range,
                      "An FPK entry byte range overflows.",
                      "FPK loader reports a clean entry-range-overflow failure");

    auto out_of_range = archive_bytes;
    WriteU32Le(out_of_range, 30, static_cast<std::uint32_t>(archive_bytes.size() + 1));
    expect_load_error(out_of_range,
                      "An FPK entry points outside the archive payload region.",
                      "FPK loader reports a clean out-of-archive failure");
}

void RemoveTestFile(const std::filesystem::path& path)
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

std::vector<unsigned char> ReadTestFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>{ input }, {} };
}

void TestFpkExtraction()
{
    using rerevved::studio::ExtractFpkEntry;
    using rerevved::studio::FpkExtractionError;
    using rerevved::studio::ParseFpkIndex;

    const auto bytes = MakeFpk();
    const auto index = ParseFpkIndex(bytes);
    Expect(index.has_value(), "synthetic FPK for extraction parses");
    if (!index)
        return;

    const auto temp          = std::filesystem::temp_directory_path();
    const auto output_path   = temp / "rerevved-studio-fpk-extraction-output.bin";
    const auto existing_path = temp / "rerevved-studio-fpk-extraction-existing.bin";
    const auto invalid_path  = temp / "rerevved-studio-fpk-extraction-invalid.bin";
    const auto blocker_path  = temp / "rerevved-studio-fpk-extraction-blocker";
    RemoveTestFile(output_path);
    RemoveTestFile(existing_path);
    RemoveTestFile(invalid_path);
    RemoveTestFile(blocker_path);

    const auto source_before = bytes;
    const auto extraction    = ExtractFpkEntry(bytes, *index, 1, output_path);
    Expect(extraction.has_value(), "selected FPK entry extraction succeeds");
    Expect(ReadTestFile(output_path) ==
               std::vector<unsigned char>{ 0x61, 0x62, 0x63 },
           "only the selected FPK payload bytes are written");
    Expect(bytes == source_before, "FPK extraction leaves source bytes unchanged");
    RemoveTestFile(output_path);

    const auto invalid_selection =
        ExtractFpkEntry(bytes, *index, index->entries.size(), invalid_path);
    Expect(!invalid_selection &&
               invalid_selection.error() == FpkExtractionError::invalid_selection,
           "invalid FPK selection is rejected");
    Expect(!std::filesystem::exists(invalid_path),
           "invalid FPK selection creates no destination");

    auto invalid_index              = *index;
    invalid_index.entries[0].offset = static_cast<std::uint32_t>(bytes.size());
    const auto invalid_range        = ExtractFpkEntry(bytes, invalid_index, 0, invalid_path);
    Expect(!invalid_range &&
               invalid_range.error() == FpkExtractionError::invalid_entry_range,
           "invalid FPK entry range is revalidated");
    Expect(!std::filesystem::exists(invalid_path),
           "invalid FPK range creates no destination");

    invalid_index.entries[0].offset = 0xFFFFFFFEU;
    invalid_index.entries[0].size   = 3;
    const auto invalid_range_overflow =
        ExtractFpkEntry(bytes, invalid_index, 0, invalid_path);
    Expect(!invalid_range_overflow &&
               invalid_range_overflow.error() == FpkExtractionError::invalid_entry_range,
           "overflowing FPK entry range is revalidated");
    Expect(!std::filesystem::exists(invalid_path),
           "overflowing FPK range creates no destination");

    {
        std::ofstream existing(existing_path, std::ios::binary);
        existing.put(static_cast<char>(0x5A));
    }
    const auto existing_destination = ExtractFpkEntry(bytes, *index, 0, existing_path);
    Expect(!existing_destination &&
               existing_destination.error() == FpkExtractionError::destination_exists,
           "existing extraction destination is rejected");
    Expect(ReadTestFile(existing_path) == std::vector<unsigned char>{ 0x5A },
           "existing extraction destination is not overwritten");
    RemoveTestFile(existing_path);

    {
        std::ofstream blocker(blocker_path, std::ios::binary);
        blocker.put(static_cast<char>(0x00));
    }
    const auto unavailable_destination =
        ExtractFpkEntry(bytes, *index, 0, blocker_path / "payload.bin");
    Expect(!unavailable_destination &&
               unavailable_destination.error() == FpkExtractionError::open_failed,
           "unavailable extraction destination reports open failure");
    RemoveTestFile(blocker_path);
}

} // namespace

int main()
{
    TestFileKinds();
    TestDocumentErrorMessages();
    TestArchiveNavigation();
    TestExtractionModalLayout();
    TestArchiveActionRowLayout();
    TestArchiveMetadataLayout();
    TestAssetCloseSelection();
    TestFpkEntryRangeFormatting();
    TestInspection();
    TestDdsMetadata();
    TestDdsDocument();
    TestGfxDocument();
    TestMapDocument();
    TestMp3Document();
    TestNifDocument();
    TestNifGeometryInventory();
    TestSyntheticModelFixture();
    TestNifMaterialPropertyInventory();
    TestNifTextureSourceInventory();
    TestNifMaterialInspectorPresentation();
    TestNifTextureSourceInspectorPresentation();
    TestNifModelAssembly();
    TestNifWireframePreview();
    TestNifWireframeNavigation();
    TestCloseOpenedArchiveEntry();
    TestEmbeddedNifWireframePreview();
    TestInitialArchiveSelection();
    TestFpkIndex();
    TestFpkDocument();
    TestFpkExtraction();
    if (failures != 0)
        std::cerr << failures << " test(s) failed.\n";
    return failures == 0 ? 0 : 1;
}
