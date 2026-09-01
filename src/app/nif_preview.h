#pragma once

#include "nif_document.h"
#include "nif_model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace rerevved::studio
{

enum class NifProjection
{
    xy,
    xz,
    yz,
};

enum class NifProjectionMode
{
    automatic,
    xy,
    xz,
    yz,
};

enum class NifPreviewError
{
    non_finite_position,
    unrepresentable_transformed_position,
    empty_projected_bounds,
    degenerate_projection,
    unavailable_region,
};

struct NifPreviewMeshView
{
    std::span<const std::array<float, 3>>         positions;
    std::span<const std::array<std::uint16_t, 3>> triangles;
    NifModelTransform                             transform;
};

struct NifWireframeLayout
{
    NifProjection         projection = NifProjection::xy;
    double                scale      = 1.0;
    std::array<double, 2> source_center{};
    std::array<double, 2> target_center{};
};

struct NifPreviewState
{
    std::size_t          selected_mesh = 0;
    NifProjectionMode    projection    = NifProjectionMode::automatic;
    std::array<float, 2> pan{};
    double               zoom = 1.0;
};

struct NifPreviewControlsLayout
{
    float mesh_width;
    float projection_width;
    float reset_width;
    bool  reset_same_line;
};

[[nodiscard]] std::string_view NifProjectionName(NifProjection projection);
[[nodiscard]] std::string_view NifProjectionModeName(NifProjectionMode projection);
[[nodiscard]] std::string_view NifPreviewErrorMessage(NifPreviewError error);

void               ResetNifPreviewView(NifPreviewState& state);
[[nodiscard]] bool SelectNifPreviewMesh(NifPreviewState& state,
                                        std::size_t      mesh_count,
                                        std::size_t      requested_mesh);
void               SelectNifPreviewProjection(NifPreviewState&  state,
                                              NifProjectionMode projection);
void               PanNifPreview(NifPreviewState& state, float horizontal, float vertical);
void               ZoomNifPreview(NifPreviewState& state, float wheel_delta);

[[nodiscard]] NifPreviewControlsLayout CalculateNifPreviewControlsLayout(
    float available_width,
    float item_spacing,
    float reset_natural_width);

[[nodiscard]] std::expected<NifWireframeLayout, NifPreviewError>
CalculateNifWireframeLayout(
    std::span<const std::array<float, 3>>         positions,
    std::span<const std::array<std::uint16_t, 3>> triangles,
    float                                         region_width,
    float                                         region_height,
    NifProjectionMode                             projection = NifProjectionMode::automatic);

[[nodiscard]] std::expected<NifWireframeLayout, NifPreviewError>
CalculateNifWireframeLayout(
    std::span<const NifPreviewMeshView> meshes,
    float                               region_width,
    float                               region_height,
    NifProjectionMode                   projection = NifProjectionMode::automatic);

[[nodiscard]] std::expected<std::array<double, 3>, NifPreviewError>
TransformNifPreviewPosition(const NifPreviewMeshView&   mesh,
                            const std::array<float, 3>& position);

[[nodiscard]] NifWireframeLayout
ApplyNifPreviewView(const NifWireframeLayout& layout, const NifPreviewState& state);

[[nodiscard]] std::array<float, 2>
ProjectNifPosition(const NifWireframeLayout& layout, const std::array<float, 3>& position);

[[nodiscard]] std::array<float, 2>
ProjectNifPosition(const NifWireframeLayout& layout, const std::array<double, 3>& position);

void DrawNifPreview(const NifDocument*           document,
                    const NifModel*              model,
                    std::optional<NifModelError> assembly_error,
                    std::string_view             document_error,
                    NifPreviewState&             state);

} // namespace rerevved::studio
