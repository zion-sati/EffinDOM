#include "UiInputRouter.h"

#include "UiFocusCoordinator.h"
#include "UiSelectionCoordinator.h"
#include "UiScrollCoordinator.h"

#include <cmath>
#include <limits>

namespace effindom::v2::ui {

namespace {

constexpr float kSelectionDragThreshold = 4.0f;
constexpr float kSelectionDragThresholdSquared = kSelectionDragThreshold * kSelectionDragThreshold;
constexpr float kSelectionHandleCenterToTextHitOffset = 21.0f;
constexpr double kNominalFrameMs = 1000.0 / 60.0;

bool PointInRect(const Rect& rect, float x, float y) {
    return x >= rect.x && x <= rect.x + rect.width &&
        y >= rect.y && y <= rect.y + rect.height;
}

bool MovedBeyondSelectionDragThreshold(float start_x, float start_y, float current_x, float current_y) {
    const float delta_x = current_x - start_x;
    const float delta_y = current_y - start_y;
    return (delta_x * delta_x) + (delta_y * delta_y) >= kSelectionDragThresholdSquared;
}

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
        interaction_time_ms - last_time_ms < threshold_ms;
}

} // namespace

void InputRouter::ClearHover(std::uint64_t handle) {
    if (handle != UI_INVALID_HANDLE) events_.PointerEvent(handle, UI_EVENT_POINTER_LEAVE);
    if (state_.last_hovered_handle == handle) state_.last_hovered_handle = UI_INVALID_HANDLE;
}

void InputRouter::Reset() {
    const float selection_press_logical_x = state_.selection_press_logical_x;
    const float selection_press_logical_y = state_.selection_press_logical_y;
    const std::uint64_t double_click_threshold_ms = state_.double_click_threshold_ms;
    state_ = InputState{};
    state_.selection_press_logical_x = selection_press_logical_x;
    state_.selection_press_logical_y = selection_press_logical_y;
    state_.double_click_threshold_ms = double_click_threshold_ms;
}

void InputRouter::SetPlatformFamily(std::uint32_t platform_family) {
    switch (static_cast<PlatformFamily>(platform_family)) {
    case PlatformFamily::Apple:
    case PlatformFamily::Windows:
    case PlatformFamily::Linux:
        state_.platform_family = static_cast<PlatformFamily>(platform_family);
        break;
    default:
        state_.platform_family = PlatformFamily::Unknown;
        break;
    }
}

bool InputRouter::HasPointerAutoScroll() const {
    return state_.primary_pointer_down && scrolling_.HasAutoScroll();
}

bool InputRouter::PointInVisibleBounds(const UINode& node, float logical_x, float logical_y) const {
    const Rect bounds = host_.ComputeInputVisibleBounds(node);
    return bounds.width > 0.0f && bounds.height > 0.0f && PointInRect(bounds, logical_x, logical_y);
}

bool InputRouter::IsAttachedToRoot(std::uint64_t handle) const {
    const std::uint64_t root_handle = nodes_.RootHandle();
    if (handle == UI_INVALID_HANDLE || root_handle == UI_INVALID_HANDLE) return false;
    for (std::uint64_t current = handle; current != UI_INVALID_HANDLE;) {
        if (current == root_handle) return true;
        const UINode* node = nodes_.Resolve(current);
        if (node == nullptr) break;
        current = node->parent_handle;
    }
    return false;
}

std::uint64_t InputRouter::FindDeepestNodeContainingPoint(
    std::uint64_t handle,
    float logical_x,
    float logical_y) const {
    const UINode* node = nodes_.Resolve(handle);
    if (node == nullptr || node->yg_node == nullptr || !PointInVisibleBounds(*node, logical_x, logical_y)) {
        return UI_INVALID_HANDLE;
    }
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
        const std::uint64_t child = FindDeepestNodeContainingPoint(*it, logical_x, logical_y);
        if (child != UI_INVALID_HANDLE) return child;
    }
    return handle;
}

std::uint64_t InputRouter::FindBestNodeContainingPoint(float logical_x, float logical_y) const {
    std::uint64_t best_handle = UI_INVALID_HANDLE;
    float best_area = std::numeric_limits<float>::max();
    std::uint32_t best_depth = 0U;
    traversal_.ForEachActive([&](std::uint64_t handle, UINode& node) {
        if (node.yg_node == nullptr || !IsAttachedToRoot(handle) ||
            !PointInVisibleBounds(node, logical_x, logical_y)) return;
        std::uint32_t depth = 0U;
        for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
            const UINode* parent = nodes_.Resolve(current);
            if (parent == nullptr) break;
            depth += 1U;
            current = parent->parent_handle;
        }
        const float area = node.layout_width * node.layout_height;
        if (best_handle == UI_INVALID_HANDLE || area < best_area || (area == best_area && depth > best_depth)) {
            best_handle = handle;
            best_area = area;
            best_depth = depth;
        }
    });
    return best_handle;
}

