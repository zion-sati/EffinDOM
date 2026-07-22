#include "UiTextEditingCoordinator.h"

#include <algorithm>

namespace effindom::v2::ui {

namespace {

#ifdef __EMSCRIPTEN__
struct IncrementalTextReplace {
    std::uint32_t start = 0U;
    std::uint32_t old_end = 0U;
    std::uint32_t new_end = 0U;
};

bool ComputeIncrementalTextReplace(
    std::string_view previous_text,
    std::string_view next_text,
    IncrementalTextReplace& out) {
    out = IncrementalTextReplace{};
    if (previous_text == next_text) {
        return false;
    }
    const std::size_t old_length = previous_text.size();
    const std::size_t new_length = next_text.size();
    std::size_t common_prefix = 0U;
    const std::size_t shared_prefix_limit = std::min(old_length, new_length);
    while (common_prefix < shared_prefix_limit && previous_text[common_prefix] == next_text[common_prefix]) {
        common_prefix += 1U;
    }
    std::size_t common_suffix = 0U;
    while (common_suffix < (old_length - common_prefix) &&
           common_suffix < (new_length - common_prefix) &&
           previous_text[old_length - common_suffix - 1U] == next_text[new_length - common_suffix - 1U]) {
        common_suffix += 1U;
    }
    out.start = static_cast<std::uint32_t>(common_prefix);
    out.old_end = static_cast<std::uint32_t>(old_length - common_suffix);
    out.new_end = static_cast<std::uint32_t>(new_length - common_suffix);
    return true;
}
#endif

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

std::size_t VisibleLineCount(const UINode& node) {
    const std::size_t available = node.break_offsets.size() > 1U ? node.break_offsets.size() - 1U : 0U;
    return node.visible_line_count == 0U ? available : std::min(node.visible_line_count, available);
}

} // namespace

UINode::UndoEntry TextEditingCoordinator::CaptureUndoEntry(const UINode& node) const {
    return UINode::UndoEntry{
        node.text_content,
        node.selection_end,
        node.selection_start,
        node.selection_end,
    };
}

void TextEditingCoordinator::ClearUndoHistory(UINode& node) const {
    node.undo_stack.clear();
    node.redo_stack.clear();
    node.undo_group_open = false;
    node.undo_group_timestamp = 0U;
    node.undo_group_caret_before = 0U;
    node.undo_group_sel_start_before = 0U;
    node.undo_group_sel_end_before = 0U;
}

void TextEditingCoordinator::PushUndoEntry(UINode& node) const {
    node.undo_stack.push_back(CaptureUndoEntry(node));
    while (node.undo_stack.size() > UINode::kMaxUndo) {
        node.undo_stack.erase(node.undo_stack.begin());
    }
}

void TextEditingCoordinator::PushRedoEntry(UINode& node) const {
    node.redo_stack.push_back(CaptureUndoEntry(node));
    while (node.redo_stack.size() > UINode::kMaxUndo) {
        node.redo_stack.erase(node.redo_stack.begin());
    }
}

void TextEditingCoordinator::BeginUndoGroup(UINode& node, std::uint64_t interaction_time_ms) const {
    if (!node.is_editable) {
        return;
    }
    const bool start_new_group =
        !node.undo_group_open ||
        interaction_time_ms < node.undo_group_timestamp ||
        (interaction_time_ms - node.undo_group_timestamp) > UINode::kUndoDebounceMs;
    if (start_new_group) {
        PushUndoEntry(node);
        node.redo_stack.clear();
        node.undo_group_open = true;
        node.undo_group_caret_before = node.selection_end;
        node.undo_group_sel_start_before = node.selection_start;
        node.undo_group_sel_end_before = node.selection_end;
    }
    node.undo_group_timestamp = interaction_time_ms;
}

bool TextEditingCoordinator::ApplyTextEdit(
    TextEditingHost& host,
    std::uint64_t handle,
    UINode& node,
    TextEdit edit,
    std::uint32_t selection_start,
    std::uint32_t selection_end) const {
    if (edit.old_text_length != node.text_content.size() ||
        edit.start > edit.removed_end ||
        edit.removed_end > node.text_content.size()) {
        return false;
    }
    node.text_content.replace(
        static_cast<std::size_t>(edit.start),
        static_cast<std::size_t>(edit.removed_end - edit.start),
        edit.inserted_text);
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    node.selection_start = std::min(selection_start, text_length);
    node.selection_end = std::min(selection_end, text_length);
    node.caret_trailing_edge = false;
    host.SetTextEditingSelectionAnchor(handle, node.selection_start);
    host.BeginTextEditTransaction();
    host.RecordTextEditApplication(edit.inserted_text.size(), edit.removed_text.size());
    if (host.WouldApplyTextHardClamp(node)) {
        const PreviousTextView previous_view(node.text_content, edit);
        std::string previous_text = previous_view.materialize(0U, previous_view.size());
        host.RecordTextEditMaterializedPreviousText(previous_text.size());
        if (host.ApplyTextHardClamp(node)) {
            host.NotifyTextEditClamped(handle, node, previous_text);
            return true;
        }
    }
    host.NotifyTextEditApplied(handle, node, edit);
    return true;
}

void TextEditingCoordinator::NotifyTextStateChanged(
    TextEditingHost& host,
    const UiEventSink& events,
    std::uint64_t handle,
    UINode& node,
    const std::string* previous_text,
    const TextEdit* edit) const {
    node.text_line_starts_dirty = true;
    if (edit != nullptr) {
        if (!host.TryApplyTextEditLineStarts(node, *edit)) {
            host.RebuildTextEditLineStarts(node);
        }
    } else if (previous_text == nullptr) {
        host.RebuildTextEditLineStarts(node);
    } else if (!host.TryApplyTextEditLineStarts(node, *previous_text)) {
        host.RebuildTextEditLineStarts(node);
    }
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    node.selection_start = std::min(node.selection_start, text_length);
    node.selection_end = std::min(node.selection_end, text_length);
    node.last_interaction_time = host.TextEditInteractionTime();
    host.SetTextEditingSelectionAnchor(handle, node.selection_start);
    const bool kept_incremental_layout_cache = edit != nullptr
        ? (host.TryApplyTextEditNonWrapCache(node, *edit) ||
           host.TryApplyTextEditWrappedCache(node, *edit))
        : (previous_text != nullptr &&
           (host.TryApplyTextEditNonWrapCache(node, *previous_text) ||
            host.TryApplyTextEditWrappedCache(node, *previous_text)));
    if (!kept_incremental_layout_cache) {
        host.InvalidateTextEditLayoutCache(node);
    }
    host.MarkTextEditYogaDirty(node);
    node.is_dirty = true;
    node.text_glyphs_dirty = true;
    node.text_selection_visuals_dirty = false;
    host.MarkTextEditLayoutDirty();
    const bool first_dirty_text_update_this_frame = host.RegisterTextEditScrollMetrics(handle);
    const bool should_ensure_caret_visible = host.IsTextEditFocused(handle);
    if (first_dirty_text_update_this_frame) {
        host.UpdateTextEditAncestorScrollMetrics(handle);
        if (should_ensure_caret_visible) {
            host.EnsureTextEditCaretVisible(handle, node);
        }
    }
    host.SetTextEditPendingCaretVisibility(
        should_ensure_caret_visible ? handle : static_cast<std::uint64_t>(UI_INVALID_HANDLE));
#ifdef __EMSCRIPTEN__
    bool emitted_incremental_replace = false;
    if (edit != nullptr) {
        const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
        const std::uint32_t inserted_start = std::min(edit->start, text_length);
        const std::uint32_t inserted_length = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(edit->inserted_text.size()),
            text_length - inserted_start);
        events.TextReplaced(
            handle,
            edit->start,
            edit->removed_end,
            std::string_view(node.text_content.data() + inserted_start, inserted_length));
        emitted_incremental_replace = true;
    } else if (previous_text != nullptr) {
        IncrementalTextReplace replace{};
        if (ComputeIncrementalTextReplace(*previous_text, node.text_content, replace)) {
            const std::uint32_t inserted_length = replace.new_end - replace.start;
            events.TextReplaced(
                handle,
                replace.start,
                replace.old_end,
                std::string_view(node.text_content.data() + replace.start, inserted_length));
            emitted_incremental_replace = true;
        }
    }
    if (!emitted_incremental_replace) {
        events.TextChanged(handle, node.text_content);
    }
#else
    events.TextChanged(handle, node.text_content);
#endif
    events.SelectionChanged(handle, node.selection_start, node.selection_end);
}

