#include "SdlEventAdapter.h"

#include "NativeFuiBridge.h"
#include "NativePageZoomController.h"
#include "effindom_ui.h"
#include "SDL3/SDL.h"

#include <algorithm>
#include <cmath>

namespace effindom::v2::native {
namespace {

NativeTouchGestureCallbacks MakeTouchGestureCallbacks(
    NativeInputRouter& router, NativePageZoomController* page_zoom) {
    NativeTouchGestureCallbacks callbacks{};
    callbacks.hit_test = [&router](float x, float y) { return router.HitTestAt(x, y); };
    callbacks.resolve_gesture_owner = [](std::uint64_t handle) {
        return __fui_resolve_gesture_owner(handle);
    };
    callbacks.gesture_intent = [](std::uint64_t handle) {
        return __fui_get_gesture_intent(handle);
    };
    callbacks.dispatch_gesture = [&router](std::uint64_t handle, std::uint32_t phase,
        std::uint32_t kind, float x, float y, float delta_x, float delta_y,
        float scale, std::int32_t pointer_count) {
        const NativePointerMoveInput scene = router.ProjectToScene(x, y);
        return __fui_on_gesture_event(handle, phase, kind, scene.x, scene.y,
            router.ProjectLengthToScene(delta_x), router.ProjectLengthToScene(delta_y),
            scale, pointer_count);
    };
    callbacks.resolve_long_press_owner = [](std::uint64_t handle) {
        return __fui_resolve_long_press_owner(handle);
    };
    callbacks.long_press_duration_ms = [](std::uint64_t handle) {
        return __fui_get_long_press_minimum_duration_ms(handle);
    };
    callbacks.long_press_movement_tolerance = [](std::uint64_t handle) {
        return __fui_get_long_press_movement_tolerance(handle);
    };
    callbacks.long_press_continues_pointer_events = [](std::uint64_t handle) {
        return __fui_long_press_continues_pointer_events(handle);
    };
    callbacks.dispatch_long_press = [&router](std::uint64_t handle, float x, float y,
        std::int32_t pointer_id, std::uint32_t pointer_type,
        std::uint32_t modifiers, std::int32_t duration_ms) {
        const NativePointerMoveInput scene = router.ProjectToScene(x, y);
        return __fui_on_long_press_event(handle, scene.x, scene.y, pointer_id, pointer_type,
            modifiers, duration_ms);
    };
    callbacks.cancel_pointer = [&router](const NativePointerContactInput& source) {
        NativePointerContactInput contact = source;
        contact.button = -1;
        contact.buttons = 0U;
        contact.pressure = 0.0f;
        (void)router.DispatchPointerContact(UI_EVENT_POINTER_CANCEL, contact);
    };
    callbacks.clear_scroll_momentum = [] { ui_clear_momentum_scroll(); };
    callbacks.begin_touch_scroll = [&router](std::uint64_t handle, float x, float y, double timestamp) {
        const NativePointerMoveInput scene = router.ProjectToScene(x, y);
        ui_touch_scroll_begin(handle, scene.x, scene.y, timestamp);
    };
    callbacks.touch_scroll_can_consume = [&router](float delta_x, float delta_y) {
        return ui_touch_scroll_can_consume(
            router.ProjectLengthToScene(delta_x), router.ProjectLengthToScene(delta_y));
    };
    callbacks.update_touch_scroll = [&router](float delta_x, float delta_y, double timestamp) {
        ui_touch_scroll_update(router.ProjectLengthToScene(delta_x),
            router.ProjectLengthToScene(delta_y), timestamp);
    };
    callbacks.end_touch_scroll = [](double timestamp) { ui_touch_scroll_end(timestamp); };
    callbacks.page_zoom_enabled = [page_zoom] {
        return page_zoom != nullptr && page_zoom->IsEnabled();
    };
    callbacks.begin_page_zoom = [page_zoom](float x, float y) {
        return page_zoom != nullptr && page_zoom->BeginPinch(x, y);
    };
    callbacks.update_page_zoom = [page_zoom](float x, float y, float scale) {
        return page_zoom != nullptr && page_zoom->UpdatePinch(scale, x, y);
    };
    callbacks.end_page_zoom = [page_zoom] {
        if (page_zoom != nullptr) page_zoom->EndPinch();
    };
    callbacks.flush_retained_changes = [] { __flushRenders(); };
    return callbacks;
}

} // namespace

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
    SdlEventAdapterOptions options, NativePageZoomController* page_zoom,
    TrackpadPinchHandler trackpad_pinch) : router_(router), options_(options),
    touch_gestures_(MakeTouchGestureCallbacks(router, page_zoom)),
    trackpad_pinch_(std::move(trackpad_pinch)) {}

