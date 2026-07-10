#include "UiRuntime.h"

#include "effindom_ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/ubrk.h>
#include <unicode/unistr.h>
#include <limits>
#include <string_view>
#include <utility>

namespace effindom::v2::ui {

namespace {

struct ClusterStop {
    std::uint32_t index = 0;
    float x = 0.0f;
};

struct LineByteRange {
    std::uint32_t raw_start = 0U;
    std::uint32_t visible_start = 0U;
    std::uint32_t end = 0U;
};

struct IncrementalTextDiff {
    std::uint32_t changed_start = 0U;
    std::uint32_t old_changed_end = 0U;
    std::uint32_t new_changed_end = 0U;
    std::int64_t byte_delta = 0;
};

std::vector<ClusterStop> BuildClusterStops(
    const std::vector<GlyphPlacement>& glyphs,
    float shaped_width,
    std::size_t line_length) {
    std::vector<ClusterStop> stops{};
    stops.reserve(glyphs.size() + 2U);
    stops.push_back(ClusterStop{0U, 0.0f});
    for (const GlyphPlacement& glyph : glyphs) {
        stops.push_back(ClusterStop{
            static_cast<std::uint32_t>(std::min<std::size_t>(glyph.cluster, line_length)),
            glyph.x,
        });
    }
    stops.push_back(ClusterStop{static_cast<std::uint32_t>(line_length), shaped_width});

    std::stable_sort(stops.begin(), stops.end(), [](const ClusterStop& lhs, const ClusterStop& rhs) {
        return lhs.index < rhs.index;
    });

    std::vector<ClusterStop> deduped{};
    deduped.reserve(stops.size());
    for (const ClusterStop& stop : stops) {
        if (!deduped.empty() && deduped.back().index == stop.index) {
            deduped.back().x = std::min(deduped.back().x, stop.x);
            continue;
        }
        deduped.push_back(stop);
    }
    return deduped;
}

std::size_t VisibleLineCount(const UINode& node) {
    const std::size_t available = node.break_offsets.size() > 1U ? node.break_offsets.size() - 1U : 0U;
    if (node.visible_line_count == 0U) return available;
    return std::min(node.visible_line_count, available);
}

bool IsLineBreakByte(char ch) {
    return ch == '\n' || ch == '\r';
}

bool IsSoftWrappedLineBoundary(const UINode& node, std::uint32_t pos) {
    if (!node.text_wrap || node.break_offsets.size() < 3U) {
        return false;
    }
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t clamped = std::min<std::uint32_t>(pos, text_length);
    if (clamped == 0U || clamped >= text_length) {
        return false;
    }
    const auto it = std::lower_bound(
        node.break_offsets.begin() + 1,
        node.break_offsets.end() - 1,
        static_cast<std::int32_t>(clamped));
    return it != node.break_offsets.end() - 1 &&
        *it == static_cast<std::int32_t>(clamped) &&
        !IsLineBreakByte(node.text_content[clamped - 1U]);
}

std::vector<std::uint32_t> ComputeTextLineStartsForRange(
    std::string_view text,
    std::uint32_t start,
    std::uint32_t end) {
    const std::uint32_t clamped_start = std::min(start, static_cast<std::uint32_t>(text.size()));
    const std::uint32_t clamped_end = std::min(std::max(start, end), static_cast<std::uint32_t>(text.size()));
    std::vector<std::uint32_t> starts{clamped_start};
    for (std::uint32_t index = clamped_start; index < clamped_end;) {
        if (text[index] == '\r') {
            if (index + 1U < clamped_end && text[index + 1U] == '\n') {
                starts.push_back(index + 2U);
                index += 2U;
            } else {
                starts.push_back(index + 1U);
                index += 1U;
            }
            continue;
        }
        if (text[index] == '\n') {
            starts.push_back(index + 1U);
            index += 1U;
            continue;
        }
        index += 1U;
    }
    return starts;
}

UINode::UndoEntry CaptureUndoEntry(const UINode& node) {
    return UINode::UndoEntry{
        node.text_content,
        node.selection_end,
        node.selection_start,
        node.selection_end,
    };
}

bool IsNamedTextMutationKey(std::string_view key) {
    return key == "Backspace" || key == "Delete";
}

bool IsNamedNonTextKey(std::string_view key) {
    return key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" || key == "ArrowDown" ||
        key == "Home" || key == "End" || key == "PageUp" || key == "PageDown" ||
        key == "Tab" || key == "Enter" || key == "Escape" ||
        key == "Shift" || key == "Control" || key == "Alt" || key == "Meta" ||
        key == "CapsLock" || key == "NumLock" || key == "ScrollLock" ||
        key == "Fn" || key == "Dead" || key == "Compose" || key == "Unidentified" ||
        key == "ContextMenu";
}

} // namespace

bool UiRuntime::SetSelectable(std::uint64_t handle, bool selectable, std::uint32_t selection_color) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    node->is_selectable = selectable;
    node->selection_color = selection_color;
    if (selectable) {
        node->is_interactive = true;
    }
    if (!selectable) {
        node->selection_start = 0U;
        node->selection_end = 0U;
        node->caret_trailing_edge = false;
        node->is_editable = false;
        node->is_text_editor = false;
        ClearUndoHistory(*node);
        if (active_selection_handle_ == handle) {
            active_selection_handle_ = UI_INVALID_HANDLE;
        }
    }
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetEditable(std::uint64_t handle, bool editable) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }

    node->is_editable = editable;
    if (editable) {
        const std::string previous_text = node->text_content;
        node->is_text_editor = true;
        node->is_selectable = true;
        node->is_interactive = true;
        if (ApplyAbsurdLineClamp(*node)) {
            NotifyTextStateChanged(handle, *node, &previous_text);
        }
    } else {
        ClearUndoHistory(*node);
    }
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetCaretColor(std::uint64_t handle, std::uint32_t color) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    node->caret_color = color;
    node->is_dirty = true;
    return true;
}

