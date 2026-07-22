#pragma once

#include "UiEventSink.h"
#include "UiVisibilityResolver.h"

#include <functional>

namespace effindom::v2::ui {

class ScrollCoordinator {
public:
    using NonWrappingContentWidthResolver = std::function<float(const UINode&, float)>;
    using FrictionResolver = std::function<float(const UINode&)>;

    ScrollCoordinator(
        NodeReader nodes,
        NodeWriter writer,
        NodeTraversalAccess traversal,
        VisibilityResolver visibility,
        NonWrappingContentWidthResolver non_wrapping_content_width,
        const UiEventSink& events,
        FrictionResolver resolve_friction)
        : nodes_(nodes),
          writer_(writer),
          traversal_(traversal),
          visibility_(std::move(visibility)),
          non_wrapping_content_width_(std::move(non_wrapping_content_width)),
          events_(events),
          resolve_friction_(std::move(resolve_friction)) {}

    bool ApplyOffset(std::uint64_t handle, UINode& node, float offset_x, float offset_y, bool notify) const;
    void UpdateMetrics(std::uint64_t handle, UINode& node) const;
    void UpdateMetricsAfterLayout() const;
    void ApplyPendingOffsets() const;
    void ClearAutoScroll();
    bool UpdateAutoScrollFor(std::uint64_t start_handle, float logical_x, float logical_y, float edge_threshold);
    void SetAutoScroll(std::uint64_t handle, float factor_x, float factor_y);
    bool HasAutoScroll() const;
    std::uint64_t ActiveAutoScrollHandle() const;
    float AutoScrollFactorX() const;
    float AutoScrollFactorY() const;
    void AdvanceAutoScroll();
    void ResetInteraction();
    void BeginPointerDrag(std::uint64_t handle);
    void CancelActiveDrag();
    std::uint64_t ActiveDragHandle() const;
    std::uint64_t ActiveTouchXHandle() const;
    std::uint64_t ActiveTouchYHandle() const;
    bool ActiveDragWasMoved() const;
    void DragPointerBy(float delta_x, float delta_y, double delta_ms);
    void EndPointerDrag();
    void BeginTouch(std::uint64_t handle, double timestamp_ms);
    void UpdateTouch(float delta_x, float delta_y, double timestamp_ms);
    void EndTouch(double timestamp_ms);
    void ClearMomentum();
    bool CanScrollOnAxis(const UINode& node, bool horizontal) const;
    bool CanConsume(const UINode& node, bool horizontal, float delta, bool use_smooth_target = false) const;
    bool CanConsumeFromTarget(std::uint64_t start_handle, float delta_x, float delta_y) const;
    std::uint64_t RetargetForDelta(std::uint64_t start_handle, float delta_x, float delta_y, bool use_smooth_target = false) const;
    std::uint64_t FindScrollableAncestor(std::uint64_t start_handle) const;
    std::uint64_t FindCommonScrollableAncestor(std::uint64_t first_handle, std::uint64_t second_handle) const;
    void HandleWheel(std::uint64_t start_handle, float delta_x, float delta_y);
    void HandlePreciseWheel(
        std::uint64_t start_handle,
        float delta_x,
        float delta_y,
        bool begins_gesture,
        bool ends_gesture);
    bool ActiveTouchAllowsPullToRefresh() const;
    bool ActiveTouchCanConsume(float delta_x, float delta_y) const;
    void Advance(double delta_ms) const;

private:
    float ViewportWidth(const UINode& node) const;
    float ViewportHeight(const UINode& node) const;
    void NotifyChanged(std::uint64_t handle, UINode& node) const;

    NodeReader nodes_;
    NodeWriter writer_;
    NodeTraversalAccess traversal_;
    VisibilityResolver visibility_;
    NonWrappingContentWidthResolver non_wrapping_content_width_;
    const UiEventSink& events_;
    FrictionResolver resolve_friction_;
    bool auto_scroll_active_ = false;
    std::uint64_t auto_scroll_handle_ = UI_INVALID_HANDLE;
    float auto_scroll_factor_x_ = 0.0f;
    float auto_scroll_factor_y_ = 0.0f;
    std::uint64_t active_handle_ = UI_INVALID_HANDLE;
    std::uint64_t active_touch_x_handle_ = UI_INVALID_HANDLE;
    std::uint64_t active_touch_y_handle_ = UI_INVALID_HANDLE;
    bool active_dragged_ = false;
    std::uint64_t momentum_x_handle_ = UI_INVALID_HANDLE;
    std::uint64_t momentum_y_handle_ = UI_INVALID_HANDLE;
    std::uint64_t precise_wheel_handle_ = UI_INVALID_HANDLE;
    bool has_touch_timestamp_ = false;
    double last_touch_timestamp_ms_ = 0.0;
};

} // namespace effindom::v2::ui
