#include "NativeInputRouter.h"

#include "Engine.h"
#include "NativeFuiBridge.h"
#include "NativePageZoomController.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

#include <algorithm>

namespace effindom::v2::native {
namespace {

bool IsEditorCommand(const std::string& key, std::uint32_t modifiers) {
    const bool named_command =
        key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" || key == "ArrowDown" ||
        key == "Home" || key == "End" || key == "PageUp" || key == "PageDown" ||
        key == "Backspace" || key == "Delete" || key == "Insert" || key == "Enter";
    if (named_command) return true;

    const bool primary_modifier =
        (modifiers & (UI_KEY_MOD_CTRL | UI_KEY_MOD_META)) != 0U;
    if (!primary_modifier || key.size() != 1U) return false;
    const char command = static_cast<char>(key.front() | 0x20);
    return command == 'a' || command == 'c' || command == 'v' || command == 'x' ||
        command == 'y' || command == 'z';
}

} // namespace

NativeInputRouter::NativeInputRouter(Engine& engine, NativeInputRouterOptions options,
    const NativePageZoomController* page_zoom)
    : engine_(engine), page_zoom_(page_zoom), options_(options), context_menu_coordinator_(*this) {}

bool NativeInputRouter::DispatchPointer(const NativePointerInput& source) {
    NativePointerInput input = source;
    const NativePointerMoveInput scene = ProjectToScene(source.x, source.y);
    input.x = scene.x;
    input.y = scene.y;
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
        input.button, input.buttons, input.down ? 0.5f : 0.0f, true, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
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
    const NativePointerMoveInput scene = ProjectToScene(input.x, input.y);
    const std::uint64_t hit = ResolvePointerTarget(scene.x, scene.y);
    pointer_metadata_ = NativePointerMetadata{
        UI_EVENT_POINTER_MOVE, hit, scene.x, scene.y, input.modifiers, 1,
        UI_POINTER_TYPE_MOUSE, -1, input.buttons, 0.0f, true, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0,
    };
    ui_set_interaction_time(static_cast<std::uint64_t>(input.timestamp_ms));
    ui_on_pointer_event(UI_EVENT_POINTER_MOVE, hit, scene.x, scene.y, 1,
        UI_POINTER_TYPE_MOUSE, -1, input.buttons, 0.0f, 1.0f, 1.0f, 0,
        input.modifiers);
    commit_requested_ = true;
}

bool NativeInputRouter::DispatchPointerContact(
    std::uint32_t event_type, const NativePointerContactInput& input) {
    const NativePointerMoveInput scene = ProjectToScene(input.x, input.y);
    const std::uint64_t hit = ResolvePointerTarget(scene.x, scene.y);
    const float scene_width = ProjectLengthToScene(input.width);
    const float scene_height = ProjectLengthToScene(input.height);
    const std::int32_t click_count = event_type == UI_EVENT_POINTER_DOWN ? 1 : 0;
    pointer_metadata_ = NativePointerMetadata{
        event_type, hit, scene.x, scene.y, input.modifiers, input.pointer_id,
        input.pointer_type, input.button, input.buttons, input.pressure,
        input.primary, input.tangential_pressure, scene_width, scene_height,
        input.tilt_x, input.tilt_y, input.twist, click_count,
    };
    ui_set_interaction_time(static_cast<std::uint64_t>(input.timestamp_ms));
    const bool handled = ui_on_pointer_event(
        static_cast<UiEvent>(event_type), hit, scene.x, scene.y, input.pointer_id,
        static_cast<UiPointerType>(input.pointer_type), input.button, input.buttons,
        input.pressure, scene_width, scene_height, click_count, input.modifiers);
    commit_requested_ = true;
    return handled;
}

bool NativeInputRouter::DispatchWheel(const NativeWheelInput& input) {
    const NativePointerMoveInput scene = ProjectToScene(input.x, input.y);
    const std::uint64_t handle = input.handle != 0U
        ? input.handle
        : ResolvePointerTarget(scene.x, scene.y);
    ui_set_interaction_time(static_cast<std::uint64_t>(input.timestamp_ms));
    const bool handled = __fui_on_wheel_event(
        handle, scene.x, scene.y, input.delta_x, input.delta_y,
        static_cast<std::uint32_t>(input.delta_mode), input.modifiers);
    if (!handled) {
        ui::GetRuntime().HandleWheelEventAt(
            handle, scene.x, scene.y, input.delta_x, input.delta_y);
    }
    commit_requested_ = true;
    return handled;
}

bool NativeInputRouter::DispatchPreciseWheel(const NativeWheelInput& input) {
    const bool has_delta = input.delta_x != 0.0f || input.delta_y != 0.0f;
    const NativePointerMoveInput scene = ProjectToScene(input.x, input.y);
    const std::uint64_t handle = input.handle != 0U
        ? input.handle
        : ResolvePointerTarget(scene.x, scene.y);
    ui_set_interaction_time(static_cast<std::uint64_t>(input.timestamp_ms));
    const bool handled = has_delta && __fui_on_wheel_event(
        handle, scene.x, scene.y, input.delta_x, input.delta_y,
        static_cast<std::uint32_t>(input.delta_mode), input.modifiers);
    if (has_delta && !handled) {
        const bool begins_runtime_gesture = input.begins_gesture || !precise_wheel_runtime_active_;
        ui::GetRuntime().HandlePreciseWheelEventAt(
            handle, scene.x, scene.y, input.delta_x, input.delta_y,
            begins_runtime_gesture, input.ends_gesture);
        precise_wheel_runtime_active_ = !input.ends_gesture;
    } else if (input.ends_gesture && precise_wheel_runtime_active_) {
        ui::GetRuntime().HandlePreciseWheelEvent(0.0f, 0.0f, false, true);
        precise_wheel_runtime_active_ = false;
    }
    commit_requested_ = true;
    return handled;
}

void NativeInputRouter::DispatchKey(const std::string& key, bool down,
    std::uint32_t modifiers, double timestamp_ms, bool text_input_active) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(key.data());
    const std::uint32_t type = down ? UI_KEY_EVENT_DOWN : UI_KEY_EVENT_UP;
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    const bool runtime_first = IsEditorCommand(key, modifiers);
    const bool defer_printable_text = text_input_active && key.size() == 1U &&
        (modifiers & (UI_KEY_MOD_CTRL | UI_KEY_MOD_ALT | UI_KEY_MOD_META)) == 0U;
    bool handled = runtime_first && ui_on_key_event(
        static_cast<UiKeyEventType>(type), bytes,
        static_cast<std::uint32_t>(key.size()), modifiers);
    if (!handled && !defer_printable_text) {
        handled = __fui_on_key_event(
            type, bytes, static_cast<std::uint32_t>(key.size()), modifiers);
    }
    if (!handled && !runtime_first && !defer_printable_text) {
        handled = ui_on_key_event(static_cast<UiKeyEventType>(type), bytes,
            static_cast<std::uint32_t>(key.size()), modifiers);
    }
    if (!handled && down && key == "F10" &&
        (modifiers & UI_KEY_MOD_SHIFT) != 0U) {
        (void)ShowContextMenuForFocusedControl();
    }
    commit_requested_ = true;
}

