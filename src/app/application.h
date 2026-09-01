#pragma once

#include "file_inspection.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rerevved::studio
{

class Application
{
public:
    void OpenPath(const std::filesystem::path& path);
    void Draw();

    [[nodiscard]] bool ShouldClose() const;

private:
    void DrawMainMenu();
    void DrawOpenPopup();
    void DrawAssetBrowser();
    void DrawInspector();
    void DrawStatusBar();
    void DrawAboutPopup();

    std::vector<FileInspection> inspections_;
    std::optional<std::size_t>  selected_;
    std::array<char, 2048>      path_buffer_{};
    std::string                 status_              = "Drop a supported file into the window to begin.";
    bool                        request_open_popup_  = false;
    bool                        request_about_popup_ = false;
    bool                        should_close_        = false;
};

} // namespace rerevved::studio
