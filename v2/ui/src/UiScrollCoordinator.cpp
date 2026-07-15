#include "UiScrollCoordinator.h"

#include <algorithm>
#include <cmath>

namespace effindom::v2::ui {

namespace {

constexpr double kNominalFrameMs = 1000.0 / 60.0;
constexpr float kTerminalMomentumVelocityPxPerSecond = 120.0f;
constexpr float kTerminalMomentumFriction = 0.88f;
constexpr float kMomentumStopDisplacementPx = 0.32f;
constexpr float kSmoothScrollTimeConstantMs = 70.0f;
constexpr float kSmoothScrollStopDistancePx = 0.2f;
constexpr double kMinInputDeltaMs = 1.0;
constexpr double kMaxInputDeltaMs = 100.0;

float VelocityStopThreshold(float displacement_threshold_px) {
    return displacement_threshold_px / static_cast<float>(kNominalFrameMs / 1000.0);
}

double ClampInputDeltaMs(double value) {
    return std::isfinite(value) ? std::max(kMinInputDeltaMs, std::min(value, kMaxInputDeltaMs)) : kNominalFrameMs;
}

float PixelsPerSecond(float delta, double delta_ms) {
    return delta / static_cast<float>(ClampInputDeltaMs(delta_ms) / 1000.0);
}

float ComputeEdgeAutoScrollFactor(float pointer, float edge_min, float edge_max, float hot_zone) {
    if (pointer >= edge_min && pointer <= edge_max) {
        if ((pointer - edge_min) < hot_zone) return -(hot_zone - (pointer - edge_min)) / hot_zone;
        if ((edge_max - pointer) < hot_zone) return (hot_zone - (edge_max - pointer)) / hot_zone;
        return 0.0f;
    }
    if (pointer < edge_min) return -std::min(1.0f + ((edge_min - pointer) / hot_zone), 3.0f);
    return std::min(1.0f + ((pointer - edge_max) / hot_zone), 3.0f);
}

} // namespace

float ScrollCoordinator::ViewportWidth(const UINode& node) const {
    return visibility_.ComputeClipBounds(node, 0.0f, 0.0f).width;
}

float ScrollCoordinator::ViewportHeight(const UINode& node) const {
    return visibility_.ComputeClipBounds(node, 0.0f, 0.0f).height;
}

void ScrollCoordinator::NotifyChanged(std::uint64_t handle, UINode& node) const {
    const float viewport_width = ViewportWidth(node);
    const float viewport_height = ViewportHeight(node);
    events_.ScrollChanged(handle, ScrollMetrics{
        node.scroll_offset_x,
        node.scroll_offset_y,
        node.scroll_content_width,
        node.scroll_content_height,
        viewport_width,
        viewport_height,
    });
    node.reported_scroll_offset_x = node.scroll_offset_x;
    node.reported_scroll_offset_y = node.scroll_offset_y;
    node.reported_scroll_content_width = node.scroll_content_width;
    node.reported_scroll_content_height = node.scroll_content_height;
    node.reported_viewport_width = viewport_width;
    node.reported_viewport_height = viewport_height;
    node.has_reported_scroll_state = true;
}

bool ScrollCoordinator::ApplyOffset(
    std::uint64_t handle,
    UINode& node,
    float offset_x,
    float offset_y,
    bool notify) const {
    const float max_x = std::max(0.0f, node.scroll_content_width - ViewportWidth(node));
    const float max_y = std::max(0.0f, node.scroll_content_height - ViewportHeight(node));
    const float clamped_x = node.scroll_enabled_x ? std::clamp(offset_x, 0.0f, max_x) : 0.0f;
    const float clamped_y = node.scroll_enabled_y ? std::clamp(offset_y, 0.0f, max_y) : 0.0f;
    if (std::abs(node.scroll_offset_x - clamped_x) < 0.001f && std::abs(node.scroll_offset_y - clamped_y) < 0.001f) {
        return false;
    }
    if (node.is_applying_scroll_offset) {
        node.scroll_offset_x = clamped_x;
        node.scroll_offset_y = clamped_y;
        node.scroll_offset_dirty = true;
        if (notify) node.has_deferred_scroll_notification = true;
        return true;
    }

    node.is_applying_scroll_offset = true;
    node.scroll_offset_x = clamped_x;
    node.scroll_offset_y = clamped_y;
    node.scroll_offset_dirty = true;
    if (notify) {
        NotifyChanged(handle, node);
        constexpr std::uint32_t kMaxDeferredNotifications = 4U;
        for (std::uint32_t count = 0U; node.has_deferred_scroll_notification && count < kMaxDeferredNotifications; count += 1U) {
            node.has_deferred_scroll_notification = false;
            NotifyChanged(handle, node);
        }
        if (node.has_deferred_scroll_notification) {
            node.has_pending_scroll_offset = true;
            node.pending_scroll_offset_x = node.scroll_offset_x;
            node.pending_scroll_offset_y = node.scroll_offset_y;
            node.pending_scroll_offset_generation += 1U;
            node.has_deferred_scroll_notification = false;
        }
    }
    node.is_applying_scroll_offset = false;
    return true;
}

void ScrollCoordinator::UpdateMetrics(std::uint64_t handle, UINode& node) const {
    if (!node.is_scroll_view || node.yg_node == nullptr) return;
    node.layout_width = YGNodeLayoutGetWidth(node.yg_node);
    node.layout_height = YGNodeLayoutGetHeight(node.yg_node);
    const Rect content_bounds = visibility_.ComputeClipBounds(node, 0.0f, 0.0f);
    float content_width = 0.0f;
    float content_height = 0.0f;
    for (const std::uint64_t child_handle : node.children) {
        const UINode* child = nodes_.Resolve(child_handle);
        if (child == nullptr || child->yg_node == nullptr || child->visibility == UI_VISIBILITY_COLLAPSED) continue;
        float child_width = YGNodeLayoutGetWidth(child->yg_node);
        if (child->is_text_node && !child->text_wrap) child_width = non_wrapping_content_width_(*child, child_width);
        content_width = std::max(content_width, YGNodeLayoutGetLeft(child->yg_node) + child_width + YGNodeLayoutGetMargin(child->yg_node, YGEdgeRight) - content_bounds.x);
        content_height = std::max(content_height, YGNodeLayoutGetTop(child->yg_node) + YGNodeLayoutGetHeight(child->yg_node) + YGNodeLayoutGetMargin(child->yg_node, YGEdgeBottom) - content_bounds.y);
    }
    node.scroll_content_width = node.has_explicit_scroll_content_width ? node.explicit_scroll_content_width : std::max(content_width, 0.0f);
    node.scroll_content_height = node.has_explicit_scroll_content_height ? node.explicit_scroll_content_height : std::max(content_height, 0.0f);
    const std::uint64_t generation = node.pending_scroll_offset_generation;
    const float target_x = node.has_pending_scroll_offset ? node.pending_scroll_offset_x : node.scroll_offset_x;
    const float target_y = node.has_pending_scroll_offset ? node.pending_scroll_offset_y : node.scroll_offset_y;
    if (!ApplyOffset(handle, node, target_x, target_y, true)) {
        const bool changed = !node.has_reported_scroll_state ||
            std::abs(node.reported_scroll_offset_x - node.scroll_offset_x) >= 0.001f ||
            std::abs(node.reported_scroll_offset_y - node.scroll_offset_y) >= 0.001f ||
            std::abs(node.reported_scroll_content_width - node.scroll_content_width) >= 0.001f ||
            std::abs(node.reported_scroll_content_height - node.scroll_content_height) >= 0.001f ||
            std::abs(node.reported_viewport_width - ViewportWidth(node)) >= 0.001f ||
            std::abs(node.reported_viewport_height - ViewportHeight(node)) >= 0.001f;
        if (changed) NotifyChanged(handle, node);
    }
    if (node.pending_scroll_offset_generation == generation) node.has_pending_scroll_offset = false;
}

void ScrollCoordinator::UpdateMetricsAfterLayout() const {
    traversal_.ForEachActiveScrollView([this](std::uint64_t handle, UINode& node) { UpdateMetrics(handle, node); });
}

void ScrollCoordinator::ApplyPendingOffsets() const {
    traversal_.ForEachActiveScrollView([this](std::uint64_t handle, UINode& node) {
        if (!node.has_pending_scroll_offset) return;
        const std::uint64_t generation = node.pending_scroll_offset_generation;
        (void)ApplyOffset(handle, node, node.pending_scroll_offset_x, node.pending_scroll_offset_y, true);
        if (node.pending_scroll_offset_generation == generation) node.has_pending_scroll_offset = false;
    });
}

void ScrollCoordinator::ClearAutoScroll() {
    auto_scroll_active_ = false;
    auto_scroll_handle_ = UI_INVALID_HANDLE;
    auto_scroll_factor_x_ = 0.0f;
    auto_scroll_factor_y_ = 0.0f;
}

std::uint64_t ScrollCoordinator::FindScrollableAncestor(std::uint64_t start_handle) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = nodes_.Resolve(current);
        if (node == nullptr) break;
        if (node->is_scroll_view) return current;
        current = node->parent_handle;
    }
    return UI_INVALID_HANDLE;
}