std::uint64_t InputRouter::FindDeepestScrollViewContainingPoint(float logical_x, float logical_y) const {
    std::uint64_t best_handle = UI_INVALID_HANDLE;
    float best_area = std::numeric_limits<float>::max();
    std::uint32_t best_depth = 0U;
    traversal_.ForEachActiveScrollView([&](std::uint64_t handle, UINode& node) {
        if (node.yg_node == nullptr || !IsAttachedToRoot(handle) ||
            !PointInVisibleBounds(node, logical_x, logical_y)) return;
        std::uint32_t depth = 0U;
        for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
            const UINode* parent = nodes_.Resolve(current);
            if (parent == nullptr) break;
            depth += 1U;
            current = parent->parent_handle;
        }
        const float area = node.layout_width * node.layout_height;
        if (best_handle == UI_INVALID_HANDLE || area < best_area || (area == best_area && depth > best_depth)) {
            best_handle = handle;
            best_area = area;
            best_depth = depth;
        }
    });
    return best_handle;
}

std::uint64_t InputRouter::FindScrollableAncestorContainingPoint(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = nodes_.Resolve(current);
        if (node == nullptr) break;
        if (node->is_scroll_view && PointInVisibleBounds(*node, logical_x, logical_y)) return current;
        current = node->parent_handle;
    }
    return UI_INVALID_HANDLE;
}

std::uint64_t InputRouter::FindScrollProxyTarget(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = nodes_.Resolve(current);
        if (node == nullptr) break;
        if (node->scroll_proxy_target_handle != UI_INVALID_HANDLE &&
            PointInVisibleBounds(*node, logical_x, logical_y)) {
            const UINode* scroll_node = nodes_.Resolve(node->scroll_proxy_target_handle);
            if (scroll_node != nullptr && scroll_node->is_scroll_view) return node->scroll_proxy_target_handle;
        }
        current = node->parent_handle;
    }
    return UI_INVALID_HANDLE;
}

std::uint64_t InputRouter::ResolveScrollTarget(
    std::uint64_t start_handle,
    float logical_x,
    float logical_y) const {
    const auto resolve_candidate = [&](std::uint64_t candidate) {
        if (candidate == UI_INVALID_HANDLE) return static_cast<std::uint64_t>(UI_INVALID_HANDLE);
        const std::uint64_t proxy = FindScrollProxyTarget(candidate, logical_x, logical_y);
        return proxy != UI_INVALID_HANDLE
            ? proxy
            : FindScrollableAncestorContainingPoint(candidate, logical_x, logical_y);
    };

    std::uint64_t resolved_start = start_handle;
    if (resolved_start != UI_INVALID_HANDLE) {
        const UINode* node = nodes_.Resolve(resolved_start);
        if (node == nullptr || node->yg_node == nullptr || !IsAttachedToRoot(resolved_start) ||
            !PointInVisibleBounds(*node, logical_x, logical_y)) {
            resolved_start = UI_INVALID_HANDLE;
        }
    }
    if (resolved_start == UI_INVALID_HANDLE) {
        resolved_start = FindDeepestNodeContainingPoint(nodes_.RootHandle(), logical_x, logical_y);
    }
    const std::uint64_t resolved = resolve_candidate(resolved_start);
    if (resolved != UI_INVALID_HANDLE) return resolved;

    const std::uint64_t best = FindBestNodeContainingPoint(logical_x, logical_y);
    if (best != resolved_start) {
        const std::uint64_t best_resolved = resolve_candidate(best);
        if (best_resolved != UI_INVALID_HANDLE) return best_resolved;
    }
    return FindDeepestScrollViewContainingPoint(logical_x, logical_y);
}

std::uint64_t InputRouter::SelectionAutoScroll(float logical_x, float logical_y, float edge_threshold) {
    state_.last_pointer_logical_x = logical_x;
    state_.last_pointer_logical_y = logical_y;

    std::uint64_t start_handle = UI_INVALID_HANDLE;
    if (selection_.state().cross_active && selection_.state().cross_dragged) {
        start_handle = selection_.state().end_node_handle != UI_INVALID_HANDLE
            ? selection_.state().end_node_handle
            : selection_.state().area_handle;
    } else if (selection_.state().active_handle != UI_INVALID_HANDLE && selection_.state().active_dragged) {
        start_handle = selection_.state().active_handle;
    }
    if (start_handle == UI_INVALID_HANDLE) {
        scrolling_.ClearAutoScroll();
        return UI_INVALID_HANDLE;
    }
    if (!scrolling_.UpdateAutoScrollFor(start_handle, logical_x, logical_y, edge_threshold)) {
        return UI_INVALID_HANDLE;
    }
    return scrolling_.HasAutoScroll() ? scrolling_.ActiveAutoScrollHandle() : UI_INVALID_HANDLE;
}