void UiRuntime::SetInteractionTime(std::uint64_t interaction_time_ms) {
    interaction_time_ms_ = interaction_time_ms;
}

void UiRuntime::ClearUndoHistory(UINode& node) {
    node.undo_stack.clear();
    node.redo_stack.clear();
    node.undo_group_open = false;
    node.undo_group_timestamp = 0U;
    node.undo_group_caret_before = 0U;
    node.undo_group_sel_start_before = 0U;
    node.undo_group_sel_end_before = 0U;
}

void UiRuntime::BeginUndoGroup(UINode& node) {
    if (!node.is_editable) {
        return;
    }

    const bool start_new_group =
        !node.undo_group_open ||
        interaction_time_ms_ < node.undo_group_timestamp ||
        (interaction_time_ms_ - node.undo_group_timestamp) > UINode::kUndoDebounceMs;
    if (start_new_group) {
        node.undo_stack.push_back(CaptureUndoEntry(node));
        while (node.undo_stack.size() > UINode::kMaxUndo) {
            node.undo_stack.erase(node.undo_stack.begin());
        }
        node.redo_stack.clear();
        node.undo_group_open = true;
        node.undo_group_caret_before = node.selection_end;
        node.undo_group_sel_start_before = node.selection_start;
        node.undo_group_sel_end_before = node.selection_end;
    }
    node.undo_group_timestamp = interaction_time_ms_;
}

void UiRuntime::NotifyTextStateChanged(
    std::uint64_t handle,
    UINode& node,
    const std::string* previous_text,
    const ExactTextReplaceNotification* exact_replace) {
    NotifyTextStateChangedImpl(handle, node, previous_text, exact_replace);
}

bool UiRuntime::ApplyAbsurdLineClamp(UINode& node) const {
    return ApplyAbsurdLineClampImpl(node);
}

void UiRuntime::RebuildTextLineStarts(UINode& node) const {
    node.text_line_starts = ComputeTextLineStartsForRange(
        std::string_view(node.text_content.data(), node.text_content.size()),
        0U,
        static_cast<std::uint32_t>(node.text_content.size()));
    node.text_line_starts_dirty = false;
}

std::size_t UiRuntime::LineIndexForTextLineStarts(const UINode& node, std::uint32_t pos) const {
    UINode& mutable_node = const_cast<UINode&>(node);
    if (mutable_node.text_line_starts_dirty || mutable_node.text_line_starts.empty()) {
        RebuildTextLineStarts(mutable_node);
    }
    if (mutable_node.text_line_starts.empty()) {
        return 0U;
    }

    const std::uint32_t clamped = std::min<std::uint32_t>(pos, static_cast<std::uint32_t>(node.text_content.size()));
    const auto it = std::upper_bound(mutable_node.text_line_starts.begin(), mutable_node.text_line_starts.end(), clamped);
    if (it == mutable_node.text_line_starts.begin()) {
        return 0U;
    }
    return static_cast<std::size_t>(std::distance(mutable_node.text_line_starts.begin(), it) - 1);
}

std::uint32_t UiRuntime::GetTextLineStart(const UINode& node, std::size_t line_index) const {
    UINode& mutable_node = const_cast<UINode&>(node);
    if (mutable_node.text_line_starts_dirty || mutable_node.text_line_starts.empty()) {
        RebuildTextLineStarts(mutable_node);
    }
    if (mutable_node.text_line_starts.empty()) {
        return 0U;
    }
    return mutable_node.text_line_starts[std::min(line_index, mutable_node.text_line_starts.size() - 1U)];
}

std::uint32_t UiRuntime::GetTextLineEnd(const UINode& node, std::size_t line_index) const {
    UINode& mutable_node = const_cast<UINode&>(node);
    if (mutable_node.text_line_starts_dirty || mutable_node.text_line_starts.empty()) {
        RebuildTextLineStarts(mutable_node);
    }
    if (mutable_node.text_line_starts.empty()) {
        return 0U;
    }

    const std::size_t clamped_index = std::min(line_index, mutable_node.text_line_starts.size() - 1U);
    const std::uint32_t start = mutable_node.text_line_starts[clamped_index];
    std::uint32_t end =
        clamped_index + 1U < mutable_node.text_line_starts.size()
        ? mutable_node.text_line_starts[clamped_index + 1U]
        : static_cast<std::uint32_t>(node.text_content.size());
    while (end > start && IsLineBreakByte(node.text_content[end - 1U])) {
        end -= 1U;
    }
    return end;
}

