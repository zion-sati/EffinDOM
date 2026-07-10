#include "UiRuntime.h"

#include "effindom_ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace effindom::v2::ui {

namespace {

constexpr float kSelectionDragThreshold = 4.0f;
constexpr float kSelectionDragThresholdSquared = kSelectionDragThreshold * kSelectionDragThreshold;
constexpr float kSelectionHandleCenterToTextHitOffset = 21.0f;
constexpr float kMaxAutoScrollFactor = 3.0f;
constexpr double kNominalFrameMs = 1000.0 / 60.0;
constexpr double kMinInputDeltaMs = 1.0;
constexpr double kMaxInputDeltaMs = 100.0;

bool IsValidTimestamp(double timestamp_ms) {
    return std::isfinite(timestamp_ms) && timestamp_ms >= 0.0;
}

double ClampInputDeltaMs(double delta_ms) {
    if (!std::isfinite(delta_ms)) {
        return kNominalFrameMs;
    }
    return std::max(kMinInputDeltaMs, std::min(delta_ms, kMaxInputDeltaMs));
}

float PixelsPerSecond(float delta, double delta_ms) {
    return delta / static_cast<float>(ClampInputDeltaMs(delta_ms) / 1000.0);
}

std::int32_t FocusOrderKey(const UINode* node) {
    return node == nullptr ? std::numeric_limits<std::int32_t>::max()
                           : (node->tab_index > 0 ? node->tab_index : std::numeric_limits<std::int32_t>::max());
}

bool PointInRect(const Rect& rect, float local_x, float local_y) {
    return local_x >= rect.x &&
        local_x <= (rect.x + rect.width) &&
        local_y >= rect.y &&
        local_y <= (rect.y + rect.height);
}

Rect BoundsForNode(const UINode& node) {
    return Rect{
        node.abs_x,
        node.abs_y,
        node.layout_width,
        node.layout_height,
    };
}

bool IntersectRect(Rect& rect, const Rect& clip) {
    const float left = std::max(rect.x, clip.x);
    const float top = std::max(rect.y, clip.y);
    const float right = std::min(rect.x + rect.width, clip.x + clip.width);
    const float bottom = std::min(rect.y + rect.height, clip.y + clip.height);
    rect.x = left;
    rect.y = top;
    rect.width = std::max(0.0f, right - left);
    rect.height = std::max(0.0f, bottom - top);
    return rect.width > 0.0f && rect.height > 0.0f;
}

bool MovedBeyondSelectionDragThreshold(float start_x, float start_y, float current_x, float current_y) {
    const float delta_x = current_x - start_x;
    const float delta_y = current_y - start_y;
    return (delta_x * delta_x) + (delta_y * delta_y) >= kSelectionDragThresholdSquared;
}

float ComputeEdgeAutoScrollFactor(float pointer, float edge_min, float edge_max, float hot_zone) {
    if (pointer >= edge_min && pointer <= edge_max) {
        if ((pointer - edge_min) < hot_zone) {
            const float t = (hot_zone - (pointer - edge_min)) / hot_zone;
            return -t;
        }
        if ((edge_max - pointer) < hot_zone) {
            const float t = (hot_zone - (edge_max - pointer)) / hot_zone;
            return t;
        }
        return 0.0f;
    }
    if (pointer < edge_min) {
        const float beyond = edge_min - pointer;
        const float t = 1.0f + (beyond / hot_zone);
        return -std::min(t, kMaxAutoScrollFactor);
    }
    if (pointer > edge_max) {
        const float beyond = pointer - edge_max;
        const float t = 1.0f + (beyond / hot_zone);
        return std::min(t, kMaxAutoScrollFactor);
    }
    return 0.0f;
}

} // namespace

Rect UiRuntime::ComputeVisibleBounds(const UINode& node) const {
    Rect bounds = BoundsForNode(node);
    const bool uses_internal_textbox_viewport =
        IsSingleLineEditorTextNode(node);
    if (node.is_text_node && !node.text_wrap && !uses_internal_textbox_viewport) {
        const Rect content_bounds = ComputeContentBounds(node, node.abs_x, node.abs_y);
        const float max_line_width =
            node.text_layout_cache_valid
            ? node.text_layout_cache_max_line_width
            : LayoutParagraph(
                  node,
                  content_bounds.width > 0.0f ? std::optional<float>(content_bounds.width) : std::nullopt)
                  .width;
        const float horizontal_overflow = std::max(max_line_width - content_bounds.width, 0.0f);
        bounds.width += horizontal_overflow;
    }
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        bounds.width = 0.0f;
        bounds.height = 0.0f;
        return bounds;
    }

    for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
        const UINode* parent = Resolve(current);
        if (parent == nullptr) {
            break;
        }
        if ((parent->clip_to_bounds || parent->is_scroll_view) &&
            !IntersectRect(bounds, ComputeClipBounds(*parent))) {
            break;
        }
        if (parent->is_portal) {
            break;
        }
        current = parent->parent_handle;
    }
    return bounds;
}

bool UiRuntime::PointInVisibleBounds(const UINode& node, float logical_x, float logical_y) const {
    const Rect bounds = ComputeVisibleBounds(node);
    return bounds.width > 0.0f && bounds.height > 0.0f && PointInRect(bounds, logical_x, logical_y);
}

bool UiRuntime::IsAttachedToRoot(std::uint64_t handle) const {
    if (handle == UI_INVALID_HANDLE || root_handle_ == UI_INVALID_HANDLE) {
        return false;
    }
    for (std::uint64_t current = handle; current != UI_INVALID_HANDLE;) {
        if (current == root_handle_) {
            return true;
        }
        const UINode* node = Resolve(current);
        if (node == nullptr) {
            break;
        }
        current = node->parent_handle;
    }
    return false;
}

std::uint64_t UiRuntime::FindDeepestNodeContainingPoint(
    std::uint64_t handle,
    float logical_x,
    float logical_y) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return UI_INVALID_HANDLE;
    }
    if (!PointInVisibleBounds(*node, logical_x, logical_y)) {
        return UI_INVALID_HANDLE;
    }

    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
        const std::uint64_t child_handle = FindDeepestNodeContainingPoint(*it, logical_x, logical_y);
        if (child_handle != UI_INVALID_HANDLE) {
            return child_handle;
        }
    }
    return handle;
}

std::uint64_t UiRuntime::FindBestNodeContainingPoint(float logical_x, float logical_y) const {
    std::uint64_t best_handle = UI_INVALID_HANDLE;
    float best_area = std::numeric_limits<float>::max();
    std::uint32_t best_depth = 0U;

    for (std::uint32_t index = 0; index < node_pool_.size(); index += 1U) {
        const UINode& node = node_pool_[index];
        if (!node.is_active || node.yg_node == nullptr) {
            continue;
        }
        const std::uint64_t handle = PackHandle(index, node.generation);
        if (!IsAttachedToRoot(handle)) {
            continue;
        }
        if (!PointInVisibleBounds(node, logical_x, logical_y)) {
            continue;
        }

        std::uint32_t depth = 0U;
        for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
            const UINode* parent = Resolve(current);
            if (parent == nullptr) {
                break;
            }
            depth += 1U;
            current = parent->parent_handle;
        }

        const float area = node.layout_width * node.layout_height;
        if (best_handle == UI_INVALID_HANDLE || area < best_area ||
            (area == best_area && depth > best_depth)) {
            best_handle = handle;
            best_area = area;
            best_depth = depth;
        }
    }

    return best_handle;
}

std::uint64_t UiRuntime::FindDeepestScrollViewContainingPoint(
    std::uint64_t handle,
    float logical_x,
    float logical_y) const {
    (void)handle;

    std::uint64_t best_handle = UI_INVALID_HANDLE;
    float best_area = std::numeric_limits<float>::max();
    std::uint32_t best_depth = 0U;

    for (std::uint32_t index = 0; index < node_pool_.size(); index += 1U) {
        const UINode& node = node_pool_[index];
        if (!node.is_active || node.yg_node == nullptr || !node.is_scroll_view) {
            continue;
        }
        const std::uint64_t handle = PackHandle(index, node.generation);
        if (!IsAttachedToRoot(handle)) {
            continue;
        }
        if (!PointInVisibleBounds(node, logical_x, logical_y)) {
            continue;
        }

        std::uint32_t depth = 0U;
        for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
            const UINode* parent = Resolve(current);
            if (parent == nullptr) {
                break;
            }
            depth += 1U;
            current = parent->parent_handle;
        }

        const float area = node.layout_width * node.layout_height;
        if (best_handle == UI_INVALID_HANDLE || area < best_area ||
            (area == best_area && depth > best_depth)) {
            best_handle = handle;
            best_area = area;
            best_depth = depth;
        }
    }

    return best_handle;
}

std::uint64_t UiRuntime::ResolveScrollTarget(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    const auto resolve_candidate = [&](std::uint64_t candidate_handle) -> std::uint64_t {
        if (candidate_handle == UI_INVALID_HANDLE) {
            return UI_INVALID_HANDLE;
        }
        const std::uint64_t proxy_handle = FindScrollProxyTarget(candidate_handle, logical_x, logical_y);
        if (proxy_handle != UI_INVALID_HANDLE) {
            return proxy_handle;
        }
        return FindScrollableAncestorContainingPoint(candidate_handle, logical_x, logical_y);
    };

    std::uint64_t resolved_start = start_handle;
    if (resolved_start != UI_INVALID_HANDLE) {
        const UINode* start_node = Resolve(resolved_start);
        const bool contains_point =
            start_node != nullptr &&
            start_node->yg_node != nullptr &&
            IsAttachedToRoot(resolved_start) &&
            PointInVisibleBounds(*start_node, logical_x, logical_y);
        if (!contains_point) {
            resolved_start = UI_INVALID_HANDLE;
        }
    }
    if (resolved_start == UI_INVALID_HANDLE) {
        resolved_start = FindDeepestNodeContainingPoint(root_handle_, logical_x, logical_y);
    }
    const std::uint64_t resolved_handle = resolve_candidate(resolved_start);
    if (resolved_handle != UI_INVALID_HANDLE) {
        return resolved_handle;
    }

    const std::uint64_t best_handle = FindBestNodeContainingPoint(logical_x, logical_y);
    if (best_handle != resolved_start) {
        const std::uint64_t best_resolved_handle = resolve_candidate(best_handle);
        if (best_resolved_handle != UI_INVALID_HANDLE) {
            return best_resolved_handle;
        }
    }
    const std::uint64_t containing_scroll = FindDeepestScrollViewContainingPoint(root_handle_, logical_x, logical_y);
    if (containing_scroll != UI_INVALID_HANDLE) {
        return containing_scroll;
    }
    return UI_INVALID_HANDLE;
}