void InputRouter::ClearAutoScroll() {
    scrolling_.ClearAutoScroll();
}

void InputRouter::UpdateAutoScroll(std::uint64_t start_handle, float logical_x, float logical_y) {
    (void)scrolling_.UpdateAutoScrollFor(start_handle, logical_x, logical_y, 0.0f);
}

void InputRouter::HandleWheel(float delta_x, float delta_y) {
    scrolling_.HandleWheel(
        ResolveScrollTarget(UI_INVALID_HANDLE, state_.last_pointer_logical_x, state_.last_pointer_logical_y),
        delta_x,
        delta_y);
}

void InputRouter::HandlePreciseWheel(
    float delta_x,
    float delta_y,
    bool begins_gesture,
    bool ends_gesture) {
    scrolling_.HandlePreciseWheel(
        ResolveScrollTarget(UI_INVALID_HANDLE, state_.last_pointer_logical_x, state_.last_pointer_logical_y),
        delta_x,
        delta_y,
        begins_gesture,
        ends_gesture);
}

void InputRouter::BeginTouchScroll(
    std::uint64_t handle,
    float logical_x,
    float logical_y,
    double timestamp_ms) {
    const std::uint64_t scroll_handle = ResolveScrollTarget(handle, logical_x, logical_y);
    if (scroll_handle == UI_INVALID_HANDLE) {
        scrolling_.CancelActiveDrag();
        return;
    }
    scrolling_.BeginTouch(scroll_handle, timestamp_ms);
}

void InputRouter::UpdateTouchScroll(float delta_x, float delta_y, double timestamp_ms) {
    scrolling_.UpdateTouch(delta_x, delta_y, timestamp_ms);
}

void InputRouter::EndTouchScroll(double timestamp_ms) {
    scrolling_.EndTouch(timestamp_ms);
}

void InputRouter::ClearMomentumScroll() {
    scrolling_.ClearMomentum();
}

bool InputRouter::ActiveTouchScrollAllowsPullToRefresh() const {
    return scrolling_.ActiveTouchAllowsPullToRefresh();
}

bool InputRouter::WheelScrollCanConsume(float delta_x, float delta_y) const {
    return scrolling_.CanConsumeFromTarget(
        ResolveScrollTarget(UI_INVALID_HANDLE, state_.last_pointer_logical_x, state_.last_pointer_logical_y),
        delta_x,
        delta_y);
}

bool InputRouter::ActiveTouchScrollCanConsume(float delta_x, float delta_y) const {
    return scrolling_.ActiveTouchCanConsume(delta_x, delta_y);
}

void InputRouter::SetCoarsePointerMode(bool coarse_pointer_mode) {
    state_.coarse_pointer_mode = coarse_pointer_mode;
}

