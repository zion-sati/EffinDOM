#include "UiRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace effindom::v2::ui {

std::uint64_t UiRuntime::FindSelectionAreaAncestor(std::uint64_t start_handle) const {
    return Selection().FindAreaAncestor(*this, start_handle);
}

void UiRuntime::CollectSelectionAreaNodes(std::uint64_t handle, std::vector<std::uint64_t>& out) const {
    SelectionCoordinator collector{};
    collector.EnsureAreaNodes(const_cast<UiRuntime&>(*this), handle);
    out.insert(out.end(), collector.state().area_nodes.begin(), collector.state().area_nodes.end());
}

void UiRuntime::EnsureSelectionAreaNodes(std::uint64_t area_handle) {
    Selection().EnsureAreaNodes(*this, area_handle);
}

int UiRuntime::FindSelectionAreaNodeIndex(std::uint64_t handle) const {
    return Selection().FindAreaNodeIndex(handle);
}

void UiRuntime::MarkSelectionAreaNodesDirty() {
    Selection().MarkAreaNodesDirty(*this);
}

void UiRuntime::ClearCrossSelection(bool notify_callback) {
    Selection().ClearCrossSelection(*this, notify_callback);
}

bool UiRuntime::UpdateCrossSelectionEndpoint(std::uint64_t handle, float logical_x, float logical_y) {
    return Selection().UpdateCrossSelectionEndpoint(*this, handle, logical_x, logical_y);
}

bool UiRuntime::GetCrossSelectionHighlight(std::uint64_t handle, std::uint32_t& out_start, std::uint32_t& out_end) const {
    return Selection().GetCrossSelectionHighlight(*this, handle, out_start, out_end);
}

bool UiRuntime::GetCrossSelectionEndpointSceneRects(
    std::uint64_t area_handle,
    Rect& out_start_rect,
    Rect& out_end_rect) {
    return Selection().GetCrossSelectionEndpointSceneRects(*this, area_handle, out_start_rect, out_end_rect);
}

void UiRuntime::NormalizeCrossSelectionEndpoints() {
    Selection().NormalizeCrossSelectionEndpoints();
}

std::string UiRuntime::BuildCrossSelectionText() const {
    return Selection().BuildCrossSelectionText(*this);
}

void UiRuntime::NotifyCrossSelectionChanged() const {
    Selection().NotifyCrossSelectionChanged(*this);
}

bool UiRuntime::HandleCrossSelectionNavigation(
    std::uint64_t area_handle,
    UINode& focused_node,
    std::string_view key,
    std::uint32_t modifiers) {
    return Selection().HandleCrossSelectionNavigation(*this, area_handle, focused_node, key, modifiers);
}

} // namespace effindom::v2::ui