std::uint64_t UiRuntime::FindScrollableAncestor(std::uint64_t start_handle) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = Resolve(current);
        if (node == nullptr) {
            break;
        }
        if (node->is_scroll_view) {
            return current;
        }
        current = node->parent_handle;
    }
    return UI_INVALID_HANDLE;
}

std::uint64_t UiRuntime::FindScrollableAncestorContainingPoint(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = Resolve(current);
        if (node == nullptr) {
            break;
        }
        if (node->is_scroll_view && PointInVisibleBounds(*node, logical_x, logical_y)) {
            return current;
        }
        current = node->parent_handle;
    }
    return UI_INVALID_HANDLE;
}

bool UiRuntime::CanScrollOnAxis(const UINode& node, bool horizontal) const {
    const float viewport_extent = horizontal ? GetScrollViewportWidth(node) : GetScrollViewportHeight(node);
    const float max_scroll = horizontal
        ? std::max(0.0f, node.scroll_content_width - viewport_extent)
        : std::max(0.0f, node.scroll_content_height - viewport_extent);
    const bool enabled = horizontal ? node.scroll_enabled_x : node.scroll_enabled_y;
    return enabled && max_scroll > 0.0f;
}

bool UiRuntime::CanConsumeScrollDelta(
    const UINode& node,
    bool horizontal,
    float delta,
    bool use_smooth_target) const {
    if (std::abs(delta) < 0.001f || !CanScrollOnAxis(node, horizontal)) {
        return false;
    }
    const float viewport_extent = horizontal ? GetScrollViewportWidth(node) : GetScrollViewportHeight(node);
    const float max_scroll = horizontal
        ? std::max(0.0f, node.scroll_content_width - viewport_extent)
        : std::max(0.0f, node.scroll_content_height - viewport_extent);
    const float offset = use_smooth_target && node.smooth_scroll_active
        ? (horizontal ? node.smooth_scroll_target_x : node.smooth_scroll_target_y)
        : (horizontal ? node.scroll_offset_x : node.scroll_offset_y);
    return delta > 0.0f ? offset < (max_scroll - 0.001f) : offset > 0.001f;
}

bool UiRuntime::CanConsumeScrollDeltaFromTarget(std::uint64_t start_handle, float delta_x, float delta_y) const {
    if (start_handle == UI_INVALID_HANDLE) {
        return false;
    }

    const float abs_x = std::abs(delta_x);
    const float abs_y = std::abs(delta_y);
    if (abs_x < 0.001f && abs_y < 0.001f) {
        return false;
    }

    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = Resolve(current);
        if (node == nullptr) {
            break;
        }
        if (node->is_scroll_view) {
            const bool can_consume_x = abs_x >= 0.001f && CanConsumeScrollDelta(*node, true, delta_x);
            const bool can_consume_y = abs_y >= 0.001f && CanConsumeScrollDelta(*node, false, delta_y);
            if (can_consume_x || can_consume_y) {
                return true;
            }
        }
        current = node->parent_handle;
    }
    return false;
}

std::uint64_t UiRuntime::RetargetScrollHandleForDelta(
    std::uint64_t start_handle,
    float delta_x,
    float delta_y,
    bool use_smooth_target) const {
    if (start_handle == UI_INVALID_HANDLE) {
        return UI_INVALID_HANDLE;
    }

    const float abs_x = std::abs(delta_x);
    const float abs_y = std::abs(delta_y);
    const bool prefer_horizontal = abs_x > abs_y;
    const bool prefer_vertical = abs_y > abs_x;
    std::uint64_t fallback_handle = UI_INVALID_HANDLE;

    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = Resolve(current);
        if (node == nullptr) {
            break;
        }
        if (node->is_scroll_view) {
            const bool can_scroll_x = CanScrollOnAxis(*node, true);
            const bool can_scroll_y = CanScrollOnAxis(*node, false);
            const bool can_consume_x = CanConsumeScrollDelta(*node, true, delta_x, use_smooth_target);
            const bool can_consume_y = CanConsumeScrollDelta(*node, false, delta_y, use_smooth_target);
            if ((prefer_horizontal && can_consume_x) ||
                (prefer_vertical && can_consume_y) ||
                (!prefer_horizontal && !prefer_vertical &&
                 ((abs_x > 0.0f && can_consume_x) || (abs_y > 0.0f && can_consume_y)))) {
                return current;
            }
            if (fallback_handle == UI_INVALID_HANDLE &&
                ((abs_x > 0.0f && can_scroll_x) || (abs_y > 0.0f && can_scroll_y))) {
                fallback_handle = current;
            }
        }
        current = node->parent_handle;
    }

    return fallback_handle != UI_INVALID_HANDLE ? fallback_handle : start_handle;
}

std::uint64_t UiRuntime::FindWheelScrollableTarget(std::uint64_t start_handle, float logical_x, float logical_y) const {
    return ResolveScrollTarget(start_handle, logical_x, logical_y);
}

std::uint64_t UiRuntime::FindScrollProxyTarget(std::uint64_t start_handle, float logical_x, float logical_y) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = Resolve(current);
        if (node == nullptr) {
            break;
        }
        if (node->scroll_proxy_target_handle != UI_INVALID_HANDLE && PointInVisibleBounds(*node, logical_x, logical_y)) {
            const UINode* scroll_node = Resolve(node->scroll_proxy_target_handle);
            if (scroll_node != nullptr && scroll_node->is_scroll_view) {
                return node->scroll_proxy_target_handle;
            }
        }
        current = node->parent_handle;
    }
    return UI_INVALID_HANDLE;
}

std::pair<float, float> UiRuntime::ClampPointToScrollViewport(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    const std::uint64_t scroll_handle = FindScrollableAncestor(start_handle);
    const UINode* scroll_node = Resolve(scroll_handle);
    if (scroll_node == nullptr) {
        return {logical_x, logical_y};
    }
    const Rect viewport = ComputeScrollViewportBounds(*scroll_node, scroll_node->abs_x, scroll_node->abs_y);
    return {
        std::clamp(logical_x, viewport.x, viewport.x + viewport.width),
        std::clamp(logical_y, viewport.y, viewport.y + viewport.height),
    };
}

void UiRuntime::ClearAutoScrollState() {
    auto_scroll_active_ = false;
    auto_scroll_view_handle_ = UI_INVALID_HANDLE;
    auto_scroll_factor_x_ = 0.0f;
    auto_scroll_factor_y_ = 0.0f;
}

std::pair<float, float> UiRuntime::ComputeAutoScrollFactors(
    const UINode& scroll_node,
    float logical_x,
    float logical_y,
    float edge_threshold) const {
    const float hot_zone = std::max(0.001f, edge_threshold > 0.0f ? edge_threshold : scroll_node.edge_hot_zone);
    const Rect viewport = ComputeScrollViewportBounds(scroll_node, scroll_node.abs_x, scroll_node.abs_y);
    const float max_scroll_x = std::max(0.0f, scroll_node.scroll_content_width - viewport.width);
    const float max_scroll_y = std::max(0.0f, scroll_node.scroll_content_height - viewport.height);

    float factor_x = 0.0f;
    float factor_y = 0.0f;
    if (scroll_node.scroll_enabled_x && max_scroll_x > 0.0f) {
        factor_x = ComputeEdgeAutoScrollFactor(logical_x, viewport.x, viewport.x + viewport.width, hot_zone);
    }
    if (scroll_node.scroll_enabled_y && max_scroll_y > 0.0f) {
        factor_y = ComputeEdgeAutoScrollFactor(logical_y, viewport.y, viewport.y + viewport.height, hot_zone);
    }
    return {factor_x, factor_y};
}

bool UiRuntime::UpdateAutoScrollStateForView(
    std::uint64_t scroll_handle,
    float logical_x,
    float logical_y,
    float edge_threshold) {
    const UINode* scroll_node = Resolve(scroll_handle);
    if (scroll_node == nullptr) {
        ClearAutoScrollState();
        return false;
    }

    const auto [factor_x, factor_y] =
        ComputeAutoScrollFactors(*scroll_node, logical_x, logical_y, edge_threshold);
    if (std::abs(factor_x) <= 0.01f && std::abs(factor_y) <= 0.01f) {
        ClearAutoScrollState();
        return false;
    }

    auto_scroll_active_ = true;
    auto_scroll_view_handle_ = scroll_handle;
    auto_scroll_factor_x_ = factor_x;
    auto_scroll_factor_y_ = factor_y;
    return true;
}

void UiRuntime::UpdateAutoScrollState(std::uint64_t start_handle, float logical_x, float logical_y) {
    const std::uint64_t scroll_handle = FindScrollableAncestor(start_handle);
    (void)UpdateAutoScrollStateForView(scroll_handle, logical_x, logical_y, 0.0f);
}