bool InputRouter::HandleKeyEvent(
    std::uint32_t type_enum,
    const std::uint8_t* key_utf8,
    std::uint32_t len,
    std::uint32_t modifiers) {
    if (type_enum != UI_KEY_EVENT_DOWN || (key_utf8 == nullptr && len > 0U)) return false;

    const std::string_view key = key_utf8 == nullptr
        ? std::string_view{}
        : std::string_view(reinterpret_cast<const char*>(key_utf8), len);

    if (key == "Escape" && host_.ClearInputSelection(true)) return true;

    const std::uint64_t focused_handle = focus_.FocusedHandle();
    UINode* node = writer_.Resolve(focused_handle);
    if (key == "Tab") {
        if (node != nullptr &&
            node->is_text_node &&
            node->is_editable &&
            node->accepts_tab &&
            modifiers == 0U) {
            return false;
        }
        const bool forward = (modifiers & UI_KEY_MOD_SHIFT) == 0U;
        const std::uint64_t next = focus_.GetNextFocusable(
            focused_handle,
            forward,
            writer_.RootHandle(),
            host_.ActiveInputFocusScope());
        if (next != UI_INVALID_HANDLE) host_.SetInputFocus(next, true);
        return true;
    }

    if (node != nullptr && node->is_text_node && node->is_selectable) {
        const std::uint64_t area_handle = host_.FindInputSelectionAreaAncestor(focused_handle);
        if (area_handle != UI_INVALID_HANDLE &&
            selection_.state().cross_active &&
            selection_.state().area_handle == area_handle &&
            host_.IsInputPrimaryShortcut(key, modifiers, 'c')) {
            const std::string stitched = host_.BuildInputCrossSelectionText();
            std::string rich_json{};
            std::string rich_plain_text{};
            if (host_.BuildInputCrossSelectionRichPayload(rich_plain_text, rich_json)) {
                host_.EmitInputClipboardWrite(rich_plain_text, &rich_json);
            } else {
                host_.EmitInputClipboardWrite(stitched);
            }
            return true;
        }
        if (area_handle != UI_INVALID_HANDLE &&
            host_.HandleInputCrossSelectionNavigation(area_handle, *node, key, modifiers)) {
            return true;
        }
        if (node->is_editable && host_.IsInputUndoShortcut(key, modifiers)) {
            (void)host_.UndoInputTextEdit(focused_handle, *node);
            return true;
        }
        if (node->is_editable && host_.IsInputRedoShortcut(key, modifiers)) {
            (void)host_.RedoInputTextEdit(focused_handle, *node);
            return true;
        }
        if (host_.IsInputPrimaryShortcut(key, modifiers, 'a')) {
            (void)host_.SelectAllInputText(focused_handle);
            return true;
        }
        if (host_.IsInputPrimaryShortcut(key, modifiers, 'c')) {
            host_.CopyInputText(*node);
            return true;
        }
        if (node->is_editable && host_.IsInputPrimaryShortcut(key, modifiers, 'x')) {
            if (host_.CutInputText(*node)) node->is_dirty = true;
            return true;
        }
        if (node->is_editable && host_.IsInputPrimaryShortcut(key, modifiers, 'v')) {
            (void)host_.PasteInputText(*node);
            return true;
        }
        if (host_.HandleInputTextEditingKey(*node, key, modifiers)) {
            node->is_dirty = true;
            return true;
        }
    }
    return false;
}