bool NativeInputRouter::BeginTextComposition(const NativeTextCompositionInput& input) {
    const std::uint64_t handle = ui::GetRuntime().FocusedHandle();
    std::uint32_t selection_start = 0U;
    std::uint32_t selection_end = 0U;
    if (handle == 0U || !ui::GetRuntime().GetTextSelectionRange(
            handle, selection_start, selection_end)) {
        return false;
    }
    const auto document = ui::GetRuntime().GetEditableTextDocument(handle);
    if (!document.has_value()) return false;
    const std::uint32_t text_length = static_cast<std::uint32_t>(document->size());
    text_composition_.active = true;
    text_composition_.handle = handle;
    text_composition_.original_text.assign(document->data(), document->size());
    text_composition_.replacement_start = input.replacement_start == kNativeTextRangeUnspecified
        ? std::min(selection_start, selection_end)
        : std::min(input.replacement_start, text_length);
    text_composition_.replacement_end = input.replacement_end == kNativeTextRangeUnspecified
        ? std::max(selection_start, selection_end)
        : std::min(std::max(input.replacement_start, input.replacement_end), text_length);
    return true;
}

void NativeInputRouter::ClearTextComposition() { text_composition_ = {}; }

bool NativeInputRouter::DispatchTextComposition(const NativeTextCompositionInput& input) {
    ui_set_interaction_time(static_cast<std::uint64_t>(input.timestamp_ms));
    if (input.phase == NativeTextCompositionPhase::Start) {
        ClearTextComposition();
        return BeginTextComposition(input);
    }
    if (!text_composition_.active && !BeginTextComposition(input)) return false;

    const std::uint64_t handle = text_composition_.handle;
    const std::uint32_t start = text_composition_.replacement_start;
    const std::uint32_t end = text_composition_.replacement_end;
    if (input.phase == NativeTextCompositionPhase::Cancel) {
        const auto* original = reinterpret_cast<const std::uint8_t*>(
            text_composition_.original_text.data());
        ui_on_ime_update(handle, original,
            static_cast<std::uint32_t>(text_composition_.original_text.size()), end);
        ui_set_text_selection_range(handle, start, end);
        ClearTextComposition();
        commit_requested_ = true;
        return true;
    }

    std::string updated = text_composition_.original_text;
    updated.replace(start, end - start, input.text);
    const std::uint32_t relative_caret = input.selection_end == kNativeTextRangeUnspecified
        ? static_cast<std::uint32_t>(input.text.size())
        : std::min(input.selection_end, static_cast<std::uint32_t>(input.text.size()));
    const std::uint32_t caret = start + relative_caret;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(updated.data());
    ui_on_ime_update(handle, bytes, static_cast<std::uint32_t>(updated.size()), caret);
    if (input.phase == NativeTextCompositionPhase::Commit) ClearTextComposition();
    commit_requested_ = true;
    return true;
}