bool ScrollCoordinator::UpdateAutoScrollFor(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y,
    float edge_threshold) {
    const std::uint64_t handle = FindScrollableAncestor(start_handle);
    const UINode* node = nodes_.Resolve(handle);
    if (node == nullptr) { ClearAutoScroll(); return false; }
    const Rect viewport = visibility_.ComputeClipBounds(*node, node->abs_x, node->abs_y);
    const float hot_zone = std::max(0.001f, edge_threshold > 0.0f ? edge_threshold : node->edge_hot_zone);
    const float max_x = std::max(0.0f, node->scroll_content_width - viewport.width);
    const float max_y = std::max(0.0f, node->scroll_content_height - viewport.height);
    const float factor_x = node->scroll_enabled_x && max_x > 0.0f
        ? ComputeEdgeAutoScrollFactor(logical_x, viewport.x, viewport.x + viewport.width, hot_zone) : 0.0f;
    const float factor_y = node->scroll_enabled_y && max_y > 0.0f
        ? ComputeEdgeAutoScrollFactor(logical_y, viewport.y, viewport.y + viewport.height, hot_zone) : 0.0f;
    if (std::abs(factor_x) <= 0.01f && std::abs(factor_y) <= 0.01f) { ClearAutoScroll(); return false; }
    SetAutoScroll(handle, factor_x, factor_y);
    return true;
}

