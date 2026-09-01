#include "nif_preview.h"

#include <imgui.h>

#include <array>
#include <string>
#include <vector>

namespace rerevved::studio
{
namespace
{

std::string FormatMeshLabel(const NifDocument&  document,
                            const NifModelMesh& mesh,
                            std::size_t         mesh_index)
{
    const auto& shape = document.tri_shapes[mesh.tri_shape_index];
    const auto& data  = document.tri_shape_data[mesh.tri_shape_data_index];
    return "Mesh " + std::to_string(mesh_index + 1) + " - shape block " +
           std::to_string(shape.block_index) + " - data block " +
           std::to_string(data.block_index);
}

} // namespace

void DrawNifPreview(const NifDocument*           document,
                    const NifModel*              model,
                    std::optional<NifModelError> assembly_error,
                    std::string_view             document_error,
                    NifPreviewState&             state)
{
    ImGui::Begin("Preview");
    if (document == nullptr)
    {
        ImGui::TextWrapped("%.*s",
                           static_cast<int>(document_error.size()),
                           document_error.data());
        ImGui::End();
        return;
    }
    if (assembly_error)
    {
        const auto message = NifModelErrorMessage(*assembly_error);
        ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
        ImGui::End();
        return;
    }

    const auto selection_count = model->meshes.size() + 1;
    (void)SelectNifPreviewMesh(state, selection_count, state.selected_mesh);
    const bool all_meshes         = state.selected_mesh == model->meshes.size();
    const auto current_mesh_label = all_meshes
                                        ? std::string("All meshes")
                                        : FormatMeshLabel(*document,
                                                          model->meshes[state.selected_mesh],
                                                          state.selected_mesh);
    const auto available_width    = ImGui::GetContentRegionAvail().x;
    const auto controls           = CalculateNifPreviewControlsLayout(
        available_width,
        ImGui::GetStyle().ItemSpacing.x,
        ImGui::CalcTextSize("Fit/Reset").x + ImGui::GetStyle().FramePadding.x * 2.0F);

    ImGui::TextUnformatted("Mesh");
    ImGui::SetNextItemWidth(controls.mesh_width);
    if (ImGui::BeginCombo("##nif-mesh", current_mesh_label.c_str()))
    {
        for (std::size_t index = 0; index < model->meshes.size(); ++index)
        {
            const auto label    = FormatMeshLabel(*document, model->meshes[index], index);
            const bool selected = index == state.selected_mesh;
            if (ImGui::Selectable(label.c_str(), selected))
                (void)SelectNifPreviewMesh(state, selection_count, index);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        const bool all_selected = state.selected_mesh == model->meshes.size();
        if (ImGui::Selectable("All meshes", all_selected))
            (void)SelectNifPreviewMesh(state, selection_count, model->meshes.size());
        if (all_selected)
            ImGui::SetItemDefaultFocus();
        ImGui::EndCombo();
    }

    ImGui::TextUnformatted("Projection");
    ImGui::SetNextItemWidth(controls.projection_width);
    if (ImGui::BeginCombo("##nif-projection",
                          NifProjectionModeName(state.projection).data()))
    {
        constexpr std::array modes{
            NifProjectionMode::automatic,
            NifProjectionMode::xy,
            NifProjectionMode::xz,
            NifProjectionMode::yz,
        };
        for (const auto mode : modes)
        {
            const bool selected = mode == state.projection;
            if (ImGui::Selectable(NifProjectionModeName(mode).data(), selected))
                SelectNifPreviewProjection(state, mode);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (controls.reset_same_line)
        ImGui::SameLine();
    if (ImGui::Button("Fit/Reset", { controls.reset_width, 0.0F }))
        ResetNifPreviewView(state);

    const bool                      draw_all_meshes = state.selected_mesh == model->meshes.size();
    std::vector<NifPreviewMeshView> mesh_views;
    mesh_views.reserve(draw_all_meshes ? model->meshes.size() : 1);
    const auto append_mesh_view = [&](std::size_t mesh_index)
    {
        const auto& mesh     = model->meshes[mesh_index];
        const auto& geometry = document->tri_shape_data[mesh.tri_shape_data_index];
        mesh_views.push_back({ geometry.vertex_positions,
                               geometry.triangles,
                               mesh.world_transform });
    };
    if (draw_all_meshes)
    {
        for (std::size_t mesh_index = 0; mesh_index < model->meshes.size(); ++mesh_index)
            append_mesh_view(mesh_index);
        ImGui::Text("Meshes: %zu", model->meshes.size());
    }
    else
    {
        append_mesh_view(state.selected_mesh);
        const auto& mesh     = model->meshes[state.selected_mesh];
        const auto& shape    = document->tri_shapes[mesh.tri_shape_index];
        const auto& geometry = document->tri_shape_data[mesh.tri_shape_data_index];
        ImGui::Text("Mesh: %zu of %zu", state.selected_mesh + 1, model->meshes.size());
        ImGui::Text("Vertices: %zu", geometry.vertex_positions.size());
        ImGui::Text("Triangles: %zu", geometry.triangles.size());
        ImGui::Text("Shape block: %u", static_cast<unsigned int>(shape.block_index));
        ImGui::Text("Data block: %u", static_cast<unsigned int>(geometry.block_index));
    }

    const auto   remaining = ImGui::GetContentRegionAvail();
    const ImVec2 canvas_size{
        remaining.x,
        remaining.y - ImGui::GetTextLineHeightWithSpacing(),
    };
    const auto fitted_layout = CalculateNifWireframeLayout(mesh_views,
                                                           canvas_size.x,
                                                           canvas_size.y,
                                                           state.projection);
    if (!fitted_layout)
    {
        const auto message = NifPreviewErrorMessage(fitted_layout.error());
        ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(NifProjectionName(fitted_layout->projection).data());
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##nif-wireframe",
                           canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemHovered())
    {
        const auto& input = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            PanNifPreview(state, input.MouseDelta.x, input.MouseDelta.y);
        if (input.MouseWheel != 0.0F)
            ZoomNifPreview(state, input.MouseWheel);
    }

    const auto viewed_layout = ApplyNifPreviewView(*fitted_layout, state);
    auto*      draw_list     = ImGui::GetWindowDrawList();
    const auto color         = ImGui::GetColorU32(ImGuiCol_Text);
    draw_list->PushClipRect(origin,
                            { origin.x + canvas_size.x, origin.y + canvas_size.y },
                            true);
    for (const auto& mesh_view : mesh_views)
    {
        for (const auto& triangle : mesh_view.triangles)
        {
            const auto first = ProjectNifPosition(
                viewed_layout,
                *TransformNifPreviewPosition(mesh_view,
                                             mesh_view.positions[triangle[0]]));
            const auto second = ProjectNifPosition(
                viewed_layout,
                *TransformNifPreviewPosition(mesh_view,
                                             mesh_view.positions[triangle[1]]));
            const auto third = ProjectNifPosition(
                viewed_layout,
                *TransformNifPreviewPosition(mesh_view,
                                             mesh_view.positions[triangle[2]]));
            const ImVec2 first_point{ origin.x + first[0], origin.y + first[1] };
            const ImVec2 second_point{ origin.x + second[0], origin.y + second[1] };
            const ImVec2 third_point{ origin.x + third[0], origin.y + third[1] };
            draw_list->AddLine(first_point, second_point, color);
            draw_list->AddLine(second_point, third_point, color);
            draw_list->AddLine(third_point, first_point, color);
        }
    }
    draw_list->PopClipRect();
    ImGui::End();
}

} // namespace rerevved::studio