void InputRouter::HandlePointerEvent(
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
    state_.suppress_touch_editor_focus_request =
        event_enum == UI_EVENT_POINTER_DOWN && pointer_type == UI_POINTER_TYPE_TOUCH;
    const float previous_pointer_x = state_.last_pointer_logical_x;
    const float previous_pointer_y = state_.last_pointer_logical_y;
    bool handled_cross_selection = false;
    const bool is_primary_pointer_button =
        button == 0 ||
        ((event_enum == UI_EVENT_POINTER_MOVE || event_enum == UI_EVENT_POINTER_UP) && state_.primary_pointer_down);
    const bool allow_pointer_text_drag =
        is_primary_pointer_button && (pointer_type != UI_POINTER_TYPE_TOUCH || selection_.state().handle_drag_active);
    const bool allow_pointer_text_down_selection =
        is_primary_pointer_button && pointer_type != UI_POINTER_TYPE_TOUCH;
    if (event_enum == UI_EVENT_POINTER_DOWN) {
        state_.primary_pointer_down = button == 0;
    } else if (event_enum == UI_EVENT_POINTER_UP || event_enum == UI_EVENT_POINTER_LEAVE) {
        state_.primary_pointer_down = false;
    }

    if (event_enum == UI_EVENT_POINTER_MOVE && handle != state_.last_hovered_handle) {
        ClearHover(state_.last_hovered_handle);
        if (handle != UI_INVALID_HANDLE) {
            events_.PointerEvent(handle, UI_EVENT_POINTER_ENTER);
        }
        state_.last_hovered_handle = handle;
    }

    UINode* selection_node = writer_.Resolve(selection_.state().active_handle);
    if (selection_node == nullptr) {
        selection_.state().active_handle = UI_INVALID_HANDLE;
    }
    UINode* scroll_drag_node = writer_.Resolve(scrolling_.ActiveDragHandle());
    if (scroll_drag_node == nullptr) {
        scrolling_.CancelActiveDrag();
    }

    if (event_enum == UI_EVENT_POINTER_DOWN) {
        UINode* down_node = handle == UI_INVALID_HANDLE ? nullptr : writer_.Resolve(handle);
        const bool down_is_selectable_text =
            down_node != nullptr && down_node->is_text_node && down_node->is_selectable;
        const bool down_preserves_selection =
            down_node != nullptr && down_node->preserves_selection_on_pointer_down;
        state_.touch_text_tap_handle = UI_INVALID_HANDLE;
        state_.touch_text_tap_moved = false;
        if (pointer_type == UI_POINTER_TYPE_TOUCH && down_is_selectable_text) {
            state_.touch_text_tap_handle = handle;
            state_.touch_text_tap_logical_x = logical_x;
            state_.touch_text_tap_logical_y = logical_y;
        }
        if (!down_is_selectable_text && !down_preserves_selection) {
            const bool cleared_cross_selection = selection_.state().cross_active;
            if (cleared_cross_selection) {
                selection_.ClearCrossSelection(selection_host_, true);
            }
            if (!cleared_cross_selection && focus_.FocusedHandle() != UI_INVALID_HANDLE) {
                host_.ClearInputSelectionHighlight(focus_.FocusedHandle(), true);
            }
            if (selection_.state().active_handle != UI_INVALID_HANDLE) {
                if (selection_.state().active_handle != focus_.FocusedHandle()) {
                    host_.ClearInputSelectionHighlight(selection_.state().active_handle, true);
                }
                selection_.state().active_handle = UI_INVALID_HANDLE;
                selection_.state().active_dragged = false;
                selection_.state().handle_drag_active = false;
                selection_.state().drag_endpoint = 1U;
                selection_.state().stationary_index = 0U;
                selection_node = nullptr;
            }
            if (handle == UI_INVALID_HANDLE ||
                (down_node != nullptr && !down_node->is_focusable && !down_node->is_selectable && !down_node->is_selection_area)) {
                host_.SetInputFocus(UI_INVALID_HANDLE);
            }
        }
    }

    if (event_enum == UI_EVENT_POINTER_DOWN && handle != UI_INVALID_HANDLE) {
        if (IsRepeatClick(
                handle,
                logical_x,
                logical_y,
                state_.interaction_time_ms,
                state_.last_click_handle,
                state_.last_click_x,
                state_.last_click_y,
                state_.last_click_time_ms,
                state_.double_click_threshold_ms)) {
            state_.click_count += 1U;
        } else {
            state_.click_count = 1U;
        }
        state_.last_click_handle = handle;
        state_.last_click_x = logical_x;
        state_.last_click_y = logical_y;
        state_.last_click_time_ms = state_.interaction_time_ms;

        UINode* down_node = writer_.Resolve(handle);
        const bool down_preserves_selection =
            down_node != nullptr && down_node->preserves_selection_on_pointer_down;
        const std::uint64_t area_handle =
            (down_node != nullptr && down_node->is_text_node && down_node->is_selectable)
            ? selection_.FindAreaAncestor(selection_host_, handle)
            : static_cast<std::uint64_t>(UI_INVALID_HANDLE);
        if (allow_pointer_text_down_selection && area_handle != UI_INVALID_HANDLE) {
            if (selection_.state().cross_active && selection_.state().area_handle != area_handle) {
                selection_.ClearCrossSelection(selection_host_, true);
            }
            selection_.EnsureAreaNodes(selection_host_, area_handle);
            if (!selection_.state().area_nodes.empty()) {
                host_.SetInputFocus(handle, false, false);
                const std::uint32_t index =
                    host_.GetInputStringIndexFromPoint(*down_node, logical_x - down_node->abs_x, logical_y - down_node->abs_y);
                const bool allow_repeat_text_selection = pointer_type != UI_POINTER_TYPE_TOUCH;
                const bool extends_existing_selection =
                    (modifiers & UI_KEY_MOD_SHIFT) != 0U &&
                    selection_.state().start_node_handle != UI_INVALID_HANDLE &&
                    selection_.state().area_handle == area_handle;
                std::uint64_t selection_start_handle = handle;
                std::uint32_t selection_start = index;
                std::uint32_t selection_end = index;
                if (allow_repeat_text_selection && state_.click_count >= 3U) {
                    const auto [paragraph_start, paragraph_end] = host_.GetInputParagraphBoundaries(*down_node, index);
                    selection_start = paragraph_start;
                    selection_end = paragraph_end;
                } else if (allow_repeat_text_selection && state_.click_count == 2U) {
                    const auto [word_start, word_end] = host_.GetInputWordBoundaries(*down_node, index);
                    selection_start = word_start;
                    selection_end = word_end;
                } else if (extends_existing_selection) {
                    selection_start_handle = selection_.state().start_node_handle;
                    selection_start = selection_.state().start_index;
                }
                selection_.BeginCrossSelection(selection_host_,
                    area_handle,
                    selection_start_handle,
                    selection_start,
                    handle,
                    selection_end,
                    false);
                scrolling_.CancelActiveDrag();
                state_.selection_press_logical_x = logical_x;
                state_.selection_press_logical_y = logical_y;
                selection_node = nullptr;
                scroll_drag_node = nullptr;
                handled_cross_selection = true;
            }
        } else if (area_handle == UI_INVALID_HANDLE && selection_.state().cross_active && !down_preserves_selection) {
            selection_.ClearCrossSelection(selection_host_, true);
        }

        if (!handled_cross_selection) {
            if (allow_pointer_text_down_selection) {
                if (UINode* node = writer_.Resolve(handle); node != nullptr && node->is_selectable) {
                    host_.SetInputFocus(handle, false, false);
                    const std::uint32_t index =
                        host_.GetInputStringIndexFromPoint(*node, logical_x - node->abs_x, logical_y - node->abs_y);
                    const bool allow_repeat_text_selection = pointer_type != UI_POINTER_TYPE_TOUCH;
                    const bool has_existing_selection_state =
                        node->last_interaction_time != 0U || node->selection_start != 0U || node->selection_end != 0U;
                    std::uint32_t selection_start = index;
                    std::uint32_t selection_end = index;
                    if (allow_repeat_text_selection && state_.click_count >= 2U && node->is_obscured) {
                        selection_start = 0U;
                        selection_end = static_cast<std::uint32_t>(node->text_content.size());
                    } else if (allow_repeat_text_selection && state_.click_count >= 3U) {
                        const auto [paragraph_start, paragraph_end] = host_.GetInputParagraphBoundaries(*node, index);
                        selection_start = paragraph_start;
                        selection_end = paragraph_end;
                    } else if (allow_repeat_text_selection && state_.click_count == 2U) {
                        const auto [word_start, word_end] = host_.GetInputWordBoundaries(*node, index);
                        selection_start = word_start;
                        selection_end = word_end;
                    } else if ((modifiers & UI_KEY_MOD_SHIFT) != 0U && has_existing_selection_state) {
                        selection_start = node->selection_start;
                    }
                    selection_.BeginNodeSelection(*node, handle, selection_start, selection_end, false);
                    node->caret_trailing_edge =
                        node->selection_start == node->selection_end &&
                        host_.ShouldUseInputTrailingCaretEdge(*node, index, logical_x - node->abs_x, logical_y - node->abs_y);
                    node->last_interaction_time = state_.interaction_time_ms;
                    host_.MarkInputTextSelectionVisualsDirty(*node);
                    state_.selection_press_logical_x = logical_x;
                    state_.selection_press_logical_y = logical_y;
                    selection_node = node;
                }
            }

            const bool blocks_scroll_drag =
                down_node != nullptr &&
                !(pointer_type == UI_POINTER_TYPE_TOUCH && host_.IsInputEditorTextNode(*down_node) && !focus_.IsFocused(handle)) &&
                (down_node->is_focusable ||
                 down_node->is_selectable ||
                 down_node->is_selection_area ||
                 down_node->preserves_selection_on_pointer_down ||
                 down_node->is_interactive);
            if (selection_node == nullptr && !blocks_scroll_drag) {
                const std::uint64_t scroll_handle = ResolveScrollTarget(handle, logical_x, logical_y);
                if (scroll_handle != UI_INVALID_HANDLE) {
                    scrolling_.BeginPointerDrag(scroll_handle);
                    scroll_drag_node = writer_.Resolve(scrolling_.ActiveDragHandle());
                }
            } else {
                scrolling_.CancelActiveDrag();
            }
        }
    }

    if (event_enum == UI_EVENT_POINTER_MOVE &&
        pointer_type == UI_POINTER_TYPE_TOUCH &&
        !selection_.state().handle_drag_active &&
        state_.touch_text_tap_handle != UI_INVALID_HANDLE) {
        state_.touch_text_tap_moved = state_.touch_text_tap_moved ||
            MovedBeyondSelectionDragThreshold(
                state_.touch_text_tap_logical_x,
                state_.touch_text_tap_logical_y,
                logical_x,
                logical_y);
        if (state_.touch_text_tap_moved && focus_.IsFocused(state_.touch_text_tap_handle)) {
            UINode* touch_node = writer_.Resolve(state_.touch_text_tap_handle);
            if (touch_node != nullptr &&
                touch_node->is_text_node &&
                touch_node->is_selectable &&
                host_.IsInputEditorTextNode(*touch_node)) {
                const std::uint32_t touch_index = host_.GetInputStringIndexFromPoint(
                    *touch_node,
                    logical_x - touch_node->abs_x,
                    logical_y - touch_node->abs_y);
                (void)host_.SetInputTextSelectionRange(state_.touch_text_tap_handle, touch_index, touch_index);
                UpdateAutoScroll(state_.touch_text_tap_handle, logical_x, logical_y);
                if (UINode* updated_touch_node = writer_.Resolve(state_.touch_text_tap_handle); updated_touch_node != nullptr) {
                    updated_touch_node->caret_trailing_edge =
                        host_.ShouldUseInputTrailingCaretEdge(
                            *updated_touch_node,
                            touch_index,
                            logical_x - updated_touch_node->abs_x,
                            logical_y - updated_touch_node->abs_y);
                    host_.MarkInputTextSelectionVisualsDirty(*updated_touch_node);
                    host_.EnsureInputTextCaretVisible(state_.touch_text_tap_handle, *updated_touch_node);
                    host_.SetInputPendingCaretVisibility(state_.touch_text_tap_handle);
                }
                scrolling_.CancelActiveDrag();
                scroll_drag_node = nullptr;
                handled_cross_selection = true;
            }
        }
    }

    CrossSelectionPointerRequest cross_selection_pointer{};
    cross_selection_pointer.pointer_move = event_enum == UI_EVENT_POINTER_MOVE;
    cross_selection_pointer.pointer_up = event_enum == UI_EVENT_POINTER_UP;
    cross_selection_pointer.primary_pointer_down = state_.primary_pointer_down;
    cross_selection_pointer.allow_text_drag = allow_pointer_text_drag;
    cross_selection_pointer.drag_threshold_exceeded = MovedBeyondSelectionDragThreshold(
        state_.selection_press_logical_x,
        state_.selection_press_logical_y,
        logical_x,
        logical_y);
    cross_selection_pointer.handle = handle;
    cross_selection_pointer.logical_x = logical_x;
    cross_selection_pointer.logical_y = logical_y;
    cross_selection_pointer.handle_center_to_text_hit_offset = kSelectionHandleCenterToTextHitOffset;
    handled_cross_selection =
        selection_.HandleCrossSelectionPointer(selection_host_, cross_selection_pointer) || handled_cross_selection;

    if (event_enum == UI_EVENT_POINTER_UP &&
        pointer_type == UI_POINTER_TYPE_TOUCH &&
        state_.touch_text_tap_handle != UI_INVALID_HANDLE) {
        if (!state_.touch_text_tap_moved) {
            UINode* tap_node = writer_.Resolve(state_.touch_text_tap_handle);
            bool tap_inside_existing_selection = false;
            bool placed_touch_caret = false;
            if (tap_node != nullptr && tap_node->is_text_node && tap_node->is_selectable) {
                const std::uint32_t tap_index = host_.GetInputStringIndexFromPoint(
                    *tap_node,
                    logical_x - tap_node->abs_x,
                    logical_y - tap_node->abs_y);
                if (host_.IsInputEditorTextNode(*tap_node)) {
                    host_.SetInputFocus(state_.touch_text_tap_handle, false, false);
                    (void)host_.SetInputTextSelectionRange(state_.touch_text_tap_handle, tap_index, tap_index);
                    if (UINode* updated_tap_node = writer_.Resolve(state_.touch_text_tap_handle); updated_tap_node != nullptr) {
                        updated_tap_node->caret_trailing_edge =
                            host_.ShouldUseInputTrailingCaretEdge(
                                *updated_tap_node,
                                tap_index,
                                logical_x - updated_tap_node->abs_x,
                                logical_y - updated_tap_node->abs_y);
                        host_.MarkInputTextSelectionVisualsDirty(*updated_tap_node);
                        host_.EnsureInputTextCaretVisible(state_.touch_text_tap_handle, *updated_tap_node);
                    host_.SetInputPendingCaretVisibility(state_.touch_text_tap_handle);
                    }
                    handled_cross_selection = true;
                    placed_touch_caret = true;
                } else if (selection_.state().cross_active &&
                    state_.touch_text_tap_handle == selection_.state().start_node_handle &&
                    state_.touch_text_tap_handle == selection_.state().end_node_handle) {
                    const std::uint32_t selection_start = std::min(selection_.state().start_index, selection_.state().end_index);
                    const std::uint32_t selection_end = std::max(selection_.state().start_index, selection_.state().end_index);
                    tap_inside_existing_selection = tap_index >= selection_start && tap_index <= selection_end;
                } else if (!selection_.state().cross_active && tap_node->selection_start != tap_node->selection_end) {
                    const std::uint32_t selection_start = std::min(tap_node->selection_start, tap_node->selection_end);
                    const std::uint32_t selection_end = std::max(tap_node->selection_start, tap_node->selection_end);
                    tap_inside_existing_selection = tap_index >= selection_start && tap_index <= selection_end;
                }
            }
            if (placed_touch_caret) {
                // The caret placement above already cleared cross-selection if needed.
            } else if (selection_.state().cross_active) {
                if (!tap_inside_existing_selection) {
                    selection_.ClearCrossSelection(selection_host_, true);
                    handled_cross_selection = true;
                }
            } else if (!tap_inside_existing_selection && tap_node != nullptr && tap_node->selection_start != tap_node->selection_end) {
                host_.ClearInputSelectionHighlight(state_.touch_text_tap_handle, true);
            }
        }
        state_.touch_text_tap_handle = UI_INVALID_HANDLE;
        state_.touch_text_tap_moved = false;
    }

    if (!handled_cross_selection && selection_node != nullptr) {
        NodeSelectionPointerRequest node_selection_pointer{};
        node_selection_pointer.pointer_move = event_enum == UI_EVENT_POINTER_MOVE;
        node_selection_pointer.primary_pointer_down = state_.primary_pointer_down;
        node_selection_pointer.allow_text_drag = allow_pointer_text_drag;
        node_selection_pointer.drag_threshold_exceeded = MovedBeyondSelectionDragThreshold(
            state_.selection_press_logical_x,
            state_.selection_press_logical_y,
            logical_x,
            logical_y);
        node_selection_pointer.logical_x = logical_x;
        node_selection_pointer.logical_y = logical_y;
        node_selection_pointer.handle_center_to_text_hit_offset = kSelectionHandleCenterToTextHitOffset;
        (void)selection_.HandleNodeSelectionPointer(selection_host_, node_selection_pointer);
    }

    if (!handled_cross_selection && event_enum == UI_EVENT_POINTER_MOVE && state_.primary_pointer_down &&
        scroll_drag_node != nullptr && selection_node == nullptr) {
        const float delta_x = logical_x - previous_pointer_x;
        const float delta_y = logical_y - previous_pointer_y;
        scrolling_.DragPointerBy(-delta_x, -delta_y, kNominalFrameMs);
    }

    if (!handled_cross_selection && event_enum == UI_EVENT_POINTER_UP && selection_node != nullptr) {
        NodeSelectionPointerRequest node_selection_pointer{};
        node_selection_pointer.pointer_up = true;
        node_selection_pointer.logical_x = logical_x;
        node_selection_pointer.logical_y = logical_y;
        node_selection_pointer.handle_center_to_text_hit_offset = kSelectionHandleCenterToTextHitOffset;
        node_selection_pointer.interaction_time_ms = state_.interaction_time_ms;
        const NodeSelectionPointerResult result = selection_.HandleNodeSelectionPointer(selection_host_, node_selection_pointer);
        if (result.finished) {
            if (UINode* finished_node = writer_.Resolve(result.handle); finished_node != nullptr) {
                host_.EnsureInputTextCaretVisible(result.handle, *finished_node);
                    host_.SetInputPendingCaretVisibility(result.handle);
            }
        }
    }

    if (!handled_cross_selection &&
        (event_enum == UI_EVENT_POINTER_UP || event_enum == UI_EVENT_POINTER_LEAVE) &&
        scroll_drag_node != nullptr) {
        scrolling_.EndPointerDrag();
    }

    if (
        !handled_cross_selection &&
        event_enum == UI_EVENT_POINTER_MOVE &&
        state_.primary_pointer_down &&
        scroll_drag_node != nullptr) {
        UpdateAutoScroll(
            handle != UI_INVALID_HANDLE ? handle : scrolling_.ActiveDragHandle(),
            logical_x,
            logical_y);
    } else if (event_enum == UI_EVENT_POINTER_UP || event_enum == UI_EVENT_POINTER_LEAVE) {
        ClearAutoScroll();
    }

    if (event_enum == UI_EVENT_POINTER_DOWN && handle != UI_INVALID_HANDLE) {
        if (const UINode* node = nodes_.Resolve(handle); node != nullptr &&
            (node->is_focusable || node->is_selectable || node->is_selection_area) &&
            !(pointer_type == UI_POINTER_TYPE_TOUCH && host_.IsInputEditorTextNode(*node))) {
            host_.SetInputFocus(handle, false, false);
        }
    }

    if (handle != UI_INVALID_HANDLE) {
        events_.PointerEvent(handle, static_cast<UiEvent>(event_enum));
    }
    state_.suppress_touch_editor_focus_request = false;
    state_.last_pointer_logical_x = logical_x;
    state_.last_pointer_logical_y = logical_y;
}


} // namespace effindom::v2::ui