void ScrollCoordinator::SetAutoScroll(std::uint64_t handle, float factor_x, float factor_y) {
    auto_scroll_active_ = true;
    auto_scroll_handle_ = handle;
    auto_scroll_factor_x_ = factor_x;
    auto_scroll_factor_y_ = factor_y;
}

bool ScrollCoordinator::HasAutoScroll() const {
    return auto_scroll_active_;
}

std::uint64_t ScrollCoordinator::ActiveAutoScrollHandle() const {
    return auto_scroll_handle_;
}

float ScrollCoordinator::AutoScrollFactorX() const {
    return auto_scroll_factor_x_;
}

float ScrollCoordinator::AutoScrollFactorY() const {
    return auto_scroll_factor_y_;
}

void ScrollCoordinator::AdvanceAutoScroll() {
    if (!auto_scroll_active_) return;
    UINode* node = writer_.Resolve(auto_scroll_handle_);
    if (node == nullptr) {
        ClearAutoScroll();
        return;
    }
    (void)ApplyOffset(
        auto_scroll_handle_,
        *node,
        node->scroll_offset_x + (auto_scroll_factor_x_ * node->auto_scroll_speed),
        node->scroll_offset_y + (auto_scroll_factor_y_ * node->auto_scroll_speed),
        true);
}

bool ScrollCoordinator::CanScrollOnAxis(const UINode& node, bool horizontal) const {
    const float viewport = horizontal ? ViewportWidth(node) : ViewportHeight(node);
    const float max_scroll = horizontal ? std::max(0.0f, node.scroll_content_width - viewport) : std::max(0.0f, node.scroll_content_height - viewport);
    return (horizontal ? node.scroll_enabled_x : node.scroll_enabled_y) && max_scroll > 0.0f;
}

bool ScrollCoordinator::CanConsume(const UINode& node, bool horizontal, float delta, bool use_smooth_target) const {
    if (std::abs(delta) < 0.001f || !CanScrollOnAxis(node, horizontal)) return false;
    const float viewport = horizontal ? ViewportWidth(node) : ViewportHeight(node);
    const float max_scroll = horizontal ? std::max(0.0f, node.scroll_content_width - viewport) : std::max(0.0f, node.scroll_content_height - viewport);
    const float offset = use_smooth_target && node.smooth_scroll_active ? (horizontal ? node.smooth_scroll_target_x : node.smooth_scroll_target_y) : (horizontal ? node.scroll_offset_x : node.scroll_offset_y);
    return delta > 0.0f ? offset < max_scroll - 0.001f : offset > 0.001f;
}

