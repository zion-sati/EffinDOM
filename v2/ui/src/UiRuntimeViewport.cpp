#include "UiRuntime.h"

#include "effindom_ui.h"

#include <algorithm>

namespace effindom::v2::ui {

void UiRuntime::EnsureHandleVisible(std::uint64_t handle) {
    const UINode* target = Resolve(handle);
    if (target == nullptr || !target->is_active || target->yg_node == nullptr) {
        return;
    }

    float target_left = 0.0f;
    float target_top = 0.0f;
    float target_right = target->layout_width;
    float target_bottom = target->layout_height;
    std::uint64_t current_handle = handle;
    while (current_handle != UI_INVALID_HANDLE) {
        const UINode* current = Resolve(current_handle);
        if (current == nullptr || current->yg_node == nullptr) {
            break;
        }
        const std::uint64_t parent_handle = current->parent_handle;
        if (parent_handle == UI_INVALID_HANDLE) {
            break;
        }
        UINode* parent = ResolveMutable(parent_handle);
        if (parent == nullptr || parent->yg_node == nullptr) {
            break;
        }

        const float current_left = YGNodeLayoutGetLeft(current->yg_node);
        const float current_top = YGNodeLayoutGetTop(current->yg_node);
        target_left += current_left;
        target_right += current_left;
        target_top += current_top;
        target_bottom += current_top;

        if (parent->is_scroll_view) {
            EnsureRectVisibleWithinScrollAncestor(
                parent_handle,
                target_left - parent->scroll_offset_x,
                target_top - parent->scroll_offset_y,
                target_right - parent->scroll_offset_x,
                target_bottom - parent->scroll_offset_y,
                &target_left,
                &target_top,
                &target_right,
                &target_bottom);
        }
        current_handle = parent_handle;
    }
}

const UINode* UiRuntime::ResolveTextSnapshotNode(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr ||
        !node->is_active ||
        !node->is_text_node ||
        node->is_editable ||
        node->visibility != UI_VISIBILITY_NORMAL ||
        !IsAttachedToRoot(handle)) {
        return nullptr;
    }
    return node;
}

const UINode* UiRuntime::ResolveTextGeometryNode(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr ||
        !node->is_active ||
        !node->is_text_node ||
        node->visibility != UI_VISIBILITY_NORMAL ||
        !IsAttachedToRoot(handle)) {
        return nullptr;
    }
    return node;
}

void UiRuntime::AppendTextSnapshotHandles(std::uint64_t handle, std::vector<std::uint64_t>& out) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr || !node->is_active || node->visibility != UI_VISIBILITY_NORMAL) {
        return;
    }
    if (ResolveTextSnapshotNode(handle) != nullptr && !node->text_content.empty()) {
        out.push_back(handle);
    }
    for (const std::uint64_t child_handle : node->children) {
        AppendTextSnapshotHandles(child_handle, out);
    }
}

std::vector<std::uint64_t> UiRuntime::GetTextSnapshotHandles() const {
    std::vector<std::uint64_t> handles{};
    const std::uint64_t root_handle = node_store_.RootHandle();
    if (root_handle == UI_INVALID_HANDLE) {
        return handles;
    }
    AppendTextSnapshotHandles(root_handle, handles);
    return handles;
}

std::optional<Rect> UiRuntime::GetVisibleBounds(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr) {
        return std::nullopt;
    }
    const Rect bounds = ComputeVisibleBounds(*node);
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return std::nullopt;
    }
    return bounds;
}

std::optional<std::string_view> UiRuntime::GetTextSnapshotDocument(std::uint64_t handle) const {
    const UINode* node = ResolveTextSnapshotNode(handle);
    if (node == nullptr) {
        return std::nullopt;
    }
    return std::string_view(node->text_content);
}

