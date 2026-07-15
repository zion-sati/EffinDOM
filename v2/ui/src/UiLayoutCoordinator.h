#pragma once

#include "UiScrollCoordinator.h"

#include <cstdint>

namespace effindom::v2::ui {

struct LayoutResult {
    bool emitted_layout_updates = false;
    bool needs_follow_up_layout = false;
    std::uint32_t stabilization_passes = 0U;
    double yoga_layout_ms = 0.0;
    double scroll_metrics_ms = 0.0;
};

class LayoutCoordinator {
public:
    LayoutCoordinator(NodeReader nodes, NodeWriter writer, ScrollCoordinator& scrolling)
        : nodes_(nodes), writer_(writer), scrolling_(scrolling) {}

    LayoutResult Update(
        std::uint64_t root_handle,
        float window_width,
        float window_height,
        bool& layout_dirty);

private:
    void ApplyLayoutStyles(std::uint64_t handle, std::uint64_t parent_handle);
    float ComputeFillAxisAvailableSpace(
        const UINode& node,
        const UINode* parent,
        bool width_axis,
        bool parent_is_horizontal,
        float window_width,
        float window_height) const;
    bool ResolveFillPercentLayout(
        std::uint64_t handle,
        std::uint64_t parent_handle,
        float window_width,
        float window_height);

    NodeReader nodes_;
    NodeWriter writer_;
    ScrollCoordinator& scrolling_;
};

} // namespace effindom::v2::ui
