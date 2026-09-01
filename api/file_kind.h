#pragma once

#include <filesystem>
#include <string_view>

namespace rerevved::studio
{

enum class FileKind
{
    unknown,
    fpk_archive,
    dds_texture,
    gfx_movie,
    bik_video,
    mp3_audio,
    nif_container,
    map_record,
};

[[nodiscard]] FileKind         ClassifyFile(const std::filesystem::path& path);
[[nodiscard]] std::string_view FileKindName(FileKind kind);

} // namespace rerevved::studio