bool TextEditingCoordinator::Undo(TextEditingHost& host, std::uint64_t handle, UINode& node) const {
    if (!node.is_editable || node.undo_stack.empty()) {
        return false;
    }
    PushRedoEntry(node);
    UINode::UndoEntry entry = std::move(node.undo_stack.back());
    node.undo_stack.pop_back();
    const auto edit = host.CreateTextEditFullReplacement(node.text_content, entry.text);
    node.undo_group_open = false;
    node.undo_group_timestamp = 0U;
    return edit.has_value() && ApplyTextEdit(host, handle, node, *edit, entry.sel_start, entry.sel_end);
}

bool TextEditingCoordinator::Redo(TextEditingHost& host, std::uint64_t handle, UINode& node) const {
    if (!node.is_editable || node.redo_stack.empty()) {
        return false;
    }
    PushUndoEntry(node);
    UINode::UndoEntry entry = std::move(node.redo_stack.back());
    node.redo_stack.pop_back();
    const auto edit = host.CreateTextEditFullReplacement(node.text_content, entry.text);
    node.undo_group_open = false;
    node.undo_group_timestamp = 0U;
    return edit.has_value() && ApplyTextEdit(host, handle, node, *edit, entry.sel_start, entry.sel_end);
}

void TextEditingCoordinator::ApplyImeUpdate(
    TextEditingHost& host,
    const UiEventSink& events,
    std::uint64_t handle,
    UINode& node,
    std::string_view updated_utf8,
    std::uint32_t caret_index) const {
    const std::uint32_t clamped_index =
        std::min<std::uint32_t>(caret_index, static_cast<std::uint32_t>(updated_utf8.size()));
    if (node.text_content == updated_utf8 &&
        node.selection_start == clamped_index && node.selection_end == clamped_index) {
        return;
    }
    const auto edit = host.CreateTextEditFullReplacement(node.text_content, updated_utf8);
    if (!edit.has_value()) {
        node.selection_start = clamped_index;
        node.selection_end = clamped_index;
        events.SelectionChanged(handle, clamped_index, clamped_index);
        return;
    }
    BeginUndoGroup(node, host.TextEditInteractionTime());
    (void)ApplyTextEdit(host, handle, node, *edit, clamped_index, clamped_index);
}