bool UiRuntime::TryApplyIncrementalTextLineStarts(UINode& node, std::string_view previous_text) const {
    return TryApplyIncrementalTextLineStartsImpl(node, previous_text);
}

float UiRuntime::RawLineXForIndex(
    std::string_view line_text,
    const ShapedTextRun& shaped,
    std::uint32_t local_index,
    bool trailing_edge) const {
    const std::vector<ClusterStop> stops = BuildClusterStops(shaped.glyphs, shaped.width, line_text.size());
    if (trailing_edge) {
        if (local_index == 0U) {
            return 0.0f;
        }
        if (local_index >= line_text.size()) {
            return shaped.width;
        }
        for (const ClusterStop& stop : stops) {
            if (stop.index >= local_index) {
                return stop.x;
            }
        }
        return shaped.width;
    }
    float x = shaped.width;
    for (const ClusterStop& stop : stops) {
        if (stop.index > local_index) {
            break;
        }
        x = stop.x;
    }
    if (local_index == 0U) {
        x = 0.0f;
    }
    if (local_index >= line_text.size()) {
        x = shaped.width;
    }
    return x;
}

bool UiRuntime::UndoTextEdit(std::uint64_t handle, UINode& node) {
    if (!node.is_editable || node.undo_stack.empty()) {
        return false;
    }

    node.redo_stack.push_back(CaptureUndoEntry(node));
    while (node.redo_stack.size() > UINode::kMaxUndo) {
        node.redo_stack.erase(node.redo_stack.begin());
    }

    const std::string previous_text = node.text_content;
    UINode::UndoEntry entry = std::move(node.undo_stack.back());
    node.undo_stack.pop_back();
    node.text_content = std::move(entry.text);
    node.selection_start = entry.sel_start;
    node.selection_end = entry.sel_end;
    node.caret_trailing_edge = false;
    (void)ApplyAbsurdLineClamp(node);
    node.undo_group_open = false;
    node.undo_group_timestamp = 0U;
    NotifyTextStateChanged(handle, node, &previous_text);
    return true;
}

bool UiRuntime::RedoTextEdit(std::uint64_t handle, UINode& node) {
    if (!node.is_editable || node.redo_stack.empty()) {
        return false;
    }

    node.undo_stack.push_back(CaptureUndoEntry(node));
    while (node.undo_stack.size() > UINode::kMaxUndo) {
        node.undo_stack.erase(node.undo_stack.begin());
    }

    const std::string previous_text = node.text_content;
    UINode::UndoEntry entry = std::move(node.redo_stack.back());
    node.redo_stack.pop_back();
    node.text_content = std::move(entry.text);
    node.selection_start = entry.sel_start;
    node.selection_end = entry.sel_end;
    node.caret_trailing_edge = false;
    (void)ApplyAbsurdLineClamp(node);
    node.undo_group_open = false;
    node.undo_group_timestamp = 0U;
    NotifyTextStateChanged(handle, node, &previous_text);
    return true;
}

bool UiRuntime::CanUndoTextEdit(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    return node != nullptr && node->is_text_node && node->is_editable && !node->undo_stack.empty();
}

bool UiRuntime::CanRedoTextEdit(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    return node != nullptr && node->is_text_node && node->is_editable && !node->redo_stack.empty();
}

bool UiRuntime::HasTextSelection(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    return node != nullptr && node->is_text_node && node->is_selectable && node->selection_start != node->selection_end;
}

bool UiRuntime::UndoTextEditAtHandle(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable) {
        return false;
    }
    SetFocus(handle);
    return UndoTextEdit(handle, *node);
}

bool UiRuntime::RedoTextEditAtHandle(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable) {
        return false;
    }
    SetFocus(handle);
    return RedoTextEdit(handle, *node);
}

bool UiRuntime::CopyTextSelection(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable || node->is_obscured || node->selection_start == node->selection_end) {
        return false;
    }
    HandleCopy(*node);
    return true;
}

bool UiRuntime::CutTextSelection(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable || node->is_obscured) {
        return false;
    }
    SetFocus(handle);
    if (!HandleCut(*node)) {
        return false;
    }
    node->is_dirty = true;
    return true;
}

bool UiRuntime::PasteText(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable) {
        return false;
    }
    SetFocus(handle);
    return HandlePaste(*node);
}