std::uint64_t UiRuntime::SelectionAutoScroll(float logical_x, float logical_y, float edge_threshold) {
    last_pointer_logical_x_ = logical_x;
    last_pointer_logical_y_ = logical_y;

    std::uint64_t start_handle = UI_INVALID_HANDLE;
    if (cross_selection_active_ && cross_selection_dragged_) {
        start_handle = end_node_handle_ != UI_INVALID_HANDLE ? end_node_handle_ : selection_area_handle_;
    } else if (active_selection_handle_ != UI_INVALID_HANDLE && active_selection_dragged_) {
        start_handle = active_selection_handle_;
    }

    if (start_handle == UI_INVALID_HANDLE) {
        ClearAutoScrollState();
        return UI_INVALID_HANDLE;
    }

    const std::uint64_t scroll_handle = FindScrollableAncestor(start_handle);
    if (!UpdateAutoScrollStateForView(scroll_handle, logical_x, logical_y, edge_threshold)) {
        return UI_INVALID_HANDLE;
    }
    return auto_scroll_active_ ? scroll_handle : UI_INVALID_HANDLE;
}

bool UiRuntime::ClearSelection(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }

    if (cross_selection_active_ &&
        (handle == start_node_handle_ ||
         handle == end_node_handle_ ||
         (selection_area_handle_ != UI_INVALID_HANDLE && SubtreeContains(selection_area_handle_, handle)))) {
        ClearCrossSelection(true);
        return true;
    }

    ClearSelectionHighlight(handle, true);
    return true;
}

bool UiRuntime::RetargetSelection(std::uint64_t from_handle, std::uint64_t to_handle) {
    UINode* from_node = ResolveMutable(from_handle);
    UINode* to_node = ResolveMutable(to_handle);
    if (from_node == nullptr || to_node == nullptr || !from_node->is_text_node || !to_node->is_text_node) {
        return false;
    }
    if (from_handle == to_handle) {
        return true;
    }

    const std::uint32_t to_length = static_cast<std::uint32_t>(to_node->text_content.size());
    const std::uint32_t from_caret = std::min<std::uint32_t>(
        from_node->selection_end,
        static_cast<std::uint32_t>(from_node->text_content.size()));
    const std::uint32_t retargeted_start = std::min<std::uint32_t>(from_node->selection_start, to_length);
    const std::uint32_t retargeted_end = std::min<std::uint32_t>(from_node->selection_end, to_length);
    const bool moved_single_selection = from_node->selection_start != from_node->selection_end;
    if (moved_single_selection || to_node->selection_start != retargeted_start || to_node->selection_end != retargeted_end) {
        to_node->selection_start = retargeted_start;
        to_node->selection_end = retargeted_end;
        to_node->is_dirty = true;
    }
    from_node->selection_start = from_caret;
    from_node->selection_end = from_caret;
    from_node->is_dirty = true;
    if (active_selection_handle_ == from_handle) {
        active_selection_handle_ = to_handle;
    }

    bool moved_cross_selection = false;
    if (cross_selection_active_) {
        if (start_node_handle_ == from_handle) {
            start_node_handle_ = to_handle;
            moved_cross_selection = true;
        }
        if (end_node_handle_ == from_handle) {
            end_node_handle_ = to_handle;
            moved_cross_selection = true;
        }
        if (moved_cross_selection) {
            const std::uint64_t previous_area_handle = selection_area_handle_;
            const std::uint64_t next_area_handle = FindSelectionAreaAncestor(to_handle);
            if (next_area_handle == UI_INVALID_HANDLE) {
                ClearCrossSelection(true);
            } else {
                if (!selection_area_nodes_.empty()) {
                    MarkSelectionAreaNodesDirty();
                }
                selection_area_handle_ = next_area_handle;
                selection_area_nodes_.clear();
                selection_area_nodes_dirty_ = true;
                EnsureSelectionAreaNodes(selection_area_handle_);
                if (!selection_area_nodes_.empty()) {
                    MarkSelectionAreaNodesDirty();
                }
                if (previous_area_handle != UI_INVALID_HANDLE && previous_area_handle != next_area_handle) {
                    as_on_cross_selection_changed(previous_area_handle, nullptr, 0U);
                    NotifyCrossSelectionChanged();
                }
            }
        }
    }

    layout_dirty_ = true;
    return true;
}

bool UiRuntime::IsPointInSelection(float logical_x, float logical_y) {
    if (root_handle_ == UI_INVALID_HANDLE) {
        return false;
    }

    if (std::any_of(current_selection_hit_rects_.begin(), current_selection_hit_rects_.end(), [logical_x, logical_y](const Rect& rect) {
        return PointInRect(rect, logical_x, logical_y);
    })) {
        return true;
    }

    const auto point_hits_node_selection = [this, logical_x, logical_y](std::uint64_t handle) {
        const UINode* node = Resolve(handle);
        if (node == nullptr ||
            !node->is_text_node ||
            !node->is_selectable ||
            node->selection_start == node->selection_end ||
            !PointInVisibleBounds(*node, logical_x, logical_y)) {
            return false;
        }
        const std::vector<Rect> rects = BuildSelectionRects(*node, node->selection_start, node->selection_end);
        const float local_x = logical_x - node->abs_x;
        const float local_y = logical_y - node->abs_y;
        return std::any_of(rects.begin(), rects.end(), [local_x, local_y](const Rect& rect) {
            return PointInRect(rect, local_x, local_y);
        });
    };

    if (cross_selection_active_) {
        EnsureSelectionAreaNodes(selection_area_handle_);
        for (const std::uint64_t handle : selection_area_nodes_) {
            const UINode* node = Resolve(handle);
            if (node == nullptr ||
                !node->is_text_node ||
                !node->is_selectable ||
                !PointInVisibleBounds(*node, logical_x, logical_y)) {
                continue;
            }

            std::uint32_t highlight_start = 0U;
            std::uint32_t highlight_end = 0U;
            if (!GetCrossSelectionHighlight(handle, highlight_start, highlight_end)) {
                continue;
            }

            const std::vector<Rect> rects = BuildSelectionRects(*node, highlight_start, highlight_end);
            const float local_x = logical_x - node->abs_x;
            const float local_y = logical_y - node->abs_y;
            if (std::any_of(rects.begin(), rects.end(), [local_x, local_y](const Rect& rect) {
                return PointInRect(rect, local_x, local_y);
            })) {
                return true;
            }
        }
        return false;
    }

    if (point_hits_node_selection(focused_handle_)) {
        return true;
    }
    if (active_selection_handle_ != focused_handle_ && point_hits_node_selection(active_selection_handle_)) {
        return true;
    }
    return false;
}

bool UiRuntime::ClearCurrentSelection(bool notify_callback) {
    if (cross_selection_active_) {
        ClearCrossSelection(notify_callback);
        return true;
    }

    const auto clear_node_selection = [this, notify_callback](std::uint64_t handle) {
        UINode* node = ResolveMutable(handle);
        if (node == nullptr || !node->is_text_node || !node->is_selectable || node->selection_start == node->selection_end) {
            return false;
        }
        ClearSelectionHighlight(handle, notify_callback);
        return true;
    };

    if (clear_node_selection(focused_handle_)) {
        return true;
    }
    if (active_selection_handle_ != focused_handle_ && clear_node_selection(active_selection_handle_)) {
        return true;
    }
    return false;
}

bool UiRuntime::CopyCurrentSelection() const {
    if (cross_selection_active_) {
        const std::string stitched = BuildCrossSelectionText();
        if (stitched.empty()) {
            return false;
        }
        std::string rich_json{};
        std::string rich_plain_text{};
        if (BuildCrossSelectionRichPayload(rich_plain_text, rich_json)) {
            EmitClipboardWrite(rich_plain_text, &rich_json);
            return true;
        }
        EmitClipboardWrite(stitched);
        return true;
    }

    const auto copy_node_selection = [this](std::uint64_t handle) {
        const UINode* node = Resolve(handle);
        if (node == nullptr || !node->is_text_node || !node->is_selectable || node->is_obscured || node->selection_start == node->selection_end) {
            return false;
        }
        HandleCopy(*node);
        return true;
    };

    if (copy_node_selection(focused_handle_)) {
        return true;
    }
    if (active_selection_handle_ != focused_handle_ && copy_node_selection(active_selection_handle_)) {
        return true;
    }
    return false;
}

void UiRuntime::ClearSelectionHighlight(std::uint64_t handle, bool notify_callback) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable) {
        return;
    }

    current_selection_hit_rects_.clear();
    selection_horizontal_extend_active_ = false;
    const std::uint32_t caret = std::min<std::uint32_t>(node->selection_end, static_cast<std::uint32_t>(node->text_content.size()));
    selection_anchor_handle_ = handle;
    selection_anchor_index_ = caret;
    if (node->selection_start == node->selection_end) {
        return;
    }

    node->selection_start = caret;
    node->selection_end = caret;
    node->last_interaction_time = interaction_time_ms_;
    MarkTextSelectionVisualsDirty(*node);
    if (notify_callback) {
        as_on_selection_changed(handle, caret, caret);
    }
}

bool UiRuntime::SetInteractive(std::uint64_t handle, bool interactive) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->is_interactive = interactive;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetPreserveSelectionOnPointerDown(std::uint64_t handle, bool preserve) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->preserves_selection_on_pointer_down = preserve;
    return true;
}

bool UiRuntime::PreservesSelectionOnPointerDown(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    return node != nullptr && node->preserves_selection_on_pointer_down;
}

bool UiRuntime::SetEditorCommandKeys(std::uint64_t handle, bool enabled) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    node->uses_editor_command_keys = enabled;
    return true;
}

bool UiRuntime::SetEditorAcceptsTab(std::uint64_t handle, bool enabled) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    node->accepts_tab = enabled;
    return true;
}

bool UiRuntime::SetScrollProxyTarget(std::uint64_t handle, std::uint64_t scroll_handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    if (scroll_handle != UI_INVALID_HANDLE) {
        const UINode* scroll_node = Resolve(scroll_handle);
        if (scroll_node == nullptr || !scroll_node->is_scroll_view) {
            return false;
        }
    }
    node->scroll_proxy_target_handle = scroll_handle;
    return true;
}