void TextEditingCoordinator::ApplyReplaceRange(
    TextEditingHost& host,
    std::uint64_t handle,
    UINode& node,
    std::uint32_t start_index,
    std::uint32_t end_index,
    std::string_view inserted_utf8,
    std::uint32_t caret_index) const {
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t replace_start = std::min(start_index, text_length);
    const std::uint32_t replace_end = std::min(std::max(start_index, end_index), text_length);
    const std::uint32_t next_text_length =
        text_length - (replace_end - replace_start) + static_cast<std::uint32_t>(inserted_utf8.size());
    const std::uint32_t clamped_caret = std::min<std::uint32_t>(caret_index, next_text_length);
    if (replace_start == replace_end && inserted_utf8.empty() &&
        node.selection_start == clamped_caret && node.selection_end == clamped_caret) {
        return;
    }
    const auto edit = TextEdit::Create(node.text_content, replace_start, replace_end, inserted_utf8);
    if (!edit.has_value() ||
        (edit->removed_text == edit->inserted_text &&
         node.selection_start == clamped_caret && node.selection_end == clamped_caret)) {
        return;
    }
    BeginUndoGroup(node, host.TextEditInteractionTime());
    (void)ApplyTextEdit(host, handle, node, *edit, clamped_caret, clamped_caret);
}

void TextEditingCoordinator::ApplyPaste(
    TextEditingHost& host,
    std::uint64_t handle,
    UINode& node,
    std::string_view inserted_utf8) const {
    const bool had_selection = node.selection_start != node.selection_end;
    if (!had_selection && inserted_utf8.empty()) {
        return;
    }
    const std::uint32_t replace_start = std::min(node.selection_start, node.selection_end);
    const std::uint32_t replace_end = std::max(node.selection_start, node.selection_end);
    const auto edit = TextEdit::Create(node.text_content, replace_start, replace_end, inserted_utf8);
    if (!edit.has_value()) {
        return;
    }
    BeginUndoGroup(node, host.TextEditInteractionTime());
    const std::uint32_t new_caret = replace_start + static_cast<std::uint32_t>(inserted_utf8.size());
    (void)ApplyTextEdit(host, handle, node, *edit, new_caret, new_caret);
}

