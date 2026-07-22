#pragma once

#include "UiNodeStoreAccess.h"
#include "UiTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace effindom::v2::ui {

class FocusCoordinator;
class SelectionCoordinator;
class SelectionHost;
class ScrollCoordinator;
class UiEventSink;

enum class PlatformFamily : std::uint32_t {
    Unknown = 0U,
    Apple = 1U,
    Windows = 2U,
    Linux = 3U,
};

struct InputState {
    std::uint64_t last_hovered_handle = UI_INVALID_HANDLE;
    bool coarse_pointer_mode = false;
    PlatformFamily platform_family = PlatformFamily::Unknown;
    bool primary_pointer_down = false;
    float last_pointer_logical_x = 0.0f;
    float last_pointer_logical_y = 0.0f;
    std::uint64_t interaction_time_ms = 0U;
    std::uint64_t last_click_handle = UI_INVALID_HANDLE;
    float last_click_x = 0.0f;
    float last_click_y = 0.0f;
    float selection_press_logical_x = 0.0f;
    float selection_press_logical_y = 0.0f;
    std::uint64_t touch_text_tap_handle = UI_INVALID_HANDLE;
    float touch_text_tap_logical_x = 0.0f;
    float touch_text_tap_logical_y = 0.0f;
    bool touch_text_tap_moved = false;
    bool suppress_touch_editor_focus_request = false;
    std::uint32_t click_count = 0U;
    std::uint64_t last_click_time_ms = 0U;
    std::uint64_t double_click_threshold_ms = 500U;
};

class InputRouter {
public:
    class Host {
    public:
        virtual ~Host() = default;
        virtual Rect ComputeInputVisibleBounds(const UINode& node) const = 0;
        virtual void ClearInputSelectionHighlight(std::uint64_t handle, bool notify_callback) = 0;
        virtual bool IsInputEditorTextNode(const UINode& node) const = 0;
        virtual std::uint32_t GetInputStringIndexFromPoint(
            const UINode& node,
            float local_x,
            float local_y) const = 0;
        virtual std::pair<std::uint32_t, std::uint32_t> GetInputWordBoundaries(
            const UINode& node,
            std::uint32_t index) const = 0;
        virtual std::pair<std::uint32_t, std::uint32_t> GetInputParagraphBoundaries(
            const UINode& node,
            std::uint32_t index) const = 0;
        virtual bool ShouldUseInputTrailingCaretEdge(
            const UINode& node,
            std::uint32_t index,
            float local_x,
            float local_y) const = 0;
        virtual void MarkInputTextSelectionVisualsDirty(UINode& node) = 0;
        virtual bool SetInputTextSelectionRange(
            std::uint64_t handle,
            std::uint32_t start,
            std::uint32_t end) = 0;
        virtual void EnsureInputTextCaretVisible(std::uint64_t handle, UINode& node) = 0;
        virtual void SetInputPendingCaretVisibility(std::uint64_t handle) = 0;
        virtual bool ClearInputSelection(bool notify_callback) = 0;
        virtual std::uint64_t ActiveInputFocusScope() = 0;
        virtual void SetInputFocus(
            std::uint64_t handle,
            bool ensure_visible = false,
            bool emit_selection_callback = true) = 0;
        virtual std::uint64_t FindInputSelectionAreaAncestor(std::uint64_t handle) const = 0;
        virtual std::string BuildInputCrossSelectionText() const = 0;
        virtual bool BuildInputCrossSelectionRichPayload(std::string& plain_text, std::string& rich_json) const = 0;
        virtual void EmitInputClipboardWrite(const std::string& plain_text, const std::string* rich_json = nullptr) const = 0;
        virtual bool HandleInputCrossSelectionNavigation(
            std::uint64_t area_handle,
            UINode& node,
            std::string_view key,
            std::uint32_t modifiers) = 0;
        virtual bool IsInputPrimaryShortcut(std::string_view key, std::uint32_t modifiers, char expected) const = 0;
        virtual bool IsInputUndoShortcut(std::string_view key, std::uint32_t modifiers) const = 0;
        virtual bool IsInputRedoShortcut(std::string_view key, std::uint32_t modifiers) const = 0;
        virtual bool UndoInputTextEdit(std::uint64_t handle, UINode& node) = 0;
        virtual bool RedoInputTextEdit(std::uint64_t handle, UINode& node) = 0;
        virtual bool SelectAllInputText(std::uint64_t handle) = 0;
        virtual void CopyInputText(const UINode& node) const = 0;
        virtual bool CutInputText(UINode& node) = 0;
        virtual bool PasteInputText(UINode& node) = 0;
        virtual bool HandleInputTextEditingKey(
            UINode& node,
            std::string_view key,
            std::uint32_t modifiers) = 0;
    };