std::uint64_t ScrollCoordinator::RetargetForDelta(std::uint64_t start_handle, float delta_x, float delta_y, bool use_smooth_target) const {
    if (start_handle == UI_INVALID_HANDLE) return UI_INVALID_HANDLE;
    const float abs_x = std::abs(delta_x);
    const float abs_y = std::abs(delta_y);
    const bool prefer_horizontal = abs_x > abs_y;
    const bool prefer_vertical = abs_y > abs_x;
    std::uint64_t fallback = UI_INVALID_HANDLE;
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = nodes_.Resolve(current);
        if (node == nullptr) break;
        if (node->is_scroll_view) {
            const bool can_x = CanScrollOnAxis(*node, true);
            const bool can_y = CanScrollOnAxis(*node, false);
            const bool consume_x = CanConsume(*node, true, delta_x, use_smooth_target);
            const bool consume_y = CanConsume(*node, false, delta_y, use_smooth_target);
            if ((prefer_horizontal && consume_x) || (prefer_vertical && consume_y) || (!prefer_horizontal && !prefer_vertical && ((abs_x > 0.0f && consume_x) || (abs_y > 0.0f && consume_y)))) return current;
            if (fallback == UI_INVALID_HANDLE && ((abs_x > 0.0f && can_x) || (abs_y > 0.0f && can_y))) fallback = current;
        }
        current = node->parent_handle;
    }
    return fallback == UI_INVALID_HANDLE ? start_handle : fallback;
}

bool ScrollCoordinator::CanConsumeFromTarget(std::uint64_t start_handle, float delta_x, float delta_y) const {
    if (start_handle == UI_INVALID_HANDLE) return false;
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = nodes_.Resolve(current);
        if (node == nullptr) break;
        if (node->is_scroll_view &&
            ((std::abs(delta_x) >= 0.001f && CanConsume(*node, true, delta_x)) ||
             (std::abs(delta_y) >= 0.001f && CanConsume(*node, false, delta_y)))) return true;
        current = node->parent_handle;
    }
    return false;
}

void ScrollCoordinator::HandleWheel(std::uint64_t start_handle, float delta_x, float delta_y) {
    const std::uint64_t handle = RetargetForDelta(start_handle, delta_x, delta_y, true);
    UINode* node = writer_.Resolve(handle);
    if (node == nullptr) return;
    node->scroll_velocity_x = 0.0f;
    node->scroll_velocity_y = 0.0f;
    if (!node->smooth_scrolling) {
        (void)ApplyOffset(handle, *node, node->scroll_offset_x + delta_x, node->scroll_offset_y + delta_y, true);
        return;
    }
    const float max_x = std::max(0.0f, node->scroll_content_width - ViewportWidth(*node));
    const float max_y = std::max(0.0f, node->scroll_content_height - ViewportHeight(*node));
    const float base_x = node->smooth_scroll_active ? node->smooth_scroll_target_x : node->scroll_offset_x;
    const float base_y = node->smooth_scroll_active ? node->smooth_scroll_target_y : node->scroll_offset_y;
    node->smooth_scroll_target_x = node->scroll_enabled_x ? std::clamp(base_x + delta_x, 0.0f, max_x) : 0.0f;
    node->smooth_scroll_target_y = node->scroll_enabled_y ? std::clamp(base_y + delta_y, 0.0f, max_y) : 0.0f;
    node->smooth_scroll_active =
        std::abs(node->smooth_scroll_target_x - node->scroll_offset_x) >= 0.001f ||
        std::abs(node->smooth_scroll_target_y - node->scroll_offset_y) >= 0.001f;
}

void ScrollCoordinator::HandlePreciseWheel(
    std::uint64_t start_handle,
    float delta_x,
    float delta_y,
    bool begins_gesture,
    bool ends_gesture) {
    if (begins_gesture || precise_wheel_handle_ == UI_INVALID_HANDLE) {
        precise_wheel_handle_ = RetargetForDelta(start_handle, delta_x, delta_y, false);
    }
    UINode* node = writer_.Resolve(precise_wheel_handle_);
    if (node != nullptr) {
        node->scroll_velocity_x = 0.0f;
        node->scroll_velocity_y = 0.0f;
        node->smooth_scroll_active = false;
        node->smooth_scroll_target_x = node->scroll_offset_x;
        node->smooth_scroll_target_y = node->scroll_offset_y;
        if (std::abs(delta_x) >= 0.001f || std::abs(delta_y) >= 0.001f) {
            (void)ApplyOffset(
                precise_wheel_handle_,
                *node,
                node->scroll_offset_x + delta_x,
                node->scroll_offset_y + delta_y,
                true);
        }
    }
    if (ends_gesture) precise_wheel_handle_ = UI_INVALID_HANDLE;
}