bool UiRuntime::RevealTextRange(std::uint64_t handle, std::uint32_t start, std::uint32_t end) {
    const UINode* node = ResolveTextSnapshotNode(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    const std::vector<Rect> scene_rects = GetTextRangeSceneRects(handle, clamped_start, clamped_end);
    if (scene_rects.empty()) {
        EnsureHandleVisible(handle);
        return true;
    }

    float scene_left = scene_rects.front().x;
    float scene_top = scene_rects.front().y;
    float scene_right = scene_rects.front().x + scene_rects.front().width;
    float scene_bottom = scene_rects.front().y + scene_rects.front().height;
    for (std::size_t index = 1; index < scene_rects.size(); index += 1U) {
        const Rect& rect = scene_rects[index];
        scene_left = std::min(scene_left, rect.x);
        scene_top = std::min(scene_top, rect.y);
        scene_right = std::max(scene_right, rect.x + rect.width);
        scene_bottom = std::max(scene_bottom, rect.y + rect.height);
    }

    float target_left = scene_left - node->abs_x;
    float target_top = scene_top - node->abs_y;
    float target_right = scene_right - node->abs_x;
    float target_bottom = scene_bottom - node->abs_y;
    std::uint64_t current_handle = handle;
    while (current_handle != UI_INVALID_HANDLE) {
        const UINode* current = Resolve(current_handle);
        if (current == nullptr || current->yg_node == nullptr) {
            break;
        }
        const std::uint64_t parent_handle = current->parent_handle;
        if (parent_handle == UI_INVALID_HANDLE) {
            break;
        }
        UINode* parent = ResolveMutable(parent_handle);
        if (parent == nullptr || parent->yg_node == nullptr) {
            break;
        }

        const float current_left = YGNodeLayoutGetLeft(current->yg_node);
        const float current_top = YGNodeLayoutGetTop(current->yg_node);
        target_left += current_left;
        target_right += current_left;
        target_top += current_top;
        target_bottom += current_top;

        if (parent->is_scroll_view) {
            EnsureRectVisibleWithinScrollAncestor(
                parent_handle,
                target_left - parent->scroll_offset_x,
                target_top - parent->scroll_offset_y,
                target_right - parent->scroll_offset_x,
                target_bottom - parent->scroll_offset_y,
                &target_left,
                &target_top,
                &target_right,
                &target_bottom);
        }
        current_handle = parent_handle;
    }

    return true;
}

bool UiRuntime::SetTextFindMatch(std::uint64_t handle, std::uint32_t start, std::uint32_t end) {
    const UINode* resolved = ResolveTextSnapshotNode(handle);
    if (resolved == nullptr) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(resolved->text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    if (clamped_start >= clamped_end) {
        return false;
    }

    const std::uint64_t previous_handle = text_find_handle_;
    text_find_handle_ = handle;
    text_find_start_ = clamped_start;
    text_find_end_ = clamped_end;

    if (previous_handle != UI_INVALID_HANDLE && previous_handle != handle) {
        if (UINode* previous = ResolveMutable(previous_handle); previous != nullptr) {
            MarkTextSelectionVisualsDirty(*previous);
        }
    }
    if (UINode* current = ResolveMutable(handle); current != nullptr) {
        MarkTextSelectionVisualsDirty(*current);
    }
    return true;
}

void UiRuntime::ClearTextFindMatch() {
    const std::uint64_t previous_handle = text_find_handle_;
    text_find_handle_ = UI_INVALID_HANDLE;
    text_find_start_ = 0U;
    text_find_end_ = 0U;
    if (previous_handle != UI_INVALID_HANDLE) {
        if (UINode* previous = ResolveMutable(previous_handle); previous != nullptr) {
            MarkTextSelectionVisualsDirty(*previous);
        }
    }
}

bool UiRuntime::PushTextFindHighlight(
    std::uint64_t handle,
    std::uint32_t start,
    std::uint32_t end,
    std::uint32_t color) {
    const UINode* resolved = ResolveTextSnapshotNode(handle);
    if (resolved == nullptr) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(resolved->text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    if (clamped_start >= clamped_end) {
        return false;
    }

    text_find_highlights_.push_back(TextFindHighlight{
        handle,
        clamped_start,
        clamped_end,
        color,
    });
    if (UINode* node = ResolveMutable(handle); node != nullptr) {
        MarkTextSelectionVisualsDirty(*node);
    }
    return true;
}

void UiRuntime::ClearTextFindHighlights() {
    for (const TextFindHighlight& highlight : text_find_highlights_) {
        if (UINode* node = ResolveMutable(highlight.handle); node != nullptr) {
            MarkTextSelectionVisualsDirty(*node);
        }
    }
    text_find_highlights_.clear();
}

void UiRuntime::EnsureRectVisibleWithinScrollAncestor(
    std::uint64_t scroll_handle,
    float target_left,
    float target_top,
    float target_right,
    float target_bottom,
    float* adjusted_left,
    float* adjusted_top,
    float* adjusted_right,
    float* adjusted_bottom) {
    UINode* scroll_node = ResolveMutable(scroll_handle);
    if (scroll_node == nullptr || !scroll_node->is_scroll_view) {
        return;
    }

    float offset_x = scroll_node->scroll_offset_x;
    float offset_y = scroll_node->scroll_offset_y;
    const float viewport_width = GetScrollViewportWidth(*scroll_node);
    const float viewport_height = GetScrollViewportHeight(*scroll_node);
    if (target_left < 0.0f) {
        offset_x += target_left;
    } else if (target_right > viewport_width) {
        offset_x += target_right - viewport_width;
    }
    if (target_top < 0.0f) {
        offset_y += target_top;
    } else if (target_bottom > viewport_height) {
        offset_y += target_bottom - viewport_height;
    }

    const float delta_x = offset_x - scroll_node->scroll_offset_x;
    const float delta_y = offset_y - scroll_node->scroll_offset_y;
    (*scroll_coordinator_).ApplyOffset(scroll_handle, *scroll_node, offset_x, offset_y, true);

    if (adjusted_left != nullptr) {
        *adjusted_left = target_left - delta_x;
    }
    if (adjusted_top != nullptr) {
        *adjusted_top = target_top - delta_y;
    }
    if (adjusted_right != nullptr) {
        *adjusted_right = target_right - delta_x;
    }
    if (adjusted_bottom != nullptr) {
        *adjusted_bottom = target_bottom - delta_y;
    }
}

void UiRuntime::UpdateAncestorScrollMetrics(std::uint64_t handle) {
    for (std::uint64_t current_handle = handle; current_handle != UI_INVALID_HANDLE;) {
        const UINode* current = Resolve(current_handle);
        if (current == nullptr) {
            break;
        }
        const std::uint64_t parent_handle = current->parent_handle;
        if (parent_handle == UI_INVALID_HANDLE) {
            break;
        }

        UINode* parent = ResolveMutable(parent_handle);
        if (parent == nullptr) {
            break;
        }
        if (parent->is_scroll_view) {
            (*scroll_coordinator_).UpdateMetrics(parent_handle, *parent);
        }
        current_handle = parent_handle;
    }
}

const std::vector<std::uint32_t>& UiRuntime::command_buffer() const {
    return command_buffer_;
}

const std::vector<std::uint32_t>& UiRuntime::semantic_buffer() const {
    return semantic_buffer_;
}

const std::vector<std::uint32_t>& UiRuntime::debug_tree_buffer() const {
    return debug_tree_buffer_;
}

const std::vector<std::uint32_t>& UiRuntime::LiveFallbackFontBuffer() const {
    RebuildLiveFallbackFontBuffer();
    return live_fallback_font_buffer_;
}

const std::uint64_t& UiRuntime::root_handle() const {
    return node_store_.RootHandleRef();
}

bool UiRuntime::HasPendingVisualWork() const {
    if (layout_dirty_ ||
        node_store_.HasPendingLifecycleCommands() ||
        pending_caret_visibility_handle_ != UI_INVALID_HANDLE ||
        (*scroll_coordinator_).HasAutoScroll() ||
        !pending_text_scroll_metric_handles_.empty()) {
        return true;
    }

    return node_store_.Traversal().AnyActive([&](std::uint64_t, const UINode& node) {
        return node.needs_creation ||
            node.is_dirty ||
            node.text_glyphs_dirty ||
            node.text_selection_visuals_dirty ||
            node.scroll_offset_dirty ||
            node.has_pending_scroll_offset ||
            node.smooth_scroll_active ||
            std::abs(node.scroll_velocity_x) >= 0.001f ||
            std::abs(node.scroll_velocity_y) >= 0.001f;
    });
}

bool UiRuntime::NeedsAnimationFrame() const {
    return node_store_.Traversal().AnyActiveScrollView([&](std::uint64_t, const UINode& node) {
        return node.smooth_scroll_active ||
            std::abs(node.scroll_velocity_x) >= 0.001f || std::abs(node.scroll_velocity_y) >= 0.001f;
    });
}

bool UiRuntime::HasPointerAutoScroll() const {
    return Input().HasPointerAutoScroll();
}

float UiRuntime::window_width() const {
    return window_width_;
}

float UiRuntime::window_height() const {
    return window_height_;
}

} // namespace effindom::v2::ui
