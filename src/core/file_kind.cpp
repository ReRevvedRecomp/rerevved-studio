#include "file_kind.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace rerevved::studio
{
namespace
{

std::string LowercaseExtension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value)
                           {
                               return static_cast<char>(std::tolower(value));
                           });
    return extension;
}

} // namespace

FileKind ClassifyFile(const std::filesystem::path& path)
{
    const auto extension = LowercaseExtension(path);
    if (extension == ".fpk")
        return FileKind::fpk_archive;
    if (extension == ".dds")
        return FileKind::dds_texture;
    if (extension == ".gfx")
        return FileKind::gfx_movie;
    if (extension == ".bik")
        return FileKind::bik_video;
    if (extension == ".mp3")
        return FileKind::mp3_audio;
    if (extension == ".nif")
        return FileKind::nif_container;
    if (extension == ".map")
        return FileKind::map_record;
    return FileKind::unknown;
}

std::string_view FileKindName(FileKind kind)
{
    switch (kind)
    {
        case FileKind::fpk_archive:
            return "FPK archive";
        case FileKind::dds_texture:
            return "DDS texture";
        case FileKind::gfx_movie:
            return "GFx movie";
        case FileKind::bik_video:
            return "Bink video";
        case FileKind::mp3_audio:
            return "MP3 audio";
        case FileKind::nif_container:
            return "Gamebryo NIF";
        case FileKind::map_record:
            return "Xbox DLC map";
        case FileKind::unknown:
            return "Unknown";
    }
    return "Unknown";
}

} // namespace rerevved::studio