void ScrollCoordinator::ResetInteraction() {
    active_handle_ = UI_INVALID_HANDLE;
    active_touch_x_handle_ = UI_INVALID_HANDLE;
    active_touch_y_handle_ = UI_INVALID_HANDLE;
    active_dragged_ = false;
    momentum_x_handle_ = UI_INVALID_HANDLE;
    momentum_y_handle_ = UI_INVALID_HANDLE;
    precise_wheel_handle_ = UI_INVALID_HANDLE;
    has_touch_timestamp_ = false;
    last_touch_timestamp_ms_ = 0.0;
    ClearAutoScroll();
}

void ScrollCoordinator::BeginPointerDrag(std::uint64_t handle) {
    active_handle_ = handle;
    active_touch_x_handle_ = UI_INVALID_HANDLE;
    active_touch_y_handle_ = UI_INVALID_HANDLE;
    active_dragged_ = false;
    if (UINode* node = writer_.Resolve(handle); node != nullptr) {
        node->scroll_velocity_x = 0.0f;
        node->scroll_velocity_y = 0.0f;
    }
}

void ScrollCoordinator::CancelActiveDrag() {
    active_handle_ = UI_INVALID_HANDLE;
    active_touch_x_handle_ = UI_INVALID_HANDLE;
    active_touch_y_handle_ = UI_INVALID_HANDLE;
    active_dragged_ = false;
    has_touch_timestamp_ = false;
}

std::uint64_t ScrollCoordinator::ActiveDragHandle() const { return active_handle_; }
std::uint64_t ScrollCoordinator::ActiveTouchXHandle() const { return active_touch_x_handle_; }
std::uint64_t ScrollCoordinator::ActiveTouchYHandle() const { return active_touch_y_handle_; }
bool ScrollCoordinator::ActiveDragWasMoved() const { return active_dragged_; }

void ScrollCoordinator::DragPointerBy(float delta_x, float delta_y, double delta_ms) {
    UINode* node = writer_.Resolve(active_handle_);
    if (node == nullptr) { CancelActiveDrag(); return; }
    active_dragged_ = active_dragged_ || std::abs(delta_x) > 0.0f || std::abs(delta_y) > 0.0f;
    (void)ApplyOffset(active_handle_, *node, node->scroll_offset_x + delta_x, node->scroll_offset_y + delta_y, true);
    node->scroll_velocity_x = PixelsPerSecond(delta_x, delta_ms);
    node->scroll_velocity_y = PixelsPerSecond(delta_y, delta_ms);
}

void ScrollCoordinator::EndPointerDrag() {
    if (UINode* node = writer_.Resolve(active_handle_); node != nullptr && !active_dragged_) {
        node->scroll_velocity_x = 0.0f;
        node->scroll_velocity_y = 0.0f;
    }
    CancelActiveDrag();
}

void ScrollCoordinator::BeginTouch(std::uint64_t handle, double timestamp_ms) {
    BeginPointerDrag(handle);
    has_touch_timestamp_ = std::isfinite(timestamp_ms) && timestamp_ms >= 0.0;
    last_touch_timestamp_ms_ = has_touch_timestamp_ ? timestamp_ms : 0.0;
    if (UINode* node = writer_.Resolve(handle); node != nullptr) {
        node->smooth_scroll_active = false;
        node->smooth_scroll_target_x = node->scroll_offset_x;
        node->smooth_scroll_target_y = node->scroll_offset_y;
        active_touch_x_handle_ = CanScrollOnAxis(*node, true) ? handle : UI_INVALID_HANDLE;
        active_touch_y_handle_ = CanScrollOnAxis(*node, false) ? handle : UI_INVALID_HANDLE;
    }
}

