#include "application.h"
#include "inspection_format.h"

#include <imgui.h>

#include <algorithm>

namespace rerevved::studio
{

void Application::OpenPath(const std::filesystem::path& path)
{
    auto inspection = InspectFile(path);
    if (!inspection)
    {
        status_ = inspection.error();
        return;
    }

    const auto existing = std::ranges::find_if(inspections_, [&](const auto& item)
                                               {
                                                   return item.path == inspection->path;
                                               });
    if (existing != inspections_.end())
    {
        selected_ = static_cast<std::size_t>(existing - inspections_.begin());
        status_   = "Selected " + inspection->path.filename().string() + ".";
        return;
    }

    inspections_.push_back(std::move(*inspection));
    selected_ = inspections_.size() - 1;
    status_   = "Opened " + inspections_.back().path.filename().string() + ".";
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
    DrawStatusBar();
    DrawOpenPopup();
    DrawAboutPopup();
}

bool Application::ShouldClose() const
{
    return should_close_;
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
        request_open_popup_ = false;
    }

    if (!ImGui::BeginPopupModal("Open path", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Enter a path to a user-owned game file.");
    ImGui::SetNextItemWidth(600.0F);
    const bool submitted = ImGui::InputText(
        "##path", path_buffer_.data(), path_buffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue);

    if (submitted || ImGui::Button("Open"))
    {
        OpenPath(std::filesystem::path(path_buffer_.data()));
        path_buffer_.fill('\0');
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void Application::DrawAssetBrowser()
{
    ImGui::Begin("Assets");
    if (inspections_.empty())
    {
        ImGui::TextWrapped(
            "No files are open. Drop an FPK, DDS, GFX, BIK, or MP3 file here, "
            "or use File -> Open path.");
        ImGui::End();
        return;
    }

    for (std::size_t index = 0; index < inspections_.size(); ++index)
    {
        const bool is_selected = selected_ && *selected_ == index;
        if (ImGui::Selectable(inspections_[index].path.filename().string().c_str(),
                              is_selected))
            selected_ = index;
    }
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

    const auto& inspection = inspections_[*selected_];
    ImGui::Text("Name: %s", inspection.path.filename().string().c_str());
    ImGui::Text("Kind: %s", FileKindName(inspection.kind).data());
    ImGui::Text("Size: %s", FormatByteSize(inspection.size).c_str());
    ImGui::SeparatorText("Header");
    ImGui::TextUnformatted(FormatHeader(inspection).c_str());
    ImGui::SeparatorText("Path");
    ImGui::TextWrapped("%s", inspection.path.string().c_str());
    ImGui::End();
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
