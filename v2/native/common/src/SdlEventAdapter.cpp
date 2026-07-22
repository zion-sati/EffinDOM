#include "SdlEventAdapter.h"

#include "effindom_ui.h"
#include "SDL3/SDL.h"

#include <cmath>

namespace effindom::v2::native {

bool SdlEventAdapter::EndsInputBatch(std::uint32_t event_type) {
    switch (event_type) {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
            return true;
        default:
            return false;
    }
}

SdlEventAdapter::SdlEventAdapter(NativeInputRouter& router,
    SdlEventAdapterOptions options) : router_(router), options_(options) {}

bool SdlEventAdapter::HandleEvent(const SDL_Event& event, double timestamp_ms) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            router_.DispatchPointer(NativePointerInput{
                LogicalCoordinate(event.button.windowID, event.button.x),
                LogicalCoordinate(event.button.windowID, event.button.y),
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                PointerButton(event.button.button), CurrentButtons(), event.button.clicks,
                CurrentModifiers(), timestamp_ms,
            });
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            router_.DispatchPointerMove(NativePointerMoveInput{
                LogicalCoordinate(event.motion.windowID, event.motion.x),
                LogicalCoordinate(event.motion.windowID, event.motion.y),
                CurrentButtons(), CurrentModifiers(), timestamp_ms,
            });
            return true;
        case SDL_EVENT_MOUSE_WHEEL: {
            const auto [delta_x, delta_y] = WheelDeltas(event);
            router_.DispatchWheel(delta_x, delta_y, timestamp_ms);
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            router_.DispatchKey(KeyName(event.key.key, event.key.scancode,
                event.key.mod, options_.keypad_always_numeric),
                event.type == SDL_EVENT_KEY_DOWN, Modifiers(event.key.mod), timestamp_ms);
            return true;
        default:
            return false;
    }
}

std::uint32_t SdlEventAdapter::CurrentModifiers() const { return Modifiers(SDL_GetModState()); }
std::uint32_t SdlEventAdapter::CurrentButtons() const {
    return PointerButtons(SDL_GetMouseState(nullptr, nullptr));
}

std::int32_t SdlEventAdapter::PointerButton(std::uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_MIDDLE: return 1;
        case SDL_BUTTON_RIGHT: return 2;
        case SDL_BUTTON_X1: return 3;
        case SDL_BUTTON_X2: return 4;
        default: return -1;
    }
}

std::uint32_t SdlEventAdapter::PointerButtons(std::uint64_t buttons) {
    std::uint32_t result = 0U;
    if ((buttons & SDL_BUTTON_LMASK) != 0U) result |= 1U;
    if ((buttons & SDL_BUTTON_RMASK) != 0U) result |= 2U;
    if ((buttons & SDL_BUTTON_MMASK) != 0U) result |= 4U;
    if ((buttons & SDL_BUTTON_X1MASK) != 0U) result |= 8U;
    if ((buttons & SDL_BUTTON_X2MASK) != 0U) result |= 16U;
    return result;
}

std::uint32_t SdlEventAdapter::Modifiers(std::uint32_t modifiers) {
    std::uint32_t result = 0U;
    if ((modifiers & SDL_KMOD_SHIFT) != 0U) result |= UI_KEY_MOD_SHIFT;
    if ((modifiers & SDL_KMOD_CTRL) != 0U) result |= UI_KEY_MOD_CTRL;
    if ((modifiers & SDL_KMOD_ALT) != 0U) result |= UI_KEY_MOD_ALT;
    if ((modifiers & SDL_KMOD_GUI) != 0U) result |= UI_KEY_MOD_META;
    return result;
}

