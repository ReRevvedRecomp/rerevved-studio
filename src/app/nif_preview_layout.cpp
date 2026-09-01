#include "nif_preview.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace rerevved::studio
{
namespace
{

std::array<std::size_t, 2> ProjectionAxes(NifProjection projection)
{
    switch (projection)
    {
        case NifProjection::xy:
            return { 0, 1 };
        case NifProjection::xz:
            return { 0, 2 };
        case NifProjection::yz:
            return { 1, 2 };
    }
    return { 0, 1 };
}

} // namespace

std::string_view NifProjectionName(NifProjection projection)
{
    switch (projection)
    {
        case NifProjection::xy:
            return "Raw XY projection";
        case NifProjection::xz:
            return "Raw XZ projection";
        case NifProjection::yz:
            return "Raw YZ projection";
    }
    return "Raw projection";
}

std::string_view NifProjectionModeName(NifProjectionMode projection)
{
    switch (projection)
    {
        case NifProjectionMode::automatic:
            return "Auto";
        case NifProjectionMode::xy:
            return "XY";
        case NifProjectionMode::xz:
            return "XZ";
        case NifProjectionMode::yz:
            return "YZ";
    }
    return "Auto";
}

std::string_view NifPreviewErrorMessage(NifPreviewError error)
{
    switch (error)
    {
        case NifPreviewError::non_finite_position:
            return "The NIF mesh contains a non-finite vertex position.";
        case NifPreviewError::unrepresentable_transformed_position:
            return "A transformed NIF vertex position is non-finite or unrepresentable.";
        case NifPreviewError::empty_projected_bounds:
            return "The NIF mesh has no projected bounds to display.";
        case NifPreviewError::degenerate_projection:
            return "The NIF mesh has a degenerate raw-axis projection.";
        case NifPreviewError::unavailable_region:
            return "The NIF Preview region has no drawable area.";
    }
    return "The NIF mesh could not be previewed.";
}

void ResetNifPreviewView(NifPreviewState& state)
{
    state.pan  = {};
    state.zoom = 1.0;
}

bool SelectNifPreviewMesh(NifPreviewState& state,
                          std::size_t      mesh_count,
                          std::size_t      requested_mesh)
{
    const bool valid    = requested_mesh < mesh_count;
    const auto selected = valid ? requested_mesh : std::size_t{ 0 };
    if (!valid || state.selected_mesh != selected)
    {
        state.selected_mesh = selected;
        ResetNifPreviewView(state);
    }
    return valid;
}

void SelectNifPreviewProjection(NifPreviewState& state, NifProjectionMode projection)
{
    if (state.projection == projection)
        return;
    state.projection = projection;
    ResetNifPreviewView(state);
}

void PanNifPreview(NifPreviewState& state, float horizontal, float vertical)
{
    state.pan[0] += horizontal;
    state.pan[1] += vertical;
}

void ZoomNifPreview(NifPreviewState& state, float wheel_delta)
{
    constexpr double minimum_zoom = 0.1;
    constexpr double maximum_zoom = 20.0;
    state.zoom                    = std::clamp(state.zoom * std::pow(1.2, wheel_delta),
                                               minimum_zoom,
                                               maximum_zoom);
}

NifPreviewControlsLayout CalculateNifPreviewControlsLayout(
    float available_width,
    float item_spacing,
    float reset_natural_width)
{
    const auto width            = std::max(1.0F, available_width);
    const auto mesh_width       = std::min(320.0F, width);
    const auto projection_width = std::min(140.0F, width);
    const auto natural_reset    = std::max(1.0F, reset_natural_width);
    const bool same_line        = projection_width + item_spacing + natural_reset <= width;
    return {
        mesh_width,
        projection_width,
        same_line ? natural_reset : std::min(natural_reset, width),
        same_line,
    };
}

std::expected<NifWireframeLayout, NifPreviewError> CalculateNifWireframeLayout(
    std::span<const std::array<float, 3>>         positions,
    std::span<const std::array<std::uint16_t, 3>> triangles,
    float                                         region_width,
    float                                         region_height,
    NifProjectionMode                             requested_projection)
{
    const std::array meshes{ NifPreviewMeshView{ positions, triangles, {} } };
    return CalculateNifWireframeLayout(
        meshes, region_width, region_height, requested_projection);
}

std::expected<std::array<double, 3>, NifPreviewError>
TransformNifPreviewPosition(const NifPreviewMeshView&   mesh,
                            const std::array<float, 3>& position)
{
    if (!std::ranges::all_of(position, [](float value)
                             {
                                 return std::isfinite(value);
                             }))
        return std::unexpected(NifPreviewError::non_finite_position);
    const auto transformed = ApplyNifModelTransform(mesh.transform, position);
    if (!transformed)
        return std::unexpected(NifPreviewError::unrepresentable_transformed_position);
    return *transformed;
}

std::expected<NifWireframeLayout, NifPreviewError> CalculateNifWireframeLayout(
    std::span<const NifPreviewMeshView> meshes,
    float                               region_width,
    float                               region_height,
    NifProjectionMode                   requested_projection)
{
    if (meshes.empty() || std::ranges::any_of(meshes, [](const NifPreviewMeshView& mesh)
                                              {
                                                  return mesh.positions.empty() ||
                                                         mesh.triangles.empty();
                                              }))
        return std::unexpected(NifPreviewError::empty_projected_bounds);
    if (!std::isfinite(region_width) || !std::isfinite(region_height) ||
        region_width <= 0.0F || region_height <= 0.0F)
        return std::unexpected(NifPreviewError::unavailable_region);

    std::array<double, 3> minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    std::array<double, 3> maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (const auto& mesh : meshes)
    {
        for (const auto& position : mesh.positions)
        {
            const auto transformed = TransformNifPreviewPosition(mesh, position);
            if (!transformed)
                return std::unexpected(transformed.error());
            for (std::size_t axis = 0; axis < transformed->size(); ++axis)
            {
                minimum[axis] = std::min(minimum[axis], (*transformed)[axis]);
                maximum[axis] = std::max(maximum[axis], (*transformed)[axis]);
            }
        }
    }

    const std::array ranges{
        maximum[0] - minimum[0],
        maximum[1] - minimum[1],
        maximum[2] - minimum[2],
    };
    if (!std::ranges::all_of(ranges, [](double range)
                             {
                                 return std::isfinite(range);
                             }))
        return std::unexpected(NifPreviewError::unrepresentable_transformed_position);
    const std::array areas{
        ranges[0] * ranges[1],
        ranges[0] * ranges[2],
        ranges[1] * ranges[2],
    };
    if (!std::ranges::all_of(areas, [](double area)
                             {
                                 return std::isfinite(area);
                             }))
        return std::unexpected(NifPreviewError::unrepresentable_transformed_position);
    if (std::ranges::all_of(ranges,
                            [](double range)
                            {
                                return range == 0.0;
                            }))
        return std::unexpected(NifPreviewError::empty_projected_bounds);
    std::size_t projection_index = 0;
    if (requested_projection == NifProjectionMode::automatic)
    {
        for (std::size_t index = 1; index < areas.size(); ++index)
        {
            if (areas[index] > areas[projection_index])
                projection_index = index;
        }
    }
    else
        projection_index = static_cast<std::size_t>(requested_projection) - 1;
    constexpr std::array projections{
        NifProjection::xy,
        NifProjection::xz,
        NifProjection::yz,
    };
    const auto projection = projections[projection_index];
    const auto axes       = ProjectionAxes(projection);
    const auto width      = ranges[axes[0]];
    const auto height     = ranges[axes[1]];
    if (width <= 0.0 || height <= 0.0)
        return std::unexpected(NifPreviewError::degenerate_projection);

    bool has_area = false;
    for (const auto& mesh : meshes)
    {
        for (const auto& triangle : mesh.triangles)
        {
            const auto first = TransformNifPreviewPosition(
                mesh, mesh.positions[triangle[0]]);
            const auto second = TransformNifPreviewPosition(
                mesh, mesh.positions[triangle[1]]);
            const auto third = TransformNifPreviewPosition(
                mesh, mesh.positions[triangle[2]]);
            if (!first || !second || !third)
            {
                const auto error = !first ? first.error() : !second ? second.error()
                                                                    : third.error();
                return std::unexpected(error);
            }
            const auto twice_area =
                ((*second)[axes[0]] - (*first)[axes[0]]) *
                    ((*third)[axes[1]] - (*first)[axes[1]]) -
                ((*second)[axes[1]] - (*first)[axes[1]]) *
                    ((*third)[axes[0]] - (*first)[axes[0]]);
            if (!std::isfinite(twice_area))
                return std::unexpected(
                    NifPreviewError::unrepresentable_transformed_position);
            has_area = has_area || twice_area != 0.0;
        }
    }
    if (!has_area)
        return std::unexpected(NifPreviewError::degenerate_projection);

    const auto padding = std::min(
        16.0F, std::min(region_width, region_height) * 0.1F);
    const auto drawable_width  = region_width - padding * 2.0F;
    const auto drawable_height = region_height - padding * 2.0F;
    if (drawable_width <= 0.0F || drawable_height <= 0.0F)
        return std::unexpected(NifPreviewError::unavailable_region);

    NifWireframeLayout layout;
    layout.projection    = projection;
    layout.scale         = std::min(drawable_width / width, drawable_height / height);
    layout.source_center = {
        minimum[axes[0]] + ranges[axes[0]] * 0.5,
        minimum[axes[1]] + ranges[axes[1]] * 0.5,
    };
    layout.target_center = { region_width * 0.5F, region_height * 0.5F };
    if (!std::isfinite(layout.scale) || layout.scale <= 0.0 ||
        !std::ranges::all_of(layout.source_center, [](double value)
                             {
                                 return std::isfinite(value);
                             }) ||
        !std::ranges::all_of(layout.target_center, [](double value)
                             {
                                 return std::isfinite(value);
                             }))
        return std::unexpected(NifPreviewError::unrepresentable_transformed_position);
    return layout;
}

NifWireframeLayout ApplyNifPreviewView(const NifWireframeLayout& layout,
                                       const NifPreviewState&    state)
{
    auto result = layout;
    result.scale *= state.zoom;
    result.target_center[0] += state.pan[0];
    result.target_center[1] += state.pan[1];
    return result;
}

std::array<float, 2> ProjectNifPosition(const NifWireframeLayout&   layout,
                                        const std::array<float, 3>& position)
{
    return ProjectNifPosition(
        layout, std::array<double, 3>{ position[0], position[1], position[2] });
}

std::array<float, 2> ProjectNifPosition(const NifWireframeLayout&    layout,
                                        const std::array<double, 3>& position)
{
    const auto axes = ProjectionAxes(layout.projection);
    return {
        static_cast<float>(layout.target_center[0] +
                           (position[axes[0]] - layout.source_center[0]) * layout.scale),
        static_cast<float>(layout.target_center[1] -
                           (position[axes[1]] - layout.source_center[1]) * layout.scale),
    };
}

} // namespace rerevved::studio