bool UiRuntime::SelectAllText(std::uint64_t handle) {
    if (cross_selection_active_ && handle == selection_area_handle_) {
        const UINode* focused_node = Resolve(focused_handle_);
        if (focused_node != nullptr &&
            focused_node->is_text_node &&
            focused_node->is_selectable &&
            !focused_node->text_content.empty()) {
            handle = focused_handle_;
        } else if (start_node_handle_ != UI_INVALID_HANDLE) {
            handle = start_node_handle_;
        }
    }

    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable || node->text_content.empty()) {
        return false;
    }

    const std::uint64_t area_handle =
        !node->is_editable ? FindSelectionAreaAncestor(handle) : static_cast<std::uint64_t>(UI_INVALID_HANDLE);
    if (area_handle != UI_INVALID_HANDLE) {
        if (cross_selection_active_ && selection_area_handle_ != area_handle) {
            ClearCrossSelection(true);
        }
        if (active_selection_handle_ != UI_INVALID_HANDLE) {
            ClearSelectionHighlight(active_selection_handle_, true);
        }

        SetFocus(handle);
        EnsureSelectionAreaNodes(area_handle);
        current_selection_hit_rects_.clear();
        selection_horizontal_extend_active_ = false;
        cross_selection_active_ = true;
        cross_selection_dragged_ = false;
        cross_selection_horizontal_extend_active_ = true;
        selection_area_handle_ = area_handle;
        start_node_handle_ = handle;
        start_index_ = 0U;
        end_node_handle_ = handle;
        end_index_ = static_cast<std::uint32_t>(node->text_content.size());
        active_selection_handle_ = UI_INVALID_HANDLE;
        active_selection_dragged_ = false;
        MarkSelectionAreaNodesDirty();
        NotifyCrossSelectionChanged();
        return true;
    }

    if (cross_selection_active_) {
        ClearCrossSelection(true);
    } else if (active_selection_handle_ != UI_INVALID_HANDLE && active_selection_handle_ != handle) {
        ClearSelectionHighlight(active_selection_handle_, true);
    }

    SetFocus(handle);
    current_selection_hit_rects_.clear();
    selection_horizontal_extend_active_ = false;
    active_selection_handle_ = handle;
    active_selection_dragged_ = false;
    node->selection_start = 0U;
    node->selection_end = static_cast<std::uint32_t>(node->text_content.size());
    node->caret_trailing_edge = false;
    node->last_interaction_time = interaction_time_ms_;
    selection_anchor_handle_ = handle;
    selection_anchor_index_ = 0U;
    MarkTextSelectionVisualsDirty(*node);
    EnsureTextCaretVisible(handle, *node);
    pending_caret_visibility_handle_ = handle;
    as_on_selection_changed(handle, node->selection_start, node->selection_end);
    return true;
}

bool UiRuntime::SelectWordAt(std::uint64_t handle, float logical_x, float logical_y) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable || node->text_content.empty()) {
        return false;
    }
    if (node->is_obscured) {
        return SelectAllText(handle);
    }

    const std::uint32_t index = GetStringIndexFromPoint(*node, logical_x - node->abs_x, logical_y - node->abs_y);
    const auto [word_start, word_end] = GetWordBoundaries(*node, index);
    if (word_start == word_end) {
        return false;
    }

    const std::uint64_t area_handle =
        !node->is_editable ? FindSelectionAreaAncestor(handle) : static_cast<std::uint64_t>(UI_INVALID_HANDLE);
    if (area_handle != UI_INVALID_HANDLE) {
        if (cross_selection_active_ && selection_area_handle_ != area_handle) {
            ClearCrossSelection(true);
        }
        if (active_selection_handle_ != UI_INVALID_HANDLE) {
            ClearSelectionHighlight(active_selection_handle_, true);
        }

        SetFocus(handle);
        EnsureSelectionAreaNodes(area_handle);
        current_selection_hit_rects_.clear();
        selection_horizontal_extend_active_ = false;
        cross_selection_active_ = true;
        cross_selection_dragged_ = false;
        cross_selection_horizontal_extend_active_ = true;
        selection_area_handle_ = area_handle;
        start_node_handle_ = handle;
        start_index_ = word_start;
        end_node_handle_ = handle;
        end_index_ = word_end;
        touch_text_tap_handle_ = UI_INVALID_HANDLE;
        touch_text_tap_moved_ = false;
        active_selection_handle_ = UI_INVALID_HANDLE;
        active_selection_dragged_ = false;
        MarkSelectionAreaNodesDirty();
        NotifyCrossSelectionChanged();
        return true;
    }

    if (cross_selection_active_) {
        ClearCrossSelection(true);
    } else if (active_selection_handle_ != UI_INVALID_HANDLE && active_selection_handle_ != handle) {
        ClearSelectionHighlight(active_selection_handle_, true);
    }

    const bool focus_unchanged = focused_handle_ == handle;
    SetFocus(handle);
    current_selection_hit_rects_.clear();
    selection_horizontal_extend_active_ = true;
    active_selection_handle_ = handle;
    active_selection_dragged_ = false;
    touch_text_tap_handle_ = UI_INVALID_HANDLE;
    touch_text_tap_moved_ = false;
    node->selection_start = word_start;
    node->selection_end = word_end;
    node->caret_trailing_edge = false;
    node->last_interaction_time = interaction_time_ms_;
    selection_anchor_handle_ = handle;
    selection_anchor_index_ = node->selection_start;
    MarkTextSelectionVisualsDirty(*node);
    EnsureTextCaretVisible(handle, *node);
    pending_caret_visibility_handle_ = handle;
    if (focus_unchanged || word_start != word_end) {
        as_on_selection_changed(handle, node->selection_start, node->selection_end);
    }
    return true;
}