bool UiRuntime::SetFocusable(std::uint64_t handle, bool focusable, std::int32_t tab_index) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->is_focusable = focusable;
    node->tab_index = tab_index;
    InvalidateFocusOrder();
    if (!focusable && focused_handle_ == handle) {
        SetFocus(UI_INVALID_HANDLE);
    }
    if (focusable && !pending_focus_node_id_.empty() && node->node_id == pending_focus_node_id_) {
        RestorePendingFocusIfPossible();
    }
    return true;
}

bool UiRuntime::RequestFocus(std::uint64_t handle) {
    if (handle == UI_INVALID_HANDLE) {
        SetFocus(UI_INVALID_HANDLE);
        return true;
    }
    const UINode* node = Resolve(handle);
    if (node == nullptr || !node->is_focusable || node->visibility != UI_VISIBILITY_NORMAL) {
        return false;
    }
    SetFocus(handle);
    return true;
}

namespace {

bool IsRepeatClick(
    std::uint64_t handle,
    float logical_x,
    float logical_y,
    std::uint64_t interaction_time_ms,
    std::uint64_t last_handle,
    float last_x,
    float last_y,
    std::uint64_t last_time_ms,
    std::uint64_t threshold_ms) {
    return handle == last_handle &&
        std::abs(logical_x - last_x) < 5.0f &&
        std::abs(logical_y - last_y) < 5.0f &&
        interaction_time_ms >= last_time_ms &&
        (interaction_time_ms - last_time_ms) < threshold_ms;
}

} // namespace

