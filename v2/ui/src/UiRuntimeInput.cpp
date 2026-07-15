#include "UiRuntime.h"

#include "effindom_ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace effindom::v2::ui {

namespace {

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

    return VisibilityResolver(node_store_.Reader()).ClipToAncestors(bounds, node.parent_handle);
}

bool UiRuntime::PointInVisibleBounds(const UINode& node, float logical_x, float logical_y) const {
    return Input().PointInVisibleBounds(node, logical_x, logical_y);
}

bool UiRuntime::IsAttachedToRoot(std::uint64_t handle) const {
    return Input().IsAttachedToRoot(handle);
}

std::uint64_t UiRuntime::FindDeepestNodeContainingPoint(
    std::uint64_t handle,
    float logical_x,
    float logical_y) const {
    return Input().FindDeepestNodeContainingPoint(handle, logical_x, logical_y);
}

std::uint64_t UiRuntime::FindBestNodeContainingPoint(float logical_x, float logical_y) const {
    return Input().FindBestNodeContainingPoint(logical_x, logical_y);
}

std::uint64_t UiRuntime::FindDeepestScrollViewContainingPoint(
    std::uint64_t handle,
    float logical_x,
    float logical_y) const {
    (void)handle;
    return Input().FindDeepestScrollViewContainingPoint(logical_x, logical_y);
}

std::uint64_t UiRuntime::ResolveScrollTarget(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    return Input().ResolveScrollTarget(start_handle, logical_x, logical_y);
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
    return Input().FindScrollableAncestorContainingPoint(start_handle, logical_x, logical_y);
}

std::uint64_t UiRuntime::FindWheelScrollableTarget(std::uint64_t start_handle, float logical_x, float logical_y) const {
    return Input().ResolveScrollTarget(start_handle, logical_x, logical_y);
}

std::uint64_t UiRuntime::FindScrollProxyTarget(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    return Input().FindScrollProxyTarget(start_handle, logical_x, logical_y);
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
    Input().ClearAutoScroll();
}

void UiRuntime::UpdateAutoScrollState(std::uint64_t start_handle, float logical_x, float logical_y) {
    Input().UpdateAutoScroll(start_handle, logical_x, logical_y);
}

std::uint64_t UiRuntime::SelectionAutoScroll(float logical_x, float logical_y, float edge_threshold) {
    return Input().SelectionAutoScroll(logical_x, logical_y, edge_threshold);
}

bool UiRuntime::ClearSelection(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }

    if (Selection().state().cross_active &&
        (handle == Selection().state().start_node_handle ||
         handle == Selection().state().end_node_handle ||
         (Selection().state().area_handle != UI_INVALID_HANDLE && SubtreeContains(Selection().state().area_handle, handle)))) {
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
    if (Selection().state().active_handle == from_handle) {
        Selection().state().active_handle = to_handle;
    }

    bool moved_cross_selection = false;
    if (Selection().state().cross_active) {
        if (Selection().state().start_node_handle == from_handle) {
            Selection().state().start_node_handle = to_handle;
            moved_cross_selection = true;
        }
        if (Selection().state().end_node_handle == from_handle) {
            Selection().state().end_node_handle = to_handle;
            moved_cross_selection = true;
        }
        if (moved_cross_selection) {
            const std::uint64_t previous_area_handle = Selection().state().area_handle;
            const std::uint64_t next_area_handle = FindSelectionAreaAncestor(to_handle);
            if (next_area_handle == UI_INVALID_HANDLE) {
                ClearCrossSelection(true);
            } else {
                if (!Selection().state().area_nodes.empty()) {
                    MarkSelectionAreaNodesDirty();
                }
                Selection().state().area_handle = next_area_handle;
                Selection().state().area_nodes.clear();
                Selection().state().area_nodes_dirty = true;
                EnsureSelectionAreaNodes(Selection().state().area_handle);
                if (!Selection().state().area_nodes.empty()) {
                    MarkSelectionAreaNodesDirty();
                }
                if (previous_area_handle != UI_INVALID_HANDLE && previous_area_handle != next_area_handle) {
                    event_sink_.CrossSelectionChanged(previous_area_handle, {});
                    NotifyCrossSelectionChanged();
                }
            }
        }
    }

    layout_dirty_ = true;
    return true;
}