bool TextEditingCoordinator::HandleTextMutationKey(
    TextEditingHost& host,
    std::uint64_t handle,
    UINode& node,
    std::string_view key,
    bool text_insertion_modifier,
    bool multiline_textbox) const {
    if (!node.is_editable || key.empty()) {
        return false;
    }
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t caret = std::min<std::uint32_t>(node.selection_end, text_length);
    const std::uint32_t selection_min = std::min(node.selection_start, node.selection_end);
    const std::uint32_t selection_max = std::max(node.selection_start, node.selection_end);
    if (key == "Backspace" || key == "Delete") {
        if (selection_min == selection_max &&
            ((key == "Backspace" && caret == 0U) || (key == "Delete" && caret >= text_length))) {
            return false;
        }
        const std::uint32_t erase_start = selection_min != selection_max
            ? selection_min
            : (key == "Backspace" ? host.NextTextEditCharacterIndex(node.text_content, caret, false) : caret);
        const std::uint32_t erase_end = selection_min != selection_max
            ? selection_max
            : (key == "Backspace" ? caret : host.NextTextEditCharacterIndex(node.text_content, caret, true));
        const auto edit = TextEdit::Create(node.text_content, erase_start, erase_end, {});
        if (!edit.has_value()) {
            return false;
        }
        BeginUndoGroup(node, host.TextEditInteractionTime());
        (void)ApplyTextEdit(host, handle, node, *edit, erase_start, erase_start);
        host.SetTextEditHorizontalSelectionActive(false);
        return true;
    }
    if (!text_insertion_modifier && key == "Enter" && multiline_textbox) {
        const auto edit = TextEdit::Create(node.text_content, selection_min, selection_max, "\n");
        if (!edit.has_value()) {
            return false;
        }
        BeginUndoGroup(node, host.TextEditInteractionTime());
        (void)ApplyTextEdit(host, handle, node, *edit, selection_min + 1U, selection_min + 1U);
        host.SetTextEditHorizontalSelectionActive(false);
        return true;
    }
    const bool named_non_text_key =
        key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" || key == "ArrowDown" ||
        key == "Home" || key == "End" || key == "PageUp" || key == "PageDown" ||
        key == "Tab" || key == "Enter" || key == "Escape" ||
        key == "Shift" || key == "Control" || key == "Alt" || key == "Meta" ||
        key == "CapsLock" || key == "NumLock" || key == "ScrollLock" ||
        key == "Fn" || key == "Dead" || key == "Compose" || key == "Unidentified" ||
        key == "ContextMenu" || key == "Insert" || key == "PrintScreen" || key == "Pause" ||
        key == "Up" || key == "Down" || key == "Left" || key == "Right" ||
        IsFunctionKeyName(key);
    if (text_insertion_modifier || named_non_text_key) {
        return false;
    }
    const auto edit = TextEdit::Create(node.text_content, selection_min, selection_max, key);
    if (!edit.has_value()) {
        return false;
    }
    BeginUndoGroup(node, host.TextEditInteractionTime());
    const std::uint32_t new_caret = selection_min + static_cast<std::uint32_t>(key.size());
    (void)ApplyTextEdit(host, handle, node, *edit, new_caret, new_caret);
    host.SetTextEditHorizontalSelectionActive(false);
    return true;
}