std::string SdlEventAdapter::KeyName(std::uint32_t keycode, std::uint32_t scancode,
    std::uint32_t modifiers, bool keypad_always_numeric) {
    auto key = static_cast<SDL_Keycode>(keycode);
    const bool keypad_numeric = keypad_always_numeric ||
        (modifiers & SDL_KMOD_NUM) != 0U;
    switch (static_cast<SDL_Scancode>(scancode)) {
        case SDL_SCANCODE_KP_0: return keypad_numeric ? "0" : "Insert";
        case SDL_SCANCODE_KP_1: return keypad_numeric ? "1" : "End";
        case SDL_SCANCODE_KP_2: return keypad_numeric ? "2" : "ArrowDown";
        case SDL_SCANCODE_KP_3: return keypad_numeric ? "3" : "PageDown";
        case SDL_SCANCODE_KP_4: return keypad_numeric ? "4" : "ArrowLeft";
        case SDL_SCANCODE_KP_5: return keypad_numeric ? "5" : "Clear";
        case SDL_SCANCODE_KP_6: return keypad_numeric ? "6" : "ArrowRight";
        case SDL_SCANCODE_KP_7: return keypad_numeric ? "7" : "Home";
        case SDL_SCANCODE_KP_8: return keypad_numeric ? "8" : "ArrowUp";
        case SDL_SCANCODE_KP_9: return keypad_numeric ? "9" : "PageUp";
        case SDL_SCANCODE_KP_PERIOD:
        case SDL_SCANCODE_KP_DECIMAL: return keypad_numeric ? "." : "Delete";
        case SDL_SCANCODE_KP_DIVIDE: return "/";
        case SDL_SCANCODE_KP_MULTIPLY: return "*";
        case SDL_SCANCODE_KP_MINUS: return "-";
        case SDL_SCANCODE_KP_PLUS: return "+";
        case SDL_SCANCODE_KP_ENTER: return "Enter";
        case SDL_SCANCODE_KP_EQUALS: return "=";
        default: break;
    }

    const auto translated_key = SDL_GetKeyFromScancode(
        static_cast<SDL_Scancode>(scancode),
        static_cast<SDL_Keymod>(modifiers), false);
    if (translated_key != SDLK_UNKNOWN) key = translated_key;

    if (key >= SDLK_A && key <= SDLK_Z) {
        const bool uppercase = ((modifiers & SDL_KMOD_SHIFT) != 0U) !=
            ((modifiers & SDL_KMOD_CAPS) != 0U);
        char value = static_cast<char>('a' + (key - SDLK_A));
        if (uppercase) value = static_cast<char>(value - 'a' + 'A');
        return std::string(1U, value);
    }

    switch (key) {
        case SDLK_UP: return "ArrowUp";
        case SDLK_DOWN: return "ArrowDown";
        case SDLK_LEFT: return "ArrowLeft";
        case SDLK_RIGHT: return "ArrowRight";
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return "Enter";
        case SDLK_ESCAPE: return "Escape";
        case SDLK_SPACE: return " ";
        case SDLK_BACKSPACE: return "Backspace";
        case SDLK_TAB: return "Tab";
        case SDLK_DELETE: return "Delete";
        case SDLK_INSERT: return "Insert";
        case SDLK_HOME: return "Home";
        case SDLK_END: return "End";
        case SDLK_PAGEUP: return "PageUp";
        case SDLK_PAGEDOWN: return "PageDown";
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: return "Shift";
        case SDLK_LCTRL:
        case SDLK_RCTRL: return "Control";
        case SDLK_LALT:
        case SDLK_RALT: return "Alt";
        case SDLK_LGUI:
        case SDLK_RGUI: return "Meta";
        case SDLK_CAPSLOCK: return "CapsLock";
        case SDLK_NUMLOCKCLEAR: return "NumLock";
        case SDLK_SCROLLLOCK: return "ScrollLock";
        case SDLK_PRINTSCREEN: return "PrintScreen";
        case SDLK_PAUSE: return "Pause";
        case SDLK_APPLICATION: return "ContextMenu";
        default: return SDL_GetKeyName(key);
    }
}

float SdlEventAdapter::LogicalCoordinate(float coordinate, float display_scale, bool normalize) {
    return normalize && display_scale > 0.0f ? coordinate / display_scale : coordinate;
}

float SdlEventAdapter::DisplayContentScale(float display_scale, float pixel_density) {
    if (!std::isfinite(display_scale) || display_scale <= 0.0f ||
        !std::isfinite(pixel_density) || pixel_density <= 0.0f) {
        return 1.0f;
    }
    const float content_scale = display_scale / pixel_density;
    return std::isfinite(content_scale) && content_scale > 0.0f ? content_scale : 1.0f;
}

std::pair<float, float> SdlEventAdapter::WheelDeltas(const SDL_Event& event) {
    // SDL reports positive X to the right and positive Y away from the user.
    // EffinDOM follows browser WheelEvent content deltas: positive X scrolls
    // right and positive Y scrolls down. SDL has already applied the system's
    // natural-scrolling preference to x/y, so direction remains descriptive
    // metadata and must not trigger another inversion.
    return {
        detail::WheelDeltaToLogicalPixels(event.wheel.x),
        detail::WheelDeltaToLogicalPixels(-event.wheel.y),
    };
}

float SdlEventAdapter::LogicalCoordinate(std::uint32_t window_id, float coordinate) const {
    if (!options_.normalize_display_pixel_coordinates &&
        !options_.normalize_display_content_coordinates) return coordinate;
    SDL_Window* window = SDL_GetWindowFromID(window_id);
    if (window == nullptr) return coordinate;
    float scale = SDL_GetWindowDisplayScale(window);
    if (options_.normalize_display_content_coordinates) {
        scale = DisplayContentScale(scale, SDL_GetWindowPixelDensity(window));
    }
    return LogicalCoordinate(coordinate, scale, true);
}

} // namespace effindom::v2::native