bool UiRuntime::IsPointInSelection(float logical_x, float logical_y) {
    if (node_store_.RootHandle() == UI_INVALID_HANDLE) {
        return false;
    }

    if (Selection().HitTests(logical_x, logical_y)) {
        return true;
    }

    const auto point_window_for_node = [this, logical_x, logical_y](const UINode& node) {
        VisualGeometryWindow window{};
        const std::size_t available_line_count = node.break_offsets.size() > 1U
            ? node.break_offsets.size() - 1U
            : 0U;
        const std::size_t line_count = node.visible_line_count == 0U
            ? available_line_count
            : std::min(node.visible_line_count, available_line_count);
        if (line_count == 0U) {
            return window;
        }
        const float local_x = logical_x - node.abs_x;
        const float local_y = logical_y - node.abs_y;
        const float content_height = GetTextContentHeight(node, line_count);
        const float content_offset_y = GetAlignedTextYOffset(node, content_height);
        const std::size_t line_index = LineIndexForYOffset(
            node,
            local_y - content_offset_y,
            line_count);
        window.line_start = line_index;
        window.line_end = line_index + 1U;
        window.local_clip = Rect{local_x - 0.5f, local_y - 0.5f, 1.0f, 1.0f};
        return window;
    };

    const auto point_hits_node_selection = [this, logical_x, logical_y, &point_window_for_node](std::uint64_t handle) {
        const UINode* node = Resolve(handle);
        if (node == nullptr ||
            !node->is_text_node ||
            !node->is_selectable ||
            node->selection_start == node->selection_end ||
            !PointInVisibleBounds(*node, logical_x, logical_y)) {
            return false;
        }
        const std::vector<Rect> rects = BuildSelectionRects(
            *node,
            node->selection_start,
            node->selection_end,
            point_window_for_node(*node));
        const float local_x = logical_x - node->abs_x;
        const float local_y = logical_y - node->abs_y;
        return std::any_of(rects.begin(), rects.end(), [local_x, local_y](const Rect& rect) {
            return PointInRect(rect, local_x, local_y);
        });
    };

    if (Selection().state().cross_active) {
        EnsureSelectionAreaNodes(Selection().state().area_handle);
        for (const std::uint64_t handle : Selection().state().area_nodes) {
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

            const std::vector<Rect> rects = BuildSelectionRects(
                *node,
                highlight_start,
                highlight_end,
                point_window_for_node(*node));
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

    if (point_hits_node_selection(Focus().FocusedHandle())) {
        return true;
    }
    if (Selection().state().active_handle != Focus().FocusedHandle() && point_hits_node_selection(Selection().state().active_handle)) {
        return true;
    }
    return false;
}

bool UiRuntime::ClearCurrentSelection(bool notify_callback) {
    if (Selection().state().cross_active) {
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

    if (clear_node_selection(Focus().FocusedHandle())) {
        return true;
    }
    if (Selection().state().active_handle != Focus().FocusedHandle() && clear_node_selection(Selection().state().active_handle)) {
        return true;
    }
    return false;
}

bool UiRuntime::CopyCurrentSelection() const {
    if (Selection().state().cross_active) {
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

    if (copy_node_selection(Focus().FocusedHandle())) {
        return true;
    }
    if (Selection().state().active_handle != Focus().FocusedHandle() && copy_node_selection(Selection().state().active_handle)) {
        return true;
    }
    return false;
}

void UiRuntime::ClearSelectionHighlight(std::uint64_t handle, bool notify_callback) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable) {
        return;
    }

    Selection().ClearHitRects();
    Selection().state().horizontal_extend_active = false;
    const std::uint32_t caret = std::min<std::uint32_t>(node->selection_end, static_cast<std::uint32_t>(node->text_content.size()));
    Selection().state().anchor_handle = handle;
    Selection().state().anchor_index = caret;
    if (node->selection_start == node->selection_end) {
        return;
    }

    node->selection_start = caret;
    node->selection_end = caret;
    node->last_interaction_time = Input().state().interaction_time_ms;
    MarkTextSelectionVisualsDirty(*node);
    if (notify_callback) {
        event_sink_.SelectionChanged(handle, caret, caret);
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
    if (!focusable && Focus().IsFocused(handle)) {
        SetFocus(UI_INVALID_HANDLE);
    }
    if (focusable && !Focus().PendingNodeId().empty() && node->node_id == Focus().PendingNodeId()) {
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
    if (Input().SuppressesTouchEditorFocus() && IsEditorTextNode(*node)) {
        return true;
    }
    SetFocus(handle);
    return true;
}

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
    Input().HandlePointerEvent(
        event_enum,
        handle,
        logical_x,
        logical_y,
        pointer_id,
        pointer_type,
        button,
        buttons,
        pressure,
        width,
        height,
        click_count,
        modifiers);
}

void UiRuntime::HandleWheelEvent(float delta_x, float delta_y) {
    Input().HandleWheel(delta_x, delta_y);
}

void UiRuntime::HandlePreciseWheelEvent(
    float delta_x,
    float delta_y,
    bool begins_gesture,
    bool ends_gesture) {
    Input().HandlePreciseWheel(delta_x, delta_y, begins_gesture, ends_gesture);
}

void UiRuntime::BeginTouchScroll(std::uint64_t handle, float logical_x, float logical_y, double timestamp_ms) {
    Input().BeginTouchScroll(handle, logical_x, logical_y, timestamp_ms);
}

void UiRuntime::UpdateTouchScroll(float delta_x, float delta_y, double timestamp_ms) {
    Input().UpdateTouchScroll(delta_x, delta_y, timestamp_ms);
}

void UiRuntime::EndTouchScroll(double timestamp_ms) {
    Input().EndTouchScroll(timestamp_ms);
}

void UiRuntime::ClearMomentumScroll() {
    Input().ClearMomentumScroll();
}

bool UiRuntime::ActiveTouchScrollAllowsPullToRefresh() const {
    return Input().ActiveTouchScrollAllowsPullToRefresh();
}

bool UiRuntime::WheelScrollCanConsume(float delta_x, float delta_y) const {
    return Input().WheelScrollCanConsume(delta_x, delta_y);
}

bool UiRuntime::ActiveTouchScrollCanConsume(float delta_x, float delta_y) const {
    return Input().ActiveTouchScrollCanConsume(delta_x, delta_y);
}

void UiRuntime::SetCoarsePointerMode(bool coarse_pointer_mode) {
    Input().SetCoarsePointerMode(coarse_pointer_mode);
}

bool UiRuntime::HandleKeyEvent(
    std::uint32_t type_enum,
    const std::uint8_t* key_utf8,
    std::uint32_t len,
    std::uint32_t modifiers) {
    return Input().HandleKeyEvent(type_enum, key_utf8, len, modifiers);
}

void UiRuntime::InvalidateFocusOrder() {
    Focus().InvalidateOrder();
}

std::uint64_t UiRuntime::GetActiveSemanticScopeRoot() {
    const std::uint64_t root_handle = node_store_.RootHandle();
    semantic_scope_stack_.erase(
        std::remove_if(
            semantic_scope_stack_.begin(),
            semantic_scope_stack_.end(),
            [this, root_handle](const SemanticScopeEntry& entry) {
                return Resolve(entry.handle) == nullptr ||
                    root_handle == UI_INVALID_HANDLE ||
                    !SubtreeContains(root_handle, entry.handle);
            }),
        semantic_scope_stack_.end());

    for (auto it = semantic_scope_stack_.rbegin(); it != semantic_scope_stack_.rend(); ++it) {
        if (Resolve(it->handle) != nullptr &&
            root_handle != UI_INVALID_HANDLE &&
            SubtreeContains(root_handle, it->handle)) {
            return it->handle;
        }
    }
    return UI_INVALID_HANDLE;
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
    Input().ClearHover(handle);
}

void UiRuntime::SetFocus(std::uint64_t new_handle, bool ensure_visible, bool emit_selection_callback) {
    const std::uint64_t old_handle = Focus().FocusedHandle();
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
        Focus().NotifyChanged(old_handle, false);
    }

    Focus().SetFocusedHandle(new_handle);
    const std::uint64_t focused_handle = Focus().FocusedHandle();
    UINode* new_node = ResolveMutable(focused_handle);
    if (focused_handle != UI_INVALID_HANDLE && ensure_visible && !Input().state().coarse_pointer_mode) {
        EnsureHandleVisible(focused_handle);
    }
    if (old_node != nullptr && old_node->is_text_node && old_node->is_selectable) {
        MarkTextSelectionVisualsDirty(*old_node);
    }
    Selection().state().horizontal_extend_active = false;
    Selection().state().cross_horizontal_extend_active = false;
    if (new_node != nullptr && new_node->is_text_node && new_node->is_selectable) {
        MarkTextSelectionVisualsDirty(*new_node);
        if (Input().state().interaction_time_ms != 0U) {
            new_node->last_interaction_time = Input().state().interaction_time_ms;
        }
        Selection().state().anchor_handle = focused_handle;
        Selection().state().anchor_index = new_node->selection_start;
        if (ensure_visible && !Input().state().coarse_pointer_mode) {
            EnsureTextCaretVisible(focused_handle, *new_node);
            pending_caret_visibility_handle_ = focused_handle;
        }
        if (emit_selection_callback) {
            event_sink_.SelectionChanged(
                focused_handle,
                new_node->selection_start,
                new_node->selection_end);
        }
    } else if (focused_handle == UI_INVALID_HANDLE) {
        Selection().state().anchor_handle = UI_INVALID_HANDLE;
        Selection().state().anchor_index = 0U;
    }

    Focus().NotifyChanged(focused_handle, true);
}

std::uint64_t UiRuntime::GetNextFocusable(std::uint64_t current, bool forward) {
    return Focus().GetNextFocusable(current, forward, node_store_.RootHandle(), GetActiveSemanticScopeRoot());
}

} // namespace effindom::v2::ui
