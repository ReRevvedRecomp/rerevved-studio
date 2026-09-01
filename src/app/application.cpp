#include "application.h"
#include "archive_explorer.h"
#include "gfx_inspector.h"
#include "inspection_format.h"
#include "map_preview.h"
#include "mp3_preview.h"
#include "nif_inspector.h"
#include "nif_preview.h"

#include <imgui.h>

#include <algorithm>

namespace rerevved::studio
{

bool Application::OpenPath(const std::filesystem::path& path)
{
    auto inspection = InspectFile(path);
    if (!inspection)
    {
        status_ = inspection.error();
        return false;
    }

    const auto existing = std::ranges::find_if(inspections_, [&](const auto& item)
                                               {
                                                   return item.inspection.path == inspection->path;
                                               });
    if (existing != inspections_.end())
    {
        selected_ = static_cast<std::size_t>(existing - inspections_.begin());
        status_   = "Selected " + inspection->path.filename().string() + ".";
        return true;
    }

    AssetDocument document{
        .inspection      = std::move(*inspection),
        .fpk             = std::nullopt,
        .archive         = {},
        .dds             = std::nullopt,
        .gfx             = std::nullopt,
        .map             = std::nullopt,
        .mp3             = std::nullopt,
        .nif             = std::nullopt,
        .nif_model       = std::nullopt,
        .nif_model_error = std::nullopt,
        .nif_preview     = {},
        .document_error  = {},
    };
    if (document.inspection.kind == FileKind::fpk_archive)
    {
        auto fpk = LoadFpkDocument(document.inspection.path);
        if (fpk)
        {
            document.fpk = std::move(*fpk);
            SelectInitialArchiveEntry(document.archive, *document.fpk);
        }
        else
            document.document_error = std::move(fpk.error());
    }
    else if (document.inspection.kind == FileKind::dds_texture)
    {
        auto dds = LoadDdsDocument(document.inspection.path);
        if (dds)
            document.dds = std::move(*dds);
        else
            document.document_error = std::move(dds.error());
    }
    else if (document.inspection.kind == FileKind::gfx_movie)
    {
        auto gfx = LoadGfxDocument(document.inspection.path);
        if (gfx)
            document.gfx = std::move(*gfx);
        else
            document.document_error = std::move(gfx.error());
    }
    else if (document.inspection.kind == FileKind::mp3_audio)
    {
        auto mp3 = LoadMp3Document(document.inspection.path);
        if (mp3)
            document.mp3 = std::move(*mp3);
        else
            document.document_error = std::move(mp3.error());
    }
    else if (document.inspection.kind == FileKind::map_record)
    {
        auto map = LoadMapDocument(document.inspection.path);
        if (map)
            document.map = std::move(*map);
        else
            document.document_error = std::move(map.error());
    }
    else if (document.inspection.kind == FileKind::nif_container)
    {
        auto nif = LoadNifDocument(document.inspection.path);
        if (nif)
        {
            document.nif = std::move(*nif);
            auto model   = AssembleNifModel(*document.nif);
            if (model)
                document.nif_model = std::move(*model);
            else
                document.nif_model_error = model.error();
        }
        else
            document.document_error = std::move(nif.error());
    }
    inspections_.push_back(std::move(document));
    selected_ = inspections_.size() - 1;
    status_   = "Opened " + inspections_.back().inspection.path.filename().string() + ".";
    return true;
}

void Application::Draw()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::Begin("ReRevved Studio Dockspace", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    DrawMainMenu();
    const ImGuiID dockspace = ImGui::GetID("ReRevvedStudioDockspace");
    ImGui::DockSpace(dockspace, ImVec2(0.0F, 0.0F), ImGuiDockNodeFlags_None);
    ImGui::End();

    ImGui::SetNextWindowDockID(dockspace, ImGuiCond_FirstUseEver);
    DrawAssetBrowser();
    ImGui::SetNextWindowDockID(dockspace, ImGuiCond_FirstUseEver);
    DrawInspector();
    ImGui::SetNextWindowDockID(dockspace, ImGuiCond_FirstUseEver);
    DrawPreview();
    DrawStatusBar();
    DrawOpenPopup();
    DrawAboutPopup();
}

bool Application::ShouldClose() const
{
    return should_close_;
}

void Application::CloseSelectedAsset()
{
    auto next_selection = selected_;
    if (!UpdateSelectionAfterAssetClose(inspections_.size(), next_selection))
        return;

    const auto closed_index = *selected_;
    const auto filename     = inspections_[closed_index].inspection.path.filename().string();

    dds_viewer_.Clear();
    inspections_.erase(inspections_.begin() + static_cast<std::ptrdiff_t>(closed_index));
    selected_ = next_selection;
    status_   = "Closed " + filename + ".";
}

void Application::DrawMainMenu()
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open path..."))
            request_open_popup_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            should_close_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About"))
            request_about_popup_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void Application::DrawOpenPopup()
{
    if (request_open_popup_)
    {
        ImGui::OpenPopup("Open path");
        open_error_.clear();
        request_open_popup_ = false;
    }

    if (!ImGui::BeginPopupModal("Open path", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const auto& style = ImGui::GetStyle();
    const auto  input_width =
        std::max(1.0F,
                 std::min(600.0F,
                          ImGui::GetMainViewport()->WorkSize.x - style.WindowPadding.x * 2.0F));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + input_width);
    ImGui::TextUnformatted("Enter a path to a user-owned game file.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(input_width);
    const bool submitted = ImGui::InputText(
        "##path", path_buffer_.data(), path_buffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue);

    const auto button_width =
        std::min(120.0F, std::max(1.0F, (input_width - style.ItemSpacing.x) / 2.0F));
    if (submitted || ImGui::Button("Open", ImVec2(button_width, 0.0F)))
    {
        if (OpenPath(std::filesystem::path(path_buffer_.data())))
        {
            path_buffer_.fill('\0');
            open_error_.clear();
            ImGui::CloseCurrentPopup();
        }
        else
        {
            open_error_ = status_;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(button_width, 0.0F)))
    {
        open_error_.clear();
        ImGui::CloseCurrentPopup();
    }
    if (!open_error_.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + input_width);
        ImGui::TextUnformatted(open_error_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

void Application::DrawAssetBrowser()
{
    ImGui::Begin("Assets");
    if (inspections_.empty())
    {
        ImGui::TextWrapped(
            "No files are open. Drop an FPK, DDS, GFX, BIK, MP3, NIF, or MAP file here, "
            "or use File -> Open path.");
        ImGui::End();
        return;
    }

    for (std::size_t index = 0; index < inspections_.size(); ++index)
    {
        const auto source_path = inspections_[index].inspection.path.string();
        const auto label       = inspections_[index].inspection.path.filename().string() + "##" +
                                 source_path;
        const bool is_selected = selected_ && *selected_ == index;
        if (ImGui::Selectable(label.c_str(), is_selected))
            selected_ = index;
        ImGui::TextWrapped("%s", source_path.c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Close selected"))
        CloseSelectedAsset();
    ImGui::End();
}

void Application::DrawInspector()
{
    ImGui::Begin("Inspector");
    if (!selected_)
    {
        ImGui::TextUnformatted("Select an asset to inspect it.");
        ImGui::End();
        return;
    }

    auto&       document   = inspections_[*selected_];
    const auto& inspection = document.inspection;
    ImGui::Text("Name: %s", inspection.path.filename().string().c_str());
    ImGui::Text("Kind: %s", FileKindName(inspection.kind).data());
    ImGui::Text("Size: %s", FormatByteSize(inspection.size).c_str());
    ImGui::SeparatorText("Header");
    ImGui::TextUnformatted(FormatHeader(inspection).c_str());
    ImGui::SeparatorText("Path");
    ImGui::TextWrapped("%s", inspection.path.string().c_str());
    if (inspection.kind == FileKind::gfx_movie)
        DrawGfxInspector(document.gfx ? &*document.gfx : nullptr, document.document_error);
    if (inspection.kind == FileKind::nif_container)
        DrawNifInspector(document.nif ? &*document.nif : nullptr, document.document_error);
    if (inspection.kind == FileKind::fpk_archive)
        DrawArchiveInspector(document.fpk ? &*document.fpk : nullptr,
                             document.archive,
                             document.document_error);
    ImGui::End();
}

void Application::DrawPreview()
{
    if (!selected_)
    {
        dds_viewer_.Draw(nullptr, {}, "Select a DDS asset to preview it.");
        return;
    }

    auto& document = inspections_[*selected_];
    if (document.inspection.kind == FileKind::fpk_archive)
    {
        DrawArchivePreview(document.inspection.path, document.archive, dds_viewer_);
        return;
    }
    if (document.inspection.kind == FileKind::map_record)
    {
        dds_viewer_.Clear();
        DrawMapPreview(document.map ? &*document.map : nullptr, document.document_error);
        return;
    }
    if (document.inspection.kind == FileKind::mp3_audio)
    {
        dds_viewer_.Clear();
        DrawMp3Preview(document.mp3 ? &*document.mp3 : nullptr, document.document_error);
        return;
    }
    if (document.inspection.kind == FileKind::nif_container)
    {
        dds_viewer_.Clear();
        DrawNifPreview(document.nif ? &*document.nif : nullptr,
                       document.nif_model ? &*document.nif_model : nullptr,
                       document.nif_model_error,
                       document.document_error,
                       document.nif_preview);
        return;
    }
    if (document.inspection.kind != FileKind::dds_texture)
    {
        dds_viewer_.Draw(nullptr, {}, "The selected asset has no preview.");
        return;
    }
    dds_viewer_.Draw(document.dds ? &*document.dds : nullptr,
                     document.inspection.path.string(),
                     document.document_error);
}

void Application::DrawStatusBar()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float      height   = 26.0F;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Status", nullptr, flags);
    ImGui::TextUnformatted(status_.c_str());
    ImGui::End();
}

void Application::DrawAboutPopup()
{
    if (request_about_popup_)
    {
        ImGui::OpenPopup("About ReRevved Studio");
        request_about_popup_ = false;
    }

    if (!ImGui::BeginPopupModal(
            "About ReRevved Studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("ReRevved Studio %s", REREVVED_STUDIO_VERSION);
    ImGui::TextUnformatted("A read-only asset inspection companion for ReRevved.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "This program is free software under GPLv3 and comes with absolutely "
        "no warranty. Retail game content is not included.");
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace rerevved::studio
