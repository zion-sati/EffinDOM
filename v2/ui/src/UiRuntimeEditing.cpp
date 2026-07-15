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

bool IsLineBreakByte(char ch) {
    return ch == '\n' || ch == '\r';
}

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
        if (Selection().state().active_handle == handle) {
            Selection().state().active_handle = UI_INVALID_HANDLE;
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
    Input().SetInteractionTime(interaction_time_ms);
}

void UiRuntime::ClearUndoHistory(UINode& node) {
    text_editing_coordinator_.ClearUndoHistory(node);
}

void UiRuntime::BeginUndoGroup(UINode& node) {
    text_editing_coordinator_.BeginUndoGroup(node, Input().state().interaction_time_ms);
}

void UiRuntime::NotifyTextStateChanged(
    std::uint64_t handle,
    UINode& node,
    const std::string* previous_text) {
    text_editing_coordinator_.NotifyTextStateChanged(
        *this,
        event_sink_,
        handle,
        node,
        previous_text,
        nullptr);
}

void UiRuntime::NotifyTextStateChanged(std::uint64_t handle, UINode& node, const TextEdit& edit) {
    text_editing_coordinator_.NotifyTextStateChanged(
        *this,
        event_sink_,
        handle,
        node,
        nullptr,
        &edit);
}

bool UiRuntime::ApplyTextEdit(
    std::uint64_t handle,
    UINode& node,
    TextEdit edit,
    std::uint32_t selection_start,
    std::uint32_t selection_end) {
    return text_editing_coordinator_.ApplyTextEdit(
        *this,
        handle,
        node,
        std::move(edit),
        selection_start,
        selection_end);
}

std::optional<TextEdit> UiRuntime::CreateFullReplacementTextEdit(
    std::string_view old_text,
    std::string_view new_text) const {
    pending_text_edit_profile_.full_text_replacement_fallbacks += 1U;
    return CreateTextEditForFullReplacement(
        old_text,
        new_text,
        &pending_text_edit_profile_.full_text_replacement_compared_bytes);
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

bool UiRuntime::TryApplyIncrementalTextLineStarts(UINode& node, const TextEdit& edit) const {
    return TryApplyIncrementalTextLineStartsImpl(node, edit);
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
    return text_editing_coordinator_.Undo(*this, handle, node);
}

bool UiRuntime::RedoTextEdit(std::uint64_t handle, UINode& node) {
    return text_editing_coordinator_.Redo(*this, handle, node);
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
    if (Selection().state().cross_active && handle == Selection().state().area_handle) {
        const UINode* focused_node = Resolve(Focus().FocusedHandle());
        if (focused_node != nullptr &&
            focused_node->is_text_node &&
            focused_node->is_selectable &&
            !focused_node->text_content.empty()) {
            handle = Focus().FocusedHandle();
        } else if (Selection().state().start_node_handle != UI_INVALID_HANDLE) {
            handle = Selection().state().start_node_handle;
        }
    }

    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable || node->text_content.empty()) {
        return false;
    }

    const std::uint64_t area_handle =
        !node->is_editable ? FindSelectionAreaAncestor(handle) : static_cast<std::uint64_t>(UI_INVALID_HANDLE);
    if (area_handle != UI_INVALID_HANDLE) {
        if (Selection().state().cross_active && Selection().state().area_handle != area_handle) {
            ClearCrossSelection(true);
        }
        if (Selection().state().active_handle != UI_INVALID_HANDLE) {
            ClearSelectionHighlight(Selection().state().active_handle, true);
        }

        SetFocus(handle);
        Selection().BeginCrossSelection(
            *this,
            area_handle,
            handle,
            0U,
            handle,
            static_cast<std::uint32_t>(node->text_content.size()),
            true);
        NotifyCrossSelectionChanged();
        return true;
    }

    if (Selection().state().cross_active) {
        ClearCrossSelection(true);
    } else if (Selection().state().active_handle != UI_INVALID_HANDLE && Selection().state().active_handle != handle) {
        ClearSelectionHighlight(Selection().state().active_handle, true);
    }

    SetFocus(handle);
    Selection().BeginNodeSelection(
        *node,
        handle,
        0U,
        static_cast<std::uint32_t>(node->text_content.size()),
        false);
    node->caret_trailing_edge = false;
    node->last_interaction_time = Input().state().interaction_time_ms;
    MarkTextSelectionVisualsDirty(*node);
    EnsureTextCaretVisible(handle, *node);
    pending_caret_visibility_handle_ = handle;
    event_sink_.SelectionChanged(handle, node->selection_start, node->selection_end);
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
        if (Selection().state().cross_active && Selection().state().area_handle != area_handle) {
            ClearCrossSelection(true);
        }
        if (Selection().state().active_handle != UI_INVALID_HANDLE) {
            ClearSelectionHighlight(Selection().state().active_handle, true);
        }

        SetFocus(handle);
        Selection().BeginCrossSelection(
            *this,
            area_handle,
            handle,
            word_start,
            handle,
            word_end,
            true);
        Input().state().touch_text_tap_handle = UI_INVALID_HANDLE;
        Input().state().touch_text_tap_moved = false;
        NotifyCrossSelectionChanged();
        return true;
    }

    if (Selection().state().cross_active) {
        ClearCrossSelection(true);
    } else if (Selection().state().active_handle != UI_INVALID_HANDLE && Selection().state().active_handle != handle) {
        ClearSelectionHighlight(Selection().state().active_handle, true);
    }

    const bool focus_unchanged = Focus().IsFocused(handle);
    SetFocus(handle);
    Input().state().touch_text_tap_handle = UI_INVALID_HANDLE;
    Input().state().touch_text_tap_moved = false;
    Selection().BeginNodeSelection(*node, handle, word_start, word_end, true);
    node->caret_trailing_edge = false;
    node->last_interaction_time = Input().state().interaction_time_ms;
    MarkTextSelectionVisualsDirty(*node);
    EnsureTextCaretVisible(handle, *node);
    pending_caret_visibility_handle_ = handle;
    if (focus_unchanged || word_start != word_end) {
        event_sink_.SelectionChanged(handle, node->selection_start, node->selection_end);
    }
    return true;
}