void NativeInputRouter::HandleWindowFocusLost(double timestamp_ms) {
    if (text_composition_.active) {
        DispatchTextComposition(NativeTextCompositionInput{
            NativeTextCompositionPhase::Cancel,
            "",
            kNativeTextRangeUnspecified,
            kNativeTextRangeUnspecified,
            kNativeTextRangeUnspecified,
            kNativeTextRangeUnspecified,
            timestamp_ms,
        });
    }
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

std::uint64_t NativeInputRouter::HitTestAt(float x, float y) const {
    const NativePointerMoveInput scene = ProjectToScene(x, y);
    return engine_.HitTest(scene.x, scene.y);
}

NativePointerMoveInput NativeInputRouter::ProjectToScene(float x, float y) const {
    if (page_zoom_ == nullptr) return {x, y};
    const NativeScenePoint scene = page_zoom_->ScreenToScene(x, y);
    return {scene.x, scene.y};
}

float NativeInputRouter::ProjectLengthToScene(float length) const {
    return page_zoom_ == nullptr ? length : page_zoom_->ScreenLengthToScene(length);
}

std::optional<NativeTextInputTarget> NativeInputRouter::FocusedTextInputTarget() const {
    const std::uint64_t handle = ui::GetRuntime().FocusedHandle();
    if (handle == 0U || !ui::GetRuntime().GetEditableTextDocument(handle).has_value()) {
        return std::nullopt;
    }
    std::uint32_t selection_start = 0U;
    std::uint32_t selection_end = 0U;
    const bool has_selection = ui::GetRuntime().GetTextSelectionRange(
        handle, selection_start, selection_end);
    float rect[4]{};
    if (!has_selection ||
        ui_copy_text_range_rects(handle, selection_end, selection_end, rect, 1U) != 1U) {
        if (!ui_get_text_visible_bounds(handle, &rect[0], &rect[1], &rect[2], &rect[3])) {
            rect[2] = 1.0f;
            rect[3] = 1.0f;
        }
        rect[2] = 1.0f;
    }
    NativeSceneRect screen{rect[0], rect[1], rect[2], rect[3]};
    if (page_zoom_ != nullptr) screen = page_zoom_->SceneToScreen(screen);
    return NativeTextInputTarget{
        handle, screen.x, screen.y, screen.width, screen.height,
    };
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
    const std::uint64_t hit = engine_.HitTest(x, y);
    return hit != 0U ? hit : captured_handle_;
}

} // namespace effindom::v2::native
