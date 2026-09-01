#include "fpk_index.h"

#include <algorithm>
#include <array>
#include <limits>

namespace rerevved::studio
{
namespace
{

constexpr std::size_t              kHeaderSize        = 14;
constexpr std::size_t              kMinimumRecordSize = 20;
constexpr std::array<std::byte, 4> kSignature{
    std::byte{ 'F' }, std::byte{ 'P' }, std::byte{ 'K' }, std::byte{ '_' }
};

std::uint32_t ReadU32Le(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

bool CheckedMultiply(std::size_t left, std::size_t right, std::size_t& result)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

} // namespace

std::expected<FpkIndex, FpkIndexError>
ParseFpkIndex(std::span<const std::byte> bytes)
{
    if (bytes.size() < kHeaderSize)
        return std::unexpected(FpkIndexError::truncated);

    const auto signature = bytes.subspan<4, 4>();
    if (!std::ranges::equal(signature, kSignature))
        return std::unexpected(FpkIndexError::invalid_signature);

    FpkIndex index{};
    index.version        = ReadU32Le(bytes, 0);
    index.header_unknown = { bytes[8], bytes[9] };
    if (index.version != 6)
        return std::unexpected(FpkIndexError::unsupported_version);

    const auto  entry_count        = static_cast<std::size_t>(ReadU32Le(bytes, 10));
    std::size_t minimum_table_size = 0;
    if (!CheckedMultiply(entry_count, kMinimumRecordSize, minimum_table_size))
        return std::unexpected(FpkIndexError::count_overflow);
    if (minimum_table_size > bytes.size() - kHeaderSize)
        return std::unexpected(FpkIndexError::truncated);

    index.entries.reserve(entry_count);
    std::size_t cursor = kHeaderSize;
    for (std::size_t entry_number = 0; entry_number < entry_count; ++entry_number)
    {
        if (bytes.size() - cursor < sizeof(std::uint32_t))
            return std::unexpected(FpkIndexError::truncated);
        const auto variable_data_size = static_cast<std::size_t>(ReadU32Le(bytes, cursor));
        cursor += sizeof(std::uint32_t);

        const auto aligned_size = (variable_data_size + 3) & ~std::size_t{ 3 };
        if (aligned_size > bytes.size() - cursor)
            return std::unexpected(FpkIndexError::truncated);
        cursor += aligned_size;
        if (bytes.size() - cursor < 16)
            return std::unexpected(FpkIndexError::truncated);

        FpkEntry entry{};
        std::ranges::copy(bytes.subspan(cursor, 4), entry.unknown0.begin());
        cursor += 4;
        std::ranges::copy(bytes.subspan(cursor, 4), entry.unknown1.begin());
        cursor += 4;
        entry.size = ReadU32Le(bytes, cursor);
        cursor += 4;
        entry.offset = ReadU32Le(bytes, cursor);
        cursor += 4;

        if (entry.size > std::numeric_limits<std::uint32_t>::max() - entry.offset)
            return std::unexpected(FpkIndexError::entry_range_overflow);
        const auto offset = static_cast<std::size_t>(entry.offset);
        const auto size   = static_cast<std::size_t>(entry.size);
        if (offset > bytes.size() || size > bytes.size() - offset)
            return std::unexpected(FpkIndexError::entry_out_of_range);
        index.entries.push_back(entry);
    }

    if (std::ranges::any_of(index.entries,
                            [cursor](const FpkEntry& entry)
                            {
                                return entry.offset < cursor;
                            }))
    {
        return std::unexpected(FpkIndexError::entry_out_of_range);
    }
    return index;
}

} // namespace rerevved::studio
