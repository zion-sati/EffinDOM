#include "NativeInputRouter.h"

#include "Engine.h"
#include "NativeFuiBridge.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

namespace effindom::v2::native {
namespace {

bool IsEditorCommand(const std::string& key, std::uint32_t modifiers) {
    const bool named_command =
        key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" || key == "ArrowDown" ||
        key == "Home" || key == "End" || key == "PageUp" || key == "PageDown" ||
        key == "Backspace" || key == "Delete" || key == "Insert" || key == "Enter" || key == "Tab";
    if (named_command) return true;

    const bool primary_modifier =
        (modifiers & (UI_KEY_MOD_CTRL | UI_KEY_MOD_META)) != 0U;
    if (!primary_modifier || key.size() != 1U) return false;
    const char command = static_cast<char>(key.front() | 0x20);
    return command == 'a' || command == 'c' || command == 'v' || command == 'x' ||
        command == 'y' || command == 'z';
}

} // namespace

NativeInputRouter::NativeInputRouter(Engine& engine, NativeInputRouterOptions options)
    : engine_(engine), options_(options), context_menu_coordinator_(*this) {}

bool NativeInputRouter::DispatchPointer(const NativePointerInput& source) {
    NativePointerInput input = source;
    if (options_.control_click_as_secondary && input.button == 0 &&
        (input.modifiers & UI_KEY_MOD_CTRL) != 0U) {
        input.button = 2;
        if (input.down) input.buttons = input.buttons == 0xFFFFFFFFU
            ? 2U
            : (input.buttons & ~1U) | 2U;
    }
    if (input.buttons == 0xFFFFFFFFU) {
        input.buttons = input.down
            ? (options_.default_buttons_follow_button && input.button == 2 ? 2U : 1U)
            : 0U;
    }
    if (input.button == 2) {
        NativeSecondaryPointerEvent event{};
        event.phase = input.down ? NativePointerPhase::Down : NativePointerPhase::Up;
        event.x = input.x;
        event.y = input.y;
        event.modifiers = input.modifiers;
        event.buttons = input.buttons;
        event.pointer_type = UI_POINTER_TYPE_MOUSE;
        event.pressure = input.down ? 0.5f : 0.0f;
        event.click_count = input.click_count;
        event.timestamp_ms = input.timestamp_ms;
        const auto result = context_menu_coordinator_.Dispatch(event);
        commit_requested_ = true;
        return result.raw_event_handled || result.fallback_shown;
    }
    return DispatchRawPointer(input);
}

bool NativeInputRouter::DispatchRawPointer(const NativePointerInput& input) {
    const std::uint64_t hit = ResolvePointerTarget(input.x, input.y);
    pointer_metadata_ = NativePointerMetadata{
        static_cast<std::uint32_t>(input.down ? UI_EVENT_POINTER_DOWN : UI_EVENT_POINTER_UP),
        hit, input.x, input.y, input.modifiers, 1, UI_POINTER_TYPE_MOUSE,
        input.button, input.buttons, input.down ? 0.5f : 0.0f, 1.0f, 1.0f,
        input.click_count,
    };
    ui_set_interaction_time(static_cast<std::uint64_t>(input.timestamp_ms));
    const bool handled = ui_on_pointer_event(
        static_cast<UiEvent>(pointer_metadata_.event_type), hit, input.x, input.y, 1,
        UI_POINTER_TYPE_MOUSE, input.button, input.buttons,
        pointer_metadata_.pressure, 1.0f, 1.0f, input.click_count, input.modifiers);
    commit_requested_ = true;
    return handled;
}

void NativeInputRouter::DispatchPointerMove(const NativePointerMoveInput& input) {
    const std::uint64_t hit = ResolvePointerTarget(input.x, input.y);
    pointer_metadata_ = NativePointerMetadata{
        UI_EVENT_POINTER_MOVE, hit, input.x, input.y, input.modifiers, 1,
        UI_POINTER_TYPE_MOUSE, -1, input.buttons, 0.0f, 1.0f, 1.0f, 0,
    };
    ui_set_interaction_time(static_cast<std::uint64_t>(input.timestamp_ms));
    ui_on_pointer_event(UI_EVENT_POINTER_MOVE, hit, input.x, input.y, 1,
        UI_POINTER_TYPE_MOUSE, -1, input.buttons, 0.0f, 1.0f, 1.0f, 0,
        input.modifiers);
    commit_requested_ = true;
}

void NativeInputRouter::DispatchWheel(float delta_x, float delta_y, double timestamp_ms) {
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    ui_on_wheel_event(delta_x, delta_y);
    commit_requested_ = true;
}

void NativeInputRouter::DispatchPreciseWheel(float delta_x, float delta_y,
    bool begins_gesture, bool ends_gesture, double timestamp_ms) {
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    ui::GetRuntime().HandlePreciseWheelEvent(
        delta_x, delta_y, begins_gesture, ends_gesture);
    commit_requested_ = true;
}

void NativeInputRouter::DispatchKey(const std::string& key, bool down,
    std::uint32_t modifiers, double timestamp_ms) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(key.data());
    const std::uint32_t type = down ? UI_KEY_EVENT_DOWN : UI_KEY_EVENT_UP;
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    const bool runtime_first = IsEditorCommand(key, modifiers);
    bool handled = runtime_first && ui_on_key_event(
        static_cast<UiKeyEventType>(type), bytes,
        static_cast<std::uint32_t>(key.size()), modifiers);
    if (!handled) {
        handled = __fui_on_key_event(
            type, bytes, static_cast<std::uint32_t>(key.size()), modifiers);
    }
    if (!handled && !runtime_first) {
        handled = ui_on_key_event(static_cast<UiKeyEventType>(type), bytes,
            static_cast<std::uint32_t>(key.size()), modifiers);
    }
    if (!handled && down && key == "F10" &&
        (modifiers & UI_KEY_MOD_SHIFT) != 0U) {
        (void)ShowContextMenuForFocusedControl();
    }
    commit_requested_ = true;
}

