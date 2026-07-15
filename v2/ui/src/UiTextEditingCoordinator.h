#pragma once

#include "UiTypes.h"
#include "UiTextEdit.h"
#include "UiEventSink.h"

#include <cstdint>
#include <optional>

namespace effindom::v2::ui {

class TextEditingHost {
public:
    virtual ~TextEditingHost() = default;

    virtual void SetTextEditingSelectionAnchor(std::uint64_t handle, std::uint32_t index) = 0;
    virtual void BeginTextEditTransaction() = 0;
    virtual void RecordTextEditApplication(std::size_t inserted_bytes, std::size_t removed_bytes) = 0;
    virtual void RecordTextEditMaterializedPreviousText(std::size_t bytes) = 0;
    virtual bool WouldApplyTextHardClamp(const UINode& node) const = 0;
    virtual bool ApplyTextHardClamp(UINode& node) const = 0;
    virtual void NotifyTextEditApplied(std::uint64_t handle, UINode& node, const TextEdit& edit) = 0;
    virtual void NotifyTextEditClamped(std::uint64_t handle, UINode& node, const std::string& previous_text) = 0;
    virtual bool TryApplyTextEditLineStarts(UINode& node, const TextEdit& edit) const = 0;
    virtual bool TryApplyTextEditLineStarts(UINode& node, std::string_view previous_text) const = 0;
    virtual void RebuildTextEditLineStarts(UINode& node) const = 0;
    virtual std::uint64_t TextEditInteractionTime() const = 0;
    virtual bool TryApplyTextEditNonWrapCache(UINode& node, const TextEdit& edit) const = 0;
    virtual bool TryApplyTextEditWrappedCache(UINode& node, const TextEdit& edit) const = 0;
    virtual bool TryApplyTextEditNonWrapCache(UINode& node, std::string_view previous_text) const = 0;
    virtual bool TryApplyTextEditWrappedCache(UINode& node, std::string_view previous_text) const = 0;
    virtual void InvalidateTextEditLayoutCache(UINode& node) = 0;
    virtual void MarkTextEditYogaDirty(UINode& node) = 0;
    virtual void MarkTextEditLayoutDirty() = 0;
    virtual bool RegisterTextEditScrollMetrics(std::uint64_t handle) = 0;
    virtual bool IsTextEditFocused(std::uint64_t handle) const = 0;
    virtual void UpdateTextEditAncestorScrollMetrics(std::uint64_t handle) = 0;
    virtual void EnsureTextEditCaretVisible(std::uint64_t handle, UINode& node) = 0;
    virtual void SetTextEditPendingCaretVisibility(std::uint64_t handle) = 0;
    virtual std::optional<TextEdit> CreateTextEditFullReplacement(
        std::string_view old_text,
        std::string_view new_text) const = 0;
    virtual std::uint32_t NextTextEditCharacterIndex(
        std::string_view utf8_text,
        std::uint32_t index,
        bool forward) const = 0;
    virtual void SetTextEditHorizontalSelectionActive(bool active) = 0;
    virtual bool TextEditHorizontalSelectionActive() const = 0;
    virtual bool HasTextEditSelectionAnchor(std::uint64_t handle) const = 0;
    virtual std::uint32_t TextEditSelectionAnchorIndex() const = 0;
    virtual std::uint32_t NextTextEditWordIndex(
        const UINode& node,
        std::uint32_t index,
        bool forward) const = 0;
    virtual std::uint32_t TextEditLineBegin(const UINode& node, std::uint32_t index) const = 0;
    virtual std::uint32_t TextEditLineEnd(const UINode& node, std::uint32_t index) const = 0;
    virtual std::uint32_t TextEditVerticalMove(const UINode& node, std::uint32_t index, bool down) const = 0;
    virtual std::uint32_t TextEditPageMove(const UINode& node, std::uint32_t index, bool down) const = 0;
    virtual std::pair<float, int> TextEditLocalPosition(const UINode& node, std::uint32_t index) const = 0;
    virtual void MarkTextEditSelectionVisualsDirty(UINode& node) = 0;
    virtual void CopyTextEditSelection(const UINode& node) const = 0;
    virtual void RequestTextEditClipboardRead(std::uint64_t handle) const = 0;
};

// Owns pure editable-node history transitions. Geometry, layout invalidation,
// caret reveal, and host notifications remain outside this coordinator.
class TextEditingCoordinator {
public:
    void ClearUndoHistory(UINode& node) const;
    void BeginUndoGroup(UINode& node, std::uint64_t interaction_time_ms) const;
    void PushUndoEntry(UINode& node) const;
    void PushRedoEntry(UINode& node) const;
    UINode::UndoEntry CaptureUndoEntry(const UINode& node) const;
    bool ApplyTextEdit(
        TextEditingHost& host,
        std::uint64_t handle,
        UINode& node,
        TextEdit edit,
        std::uint32_t selection_start,
        std::uint32_t selection_end) const;
    void NotifyTextStateChanged(
        TextEditingHost& host,
        const UiEventSink& events,
        std::uint64_t handle,
        UINode& node,
        const std::string* previous_text,
        const TextEdit* edit) const;
    bool Undo(TextEditingHost& host, std::uint64_t handle, UINode& node) const;
    bool Redo(TextEditingHost& host, std::uint64_t handle, UINode& node) const;
    void ApplyImeUpdate(
        TextEditingHost& host,
        const UiEventSink& events,
        std::uint64_t handle,
        UINode& node,
        std::string_view updated_utf8,
        std::uint32_t caret_index) const;
    void ApplyReplaceRange(
        TextEditingHost& host,
        std::uint64_t handle,
        UINode& node,
        std::uint32_t start_index,
        std::uint32_t end_index,
        std::string_view inserted_utf8,
        std::uint32_t caret_index) const;
    void ApplyPaste(
        TextEditingHost& host,
        std::uint64_t handle,
        UINode& node,
        std::string_view inserted_utf8) const;
    bool HandleTextMutationKey(
        TextEditingHost& host,
        std::uint64_t handle,
        UINode& node,
        std::string_view key,
        bool text_insertion_modifier,
        bool multiline_textbox) const;
    bool HandleTextNavigationKey(
        TextEditingHost& host,
        const UiEventSink& events,
        std::uint64_t focused_handle,
        UINode& node,
        std::string_view key,
        bool shift,
        bool word_modifier,
        bool line_boundary_modifier,
        bool document_boundary_modifier) const;
    bool CutSelection(TextEditingHost& host, std::uint64_t handle, UINode& node) const;
    bool RequestPaste(TextEditingHost& host, std::uint64_t handle, const UINode& node) const;
};

} // namespace effindom::v2::ui
