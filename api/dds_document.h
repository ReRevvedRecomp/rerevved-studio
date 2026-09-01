#pragma once

#include "dds_metadata.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rerevved::studio
{

enum class DdsPreviewError
{
    unsupported_format,
    invalid_dimensions,
    size_overflow,
    truncated_pixel_data,
};

struct DdsDocument
{
    DdsMetadata            metadata;
    std::vector<std::byte> rgba8;
};

using DdsDocumentError = std::variant<DdsMetadataError, DdsPreviewError>;

[[nodiscard]] std::string_view DdsDocumentErrorMessage(const DdsDocumentError& error);

[[nodiscard]] std::expected<DdsDocument, DdsDocumentError>
ParseDdsDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<DdsDocument, std::string>
LoadDdsDocument(const std::filesystem::path& path);

} // namespace rerevved::studio
