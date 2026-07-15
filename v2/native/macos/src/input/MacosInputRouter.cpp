#include "MacosInputRouter.h"

#include "Engine.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"

extern "C" bool __fui_on_key_event(
    std::uint32_t event_type,
    const std::uint8_t* key,
    std::uint32_t len,
    std::uint32_t modifiers);

namespace effindom::v2::native {
namespace {

std::int32_t PointerButton(std::uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_MIDDLE: return 1;
        case SDL_BUTTON_RIGHT: return 2;
        case SDL_BUTTON_X1: return 3;
        case SDL_BUTTON_X2: return 4;
        default: return -1;
    }
}

std::uint32_t PointerButtons(SDL_MouseButtonFlags buttons) {
    std::uint32_t result = 0U;
    if ((buttons & SDL_BUTTON_LMASK) != 0U) result |= 1U;
    if ((buttons & SDL_BUTTON_RMASK) != 0U) result |= 2U;
    if ((buttons & SDL_BUTTON_MMASK) != 0U) result |= 4U;
    if ((buttons & SDL_BUTTON_X1MASK) != 0U) result |= 8U;
    if ((buttons & SDL_BUTTON_X2MASK) != 0U) result |= 16U;
    return result;
}

std::uint32_t Modifiers(SDL_Keymod modifiers) {
    std::uint32_t result = 0U;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) result |= UI_KEY_MOD_SHIFT;
    if ((modifiers & SDL_KMOD_CTRL) != 0) result |= UI_KEY_MOD_CTRL;
    if ((modifiers & SDL_KMOD_ALT) != 0) result |= UI_KEY_MOD_ALT;
    if ((modifiers & SDL_KMOD_GUI) != 0) result |= UI_KEY_MOD_META;
    return result;
}

} // namespace

MacosInputRouter::MacosInputRouter(Engine& engine) : engine_(engine) {}

bool MacosInputRouter::HandleEvent(const SDL_Event& event, double timestamp_ms) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            DispatchPointer(
                event.button.x,
                event.button.y,
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                PointerButton(event.button.button),
                PointerButtons(SDL_GetMouseState(nullptr, nullptr)),
                event.button.clicks,
                timestamp_ms);
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            DispatchPointerMove(event.motion.x, event.motion.y, Modifiers(SDL_GetModState()), timestamp_ms);
            return true;
        case SDL_EVENT_MOUSE_WHEEL: {
            const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
            DispatchWheel(
                detail::WheelDeltaToLogicalPixels(event.wheel.x * direction),
                detail::WheelDeltaToLogicalPixels(event.wheel.y * direction),
                timestamp_ms);
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            DispatchKey(
                SDL_GetKeyName(event.key.key),
                event.type == SDL_EVENT_KEY_DOWN,
                Modifiers(event.key.mod),
                timestamp_ms);
            return true;
        default:
            return false;
    }
}

void MacosInputRouter::DispatchPointer(
    float x,
    float y,
    bool down,
    std::int32_t button,
    std::uint32_t buttons,
    std::int32_t click_count,
    double timestamp_ms) {
    if (buttons == 0xFFFFFFFFU) buttons = down ? 1U : 0U;
    const std::uint64_t hit = ResolvePointerTarget(x, y);
    pointer_metadata_ = NativePointerMetadata{
        down ? UI_EVENT_POINTER_DOWN : UI_EVENT_POINTER_UP,
        hit, x, y, Modifiers(SDL_GetModState()), 1, UI_POINTER_TYPE_MOUSE,
        button, buttons, down ? 0.5f : 0.0f, 1.0f, 1.0f, click_count,
    };
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    ui_on_pointer_event(
        static_cast<UiEvent>(pointer_metadata_.event_type), hit, x, y, 1,
        UI_POINTER_TYPE_MOUSE, pointer_metadata_.button, pointer_metadata_.buttons,
        pointer_metadata_.pressure, 1.0f, 1.0f, click_count, pointer_metadata_.modifiers);
    commit_requested_ = true;
}

void MacosInputRouter::DispatchPointerMove(
    float x,
    float y,
    std::uint32_t modifiers,
    double timestamp_ms) {
    const std::uint64_t hit = ResolvePointerTarget(x, y);
    const std::uint32_t buttons = PointerButtons(SDL_GetMouseState(nullptr, nullptr));
    pointer_metadata_ = NativePointerMetadata{
        UI_EVENT_POINTER_MOVE, hit, x, y, modifiers, 1, UI_POINTER_TYPE_MOUSE,
        -1, buttons, 0.0f, 1.0f, 1.0f, 0,
    };
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    ui_on_pointer_event(
        UI_EVENT_POINTER_MOVE, hit, x, y, 1, UI_POINTER_TYPE_MOUSE, -1, buttons,
        0.0f, 1.0f, 1.0f, 0, modifiers);
    commit_requested_ = true;
}

void MacosInputRouter::DispatchWheel(float delta_x, float delta_y, double timestamp_ms) {
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    ui_on_wheel_event(delta_x, delta_y);
    commit_requested_ = true;
}

void MacosInputRouter::DispatchPreciseWheel(
    float delta_x,
    float delta_y,
    bool begins_gesture,
    bool ends_gesture,
    double timestamp_ms) {
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    ui::GetRuntime().HandlePreciseWheelEvent(delta_x, delta_y, begins_gesture, ends_gesture);
    commit_requested_ = true;
}

void MacosInputRouter::DispatchKey(
    const std::string& key,
    bool down,
    std::uint32_t modifiers,
    double timestamp_ms) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(key.data());
    const std::uint32_t type = down ? UI_KEY_EVENT_DOWN : UI_KEY_EVENT_UP;
    ui_set_interaction_time(static_cast<std::uint64_t>(timestamp_ms));
    if (!__fui_on_key_event(type, bytes, static_cast<std::uint32_t>(key.size()), modifiers)) {
        ui_on_key_event(
            static_cast<UiKeyEventType>(type),
            bytes,
            static_cast<std::uint32_t>(key.size()),
            modifiers);
    }
    commit_requested_ = true;
}

void MacosInputRouter::Capture(std::uint64_t handle) { captured_handle_ = handle; }
void MacosInputRouter::ReleaseCapture() { captured_handle_ = 0U; }

bool MacosInputRouter::ConsumeCommitRequest() {
    const bool requested = commit_requested_;
    commit_requested_ = false;
    return requested;
}

const NativePointerMetadata& MacosInputRouter::PointerMetadata() const { return pointer_metadata_; }

std::uint64_t MacosInputRouter::ResolvePointerTarget(float x, float y) const {
    return captured_handle_ != 0U ? captured_handle_ : engine_.HitTest(x, y);
}

} // namespace effindom::v2::native