    InputRouter(
        NodeReader nodes,
        NodeWriter writer,
        NodeTraversalAccess traversal,
        FocusCoordinator& focus,
        SelectionCoordinator& selection,
        ScrollCoordinator& scrolling,
        SelectionHost& selection_host,
        const UiEventSink& events,
        Host& host)
        : nodes_(nodes),
          writer_(writer),
          traversal_(traversal),
          focus_(focus),
          selection_(selection),
          scrolling_(scrolling),
          selection_host_(selection_host),
          events_(events),
          host_(host) {}

    InputState& state() { return state_; }
    const InputState& state() const { return state_; }
    void Reset();
    void SetInteractionTime(std::uint64_t interaction_time_ms) { state_.interaction_time_ms = interaction_time_ms; }
    void SetPlatformFamily(std::uint32_t platform_family);
    bool IsApplePlatformFamily() const { return state_.platform_family == PlatformFamily::Apple; }
    bool SuppressesTouchEditorFocus() const { return state_.suppress_touch_editor_focus_request; }
    bool HasPointerAutoScroll() const;

    bool HandleKeyEvent(
        std::uint32_t type_enum,
        const std::uint8_t* key_utf8,
        std::uint32_t len,
        std::uint32_t modifiers);
    bool PointInVisibleBounds(const UINode& node, float logical_x, float logical_y) const;
    bool IsAttachedToRoot(std::uint64_t handle) const;
    std::uint64_t FindDeepestNodeContainingPoint(
        std::uint64_t handle,
        float logical_x,
        float logical_y) const;
    std::uint64_t FindBestNodeContainingPoint(float logical_x, float logical_y) const;
    std::uint64_t FindDeepestScrollViewContainingPoint(float logical_x, float logical_y) const;
    std::uint64_t ResolveScrollTarget(
        std::uint64_t start_handle,
        float logical_x,
        float logical_y) const;
    std::uint64_t FindScrollableAncestorContainingPoint(
        std::uint64_t start_handle,
        float logical_x,
        float logical_y) const;
    std::uint64_t FindScrollProxyTarget(
        std::uint64_t start_handle,
        float logical_x,
        float logical_y) const;
    std::uint64_t SelectionAutoScroll(float logical_x, float logical_y, float edge_threshold);
    void ClearAutoScroll();
    void UpdateAutoScroll(std::uint64_t start_handle, float logical_x, float logical_y);
    void HandleWheel(float delta_x, float delta_y);
    void HandlePreciseWheel(float delta_x, float delta_y, bool begins_gesture, bool ends_gesture);
    void BeginTouchScroll(std::uint64_t handle, float logical_x, float logical_y, double timestamp_ms);
    void UpdateTouchScroll(float delta_x, float delta_y, double timestamp_ms);
    void EndTouchScroll(double timestamp_ms);
    void ClearMomentumScroll();
    bool ActiveTouchScrollAllowsPullToRefresh() const;
    bool WheelScrollCanConsume(float delta_x, float delta_y) const;
    bool ActiveTouchScrollCanConsume(float delta_x, float delta_y) const;
    void SetCoarsePointerMode(bool coarse_pointer_mode);
    bool HandlePointerEvent(
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
        std::uint32_t modifiers);
    void ClearHover(std::uint64_t handle);

private:
    NodeReader nodes_;
    NodeWriter writer_;
    NodeTraversalAccess traversal_;
    FocusCoordinator& focus_;
    SelectionCoordinator& selection_;
    ScrollCoordinator& scrolling_;
    SelectionHost& selection_host_;
    const UiEventSink& events_;
    Host& host_;
    InputState state_{};
};

} // namespace effindom::v2::ui