void UiRuntime::HandlePointerEvent(
    std::uint32_t event_enum,
    std::uint64_t handle,
    float logical_x,
    float logical_y,
    std::int32_t pointer_id,
    std::uint32_t pointer_type,
    std::int32_t button,
    std::uint32_t buttons,
    float pressure,
    float width,
    float height,
    std::int32_t click_count,
    std::uint32_t modifiers) {
    (void)pointer_id;
    (void)buttons;
    (void)pressure;
    (void)width;
    (void)height;
    (void)click_count;
    const float previous_pointer_x = last_pointer_logical_x_;
    const float previous_pointer_y = last_pointer_logical_y_;
    bool handled_cross_selection = false;
    const bool is_primary_pointer_button =
        button == 0 ||
        ((event_enum == UI_EVENT_POINTER_MOVE || event_enum == UI_EVENT_POINTER_UP) && primary_pointer_down_);
    const bool allow_pointer_text_drag =
        is_primary_pointer_button && (pointer_type != UI_POINTER_TYPE_TOUCH || selection_handle_drag_active_);
    const bool allow_pointer_text_down_selection =
        is_primary_pointer_button && pointer_type != UI_POINTER_TYPE_TOUCH;
    const auto should_use_trailing_caret_edge = [this](const UINode& node, std::uint32_t index, float local_x, float local_y) {
        const auto [leading_x, leading_line] = GetLocalPositionFromIndex(node, index, false);
        const auto [trailing_x, trailing_line] = GetLocalPositionFromIndex(node, index, true);
        if (trailing_line < 0 || leading_line < 0 || trailing_line + 1 != leading_line) {
            return false;
        }
        const float leading_line_top = GetLineTopForIndex(node, static_cast<std::size_t>(leading_line));
        const float leading_line_height = GetLineHeightForIndex(node, static_cast<std::size_t>(leading_line));
        const float x_slop = std::max(4.0f, std::min(node.font_size * 0.5f, 10.0f));
        (void)leading_x;
        return local_x >= trailing_x - x_slop &&
            local_y <= leading_line_top + (leading_line_height * 0.5f);
    };
    const auto is_text_target_in_active_selection_area = [this](std::uint64_t candidate_handle) {
        if (candidate_handle == UI_INVALID_HANDLE || selection_area_handle_ == UI_INVALID_HANDLE) {
            return false;
        }
        const UINode* candidate = Resolve(candidate_handle);
        return candidate != nullptr &&
            candidate->is_text_node &&
            candidate->is_selectable &&
            FindSelectionAreaAncestor(candidate_handle) == selection_area_handle_;
    };

    if (event_enum == UI_EVENT_POINTER_DOWN) {
        primary_pointer_down_ = button == 0;
    } else if (event_enum == UI_EVENT_POINTER_UP || event_enum == UI_EVENT_POINTER_LEAVE) {
        primary_pointer_down_ = false;
    }

    if (event_enum == UI_EVENT_POINTER_MOVE && handle != last_hovered_handle_) {
        ClearHover(last_hovered_handle_);
        if (handle != UI_INVALID_HANDLE) {
            as_on_pointer_event(handle, UI_EVENT_POINTER_ENTER);
        }
        last_hovered_handle_ = handle;
    }

    UINode* selection_node = ResolveMutable(active_selection_handle_);
    if (selection_node == nullptr) {
        active_selection_handle_ = UI_INVALID_HANDLE;
    }
    UINode* scroll_drag_node = ResolveMutable(active_scroll_handle_);
    if (scroll_drag_node == nullptr) {
        active_scroll_handle_ = UI_INVALID_HANDLE;
    }

    if (event_enum == UI_EVENT_POINTER_DOWN) {
        UINode* down_node = handle == UI_INVALID_HANDLE ? nullptr : ResolveMutable(handle);
        const bool down_is_selectable_text =
            down_node != nullptr && down_node->is_text_node && down_node->is_selectable;
        const bool down_preserves_selection =
            down_node != nullptr && down_node->preserves_selection_on_pointer_down;
        touch_text_tap_handle_ = UI_INVALID_HANDLE;
        touch_text_tap_moved_ = false;
        if (pointer_type == UI_POINTER_TYPE_TOUCH && down_is_selectable_text) {
            touch_text_tap_handle_ = handle;
            touch_text_tap_logical_x_ = logical_x;
            touch_text_tap_logical_y_ = logical_y;
        }
        if (!down_is_selectable_text && !down_preserves_selection) {
            const bool cleared_cross_selection = cross_selection_active_;
            if (cleared_cross_selection) {
                ClearCrossSelection(true);
            }
            if (!cleared_cross_selection && focused_handle_ != UI_INVALID_HANDLE) {
                ClearSelectionHighlight(focused_handle_, true);
            }
            if (active_selection_handle_ != UI_INVALID_HANDLE) {
                if (active_selection_handle_ != focused_handle_) {
                    ClearSelectionHighlight(active_selection_handle_, true);
                }
                active_selection_handle_ = UI_INVALID_HANDLE;
                active_selection_dragged_ = false;
                selection_handle_drag_active_ = false;
                active_selection_drag_endpoint_ = 1U;
                active_selection_stationary_index_ = 0U;
                selection_node = nullptr;
            }
            if (handle == UI_INVALID_HANDLE ||
                (down_node != nullptr && !down_node->is_focusable && !down_node->is_selectable && !down_node->is_selection_area)) {
                SetFocus(UI_INVALID_HANDLE);
            }
        }
    }

    if (event_enum == UI_EVENT_POINTER_DOWN && handle != UI_INVALID_HANDLE) {
        if (IsRepeatClick(
                handle,
                logical_x,
                logical_y,
                interaction_time_ms_,
                last_click_handle_,
                last_click_x_,
                last_click_y_,
                last_click_time_ms_,
                double_click_threshold_ms_)) {
            click_count_ += 1U;
        } else {
            click_count_ = 1U;
        }
        last_click_handle_ = handle;
        last_click_x_ = logical_x;
        last_click_y_ = logical_y;
        last_click_time_ms_ = interaction_time_ms_;

        UINode* down_node = ResolveMutable(handle);
        const bool down_preserves_selection =
            down_node != nullptr && down_node->preserves_selection_on_pointer_down;
        const std::uint64_t area_handle =
            (down_node != nullptr && down_node->is_text_node && down_node->is_selectable)
            ? FindSelectionAreaAncestor(handle)
            : static_cast<std::uint64_t>(UI_INVALID_HANDLE);
        if (allow_pointer_text_down_selection && area_handle != UI_INVALID_HANDLE) {
            if (cross_selection_active_ && selection_area_handle_ != area_handle) {
                ClearCrossSelection(true);
            }
            EnsureSelectionAreaNodes(area_handle);
            if (!selection_area_nodes_.empty()) {
                SetFocus(handle, false, false);
                const std::uint32_t index =
                    GetStringIndexFromPoint(*down_node, logical_x - down_node->abs_x, logical_y - down_node->abs_y);
                const bool allow_repeat_text_selection = pointer_type != UI_POINTER_TYPE_TOUCH;
                cross_selection_active_ = true;
                cross_selection_dragged_ = false;
                selection_area_handle_ = area_handle;
                if (allow_repeat_text_selection && click_count_ >= 3U) {
                    const auto [paragraph_start, paragraph_end] = GetParagraphBoundaries(*down_node, index);
                    start_node_handle_ = handle;
                    start_index_ = paragraph_start;
                    end_node_handle_ = handle;
                    end_index_ = paragraph_end;
                } else if (allow_repeat_text_selection && click_count_ == 2U) {
                    const auto [word_start, word_end] = GetWordBoundaries(*down_node, index);
                    start_node_handle_ = handle;
                    start_index_ = word_start;
                    end_node_handle_ = handle;
                    end_index_ = word_end;
                } else if ((modifiers & UI_KEY_MOD_SHIFT) != 0U &&
                    start_node_handle_ != UI_INVALID_HANDLE &&
                    selection_area_handle_ == area_handle) {
                    end_node_handle_ = handle;
                    end_index_ = index;
                } else {
                    start_node_handle_ = handle;
                    start_index_ = index;
                    end_node_handle_ = handle;
                    end_index_ = index;
                }
                active_selection_handle_ = UI_INVALID_HANDLE;
                active_selection_dragged_ = false;
                active_scroll_handle_ = UI_INVALID_HANDLE;
                active_scroll_dragged_ = false;
                selection_press_logical_x_ = logical_x;
                selection_press_logical_y_ = logical_y;
                selection_node = nullptr;
                scroll_drag_node = nullptr;
                handled_cross_selection = true;
                MarkSelectionAreaNodesDirty();
            }
        } else if (area_handle == UI_INVALID_HANDLE && cross_selection_active_ && !down_preserves_selection) {
            ClearCrossSelection(true);
        }

        if (!handled_cross_selection) {
            if (allow_pointer_text_down_selection) {
                if (UINode* node = ResolveMutable(handle); node != nullptr && node->is_selectable) {
                    SetFocus(handle, false, false);
                    const std::uint32_t index =
                        GetStringIndexFromPoint(*node, logical_x - node->abs_x, logical_y - node->abs_y);
                    const bool allow_repeat_text_selection = pointer_type != UI_POINTER_TYPE_TOUCH;
                    const bool has_existing_selection_state =
                        node->last_interaction_time != 0U || node->selection_start != 0U || node->selection_end != 0U;
                    if (allow_repeat_text_selection && click_count_ >= 2U && node->is_obscured) {
                        node->selection_start = 0U;
                        node->selection_end = static_cast<std::uint32_t>(node->text_content.size());
                    } else if (allow_repeat_text_selection && click_count_ >= 3U) {
                        const auto [paragraph_start, paragraph_end] = GetParagraphBoundaries(*node, index);
                        node->selection_start = paragraph_start;
                        node->selection_end = paragraph_end;
                    } else if (allow_repeat_text_selection && click_count_ == 2U) {
                        const auto [word_start, word_end] = GetWordBoundaries(*node, index);
                        node->selection_start = word_start;
                        node->selection_end = word_end;
                    } else if ((modifiers & UI_KEY_MOD_SHIFT) != 0U && has_existing_selection_state) {
                        node->selection_end = index;
                    } else {
                        node->selection_start = index;
                        node->selection_end = index;
                    }
                    node->caret_trailing_edge =
                        node->selection_start == node->selection_end &&
                        should_use_trailing_caret_edge(*node, index, logical_x - node->abs_x, logical_y - node->abs_y);
                    node->last_interaction_time = interaction_time_ms_;
                    MarkTextSelectionVisualsDirty(*node);
                    active_selection_handle_ = handle;
                    active_selection_dragged_ = false;
                    selection_press_logical_x_ = logical_x;
                    selection_press_logical_y_ = logical_y;
                    selection_anchor_handle_ = handle;
                    selection_anchor_index_ = node->selection_start;
                    selection_node = node;
                }
            }

            const bool blocks_scroll_drag =
                down_node != nullptr &&
                (down_node->is_focusable ||
                 down_node->is_selectable ||
                 down_node->is_selection_area ||
                 down_node->preserves_selection_on_pointer_down ||
                 down_node->is_interactive);
            if (selection_node == nullptr && !blocks_scroll_drag) {
                const std::uint64_t scroll_handle = ResolveScrollTarget(handle, logical_x, logical_y);
                if (scroll_handle != UI_INVALID_HANDLE) {
                    active_scroll_handle_ = scroll_handle;
                    active_scroll_dragged_ = false;
                    scroll_drag_node = ResolveMutable(scroll_handle);
                    if (scroll_drag_node != nullptr) {
                        scroll_drag_node->scroll_velocity_x = 0.0f;
                        scroll_drag_node->scroll_velocity_y = 0.0f;
                    }
                }
            } else {
                active_scroll_handle_ = UI_INVALID_HANDLE;
                active_scroll_dragged_ = false;
            }
        }
    }

    if (event_enum == UI_EVENT_POINTER_MOVE &&
        pointer_type == UI_POINTER_TYPE_TOUCH &&
        !selection_handle_drag_active_ &&
        touch_text_tap_handle_ != UI_INVALID_HANDLE) {
        touch_text_tap_moved_ = touch_text_tap_moved_ ||
            MovedBeyondSelectionDragThreshold(
                touch_text_tap_logical_x_,
                touch_text_tap_logical_y_,
                logical_x,
                logical_y);
        if (touch_text_tap_moved_) {
            UINode* touch_node = ResolveMutable(touch_text_tap_handle_);
            if (touch_node != nullptr &&
                touch_node->is_text_node &&
                touch_node->is_selectable &&
                IsEditorTextNode(*touch_node)) {
                const std::uint32_t touch_index = GetStringIndexFromPoint(
                    *touch_node,
                    logical_x - touch_node->abs_x,
                    logical_y - touch_node->abs_y);
                (void)SetTextSelectionRange(touch_text_tap_handle_, touch_index, touch_index);
                UpdateAutoScrollState(touch_text_tap_handle_, logical_x, logical_y);
                if (UINode* updated_touch_node = ResolveMutable(touch_text_tap_handle_); updated_touch_node != nullptr) {
                    updated_touch_node->caret_trailing_edge =
                        should_use_trailing_caret_edge(
                            *updated_touch_node,
                            touch_index,
                            logical_x - updated_touch_node->abs_x,
                            logical_y - updated_touch_node->abs_y);
                    MarkTextSelectionVisualsDirty(*updated_touch_node);
                    EnsureTextCaretVisible(touch_text_tap_handle_, *updated_touch_node);
                    pending_caret_visibility_handle_ = touch_text_tap_handle_;
                }
                handled_cross_selection = true;
            }
        }
    }

    if (cross_selection_active_ &&
        ((event_enum == UI_EVENT_POINTER_MOVE && primary_pointer_down_) ||
         event_enum == UI_EVENT_POINTER_UP)) {
        const float selection_drag_logical_x = logical_x;
        const float selection_drag_logical_y =
            selection_handle_drag_active_ ? logical_y - kSelectionHandleCenterToTextHitOffset : logical_y;
        const bool is_drag_update = event_enum == UI_EVENT_POINTER_MOVE && primary_pointer_down_;
        if (is_drag_update && allow_pointer_text_drag) {
            cross_selection_dragged_ = cross_selection_dragged_ ||
                MovedBeyondSelectionDragThreshold(
                    selection_press_logical_x_,
                    selection_press_logical_y_,
                    logical_x,
                    logical_y);
        }
        const std::uint64_t selection_handle =
            selection_handle_drag_active_ && active_selection_drag_endpoint_ == 0U && start_node_handle_ != UI_INVALID_HANDLE
            ? start_node_handle_
            : (end_node_handle_ != UI_INVALID_HANDLE ? end_node_handle_ : (handle != UI_INVALID_HANDLE ? handle : selection_area_handle_));
        if (cross_selection_dragged_) {
            if (is_text_target_in_active_selection_area(handle)) {
                handled_cross_selection =
                    UpdateCrossSelectionEndpoint(handle, selection_drag_logical_x, selection_drag_logical_y) ||
                    handled_cross_selection;
            } else {
                const auto [clamped_x, clamped_y] =
                    ClampPointToScrollViewport(selection_handle, selection_drag_logical_x, selection_drag_logical_y);
                const bool point_was_clamped =
                    clamped_x != selection_drag_logical_x || clamped_y != selection_drag_logical_y;
                handled_cross_selection =
                    UpdateCrossSelectionEndpoint(point_was_clamped ? UI_INVALID_HANDLE : handle, clamped_x, clamped_y) ||
                    handled_cross_selection;
            }
        }
        if (event_enum == UI_EVENT_POINTER_UP) {
            if (BuildCrossSelectionText().empty()) {
                ClearCrossSelection(true);
            } else {
                if (selection_handle_drag_active_) {
                    NormalizeCrossSelectionEndpoints();
                }
                cross_selection_horizontal_extend_active_ = true;
                NotifyCrossSelectionChanged();
            }
            cross_selection_dragged_ = false;
            selection_handle_drag_active_ = false;
            active_selection_drag_endpoint_ = 1U;
        } else if (selection_handle_drag_active_ && cross_selection_dragged_) {
            NotifyCrossSelectionChanged();
        }
    }

    if (event_enum == UI_EVENT_POINTER_UP &&
        pointer_type == UI_POINTER_TYPE_TOUCH &&
        touch_text_tap_handle_ != UI_INVALID_HANDLE) {
        if (!touch_text_tap_moved_) {
            UINode* tap_node = ResolveMutable(touch_text_tap_handle_);
            bool tap_inside_existing_selection = false;
            bool placed_touch_caret = false;
            if (tap_node != nullptr && tap_node->is_text_node && tap_node->is_selectable) {
                const std::uint32_t tap_index = GetStringIndexFromPoint(
                    *tap_node,
                    logical_x - tap_node->abs_x,
                    logical_y - tap_node->abs_y);
                if (IsEditorTextNode(*tap_node)) {
                    (void)SetTextSelectionRange(touch_text_tap_handle_, tap_index, tap_index);
                    if (UINode* updated_tap_node = ResolveMutable(touch_text_tap_handle_); updated_tap_node != nullptr) {
                        updated_tap_node->caret_trailing_edge =
                            should_use_trailing_caret_edge(
                                *updated_tap_node,
                                tap_index,
                                logical_x - updated_tap_node->abs_x,
                                logical_y - updated_tap_node->abs_y);
                        MarkTextSelectionVisualsDirty(*updated_tap_node);
                        EnsureTextCaretVisible(touch_text_tap_handle_, *updated_tap_node);
                        pending_caret_visibility_handle_ = touch_text_tap_handle_;
                    }
                    handled_cross_selection = true;
                    placed_touch_caret = true;
                } else if (cross_selection_active_ &&
                    touch_text_tap_handle_ == start_node_handle_ &&
                    touch_text_tap_handle_ == end_node_handle_) {
                    const std::uint32_t selection_start = std::min(start_index_, end_index_);
                    const std::uint32_t selection_end = std::max(start_index_, end_index_);
                    tap_inside_existing_selection = tap_index >= selection_start && tap_index <= selection_end;
                } else if (!cross_selection_active_ && tap_node->selection_start != tap_node->selection_end) {
                    const std::uint32_t selection_start = std::min(tap_node->selection_start, tap_node->selection_end);
                    const std::uint32_t selection_end = std::max(tap_node->selection_start, tap_node->selection_end);
                    tap_inside_existing_selection = tap_index >= selection_start && tap_index <= selection_end;
                }
            }
            if (placed_touch_caret) {
                // The caret placement above already cleared cross-selection if needed.
            } else if (cross_selection_active_) {
                if (!tap_inside_existing_selection) {
                    ClearCrossSelection(true);
                    handled_cross_selection = true;
                }
            } else if (!tap_inside_existing_selection && tap_node != nullptr && tap_node->selection_start != tap_node->selection_end) {
                ClearSelectionHighlight(touch_text_tap_handle_, true);
            }
        }
        touch_text_tap_handle_ = UI_INVALID_HANDLE;
        touch_text_tap_moved_ = false;
    }

    if (allow_pointer_text_drag &&
        !handled_cross_selection && event_enum == UI_EVENT_POINTER_MOVE && primary_pointer_down_ && selection_node != nullptr) {
        active_selection_dragged_ = active_selection_dragged_ ||
            MovedBeyondSelectionDragThreshold(
                selection_press_logical_x_,
                selection_press_logical_y_,
                logical_x,
                logical_y);
        if (active_selection_dragged_) {
            const float selection_drag_logical_x = logical_x;
            const float selection_drag_logical_y =
                selection_handle_drag_active_ ? logical_y - kSelectionHandleCenterToTextHitOffset : logical_y;
            const auto [clamped_x, clamped_y] =
                ClampPointToScrollViewport(active_selection_handle_, selection_drag_logical_x, selection_drag_logical_y);
            const std::uint32_t next_selection_end = GetStringIndexFromPoint(
                *selection_node,
                clamped_x - selection_node->abs_x,
                clamped_y - selection_node->abs_y);
            std::uint32_t& dragged_endpoint =
                active_selection_drag_endpoint_ == 0U
                ? selection_node->selection_start
                : selection_node->selection_end;
            const std::uint32_t previous_start = selection_node->selection_start;
            const std::uint32_t previous_end = selection_node->selection_end;
            if (selection_handle_drag_active_) {
                selection_node->selection_start = std::min(active_selection_stationary_index_, next_selection_end);
                selection_node->selection_end = std::max(active_selection_stationary_index_, next_selection_end);
            } else {
                dragged_endpoint = next_selection_end;
            }
            if (selection_node->selection_start != previous_start || selection_node->selection_end != previous_end) {
                MarkTextSelectionVisualsDirty(*selection_node);
                if (selection_handle_drag_active_) {
                    as_on_selection_changed(
                        active_selection_handle_,
                        selection_node->selection_start,
                        selection_node->selection_end);
                }
            }
        }
    }

    if (!handled_cross_selection && event_enum == UI_EVENT_POINTER_MOVE && primary_pointer_down_ &&
        scroll_drag_node != nullptr && selection_node == nullptr) {
        const float delta_x = logical_x - previous_pointer_x;
        const float delta_y = logical_y - previous_pointer_y;
        active_scroll_dragged_ = active_scroll_dragged_ || std::abs(delta_x) > 0.0f || std::abs(delta_y) > 0.0f;
        (void)ApplyScrollOffset(
            active_scroll_handle_,
            *scroll_drag_node,
            scroll_drag_node->scroll_offset_x - delta_x,
            scroll_drag_node->scroll_offset_y - delta_y,
            true);
        scroll_drag_node->scroll_velocity_x = PixelsPerSecond(-delta_x, kNominalFrameMs);
        scroll_drag_node->scroll_velocity_y = PixelsPerSecond(-delta_y, kNominalFrameMs);
    }

    if (!handled_cross_selection &&
        event_enum == UI_EVENT_POINTER_UP &&
        selection_node != nullptr) {
        if (active_selection_dragged_) {
            const float selection_drag_logical_x = logical_x;
            const float selection_drag_logical_y =
                selection_handle_drag_active_ ? logical_y - kSelectionHandleCenterToTextHitOffset : logical_y;
            const auto [clamped_x, clamped_y] =
                ClampPointToScrollViewport(active_selection_handle_, selection_drag_logical_x, selection_drag_logical_y);
            std::uint32_t& dragged_endpoint =
                active_selection_drag_endpoint_ == 0U
                ? selection_node->selection_start
                : selection_node->selection_end;
            const std::uint32_t next_selection_end = GetStringIndexFromPoint(
                *selection_node,
                clamped_x - selection_node->abs_x,
                clamped_y - selection_node->abs_y);
            if (selection_handle_drag_active_) {
                selection_node->selection_start = std::min(active_selection_stationary_index_, next_selection_end);
                selection_node->selection_end = std::max(active_selection_stationary_index_, next_selection_end);
            } else {
                dragged_endpoint = next_selection_end;
            }
        }
        selection_node->last_interaction_time = interaction_time_ms_;
        MarkTextSelectionVisualsDirty(*selection_node);
        selection_horizontal_extend_active_ = selection_node->selection_start != selection_node->selection_end;
        EnsureTextCaretVisible(active_selection_handle_, *selection_node);
        pending_caret_visibility_handle_ = active_selection_handle_;
        as_on_selection_changed(
            active_selection_handle_,
            selection_node->selection_start,
            selection_node->selection_end);
        active_selection_handle_ = UI_INVALID_HANDLE;
        active_selection_dragged_ = false;
        selection_handle_drag_active_ = false;
        active_selection_drag_endpoint_ = 1U;
        active_selection_stationary_index_ = 0U;
    }

    if (!handled_cross_selection &&
        (event_enum == UI_EVENT_POINTER_UP || event_enum == UI_EVENT_POINTER_LEAVE) &&
        scroll_drag_node != nullptr) {
        if (!active_scroll_dragged_) {
            scroll_drag_node->scroll_velocity_x = 0.0f;
            scroll_drag_node->scroll_velocity_y = 0.0f;
        }
        active_scroll_handle_ = UI_INVALID_HANDLE;
        active_scroll_dragged_ = false;
    }

    if (
        !handled_cross_selection &&
        event_enum == UI_EVENT_POINTER_MOVE &&
        primary_pointer_down_ &&
        scroll_drag_node != nullptr) {
        UpdateAutoScrollState(
            handle != UI_INVALID_HANDLE ? handle : active_scroll_handle_,
            logical_x,
            logical_y);
    } else if (event_enum == UI_EVENT_POINTER_UP || event_enum == UI_EVENT_POINTER_LEAVE) {
        ClearAutoScrollState();
    }

    if (event_enum == UI_EVENT_POINTER_DOWN && handle != UI_INVALID_HANDLE) {
        if (const UINode* node = Resolve(handle); node != nullptr &&
            (node->is_focusable || node->is_selectable || node->is_selection_area)) {
            SetFocus(handle, false, false);
        }
    }

    if (handle != UI_INVALID_HANDLE) {
        as_on_pointer_event(handle, static_cast<UiEvent>(event_enum));
    }
    last_pointer_logical_x_ = logical_x;
    last_pointer_logical_y_ = logical_y;
}