bool UiRuntime::BeginSelectionEndpointDrag(std::uint64_t handle, std::uint32_t endpoint) {
    constexpr std::uint32_t kEndpointStart = 0U;
    constexpr std::uint32_t kEndpointEnd = 1U;
    if (endpoint != kEndpointStart && endpoint != kEndpointEnd) {
        return false;
    }

    if (cross_selection_active_ && handle == selection_area_handle_) {
        EnsureSelectionAreaNodes(selection_area_handle_);
        if (selection_area_nodes_.empty() ||
            start_node_handle_ == UI_INVALID_HANDLE ||
            end_node_handle_ == UI_INVALID_HANDLE ||
            (start_node_handle_ == end_node_handle_ && start_index_ == end_index_)) {
            return false;
        }
        cross_selection_dragged_ = true;
        cross_selection_horizontal_extend_active_ = true;
        selection_handle_drag_active_ = true;
        active_selection_drag_endpoint_ = endpoint;
        active_selection_stationary_index_ = 0U;
        touch_text_tap_handle_ = UI_INVALID_HANDLE;
        touch_text_tap_moved_ = false;
        active_selection_handle_ = UI_INVALID_HANDLE;
        active_selection_dragged_ = false;
        active_scroll_handle_ = UI_INVALID_HANDLE;
        active_scroll_dragged_ = false;
        MarkSelectionAreaNodesDirty();
        return true;
    }

    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable || node->selection_start == node->selection_end) {
        return false;
    }
    if (cross_selection_active_) {
        ClearCrossSelection(true);
    }

    SetFocus(handle);
    selection_handle_drag_active_ = true;
    active_selection_drag_endpoint_ = endpoint;
    active_selection_stationary_index_ = endpoint == 0U ? node->selection_end : node->selection_start;
    touch_text_tap_handle_ = UI_INVALID_HANDLE;
    touch_text_tap_moved_ = false;
    active_selection_handle_ = handle;
    active_selection_dragged_ = true;
    active_scroll_handle_ = UI_INVALID_HANDLE;
    active_scroll_dragged_ = false;
    selection_horizontal_extend_active_ = true;
    selection_anchor_handle_ = handle;
    selection_anchor_index_ = node->selection_start;
    MarkTextSelectionVisualsDirty(*node);
    return true;
}

bool UiRuntime::SetTextSelectionRange(std::uint64_t handle, std::uint32_t selection_start, std::uint32_t selection_end) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable) {
        return false;
    }

    if (cross_selection_active_) {
        ClearCrossSelection(true);
    } else if (active_selection_handle_ != UI_INVALID_HANDLE && active_selection_handle_ != handle) {
        ClearSelectionHighlight(active_selection_handle_, true);
    }

    if (selection_handle_drag_active_ && active_selection_handle_ == handle) {
        return true;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    const std::uint32_t clamped_start = std::min(selection_start, text_length);
    const std::uint32_t clamped_end = std::min(selection_end, text_length);
    const bool selection_changed =
        node->selection_start != clamped_start ||
        node->selection_end != clamped_end;
    node->selection_start = clamped_start;
    node->selection_end = clamped_end;

    current_selection_hit_rects_.clear();
    selection_horizontal_extend_active_ = false;
    active_selection_handle_ = handle;
    active_selection_dragged_ = false;
    selection_anchor_handle_ = handle;
    selection_anchor_index_ = node->selection_start;

    node->caret_trailing_edge = false;
    node->last_interaction_time = interaction_time_ms_;
    MarkTextSelectionVisualsDirty(*node);
    if (selection_changed || focused_handle_ == handle) {
        as_on_selection_changed(handle, node->selection_start, node->selection_end);
    }
    return true;
}

void UiRuntime::HandleCopy(const UINode& node) const {
    if (node.is_obscured || node.selection_start == node.selection_end) {
        return;
    }

    const std::uint32_t start = std::min(node.selection_start, node.selection_end);
    const std::uint32_t end = std::max(node.selection_start, node.selection_end);
    std::string plain_text{};
    std::string rich_json{};
    if (BuildSelectionClipboardRichPayload(node, start, end, plain_text, rich_json)) {
        EmitClipboardWrite(plain_text, &rich_json);
        return;
    }
    const std::string_view selection(node.text_content.data() + start, static_cast<std::size_t>(end - start));
    EmitClipboardWrite(selection);
}

bool UiRuntime::DeleteSelection(UINode& node) {
    if (node.selection_start == node.selection_end) {
        return false;
    }

    const std::uint32_t start = std::min(node.selection_start, node.selection_end);
    const std::uint32_t end = std::max(node.selection_start, node.selection_end);
    node.text_content.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    node.selection_start = start;
    node.selection_end = start;
    node.caret_trailing_edge = false;
    selection_anchor_handle_ = focused_handle_;
    selection_anchor_index_ = start;
    return true;
}