void NativeInputRouter::HandleWindowFocusLost(double timestamp_ms) {
    context_menu_coordinator_.ResetGesture();
    HideActiveContextMenu();
    if (options_.cancel_pointer_on_focus_lost) CancelPointer(timestamp_ms);
    commit_requested_ = true;
}

void NativeInputRouter::Capture(std::uint64_t handle) { captured_handle_ = handle; }
void NativeInputRouter::ReleaseCapture() { captured_handle_ = 0U; }

void NativeInputRouter::CancelPointer(double timestamp_ms) {
    if (captured_handle_ == 0U) return;
    pointer_metadata_.event_type = UI_EVENT_POINTER_CANCEL;
    pointer_metadata_.handle = captured_handle_;
    pointer_metadata_.button = -1;
    pointer_metadata_.buttons = 0U;
    pointer_metadata_.pressure = 0.0f;
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    ui_on_pointer_event(UI_EVENT_POINTER_CANCEL, captured_handle_, pointer_metadata_.x,
        pointer_metadata_.y, pointer_metadata_.pointer_id,
        static_cast<UiPointerType>(pointer_metadata_.pointer_type), -1, 0U, 0.0f,
        pointer_metadata_.width, pointer_metadata_.height, 0,
        pointer_metadata_.modifiers);
    captured_handle_ = 0U;
    commit_requested_ = true;
}

bool NativeInputRouter::ConsumeCommitRequest() {
    const bool requested = commit_requested_;
    commit_requested_ = false;
    return requested;
}

const NativePointerMetadata& NativeInputRouter::PointerMetadata() const {
    return pointer_metadata_;
}

bool NativeInputRouter::DispatchRawSecondaryPointer(
    const NativeSecondaryPointerEvent& event) {
    return DispatchRawPointer(NativePointerInput{
        event.x, event.y, event.phase == NativePointerPhase::Down, 2,
        event.buttons, event.click_count, event.modifiers, event.timestamp_ms,
    });
}

void NativeInputRouter::HideActiveContextMenu() { __fui_hide_active_context_menu(); }
void NativeInputRouter::FlushRetainedChanges() { __flushRenders(); }
std::uint64_t NativeInputRouter::HitTest(float x, float y) const { return engine_.HitTest(x, y); }
bool NativeInputRouter::CanShowContextMenu(std::uint64_t handle) const {
    return __fui_can_show_context_menu(handle);
}
void NativeInputRouter::ShowContextMenu(std::uint64_t handle, float x, float y) {
    __fui_on_context_menu(handle, x, y);
}
void NativeInputRouter::RequestFrame() { commit_requested_ = true; }

bool NativeInputRouter::ShowContextMenuForFocusedControl() {
    const std::uint64_t handle = ui_get_focused_handle();
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (handle == 0U || !ui_get_bounds(handle, &x, &y, &width, &height)) return false;
    return context_menu_coordinator_.ShowAt(x, y + height);
}

std::uint64_t NativeInputRouter::ResolvePointerTarget(float x, float y) const {
    return captured_handle_ != 0U ? captured_handle_ : engine_.HitTest(x, y);
}

} // namespace effindom::v2::native