void UiRuntime::HandleWheelEvent(float delta_x, float delta_y) {
    const std::uint64_t scroll_handle = RetargetScrollHandleForDelta(
        FindWheelScrollableTarget(UI_INVALID_HANDLE, last_pointer_logical_x_, last_pointer_logical_y_),
        delta_x,
        delta_y,
        true);
    if (scroll_handle == UI_INVALID_HANDLE) {
        return;
    }
    UINode* scroll_node = ResolveMutable(scroll_handle);
    if (scroll_node == nullptr) {
        return;
    }
    scroll_node->scroll_velocity_x = 0.0f;
    scroll_node->scroll_velocity_y = 0.0f;
    if (scroll_node->smooth_scrolling) {
        const float max_x = std::max(
            0.0f,
            scroll_node->scroll_content_width - GetScrollViewportWidth(*scroll_node));
        const float max_y = std::max(
            0.0f,
            scroll_node->scroll_content_height - GetScrollViewportHeight(*scroll_node));
        const float base_x = scroll_node->smooth_scroll_active
            ? scroll_node->smooth_scroll_target_x
            : scroll_node->scroll_offset_x;
        const float base_y = scroll_node->smooth_scroll_active
            ? scroll_node->smooth_scroll_target_y
            : scroll_node->scroll_offset_y;
        scroll_node->smooth_scroll_target_x = scroll_node->scroll_enabled_x
            ? std::clamp(base_x + delta_x, 0.0f, max_x)
            : 0.0f;
        scroll_node->smooth_scroll_target_y = scroll_node->scroll_enabled_y
            ? std::clamp(base_y + delta_y, 0.0f, max_y)
            : 0.0f;
        scroll_node->smooth_scroll_active =
            std::abs(scroll_node->smooth_scroll_target_x - scroll_node->scroll_offset_x) >= 0.001f ||
            std::abs(scroll_node->smooth_scroll_target_y - scroll_node->scroll_offset_y) >= 0.001f;
        return;
    }
    (void)ApplyScrollOffset(
        scroll_handle,
        *scroll_node,
        scroll_node->scroll_offset_x + delta_x,
        scroll_node->scroll_offset_y + delta_y,
        true);
}