bool UiRuntime::HandleCut(UINode& node) {
    if (!node.is_editable || node.is_obscured || node.selection_start == node.selection_end) {
        return false;
    }

    BeginUndoGroup(node);
    const std::string previous_text = node.text_content;
    HandleCopy(node);
    if (!DeleteSelection(node)) return false;
    NotifyTextStateChanged(focused_handle_, node, &previous_text);
    return true;
}

bool UiRuntime::HandlePaste(UINode& node) const {
    if (focused_handle_ == UI_INVALID_HANDLE || !node.is_editable) {
        return false;
    }
    as_on_request_clipboard_read(focused_handle_);
    return true;
}

void UiRuntime::HandleImeUpdate(std::uint64_t handle, const std::uint8_t* utf8_str, std::uint32_t len, std::uint32_t caret_idx) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable || (utf8_str == nullptr && len > 0U)) {
        return;
    }
    if (node->is_selectable) {
        SetFocus(handle);
    }

    const std::string updated_text =
        len == 0U ? std::string{} : std::string(reinterpret_cast<const char*>(utf8_str), static_cast<std::size_t>(len));
    const std::uint32_t clamped_index =
        std::min<std::uint32_t>(caret_idx, static_cast<std::uint32_t>(updated_text.size()));
    if (node->text_content == updated_text && node->selection_start == clamped_index && node->selection_end == clamped_index) {
        return;
    }

    BeginUndoGroup(*node);
    const std::string previous_text = node->text_content;
    node->text_content = updated_text;
    node->selection_start = clamped_index;
    node->selection_end = clamped_index;
    node->caret_trailing_edge = false;
    (void)ApplyAbsurdLineClamp(*node);
    NotifyTextStateChanged(handle, *node, &previous_text);
}

void UiRuntime::HandleTextReplaceRange(
    std::uint64_t handle,
    std::uint32_t start_idx,
    std::uint32_t end_idx,
    const std::uint8_t* utf8_str,
    std::uint32_t len,
    std::uint32_t caret_idx) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable || (utf8_str == nullptr && len > 0U)) {
        return;
    }
    if (node->is_selectable) {
        SetFocus(handle);
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    const std::uint32_t replace_start = std::min(start_idx, text_length);
    const std::uint32_t replace_end = std::min(std::max(start_idx, end_idx), text_length);
    const std::string inserted_text =
        len == 0U ? std::string{} : std::string(reinterpret_cast<const char*>(utf8_str), static_cast<std::size_t>(len));
    const std::uint32_t next_text_length =
        text_length - (replace_end - replace_start) + static_cast<std::uint32_t>(inserted_text.size());
    const std::uint32_t clamped_caret = std::min<std::uint32_t>(caret_idx, next_text_length);

    if (replace_start == replace_end &&
        inserted_text.empty() &&
        node->selection_start == clamped_caret &&
        node->selection_end == clamped_caret) {
        return;
    }

    std::string updated_text = node->text_content;
    updated_text.replace(
        static_cast<std::size_t>(replace_start),
        static_cast<std::size_t>(replace_end - replace_start),
        inserted_text);
    if (updated_text == node->text_content &&
        node->selection_start == clamped_caret &&
        node->selection_end == clamped_caret) {
        return;
    }

    BeginUndoGroup(*node);
    const std::string previous_text = node->text_content;
    node->text_content = std::move(updated_text);
    node->selection_start = clamped_caret;
    node->selection_end = clamped_caret;
    node->caret_trailing_edge = false;
    (void)ApplyAbsurdLineClamp(*node);
    const ExactTextReplaceNotification exact_replace{
        replace_start,
        replace_end,
        replace_start,
        static_cast<std::uint32_t>(inserted_text.size()),
    };
    NotifyTextStateChanged(handle, *node, &previous_text, &exact_replace);
}

void UiRuntime::HandlePasteText(std::uint64_t handle, const std::uint8_t* utf8_str, std::uint32_t len) {
    if (utf8_str == nullptr && len > 0U) {
        return;
    }

    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable) {
        return;
    }
    SetFocus(handle);

    const bool had_selection = node->selection_start != node->selection_end;
    if (!had_selection && len == 0U) {
        return;
    }

    BeginUndoGroup(*node);
    const std::string previous_text = node->text_content;
    (void)DeleteSelection(*node);
    const std::uint32_t insert_at = std::min<std::uint32_t>(node->selection_start, static_cast<std::uint32_t>(node->text_content.size()));
    if (len > 0U) {
        node->text_content.insert(
            static_cast<std::size_t>(insert_at),
            reinterpret_cast<const char*>(utf8_str),
            static_cast<std::size_t>(len));
    }

    const std::uint32_t new_caret = insert_at + len;
    node->selection_start = new_caret;
    node->selection_end = new_caret;
    node->caret_trailing_edge = false;
    (void)ApplyAbsurdLineClamp(*node);
    NotifyTextStateChanged(handle, *node, &previous_text);
}

