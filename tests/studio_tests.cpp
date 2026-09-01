#include "dds_metadata.h"
#include "file_inspection.h"
#include "file_kind.h"
#include "fpk_extraction.h"
#include "fpk_index.h"
#include "inspection_format.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
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
    Expect(ClassifyFile("notes.txt") == FileKind::unknown,
           "unknown extension classification");
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
    std::filesystem::remove(path, ignored);
}

void AppendU32Le(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void WriteU32Le(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xFFU);
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
    TestInspection();
    TestDdsMetadata();
    TestFpkIndex();
    TestFpkExtraction();
    if (failures != 0)
        std::cerr << failures << " test(s) failed.\n";
    return failures == 0 ? 0 : 1;
}