void UiRuntime::BeginTouchScroll(std::uint64_t handle, float logical_x, float logical_y, double timestamp_ms) {
    const std::uint64_t scroll_handle = ResolveScrollTarget(handle, logical_x, logical_y);
    if (scroll_handle == UI_INVALID_HANDLE) {
        active_scroll_handle_ = UI_INVALID_HANDLE;
        active_touch_scroll_handle_x_ = UI_INVALID_HANDLE;
        active_touch_scroll_handle_y_ = UI_INVALID_HANDLE;
        active_scroll_dragged_ = false;
        has_last_touch_scroll_timestamp_ = false;
        return;
    }
    active_scroll_handle_ = scroll_handle;
    active_touch_scroll_handle_x_ = UI_INVALID_HANDLE;
    active_touch_scroll_handle_y_ = UI_INVALID_HANDLE;
    active_scroll_dragged_ = false;
    has_last_touch_scroll_timestamp_ = IsValidTimestamp(timestamp_ms);
    last_touch_scroll_timestamp_ms_ = has_last_touch_scroll_timestamp_ ? timestamp_ms : 0.0;
    if (UINode* scroll_node = ResolveMutable(scroll_handle); scroll_node != nullptr) {
        scroll_node->smooth_scroll_active = false;
        scroll_node->smooth_scroll_target_x = scroll_node->scroll_offset_x;
        scroll_node->smooth_scroll_target_y = scroll_node->scroll_offset_y;
        if (CanScrollOnAxis(*scroll_node, true)) {
            active_touch_scroll_handle_x_ = scroll_handle;
        }
        if (CanScrollOnAxis(*scroll_node, false)) {
            active_touch_scroll_handle_y_ = scroll_handle;
        }
        scroll_node->scroll_velocity_x = 0.0f;
        scroll_node->scroll_velocity_y = 0.0f;
    }
}

void UiRuntime::UpdateTouchScroll(float delta_x, float delta_y, double timestamp_ms) {
    if (active_scroll_handle_ == UI_INVALID_HANDLE) {
        return;
    }
    double delta_ms = kNominalFrameMs;
    if (IsValidTimestamp(timestamp_ms)) {
        if (has_last_touch_scroll_timestamp_) {
            delta_ms = ClampInputDeltaMs(timestamp_ms - last_touch_scroll_timestamp_ms_);
        }
        last_touch_scroll_timestamp_ms_ = timestamp_ms;
        has_last_touch_scroll_timestamp_ = true;
    } else if (has_last_touch_scroll_timestamp_) {
        last_touch_scroll_timestamp_ms_ += kNominalFrameMs;
    }
    const std::uint64_t primary_scroll_handle = RetargetScrollHandleForDelta(active_scroll_handle_, delta_x, delta_y);
    const std::uint64_t scroll_handle_x =
        std::abs(delta_x) > 0.0f
            ? RetargetScrollHandleForDelta(
                  active_touch_scroll_handle_x_ != UI_INVALID_HANDLE ? active_touch_scroll_handle_x_ : active_scroll_handle_,
                  delta_x,
                  0.0f)
            : active_touch_scroll_handle_x_;
    const std::uint64_t scroll_handle_y =
        std::abs(delta_y) > 0.0f
            ? RetargetScrollHandleForDelta(
                  active_touch_scroll_handle_y_ != UI_INVALID_HANDLE ? active_touch_scroll_handle_y_ : active_scroll_handle_,
                  0.0f,
                  delta_y)
            : active_touch_scroll_handle_y_;
    active_scroll_handle_ = primary_scroll_handle != UI_INVALID_HANDLE
        ? primary_scroll_handle
        : (scroll_handle_x != UI_INVALID_HANDLE ? scroll_handle_x : scroll_handle_y);
    active_touch_scroll_handle_x_ = scroll_handle_x;
    active_touch_scroll_handle_y_ = scroll_handle_y;
    if (scroll_handle_x == UI_INVALID_HANDLE && scroll_handle_y == UI_INVALID_HANDLE) {
        active_scroll_handle_ = UI_INVALID_HANDLE;
        active_scroll_dragged_ = false;
        return;
    }
    active_scroll_dragged_ = active_scroll_dragged_ || std::abs(delta_x) > 0.0f || std::abs(delta_y) > 0.0f;

    if (scroll_handle_x != UI_INVALID_HANDLE && scroll_handle_x == scroll_handle_y) {
        UINode* shared_scroll_node = ResolveMutable(scroll_handle_x);
        if (shared_scroll_node == nullptr) {
            active_scroll_handle_ = UI_INVALID_HANDLE;
            active_touch_scroll_handle_x_ = UI_INVALID_HANDLE;
            active_touch_scroll_handle_y_ = UI_INVALID_HANDLE;
            active_scroll_dragged_ = false;
            return;
        }
        (void)ApplyScrollOffset(
            scroll_handle_x,
            *shared_scroll_node,
            shared_scroll_node->scroll_offset_x + delta_x,
            shared_scroll_node->scroll_offset_y + delta_y,
            true);
        shared_scroll_node->scroll_velocity_x = PixelsPerSecond(delta_x, delta_ms);
        shared_scroll_node->scroll_velocity_y = PixelsPerSecond(delta_y, delta_ms);
        return;
    }

    if (scroll_handle_x != UI_INVALID_HANDLE) {
        if (UINode* scroll_node_x = ResolveMutable(scroll_handle_x); scroll_node_x != nullptr) {
            (void)ApplyScrollOffset(
                scroll_handle_x,
                *scroll_node_x,
                scroll_node_x->scroll_offset_x + delta_x,
                scroll_node_x->scroll_offset_y,
                true);
            scroll_node_x->scroll_velocity_x = PixelsPerSecond(delta_x, delta_ms);
            scroll_node_x->scroll_velocity_y = 0.0f;
        }
    }

    if (scroll_handle_y != UI_INVALID_HANDLE) {
        if (UINode* scroll_node_y = ResolveMutable(scroll_handle_y); scroll_node_y != nullptr) {
            (void)ApplyScrollOffset(
                scroll_handle_y,
                *scroll_node_y,
                scroll_node_y->scroll_offset_x,
                scroll_node_y->scroll_offset_y + delta_y,
                true);
            scroll_node_y->scroll_velocity_x = 0.0f;
            scroll_node_y->scroll_velocity_y = PixelsPerSecond(delta_y, delta_ms);
        }
    }
}

void UiRuntime::EndTouchScroll(double timestamp_ms) {
    if (IsValidTimestamp(timestamp_ms)) {
        last_touch_scroll_timestamp_ms_ = timestamp_ms;
        has_last_touch_scroll_timestamp_ = true;
    }
    const auto clear_velocity_if_needed = [this](std::uint64_t handle) {
        if (handle == UI_INVALID_HANDLE) {
            return;
        }
        if (UINode* scroll_node = ResolveMutable(handle); scroll_node != nullptr && !active_scroll_dragged_) {
            scroll_node->scroll_velocity_x = 0.0f;
            scroll_node->scroll_velocity_y = 0.0f;
        }
    };
    clear_velocity_if_needed(active_scroll_handle_);
    if (active_touch_scroll_handle_x_ != active_scroll_handle_) {
        clear_velocity_if_needed(active_touch_scroll_handle_x_);
    }
    if (active_touch_scroll_handle_y_ != active_scroll_handle_ &&
        active_touch_scroll_handle_y_ != active_touch_scroll_handle_x_) {
        clear_velocity_if_needed(active_touch_scroll_handle_y_);
    }
    
    // Preserve momentum handles if we dragged for momentum scrolling
    if (active_scroll_dragged_) {
        momentum_scroll_handle_x_ = active_scroll_handle_;
        momentum_scroll_handle_y_ = active_scroll_handle_;
        if (active_touch_scroll_handle_x_ != active_scroll_handle_) {
            momentum_scroll_handle_x_ = active_touch_scroll_handle_x_;
        }
        if (active_touch_scroll_handle_y_ != active_scroll_handle_ &&
            active_touch_scroll_handle_y_ != active_touch_scroll_handle_x_) {
            momentum_scroll_handle_y_ = active_touch_scroll_handle_y_;
        }
    } else {
        momentum_scroll_handle_x_ = UI_INVALID_HANDLE;
        momentum_scroll_handle_y_ = UI_INVALID_HANDLE;
    }
    
    active_scroll_handle_ = UI_INVALID_HANDLE;
    active_touch_scroll_handle_x_ = UI_INVALID_HANDLE;
    active_touch_scroll_handle_y_ = UI_INVALID_HANDLE;
    active_scroll_dragged_ = false;
    has_last_touch_scroll_timestamp_ = false;
}

void UiRuntime::ClearMomentumScroll() {
    const auto brake_momentum = [this](std::uint64_t handle) {
        if (handle == UI_INVALID_HANDLE) {
            return;
        }
        if (UINode* scroll_node = ResolveMutable(handle); scroll_node != nullptr) {
            const float momentum_factor = 0.01f; // cut momentum by 99%
            scroll_node->scroll_velocity_x *= momentum_factor;
            scroll_node->scroll_velocity_y *= momentum_factor;
        }
    };
    
    brake_momentum(momentum_scroll_handle_x_);
    if (momentum_scroll_handle_y_ != momentum_scroll_handle_x_) {
        brake_momentum(momentum_scroll_handle_y_);
    }
}

bool UiRuntime::ActiveTouchScrollAllowsPullToRefresh() const {
    if (active_touch_scroll_handle_y_ == UI_INVALID_HANDLE) {
        return true;
    }
    const std::uint64_t pull_down_target =
        RetargetScrollHandleForDelta(active_touch_scroll_handle_y_, 0.0f, -1.0f);
    const UINode* scroll_node = Resolve(pull_down_target);
    if (scroll_node == nullptr || !scroll_node->scroll_enabled_y) {
        return false;
    }
    return !CanConsumeScrollDelta(*scroll_node, false, -1.0f);
}

bool UiRuntime::WheelScrollCanConsume(float delta_x, float delta_y) const {
    return CanConsumeScrollDeltaFromTarget(
        FindWheelScrollableTarget(UI_INVALID_HANDLE, last_pointer_logical_x_, last_pointer_logical_y_),
        delta_x,
        delta_y);
}