void SdlEventAdapter::SyncTextInput(SDL_Window* window) {
    if (window == nullptr) return;
    text_input_.Synchronize(router_.FocusedTextInputTarget(), window_focused_, {
        [window] { return SDL_StartTextInput(window); },
        [window] { (void)SDL_StopTextInput(window); },
        [window](const NativeTextInputTarget& target) {
            SDL_Rect area{
                static_cast<int>(std::floor(target.x)),
                static_cast<int>(std::floor(target.y)),
                std::max(1, static_cast<int>(std::ceil(target.width))),
                std::max(1, static_cast<int>(std::ceil(target.height))),
            };
            (void)SDL_SetTextInputArea(window, &area, 0);
        },
    });
}

NativeTextInputState SdlEventAdapter::TextInputState() const {
    return text_input_.State();
}

bool SdlEventAdapter::HandleEvent(const SDL_Event& event, double timestamp_ms) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            mouse_buttons_ = UpdatePointerButtons(mouse_buttons_, event.button.button, down);
            router_.DispatchPointer(NativePointerInput{
                LogicalCoordinate(event.button.windowID, event.button.x),
                LogicalCoordinate(event.button.windowID, event.button.y),
                down, PointerButton(event.button.button), mouse_buttons_, event.button.clicks,
                CurrentModifiers(), timestamp_ms,
            });
            return true;
        }
        case SDL_EVENT_MOUSE_MOTION:
            mouse_buttons_ = PointerButtons(event.motion.state);
            router_.DispatchPointerMove(NativePointerMoveInput{
                LogicalCoordinate(event.motion.windowID, event.motion.x),
                LogicalCoordinate(event.motion.windowID, event.motion.y),
                mouse_buttons_, CurrentModifiers(), timestamp_ms,
            });
            return true;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED: {
            const std::uint64_t finger_id = static_cast<std::uint64_t>(event.tfinger.fingerID);
            if (event.type == SDL_EVENT_FINGER_DOWN && primary_touch_id_ == 0U) {
                primary_touch_id_ = finger_id;
            }
            NativePointerContactInput contact{};
            contact.x = TouchCoordinate(event.tfinger.windowID, event.tfinger.x, true);
            contact.y = TouchCoordinate(event.tfinger.windowID, event.tfinger.y, false);
            contact.pointer_id = ContactPointerId(finger_id, false);
            contact.pointer_type = UI_POINTER_TYPE_TOUCH;
            contact.primary = finger_id == primary_touch_id_;
            contact.button = event.type == SDL_EVENT_FINGER_MOTION ? -1 : 0;
            contact.buttons = event.type == SDL_EVENT_FINGER_UP ||
                    event.type == SDL_EVENT_FINGER_CANCELED ? 0U : 1U;
            contact.pressure = event.tfinger.pressure;
            contact.modifiers = CurrentModifiers();
            contact.timestamp_ms = timestamp_ms;
            const std::uint32_t pointer_event = event.type == SDL_EVENT_FINGER_DOWN
                ? UI_EVENT_POINTER_DOWN
                : event.type == SDL_EVENT_FINGER_MOTION
                    ? UI_EVENT_POINTER_MOVE
                    : event.type == SDL_EVENT_FINGER_CANCELED
                        ? UI_EVENT_POINTER_CANCEL
                        : UI_EVENT_POINTER_UP;
            RememberContact(finger_id, false, contact);
            const bool consume_terminal = (pointer_event == UI_EVENT_POINTER_UP ||
                pointer_event == UI_EVENT_POINTER_CANCEL) &&
                touch_gestures_.ConsumesTerminal(contact.pointer_id);
            bool pointer_handled = false;
            if (!consume_terminal || pointer_event == UI_EVENT_POINTER_CANCEL) {
                pointer_handled = router_.DispatchPointerContact(pointer_event, contact);
            }
            (void)touch_gestures_.HandlePointer(pointer_event, contact,
                router_.PointerMetadata().handle, pointer_handled);
            if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) {
                ForgetContact(finger_id, false);
                ReleaseContactPointerId(finger_id, false);
                if (primary_touch_id_ == finger_id) {
                    primary_touch_id_ = touch_pointer_ids_.empty()
                        ? 0U
                        : touch_pointer_ids_.begin()->first;
                }
            }
            return true;
        }
        case SDL_EVENT_PEN_PROXIMITY_IN:
            (void)ContactPointerId(static_cast<std::uint64_t>(event.pproximity.which), true);
            return true;
        case SDL_EVENT_PEN_PROXIMITY_OUT: {
            const std::uint64_t pen_id = static_cast<std::uint64_t>(event.pproximity.which);
            const auto active = active_pen_contacts_.find(pen_id);
            if (active != active_pen_contacts_.end()) {
                NativePointerContactInput contact = active->second;
                contact.button = -1;
                contact.buttons = 0U;
                contact.pressure = 0.0f;
                contact.timestamp_ms = timestamp_ms;
                router_.DispatchPointerContact(UI_EVENT_POINTER_CANCEL, contact);
                (void)touch_gestures_.HandlePointer(UI_EVENT_POINTER_CANCEL, contact,
                    router_.PointerMetadata().handle, false);
            }
            ForgetContact(pen_id, true);
            ReleaseContactPointerId(pen_id, true);
            pen_axes_.erase(pen_id);
            return true;
        }
        case SDL_EVENT_PEN_AXIS: {
            const std::uint64_t pen_id = static_cast<std::uint64_t>(event.paxis.which);
            PenAxes& axes = pen_axes_[pen_id];
            switch (event.paxis.axis) {
                case SDL_PEN_AXIS_PRESSURE: axes.pressure = event.paxis.value; break;
                case SDL_PEN_AXIS_XTILT: axes.tilt_x = event.paxis.value; break;
                case SDL_PEN_AXIS_YTILT: axes.tilt_y = event.paxis.value; break;
                case SDL_PEN_AXIS_ROTATION: axes.twist = event.paxis.value; break;
                case SDL_PEN_AXIS_TANGENTIAL_PRESSURE:
                    axes.tangential_pressure = event.paxis.value;
                    break;
                default: break;
            }
            NativePointerContactInput contact = PenContact(event.paxis.windowID, pen_id,
                event.paxis.pen_state, event.paxis.x, event.paxis.y, timestamp_ms);
            RememberContact(pen_id, true, contact);
            router_.DispatchPointerContact(UI_EVENT_POINTER_MOVE, contact);
            return true;
        }
        case SDL_EVENT_PEN_MOTION: {
            const std::uint64_t pen_id = static_cast<std::uint64_t>(event.pmotion.which);
            NativePointerContactInput contact = PenContact(event.pmotion.windowID, pen_id,
                event.pmotion.pen_state, event.pmotion.x, event.pmotion.y, timestamp_ms);
            RememberContact(pen_id, true, contact);
            const bool handled = router_.DispatchPointerContact(UI_EVENT_POINTER_MOVE, contact);
            (void)touch_gestures_.HandlePointer(UI_EVENT_POINTER_MOVE, contact,
                router_.PointerMetadata().handle, handled);
            return true;
        }
        case SDL_EVENT_PEN_DOWN:
        case SDL_EVENT_PEN_UP: {
            NativePointerContactInput contact = PenContact(event.ptouch.windowID,
                static_cast<std::uint64_t>(event.ptouch.which), event.ptouch.pen_state,
                event.ptouch.x, event.ptouch.y, timestamp_ms);
            contact.button = 0;
            RememberContact(static_cast<std::uint64_t>(event.ptouch.which), true, contact);
            const std::uint32_t pointer_event = event.type == SDL_EVENT_PEN_DOWN
                ? UI_EVENT_POINTER_DOWN : UI_EVENT_POINTER_UP;
            const bool consume_terminal = pointer_event == UI_EVENT_POINTER_UP &&
                touch_gestures_.ConsumesTerminal(contact.pointer_id);
            const bool handled = consume_terminal ? false
                : router_.DispatchPointerContact(pointer_event, contact);
            (void)touch_gestures_.HandlePointer(pointer_event, contact,
                router_.PointerMetadata().handle, handled);
            if (event.type == SDL_EVENT_PEN_UP) {
                ForgetContact(static_cast<std::uint64_t>(event.ptouch.which), true);
            }
            return true;
        }
        case SDL_EVENT_PEN_BUTTON_DOWN:
        case SDL_EVENT_PEN_BUTTON_UP: {
            NativePointerContactInput contact = PenContact(event.pbutton.windowID,
                static_cast<std::uint64_t>(event.pbutton.which), event.pbutton.pen_state,
                event.pbutton.x, event.pbutton.y, timestamp_ms);
            contact.button = static_cast<std::int32_t>(event.pbutton.button);
            RememberContact(static_cast<std::uint64_t>(event.pbutton.which), true, contact);
            router_.DispatchPointerContact(
                event.type == SDL_EVENT_PEN_BUTTON_DOWN
                    ? UI_EVENT_POINTER_DOWN
                    : UI_EVENT_POINTER_UP,
                contact);
            if (event.type == SDL_EVENT_PEN_BUTTON_UP && contact.buttons == 0U) {
                ForgetContact(static_cast<std::uint64_t>(event.pbutton.which), true);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            const auto [delta_x, delta_y] = WheelDeltas(event);
            router_.DispatchWheel(NativeWheelInput{
                0U,
                LogicalCoordinate(event.wheel.windowID, event.wheel.mouse_x),
                LogicalCoordinate(event.wheel.windowID, event.wheel.mouse_y),
                delta_x,
                delta_y,
                NativeWheelDeltaMode::Pixel,
                CurrentModifiers(),
                false,
                false,
                false,
                timestamp_ms,
            });
            return true;
        }
        case SDL_EVENT_PINCH_BEGIN:
        case SDL_EVENT_PINCH_END:
            return static_cast<bool>(trackpad_pinch_);
        case SDL_EVENT_PINCH_UPDATE: {
            if (!trackpad_pinch_) return false;
            float x = 0.0f;
            float y = 0.0f;
            (void)SDL_GetMouseState(&x, &y);
            return trackpad_pinch_(
                LogicalCoordinate(event.pinch.windowID, x),
                LogicalCoordinate(event.pinch.windowID, y),
                1.0f - event.pinch.scale,
                event.pinch.scale);
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            router_.DispatchKey(KeyName(event.key.key, event.key.scancode,
                event.key.mod, options_.keypad_always_numeric),
                event.type == SDL_EVENT_KEY_DOWN, Modifiers(event.key.mod), timestamp_ms,
                text_input_.State() != NativeTextInputState::Inactive);
            return true;
        case SDL_EVENT_TEXT_EDITING: {
            const std::string text = event.edit.text == nullptr ? "" : event.edit.text;
            const std::uint32_t selection_start = static_cast<std::uint32_t>(
                std::max(event.edit.start, 0));
            const std::uint32_t selection_end = selection_start +
                static_cast<std::uint32_t>(std::max(event.edit.length, 0));
            if (text.empty()) text_input_.BeginCancel();
            else text_input_.BeginComposition();
            router_.DispatchTextComposition(NativeTextCompositionInput{
                text.empty() ? NativeTextCompositionPhase::Cancel
                             : NativeTextCompositionPhase::Update,
                text,
                kNativeTextRangeUnspecified,
                kNativeTextRangeUnspecified,
                selection_start,
                selection_end,
                timestamp_ms,
            });
            text_input_.FinishComposition();
            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
            text_input_.BeginCommit();
            router_.DispatchTextComposition(NativeTextCompositionInput{
                NativeTextCompositionPhase::Commit,
                event.text.text == nullptr ? "" : event.text.text,
                kNativeTextRangeUnspecified,
                kNativeTextRangeUnspecified,
                kNativeTextRangeUnspecified,
                kNativeTextRangeUnspecified,
                timestamp_ms,
            });
            text_input_.FinishComposition();
            return true;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: {
            window_focused_ = true;
            SDL_Window* window = SDL_GetWindowFromID(event.window.windowID);
            SyncTextInput(window);
            return true;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_TERMINATING:
            (void)touch_gestures_.Cancel(timestamp_ms);
            CancelActiveContacts(timestamp_ms);
            mouse_buttons_ = 0U;
            if (event.type != SDL_EVENT_WINDOW_MOUSE_LEAVE) {
                window_focused_ = false;
                SDL_Window* window = SDL_GetWindowFromID(event.window.windowID);
                SyncTextInput(window);
                router_.HandleWindowFocusLost(timestamp_ms);
            } else {
                router_.CancelPointer(timestamp_ms);
            }
            return false;
        default:
            return false;
    }
}

std::uint32_t SdlEventAdapter::CurrentModifiers() const { return Modifiers(SDL_GetModState()); }
std::uint32_t SdlEventAdapter::CurrentButtons() const {
    return mouse_buttons_;
}

bool SdlEventAdapter::IsCoarsePointer() const {
    if (!active_touch_contacts_.empty()) return true;
    if (options_.coarse_pointer_query) return options_.coarse_pointer_query();
    int count = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&count);
    SDL_free(devices);
    return count > 0;
}

bool SdlEventAdapter::AdvanceGestureClock(double timestamp_ms) {
    return touch_gestures_.Advance(timestamp_ms);
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

std::uint32_t SdlEventAdapter::UpdatePointerButtons(
    std::uint32_t current, std::uint8_t button, bool down) {
    std::uint32_t mask = 0U;
    switch (button) {
        case SDL_BUTTON_LEFT: mask = 1U; break;
        case SDL_BUTTON_RIGHT: mask = 2U; break;
        case SDL_BUTTON_MIDDLE: mask = 4U; break;
        case SDL_BUTTON_X1: mask = 8U; break;
        case SDL_BUTTON_X2: mask = 16U; break;
        default: break;
    }
    if (mask == 0U) return current;
    return down ? current | mask : current & ~mask;
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

float SdlEventAdapter::TouchCoordinate(
    std::uint32_t window_id, float normalized, bool horizontal) const {
    SDL_Window* window = SDL_GetWindowFromID(window_id);
    int width = 0;
    int height = 0;
    if (window == nullptr || !SDL_GetWindowSize(window, &width, &height)) return normalized;
    return normalized * static_cast<float>(horizontal ? width : height);
}

std::int32_t SdlEventAdapter::ContactPointerId(std::uint64_t device_id, bool pen) {
    auto& ids = pen ? pen_pointer_ids_ : touch_pointer_ids_;
    const auto existing = ids.find(device_id);
    if (existing != ids.end()) return existing->second;
    const std::int32_t pointer_id = next_contact_pointer_id_++;
    ids.emplace(device_id, pointer_id);
    return pointer_id;
}

void SdlEventAdapter::ReleaseContactPointerId(std::uint64_t device_id, bool pen) {
    (pen ? pen_pointer_ids_ : touch_pointer_ids_).erase(device_id);
}

NativePointerContactInput SdlEventAdapter::PenContact(std::uint32_t window_id,
    std::uint64_t pen_id, std::uint32_t pen_state, float x, float y, double timestamp_ms) {
    const PenAxes& axes = pen_axes_[pen_id];
    NativePointerContactInput contact{};
    contact.x = LogicalCoordinate(window_id, x);
    contact.y = LogicalCoordinate(window_id, y);
    contact.pointer_id = ContactPointerId(pen_id, true);
    contact.pointer_type = UI_POINTER_TYPE_PEN;
    contact.primary = true;
    contact.button = -1;
    contact.buttons = (pen_state & SDL_PEN_INPUT_DOWN) != 0U ? 1U : 0U;
    contact.pressure = axes.pressure;
    contact.tangential_pressure = axes.tangential_pressure;
    contact.tilt_x = axes.tilt_x;
    contact.tilt_y = axes.tilt_y;
    contact.twist = axes.twist;
    contact.modifiers = CurrentModifiers();
    contact.timestamp_ms = timestamp_ms;
    return contact;
}

void SdlEventAdapter::RememberContact(std::uint64_t device_id, bool pen,
    const NativePointerContactInput& contact) {
    (pen ? active_pen_contacts_ : active_touch_contacts_)[device_id] = contact;
}

void SdlEventAdapter::ForgetContact(std::uint64_t device_id, bool pen) {
    (pen ? active_pen_contacts_ : active_touch_contacts_).erase(device_id);
}

void SdlEventAdapter::CancelActiveContacts(double timestamp_ms) {
    const auto cancel = [this, timestamp_ms](const auto& contacts) {
        for (const auto& [device_id, active] : contacts) {
            (void)device_id;
            NativePointerContactInput contact = active;
            contact.button = -1;
            contact.buttons = 0U;
            contact.pressure = 0.0f;
            contact.timestamp_ms = timestamp_ms;
            router_.DispatchPointerContact(UI_EVENT_POINTER_CANCEL, contact);
        }
    };
    cancel(active_touch_contacts_);
    cancel(active_pen_contacts_);
    active_touch_contacts_.clear();
    active_pen_contacts_.clear();
    touch_pointer_ids_.clear();
    pen_pointer_ids_.clear();
    pen_axes_.clear();
    primary_touch_id_ = 0U;
}

} // namespace effindom::v2::native