void ScrollCoordinator::UpdateTouch(float delta_x, float delta_y, double timestamp_ms) {
    if (active_handle_ == UI_INVALID_HANDLE) return;
    double delta_ms = kNominalFrameMs;
    if (std::isfinite(timestamp_ms) && timestamp_ms >= 0.0) {
        if (has_touch_timestamp_) delta_ms = ClampInputDeltaMs(timestamp_ms - last_touch_timestamp_ms_);
        last_touch_timestamp_ms_ = timestamp_ms;
        has_touch_timestamp_ = true;
    } else if (has_touch_timestamp_) last_touch_timestamp_ms_ += kNominalFrameMs;
    const std::uint64_t primary = RetargetForDelta(active_handle_, delta_x, delta_y);
    const std::uint64_t x = std::abs(delta_x) > 0.0f ? RetargetForDelta(active_touch_x_handle_ != UI_INVALID_HANDLE ? active_touch_x_handle_ : active_handle_, delta_x, 0.0f) : active_touch_x_handle_;
    const std::uint64_t y = std::abs(delta_y) > 0.0f ? RetargetForDelta(active_touch_y_handle_ != UI_INVALID_HANDLE ? active_touch_y_handle_ : active_handle_, 0.0f, delta_y) : active_touch_y_handle_;
    active_handle_ = primary != UI_INVALID_HANDLE ? primary : (x != UI_INVALID_HANDLE ? x : y);
    active_touch_x_handle_ = x;
    active_touch_y_handle_ = y;
    if (x == UI_INVALID_HANDLE && y == UI_INVALID_HANDLE) { CancelActiveDrag(); return; }
    active_dragged_ = active_dragged_ || std::abs(delta_x) > 0.0f || std::abs(delta_y) > 0.0f;
    if (x != UI_INVALID_HANDLE && x == y) {
        if (UINode* node = writer_.Resolve(x); node != nullptr) {
            (void)ApplyOffset(x, *node, node->scroll_offset_x + delta_x, node->scroll_offset_y + delta_y, true);
            node->scroll_velocity_x = PixelsPerSecond(delta_x, delta_ms);
            node->scroll_velocity_y = PixelsPerSecond(delta_y, delta_ms);
        }
        return;
    }
    if (UINode* node = writer_.Resolve(x); node != nullptr) {
        (void)ApplyOffset(x, *node, node->scroll_offset_x + delta_x, node->scroll_offset_y, true);
        node->scroll_velocity_x = PixelsPerSecond(delta_x, delta_ms);
        node->scroll_velocity_y = 0.0f;
    }
    if (UINode* node = writer_.Resolve(y); node != nullptr) {
        (void)ApplyOffset(y, *node, node->scroll_offset_x, node->scroll_offset_y + delta_y, true);
        node->scroll_velocity_x = 0.0f;
        node->scroll_velocity_y = PixelsPerSecond(delta_y, delta_ms);
    }
}

void ScrollCoordinator::EndTouch(double timestamp_ms) {
    if (std::isfinite(timestamp_ms) && timestamp_ms >= 0.0) { last_touch_timestamp_ms_ = timestamp_ms; has_touch_timestamp_ = true; }
    const auto clear_if_needed = [this](std::uint64_t handle) {
        if (UINode* node = writer_.Resolve(handle); node != nullptr && !active_dragged_) { node->scroll_velocity_x = 0.0f; node->scroll_velocity_y = 0.0f; }
    };
    clear_if_needed(active_handle_);
    if (active_touch_x_handle_ != active_handle_) clear_if_needed(active_touch_x_handle_);
    if (active_touch_y_handle_ != active_handle_ && active_touch_y_handle_ != active_touch_x_handle_) clear_if_needed(active_touch_y_handle_);
    momentum_x_handle_ = active_dragged_ ? (active_touch_x_handle_ != active_handle_ ? active_touch_x_handle_ : active_handle_) : UI_INVALID_HANDLE;
    momentum_y_handle_ = active_dragged_ ? (active_touch_y_handle_ != active_handle_ && active_touch_y_handle_ != active_touch_x_handle_ ? active_touch_y_handle_ : active_handle_) : UI_INVALID_HANDLE;
    CancelActiveDrag();
}

void ScrollCoordinator::ClearMomentum() {
    const auto brake = [this](std::uint64_t handle) { if (UINode* node = writer_.Resolve(handle); node != nullptr) { node->scroll_velocity_x *= 0.01f; node->scroll_velocity_y *= 0.01f; } };
    brake(momentum_x_handle_);
    if (momentum_y_handle_ != momentum_x_handle_) brake(momentum_y_handle_);
}