bool UiRuntime::ActiveTouchScrollCanConsume(float delta_x, float delta_y) const {
    if (active_scroll_handle_ == UI_INVALID_HANDLE) {
        return false;
    }
    if (std::abs(delta_x) >= 0.001f) {
        if (CanConsumeScrollDeltaFromTarget(
                active_touch_scroll_handle_x_ != UI_INVALID_HANDLE ? active_touch_scroll_handle_x_ : active_scroll_handle_,
                delta_x,
                0.0f)) {
            return true;
        }
    }
    if (std::abs(delta_y) >= 0.001f) {
        if (CanConsumeScrollDeltaFromTarget(
                active_touch_scroll_handle_y_ != UI_INVALID_HANDLE ? active_touch_scroll_handle_y_ : active_scroll_handle_,
                0.0f,
                delta_y)) {
            return true;
        }
    }
    return false;
}

void UiRuntime::SetCoarsePointerMode(bool coarse_pointer_mode) {
    coarse_pointer_mode_ = coarse_pointer_mode;
}

bool UiRuntime::HandleKeyEvent(
    std::uint32_t type_enum,
    const std::uint8_t* key_utf8,
    std::uint32_t len,
    std::uint32_t modifiers) {
    if (type_enum != UI_KEY_EVENT_DOWN || (key_utf8 == nullptr && len > 0U)) return false;

    const std::string_view key =
        key_utf8 == nullptr ? std::string_view{} : std::string_view(reinterpret_cast<const char*>(key_utf8), len);

    if (key == "Escape") {
        if (ClearCurrentSelection(true)) {
            return true;
        }
    }

    UINode* node = ResolveMutable(focused_handle_);
    if (key == "Tab") {
        if (node != nullptr &&
            node->is_text_node &&
            node->is_editable &&
            node->accepts_tab &&
            modifiers == 0U) {
            return false;
        }
        const bool forward = (modifiers & UI_KEY_MOD_SHIFT) == 0U;
        const std::uint64_t next = GetNextFocusable(focused_handle_, forward);
        if (next != UI_INVALID_HANDLE) {
            SetFocus(next, true);
        }
        return true;
    }

    if (node != nullptr && node->is_text_node && node->is_selectable) {
        const std::uint64_t area_handle = FindSelectionAreaAncestor(focused_handle_);
        if (area_handle != UI_INVALID_HANDLE &&
            cross_selection_active_ &&
            selection_area_handle_ == area_handle &&
            IsPrimaryShortcut(key, modifiers, 'c')) {
            const std::string stitched = BuildCrossSelectionText();
            std::string rich_json{};
            std::string rich_plain_text{};
            if (BuildCrossSelectionRichPayload(rich_plain_text, rich_json)) {
                EmitClipboardWrite(rich_plain_text, &rich_json);
            } else {
                EmitClipboardWrite(stitched);
            }
            return true;
        }
        if (area_handle != UI_INVALID_HANDLE && HandleCrossSelectionNavigation(area_handle, *node, key, modifiers)) {
            return true;
        }
        if (node->is_editable && IsUndoShortcut(key, modifiers)) {
            (void)UndoTextEdit(focused_handle_, *node);
            return true;
        }
        if (node->is_editable && IsRedoShortcut(key, modifiers)) {
            (void)RedoTextEdit(focused_handle_, *node);
            return true;
        }
        if (IsPrimaryShortcut(key, modifiers, 'a')) {
            (void)SelectAllText(focused_handle_);
            return true;
        }
        if (IsPrimaryShortcut(key, modifiers, 'c')) {
            HandleCopy(*node);
            return true;
        }
        if (node->is_editable && IsPrimaryShortcut(key, modifiers, 'x')) {
            if (HandleCut(*node)) {
                node->is_dirty = true;
            }
            return true;
        }
        if (node->is_editable && IsPrimaryShortcut(key, modifiers, 'v')) {
            (void)HandlePaste(*node);
            return true;
        }
        if (HandleTextEditingKey(*node, key, modifiers)) {
            node->is_dirty = true;
            return true;
        }
    }
    return false;
}

void UiRuntime::InvalidateFocusOrder() {
    focus_order_dirty_ = true;
}

void UiRuntime::RebuildFocusOrder() {
    focus_order_.clear();
    if (root_handle_ == UI_INVALID_HANDLE) {
        focus_order_dirty_ = false;
        return;
    }

    const std::uint64_t focus_scope_root = GetActiveSemanticScopeRoot();
    AppendFocusableHandles(focus_scope_root != UI_INVALID_HANDLE ? focus_scope_root : root_handle_, focus_order_);
    std::stable_sort(focus_order_.begin(), focus_order_.end(), [this](std::uint64_t a, std::uint64_t b) {
        const UINode* node_a = Resolve(a);
        const UINode* node_b = Resolve(b);
        return FocusOrderKey(node_a) < FocusOrderKey(node_b);
    });
    focus_order_dirty_ = false;
}

std::uint64_t UiRuntime::GetActiveSemanticScopeRoot() {
    semantic_scope_stack_.erase(
        std::remove_if(
            semantic_scope_stack_.begin(),
            semantic_scope_stack_.end(),
            [this](const SemanticScopeEntry& entry) {
                return Resolve(entry.handle) == nullptr ||
                    root_handle_ == UI_INVALID_HANDLE ||
                    !SubtreeContains(root_handle_, entry.handle);
            }),
        semantic_scope_stack_.end());

    for (auto it = semantic_scope_stack_.rbegin(); it != semantic_scope_stack_.rend(); ++it) {
        if (Resolve(it->handle) != nullptr &&
            root_handle_ != UI_INVALID_HANDLE &&
            SubtreeContains(root_handle_, it->handle)) {
            return it->handle;
        }
    }
    return UI_INVALID_HANDLE;
}

void UiRuntime::AppendFocusableHandles(std::uint64_t handle, std::vector<std::uint64_t>& out) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr) {
        return;
    }
    if (node->visibility != UI_VISIBILITY_NORMAL) {
        return;
    }
    if (node->is_focusable && node->tab_index >= 0) {
        out.push_back(handle);
    }
    for (const std::uint64_t child_handle : node->children) {
        AppendFocusableHandles(child_handle, out);
    }
}

bool UiRuntime::SubtreeContains(std::uint64_t subtree_root, std::uint64_t target_handle) const {
    if (subtree_root == UI_INVALID_HANDLE || target_handle == UI_INVALID_HANDLE) {
        return false;
    }
    if (subtree_root == target_handle) {
        return true;
    }
    const UINode* node = Resolve(subtree_root);
    if (node == nullptr) {
        return false;
    }
    return std::any_of(node->children.begin(), node->children.end(), [this, target_handle](std::uint64_t child_handle) {
        return SubtreeContains(child_handle, target_handle);
    });
}

void UiRuntime::ClearHover(std::uint64_t handle) {
    if (handle != UI_INVALID_HANDLE) {
        as_on_pointer_event(handle, UI_EVENT_POINTER_LEAVE);
    }
    if (last_hovered_handle_ == handle) {
        last_hovered_handle_ = UI_INVALID_HANDLE;
    }
}

void UiRuntime::SetFocus(std::uint64_t new_handle, bool ensure_visible, bool emit_selection_callback) {
    const std::uint64_t old_handle = focused_handle_;
    UINode* old_node = ResolveMutable(old_handle);
    if (new_handle != UI_INVALID_HANDLE && Resolve(new_handle) == nullptr) {
        new_handle = UI_INVALID_HANDLE;
    }
    if (old_handle == new_handle) {
        return;
    }

    if (old_node != nullptr && old_node->is_text_node && old_node->is_selectable) {
        ClearSelectionHighlight(old_handle, true);
    }
    if (old_handle != UI_INVALID_HANDLE) {
        as_on_focus_changed(old_handle, false);
    }

    focused_handle_ = new_handle;
    if (focused_handle_ != UI_INVALID_HANDLE) {
        pending_focus_node_id_.clear();
    }
    UINode* new_node = ResolveMutable(focused_handle_);
    if (focused_handle_ != UI_INVALID_HANDLE && ensure_visible && !coarse_pointer_mode_) {
        EnsureHandleVisible(focused_handle_);
    }
    if (old_node != nullptr && old_node->is_text_node && old_node->is_selectable) {
        MarkTextSelectionVisualsDirty(*old_node);
    }
    selection_horizontal_extend_active_ = false;
    cross_selection_horizontal_extend_active_ = false;
    if (new_node != nullptr && new_node->is_text_node && new_node->is_selectable) {
        MarkTextSelectionVisualsDirty(*new_node);
        if (interaction_time_ms_ != 0U) {
            new_node->last_interaction_time = interaction_time_ms_;
        }
        selection_anchor_handle_ = focused_handle_;
        selection_anchor_index_ = new_node->selection_start;
        if (ensure_visible && !coarse_pointer_mode_) {
            EnsureTextCaretVisible(focused_handle_, *new_node);
            pending_caret_visibility_handle_ = focused_handle_;
        }
        if (emit_selection_callback) {
            as_on_selection_changed(
                focused_handle_,
                new_node->selection_start,
                new_node->selection_end);
        }
    } else if (focused_handle_ == UI_INVALID_HANDLE) {
        selection_anchor_handle_ = UI_INVALID_HANDLE;
        selection_anchor_index_ = 0U;
    }

    if (focused_handle_ != UI_INVALID_HANDLE) {
        as_on_focus_changed(focused_handle_, true);
    }
}

std::uint64_t UiRuntime::GetNextFocusable(std::uint64_t current, bool forward) {
    if (focus_order_dirty_) {
        RebuildFocusOrder();
    }
    if (focus_order_.empty()) {
        return UI_INVALID_HANDLE;
    }

    const auto it = std::find(focus_order_.begin(), focus_order_.end(), current);
    if (it == focus_order_.end()) {
        return forward ? focus_order_.front() : focus_order_.back();
    }

    if (forward) {
        const auto next = std::next(it);
        return next == focus_order_.end() ? focus_order_.front() : *next;
    }

    if (it == focus_order_.begin()) {
        return focus_order_.back();
    }
    return *std::prev(it);
}

} // namespace effindom::v2::ui