bool UiRuntime::BeginSelectionEndpointDrag(std::uint64_t handle, std::uint32_t endpoint) {
    constexpr std::uint32_t kEndpointStart = 0U;
    constexpr std::uint32_t kEndpointEnd = 1U;
    if (endpoint != kEndpointStart && endpoint != kEndpointEnd) {
        return false;
    }

    if (Selection().state().cross_active && handle == Selection().state().area_handle) {
        if (!Selection().BeginCrossSelectionEndpointDrag(*this, endpoint)) {
            return false;
        }
        Input().state().touch_text_tap_handle = UI_INVALID_HANDLE;
        Input().state().touch_text_tap_moved = false;
        (*scroll_coordinator_).CancelActiveDrag();
        return true;
    }

    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable || node->selection_start == node->selection_end) {
        return false;
    }
    if (Selection().state().cross_active) {
        ClearCrossSelection(true);
    }

    SetFocus(handle);
    Input().state().touch_text_tap_handle = UI_INVALID_HANDLE;
    Input().state().touch_text_tap_moved = false;
    (*scroll_coordinator_).CancelActiveDrag();
    return Selection().BeginNodeSelectionEndpointDrag(*this, *node, handle, endpoint);
}

bool UiRuntime::SetTextSelectionRange(std::uint64_t handle, std::uint32_t selection_start, std::uint32_t selection_end) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable) {
        return false;
    }

    if (Selection().state().cross_active) {
        ClearCrossSelection(true);
    } else if (Selection().state().active_handle != UI_INVALID_HANDLE && Selection().state().active_handle != handle) {
        ClearSelectionHighlight(Selection().state().active_handle, true);
    }

    if (Selection().state().handle_drag_active && Selection().state().active_handle == handle) {
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

    Selection().ClearHitRects();
    Selection().state().horizontal_extend_active = false;
    Selection().state().active_handle = handle;
    Selection().state().active_dragged = false;
    Selection().state().anchor_handle = handle;
    Selection().state().anchor_index = node->selection_start;

    node->caret_trailing_edge = false;
    node->last_interaction_time = Input().state().interaction_time_ms;
    MarkTextSelectionVisualsDirty(*node);
    if (selection_changed || Focus().IsFocused(handle)) {
        event_sink_.SelectionChanged(handle, node->selection_start, node->selection_end);
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

bool UiRuntime::HandleCut(UINode& node) {
    return text_editing_coordinator_.CutSelection(*this, Focus().FocusedHandle(), node);
}

bool UiRuntime::HandlePaste(UINode& node) const {
    return text_editing_coordinator_.RequestPaste(
        const_cast<UiRuntime&>(*this),
        Focus().FocusedHandle(),
        node);
}

void UiRuntime::HandleImeUpdate(std::uint64_t handle, const std::uint8_t* utf8_str, std::uint32_t len, std::uint32_t caret_idx) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || !node->is_editable || (utf8_str == nullptr && len > 0U)) {
        return;
    }
    if (node->is_selectable) {
        SetFocus(handle);
    }

    const std::string_view updated_text = len == 0U
        ? std::string_view{}
        : std::string_view(reinterpret_cast<const char*>(utf8_str), len);
    text_editing_coordinator_.ApplyImeUpdate(*this, event_sink_, handle, *node, updated_text, caret_idx);
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

    const std::string_view inserted_text = len == 0U
        ? std::string_view{}
        : std::string_view(reinterpret_cast<const char*>(utf8_str), len);
    text_editing_coordinator_.ApplyReplaceRange(
        *this,
        handle,
        *node,
        start_idx,
        end_idx,
        inserted_text,
        caret_idx);
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

    const std::string_view inserted_text = len == 0U
        ? std::string_view{}
        : std::string_view(reinterpret_cast<const char*>(utf8_str), len);
    text_editing_coordinator_.ApplyPaste(*this, handle, *node, inserted_text);
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
    const bool multiline_textbox = IsMultilineEditorTextNode(node);

    if (text_editing_coordinator_.HandleTextMutationKey(
            *this,
            Focus().FocusedHandle(),
            node,
            key,
            text_insertion_modifier,
            multiline_textbox)) {
        return true;
    }

    return text_editing_coordinator_.HandleTextNavigationKey(
        *this,
        event_sink_,
        Focus().FocusedHandle(),
        node,
        key,
        shift,
        word_modifier,
        line_boundary_modifier,
        document_boundary_modifier);
}

void UiRuntime::MarkTextSelectionVisualsDirty(UINode& node) {
    if (!node.is_text_node) {
        return;
    }
    node.is_dirty = true;
    node.text_selection_visuals_dirty = true;
}

} // namespace effindom::v2::ui