bool ScrollCoordinator::ActiveTouchAllowsPullToRefresh() const {
    if (active_touch_y_handle_ == UI_INVALID_HANDLE) return true;
    const UINode* node = nodes_.Resolve(RetargetForDelta(active_touch_y_handle_, 0.0f, -1.0f));
    return node != nullptr && node->scroll_enabled_y && !CanConsume(*node, false, -1.0f);
}

bool ScrollCoordinator::ActiveTouchCanConsume(float delta_x, float delta_y) const {
    if (active_handle_ == UI_INVALID_HANDLE) return false;
    if (std::abs(delta_x) >= 0.001f) { const UINode* node = nodes_.Resolve(RetargetForDelta(active_touch_x_handle_ != UI_INVALID_HANDLE ? active_touch_x_handle_ : active_handle_, delta_x, 0.0f)); if (node != nullptr && CanConsume(*node, true, delta_x)) return true; }
    if (std::abs(delta_y) >= 0.001f) { const UINode* node = nodes_.Resolve(RetargetForDelta(active_touch_y_handle_ != UI_INVALID_HANDLE ? active_touch_y_handle_ : active_handle_, 0.0f, delta_y)); if (node != nullptr && CanConsume(*node, false, delta_y)) return true; }
    return false;
}

void ScrollCoordinator::Advance(double delta_ms) const {
    const float seconds = static_cast<float>(delta_ms / 1000.0);
    traversal_.ForEachActiveScrollView([&](std::uint64_t handle, UINode& node) {
        if (node.smooth_scroll_active) {
            const float blend = delta_ms <= 0.0 ? 0.0f : 1.0f - std::exp(-static_cast<float>(delta_ms) / kSmoothScrollTimeConstantMs);
            const float remaining_x = node.smooth_scroll_target_x - node.scroll_offset_x;
            const float remaining_y = node.smooth_scroll_target_y - node.scroll_offset_y;
            const bool reached = std::abs(remaining_x) <= kSmoothScrollStopDistancePx && std::abs(remaining_y) <= kSmoothScrollStopDistancePx;
            const bool changed = ApplyOffset(handle, node,
                reached ? node.smooth_scroll_target_x : node.scroll_offset_x + (remaining_x * blend),
                reached ? node.smooth_scroll_target_y : node.scroll_offset_y + (remaining_y * blend), true);
            if (reached || !changed) {
                node.smooth_scroll_active = false;
                node.smooth_scroll_target_x = node.scroll_offset_x;
                node.smooth_scroll_target_y = node.scroll_offset_y;
            }
            return;
        }
        if (handle == active_handle_ || handle == active_touch_x_handle_ || handle == active_touch_y_handle_) return;
        if (std::abs(node.scroll_velocity_x) < VelocityStopThreshold(0.001f) && std::abs(node.scroll_velocity_y) < VelocityStopThreshold(0.001f)) {
            node.scroll_velocity_x = 0.0f;
            node.scroll_velocity_y = 0.0f;
            return;
        }
        const float frame_factor = static_cast<float>(delta_ms / kNominalFrameMs);
        float friction = resolve_friction_(node);
        if (std::max(std::abs(node.scroll_velocity_x), std::abs(node.scroll_velocity_y)) < kTerminalMomentumVelocityPxPerSecond && frame_factor > 0.0f) friction *= kTerminalMomentumFriction;
        const float decay = frame_factor <= 0.0f ? 1.0f : std::pow(friction, frame_factor);
        const float nominal_seconds = static_cast<float>(kNominalFrameMs / 1000.0);
        const float displacement = friction > 0.0f && friction < 1.0f ? nominal_seconds * ((1.0f - decay) / (1.0f - friction)) : seconds;
        const bool changed = ApplyOffset(handle, node, node.scroll_offset_x + (node.scroll_velocity_x * displacement), node.scroll_offset_y + (node.scroll_velocity_y * displacement), true);
        node.scroll_velocity_x *= decay;
        node.scroll_velocity_y *= decay;
        if (!changed || (std::abs(node.scroll_velocity_x) < VelocityStopThreshold(kMomentumStopDisplacementPx) && std::abs(node.scroll_velocity_y) < VelocityStopThreshold(kMomentumStopDisplacementPx))) {
            node.scroll_velocity_x = 0.0f;
            node.scroll_velocity_y = 0.0f;
        }
    });
}

} // namespace effindom::v2::ui
