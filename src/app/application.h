#pragma once

#include "archive_explorer.h"
#include "dds_document.h"
#include "dds_viewer.h"
#include "file_inspection.h"
#include "fpk_document.h"
#include "gfx_document.h"
#include "map_document.h"
#include "mp3_document.h"
#include "nif_document.h"
#include "nif_model.h"
#include "nif_preview.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rerevved::studio
{

[[nodiscard]] constexpr bool
UpdateSelectionAfterAssetClose(std::size_t asset_count, std::optional<std::size_t>& selection)
{
    if (!selection || *selection >= asset_count)
        return false;
    if (asset_count == 1)
        selection.reset();
    else if (*selection == asset_count - 1)
        --*selection;
    return true;
}

struct AssetDocument
{
    FileInspection               inspection;
    std::optional<FpkDocument>   fpk;
    ArchiveExplorerState         archive;
    std::optional<DdsDocument>   dds;
    std::optional<GfxDocument>   gfx;
    std::optional<MapDocument>   map;
    std::optional<Mp3Document>   mp3;
    std::optional<NifDocument>   nif;
    std::optional<NifModel>      nif_model;
    std::optional<NifModelError> nif_model_error;
    NifPreviewState              nif_preview;
    std::string                  document_error;
};

class Application
{
public:
    [[nodiscard]] bool OpenPath(const std::filesystem::path& path);
    void               Draw();

    [[nodiscard]] bool ShouldClose() const;

private:
    void CloseSelectedAsset();
    void DrawMainMenu();
    void DrawOpenPopup();
    void DrawAssetBrowser();
    void DrawInspector();
    void DrawPreview();
    void DrawStatusBar();
    void DrawAboutPopup();

    std::vector<AssetDocument> inspections_;
    std::optional<std::size_t> selected_;
    DdsViewer                  dds_viewer_;
    std::array<char, 2048>     path_buffer_{};
    std::string                open_error_;
    std::string                status_              = "Drop a supported file into the window to begin.";
    bool                       request_open_popup_  = false;
    bool                       request_about_popup_ = false;
    bool                       should_close_        = false;
};

} // namespace rerevved::studio