bool TextEditingCoordinator::HandleTextNavigationKey(
    TextEditingHost& host,
    const UiEventSink& events,
    std::uint64_t focused_handle,
    UINode& node,
    std::string_view key,
    bool shift,
    bool word_modifier,
    bool line_boundary_modifier,
    bool document_boundary_modifier) const {
    const bool horizontal_selection_key = key == "ArrowLeft" || key == "ArrowRight";
    const bool vertical_navigation_key = key == "ArrowUp" || key == "ArrowDown";
    const bool page_navigation_key = key == "PageUp" || key == "PageDown";
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t caret = std::min<std::uint32_t>(node.selection_end, text_length);
    if (shift && node.selection_start == node.selection_end && horizontal_selection_key &&
        !host.TextEditHorizontalSelectionActive() && !node.is_editable && !IsEditorTextNode(node)) {
        return false;
    }
    if (shift && (vertical_navigation_key || page_navigation_key) &&
        !node.is_editable && !IsEditorTextNode(node)) {
        return false;
    }
    std::optional<std::uint32_t> next_index{};
    if (!shift && node.selection_start != node.selection_end) {
        const std::uint32_t selection_min = std::min(node.selection_start, node.selection_end);
        const std::uint32_t selection_max = std::max(node.selection_start, node.selection_end);
        if ((key == "ArrowLeft" || key == "ArrowRight") &&
            !word_modifier && !line_boundary_modifier && !document_boundary_modifier) {
            next_index = key == "ArrowLeft" ? selection_min : selection_max;
        } else if (key == "ArrowLeft") {
            next_index = selection_min;
        } else if (key == "ArrowRight") {
            next_index = selection_max;
        }
        if (next_index.has_value() && word_modifier && (key == "ArrowLeft" || key == "ArrowRight")) {
            next_index = key == "ArrowLeft" ? selection_min : selection_max;
        }
    }
    if (!next_index.has_value()) {
        if (key == "ArrowLeft") {
            if (!shift && !line_boundary_modifier && !word_modifier && !document_boundary_modifier &&
                node.selection_start == node.selection_end && !node.caret_trailing_edge &&
                IsSoftWrappedLineBoundary(node, caret)) {
                next_index = caret;
                node.caret_trailing_edge = true;
            } else {
                next_index = line_boundary_modifier
                    ? host.TextEditLineBegin(node, caret)
                    : (word_modifier ? host.NextTextEditWordIndex(node, caret, false)
                                     : host.NextTextEditCharacterIndex(node.text_content, caret, false));
            }
        } else if (key == "ArrowRight") {
            if (!shift && !line_boundary_modifier && !word_modifier && !document_boundary_modifier &&
                node.selection_start == node.selection_end && node.caret_trailing_edge &&
                IsSoftWrappedLineBoundary(node, caret)) {
                next_index = caret;
                node.caret_trailing_edge = false;
            } else {
                next_index = line_boundary_modifier
                    ? host.TextEditLineEnd(node, caret)
                    : (word_modifier ? host.NextTextEditWordIndex(node, caret, true)
                                     : host.NextTextEditCharacterIndex(node.text_content, caret, true));
            }
        } else if (key == "ArrowUp") {
            next_index = host.TextEditVerticalMove(node, caret, false);
        } else if (key == "ArrowDown") {
            next_index = host.TextEditVerticalMove(node, caret, true);
        } else if (key == "PageUp") {
            next_index = host.TextEditPageMove(node, caret, false);
        } else if (key == "PageDown") {
            next_index = host.TextEditPageMove(node, caret, true);
        } else if (key == "Home") {
            next_index = document_boundary_modifier ? 0U : host.TextEditLineBegin(node, caret);
        } else if (key == "End") {
            next_index = document_boundary_modifier ? text_length : host.TextEditLineEnd(node, caret);
        }
    }
    if (!next_index.has_value()) {
        return false;
    }
    if (shift && (vertical_navigation_key || page_navigation_key)) {
        const std::size_t line_count = VisibleLineCount(node);
        if (line_count > 0U) {
            const auto [ignored_x, line_index] = host.TextEditLocalPosition(node, caret);
            const auto [ignored_next_x, next_line_index] = host.TextEditLocalPosition(node, *next_index);
            (void)ignored_x;
            (void)ignored_next_x;
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
        if (!host.HasTextEditSelectionAnchor(focused_handle)) {
            host.SetTextEditingSelectionAnchor(focused_handle, node.selection_start);
        } else if (node.selection_start == node.selection_end) {
            host.SetTextEditingSelectionAnchor(focused_handle, node.selection_start);
        }
        node.selection_start = host.TextEditSelectionAnchorIndex();
        node.selection_end = clamped_next;
    } else {
        if (*next_index != caret) {
            node.caret_trailing_edge = false;
        }
        node.selection_start = clamped_next;
        node.selection_end = clamped_next;
        host.SetTextEditingSelectionAnchor(focused_handle, clamped_next);
    }
    host.SetTextEditHorizontalSelectionActive(shift && horizontal_selection_key);
    node.last_interaction_time = host.TextEditInteractionTime();
    host.MarkTextEditSelectionVisualsDirty(node);
    host.EnsureTextEditCaretVisible(focused_handle, node);
    host.SetTextEditPendingCaretVisibility(focused_handle);
    events.SelectionChanged(focused_handle, node.selection_start, node.selection_end);
    return true;
}

bool TextEditingCoordinator::CutSelection(TextEditingHost& host, std::uint64_t handle, UINode& node) const {
    if (!node.is_editable || node.is_obscured || node.selection_start == node.selection_end) {
        return false;
    }
    const std::uint32_t start = std::min(node.selection_start, node.selection_end);
    const std::uint32_t end = std::max(node.selection_start, node.selection_end);
    const auto edit = TextEdit::Create(node.text_content, start, end, {});
    if (!edit.has_value()) {
        return false;
    }
    host.CopyTextEditSelection(node);
    BeginUndoGroup(node, host.TextEditInteractionTime());
    return ApplyTextEdit(host, handle, node, *edit, start, start);
}

bool TextEditingCoordinator::RequestPaste(TextEditingHost& host, std::uint64_t handle, const UINode& node) const {
    if (handle == UI_INVALID_HANDLE || !node.is_editable) {
        return false;
    }
    host.RequestTextEditClipboardRead(handle);
    return true;
}

} // namespace effindom::v2::ui