bool UiRuntime::HandleTextEditingKey(UINode& node, std::string_view key, std::uint32_t modifiers) {
    if (key.empty()) {
        return false;
    }

    const bool shift = (modifiers & UI_KEY_MOD_SHIFT) != 0U;
    const bool word_modifier = HasWordNavigationModifier(modifiers);
    const bool line_boundary_modifier = HasLineBoundaryModifier(modifiers);
    const bool document_boundary_modifier = HasDocumentBoundaryModifier(modifiers);
    const bool text_insertion_modifier = (modifiers & (UI_KEY_MOD_CTRL | UI_KEY_MOD_META | UI_KEY_MOD_ALT)) != 0U;
    const bool horizontal_selection_key = key == "ArrowLeft" || key == "ArrowRight";
    const bool vertical_navigation_key = key == "ArrowUp" || key == "ArrowDown";
    const bool page_navigation_key = key == "PageUp" || key == "PageDown";
    const bool multiline_textbox = IsMultilineEditorTextNode(node);
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    std::uint32_t caret = std::min<std::uint32_t>(node.selection_end, text_length);
    std::optional<std::uint32_t> next_index{};

    if (node.is_editable && IsNamedTextMutationKey(key)) {
        const std::uint32_t selection_min = std::min(node.selection_start, node.selection_end);
        const std::uint32_t selection_max = std::max(node.selection_start, node.selection_end);
        if (selection_min == selection_max) {
            if (key == "Backspace" && caret == 0U) {
                return false;
            }
            if (key == "Delete" && caret >= text_length) {
                return false;
            }
        }

        BeginUndoGroup(node);
        const std::string previous_text = node.text_content;
        if (selection_min != selection_max) {
            (void)DeleteSelection(node);
        } else if (key == "Backspace") {
            const std::uint32_t erase_start = NextCharacterIndex(node.text_content, caret, false);
            node.text_content.erase(
                static_cast<std::size_t>(erase_start),
                static_cast<std::size_t>(caret - erase_start));
            node.selection_start = erase_start;
            node.selection_end = erase_start;
            node.caret_trailing_edge = false;
        } else {
            const std::uint32_t erase_end = NextCharacterIndex(node.text_content, caret, true);
            node.text_content.erase(
                static_cast<std::size_t>(caret),
                static_cast<std::size_t>(erase_end - caret));
            node.selection_start = caret;
            node.selection_end = caret;
            node.caret_trailing_edge = false;
        }
        NotifyTextStateChanged(focused_handle_, node, &previous_text);
        selection_horizontal_extend_active_ = false;
        return true;
    }

    if (node.is_editable && !text_insertion_modifier && key == "Enter" && multiline_textbox) {
        BeginUndoGroup(node);
        const std::string previous_text = node.text_content;
        (void)DeleteSelection(node);
        const std::uint32_t insert_at =
            std::min<std::uint32_t>(node.selection_start, static_cast<std::uint32_t>(node.text_content.size()));
        node.text_content.insert(insert_at, "\n", 1U);
        const std::uint32_t new_caret = insert_at + 1U;
        node.selection_start = new_caret;
        node.selection_end = new_caret;
        node.caret_trailing_edge = false;
        (void)ApplyAbsurdLineClamp(node);
        NotifyTextStateChanged(focused_handle_, node, &previous_text);
        selection_horizontal_extend_active_ = false;
        return true;
    }

    if (node.is_editable && !text_insertion_modifier && !IsNamedNonTextKey(key)) {
        BeginUndoGroup(node);
        const std::string previous_text = node.text_content;
        (void)DeleteSelection(node);
        const std::uint32_t insert_at = std::min<std::uint32_t>(node.selection_start, static_cast<std::uint32_t>(node.text_content.size()));
        node.text_content.insert(insert_at, key.data(), key.size());
        const std::uint32_t new_caret = insert_at + static_cast<std::uint32_t>(key.size());
        node.selection_start = new_caret;
        node.selection_end = new_caret;
        node.caret_trailing_edge = false;
        (void)ApplyAbsurdLineClamp(node);
        NotifyTextStateChanged(focused_handle_, node, &previous_text);
        selection_horizontal_extend_active_ = false;
        return true;
    }

    if (shift &&
        node.selection_start == node.selection_end &&
        horizontal_selection_key &&
        !selection_horizontal_extend_active_ &&
        !node.is_editable &&
        !IsEditorTextNode(node)) {
        return false;
    }
    if (shift &&
        (vertical_navigation_key || page_navigation_key) &&
        !node.is_editable &&
        !IsEditorTextNode(node)) {
        return false;
    }

    if (!shift && node.selection_start != node.selection_end) {
        const std::uint32_t selection_min = std::min(node.selection_start, node.selection_end);
        const std::uint32_t selection_max = std::max(node.selection_start, node.selection_end);
        if ((key == "ArrowLeft" || key == "ArrowRight") &&
            !word_modifier &&
            !line_boundary_modifier &&
            !document_boundary_modifier) {
            next_index = key == "ArrowLeft" ? selection_min : selection_max;
        } else if (key == "ArrowLeft") {
            next_index = selection_min;
        } else if (key == "ArrowRight") {
            next_index = selection_max;
        }
        if (next_index.has_value() && word_modifier && (key == "ArrowLeft" || key == "ArrowRight")) next_index = key == "ArrowLeft" ? selection_min : selection_max;
    }

    if (!next_index.has_value()) {
        if (key == "ArrowLeft") {
            if (!shift &&
                !line_boundary_modifier &&
                !word_modifier &&
                !document_boundary_modifier &&
                node.selection_start == node.selection_end &&
                !node.caret_trailing_edge &&
                IsSoftWrappedLineBoundary(node, caret)) {
                next_index = caret;
                node.caret_trailing_edge = true;
            } else {
                next_index = line_boundary_modifier
                    ? IndexForLineBegin(node, caret)
                    : (word_modifier ? NextWordIndex(node, caret, false) : NextCharacterIndex(node.text_content, caret, false));
            }
        } else if (key == "ArrowRight") {
            if (!shift &&
                !line_boundary_modifier &&
                !word_modifier &&
                !document_boundary_modifier &&
                node.selection_start == node.selection_end &&
                node.caret_trailing_edge &&
                IsSoftWrappedLineBoundary(node, caret)) {
                next_index = caret;
                node.caret_trailing_edge = false;
            } else {
                next_index = line_boundary_modifier
                    ? IndexForLineEnd(node, caret)
                    : (word_modifier ? NextWordIndex(node, caret, true) : NextCharacterIndex(node.text_content, caret, true));
            }
        } else if (key == "ArrowUp") {
            next_index = IndexForVerticalMove(node, caret, false);
        } else if (key == "ArrowDown") {
            next_index = IndexForVerticalMove(node, caret, true);
        } else if (key == "PageUp") {
            next_index = IndexForPageMove(node, caret, false);
        } else if (key == "PageDown") {
            next_index = IndexForPageMove(node, caret, true);
        } else if (key == "Home") {
            next_index = document_boundary_modifier ? 0U : IndexForLineBegin(node, caret);
        } else if (key == "End") {
            next_index = document_boundary_modifier ? text_length : IndexForLineEnd(node, caret);
        }
    }

    if (!next_index.has_value()) {
        return false;
    }

    if (shift &&
        (key == "ArrowUp" || key == "ArrowDown" || key == "PageUp" || key == "PageDown")) {
        const std::size_t line_count = VisibleLineCount(node);
        if (line_count > 0U) {
            const auto [__, line_index] = GetLocalPositionFromIndex(node, caret);
            const auto [___, next_line_index] = GetLocalPositionFromIndex(node, *next_index);
            if ((key == "ArrowUp" || key == "PageUp") &&
                (line_index == 0 || (key == "PageUp" && next_line_index == 0))) {
                if (key != "PageUp" || *next_index == caret || next_line_index == 0) {
                    next_index = 0U;
                }
            } else if ((key == "ArrowDown" || key == "PageDown") &&
                       (static_cast<std::size_t>(line_index) + 1U >= line_count ||
                        (key == "PageDown" && static_cast<std::size_t>(next_line_index) + 1U >= line_count))) {
                if (key != "PageDown" || *next_index == caret ||
                    static_cast<std::size_t>(next_line_index) + 1U >= line_count) {
                    next_index = text_length;
                }
            }
        }
    }

    const std::uint32_t clamped_next = std::min<std::uint32_t>(*next_index, text_length);
    if (shift) {
        node.caret_trailing_edge = false;
        if (selection_anchor_handle_ != focused_handle_) {
            selection_anchor_handle_ = focused_handle_;
            selection_anchor_index_ = node.selection_start;
        } else if (node.selection_start == node.selection_end) {
            selection_anchor_index_ = node.selection_start;
        }
        node.selection_start = selection_anchor_index_;
        node.selection_end = clamped_next;
    } else {
        if (*next_index != caret) {
            node.caret_trailing_edge = false;
        }
        node.selection_start = clamped_next;
        node.selection_end = clamped_next;
        selection_anchor_handle_ = focused_handle_;
        selection_anchor_index_ = clamped_next;
    }
    selection_horizontal_extend_active_ = shift && horizontal_selection_key;

    node.last_interaction_time = interaction_time_ms_;
    MarkTextSelectionVisualsDirty(node);
    EnsureTextCaretVisible(focused_handle_, node);
    pending_caret_visibility_handle_ = focused_handle_;
    as_on_selection_changed(
        focused_handle_,
        node.selection_start,
        node.selection_end);
    return true;
}

void UiRuntime::MarkTextSelectionVisualsDirty(UINode& node) {
    if (!node.is_text_node) {
        return;
    }
    node.is_dirty = true;
    node.text_selection_visuals_dirty = true;
}

} // namespace effindom::v2::ui
