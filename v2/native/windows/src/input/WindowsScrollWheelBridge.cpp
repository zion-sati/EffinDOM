#include "WindowsScrollWheelBridge.h"

#include "SDL3/SDL.h"
#include "effindom_ui.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace effindom::v2::native {
namespace {

constexpr UINT kScrollEndDelayMs = 140U;

std::uint32_t KeyModifiers() {
    std::uint32_t result = 0U;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) result |= UI_KEY_MOD_SHIFT;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) result |= UI_KEY_MOD_CTRL;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) result |= UI_KEY_MOD_ALT;
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        result |= UI_KEY_MOD_META;
    }
    return result;
}

} // namespace

struct WindowsScrollWheelBridge::Impl {
    Impl(SDL_Window* sdl_window, Handler wheel_handler, MouseHandler mouse_handler, ResizeHandler resize_handler)
        : window(sdl_window), on_wheel(std::move(wheel_handler)), on_mouse(std::move(mouse_handler)),
          on_resize(std::move(resize_handler)) {
        wake_event_type = SDL_RegisterEvents(1);
        if (wake_event_type == 0U) {
            throw std::runtime_error(std::string("SDL_RegisterEvents failed: ") + SDL_GetError());
        }
        window_id = SDL_GetWindowID(sdl_window);
        const SDL_PropertiesID properties = SDL_GetWindowProperties(sdl_window);
        hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            properties,
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr));
        if (hwnd == nullptr) throw std::runtime_error("SDL did not expose its Win32 HWND");
        subclass_id = reinterpret_cast<UINT_PTR>(this);
        if (!SetWindowSubclass(hwnd, &Impl::SubclassProcedure, subclass_id, reinterpret_cast<DWORD_PTR>(this))) {
            throw std::runtime_error("Win32 native input/resize subclass could not be installed");
        }
    }

    ~Impl() {
        if (hwnd != nullptr) {
            KillTimer(hwnd, subclass_id);
            SetPointerCapture(false);
            RemoveWindowSubclass(hwnd, &Impl::SubclassProcedure, subclass_id);
        }
    }

    void SetPointerCapture(bool captured) {
        if (hwnd == nullptr) return;
        if (captured) {
            if (GetCapture() != hwnd) {
                SetCapture(hwnd);
                owns_pointer_capture = GetCapture() == hwnd;
            }
        } else if (owns_pointer_capture) {
            owns_pointer_capture = false;
            if (GetCapture() == hwnd) ReleaseCapture();
        }
    }

    void WakeEventLoop() {
        if (wake_pending) return;
        SDL_Event event{};
        event.type = wake_event_type;
        event.user.windowID = window_id;
        if (SDL_PushEvent(&event)) {
            wake_pending = true;
        } else {
            SDL_LogError(
                SDL_LOG_CATEGORY_INPUT,
                "EffinDOM could not wake the SDL event loop for native window input: %s",
                SDL_GetError());
        }
    }

    void DispatchWheel(UINT message, WPARAM wparam) {
        const auto delta = static_cast<std::int16_t>(GET_WHEEL_DELTA_WPARAM(wparam));
        const float logical_delta = message == WM_MOUSEHWHEEL
            ? detail::WindowsHorizontalWheelDeltaToLogicalPixels(delta)
            : detail::WindowsVerticalWheelDeltaToLogicalPixels(delta);
        const bool precise = detail::IsWindowsPreciseWheelDelta(delta);
        if (!precise && precise_scrolling) EndPreciseScrollGesture();
        const bool begins_gesture = precise && !precise_scrolling;
        precise_scrolling = precise_scrolling || precise;
        on_wheel(NativeWheelEvent{
            message == WM_MOUSEHWHEEL ? logical_delta : 0.0f,
            message == WM_MOUSEWHEEL ? logical_delta : 0.0f,
            precise,
            begins_gesture,
            false,
        });
        if (precise && SetTimer(hwnd, subclass_id, kScrollEndDelayMs, nullptr) == 0U) {
            EndPreciseScrollGesture();
        }
        WakeEventLoop();
    }

    void EndPreciseScrollGesture() {
        KillTimer(hwnd, subclass_id);
        if (!precise_scrolling) return;
        precise_scrolling = false;
        on_wheel(NativeWheelEvent{0.0f, 0.0f, true, false, true});
        WakeEventLoop();
    }

    void DispatchMouse(NativeMouseEvent::Type type, WPARAM wparam, LPARAM lparam, std::int32_t clicks) {
        RECT client{};
        int logical_width = 0;
        int logical_height = 0;
        GetClientRect(hwnd, &client);
        SDL_GetWindowSize(window, &logical_width, &logical_height);
        const LONG pixel_width = std::max(client.right - client.left, 1L);
        const LONG pixel_height = std::max(client.bottom - client.top, 1L);
        const float display_scale = std::max(SDL_GetWindowDisplayScale(window), 1.0f);
        const float x = static_cast<float>(GET_X_LPARAM(lparam)) *
            static_cast<float>(logical_width) / static_cast<float>(pixel_width) / display_scale;
        const float y = static_cast<float>(GET_Y_LPARAM(lparam)) *
            static_cast<float>(logical_height) / static_cast<float>(pixel_height) / display_scale;
        std::uint32_t buttons = 0U;
        if ((wparam & MK_LBUTTON) != 0U) buttons |= 1U;
        if ((wparam & MK_RBUTTON) != 0U) buttons |= 2U;
        if ((wparam & MK_MBUTTON) != 0U) buttons |= 4U;
        if ((wparam & MK_XBUTTON1) != 0U) buttons |= 8U;
        if ((wparam & MK_XBUTTON2) != 0U) buttons |= 16U;
        on_mouse(NativeMouseEvent{type, x, y, KeyModifiers(), buttons, clicks});
        WakeEventLoop();
    }

    static LRESULT CALLBACK SubclassProcedure(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        UINT_PTR,
        DWORD_PTR reference) {
        auto& bridge = *reinterpret_cast<Impl*>(reference);
        switch (message) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                bridge.primary_drag_active = true;
                bridge.SetPointerCapture(true);
                // Let the shared input router count the complete click
                // sequence. Supplying Win32's partial count here resets an
                // ordinary third down to one after WM_LBUTTONDBLCLK.
                bridge.DispatchMouse(NativeMouseEvent::Type::Down, wparam, lparam, 0);
                return 0;
            case WM_MOUSEMOVE:
                if (bridge.primary_drag_active ||
                    (bridge.owns_pointer_capture && (wparam & MK_LBUTTON) != 0U)) {
                    bridge.primary_drag_active = true;
                    bridge.DispatchMouse(NativeMouseEvent::Type::Move, wparam, lparam, 0);
                    return 0;
                }
                // Preserve SDL's ordinary hover tracking and WM_SETCURSOR
                // behavior. Only an active primary drag needs the native
                // selection bypass.
                break;
            case WM_LBUTTONUP:
                if (bridge.primary_drag_active || bridge.owns_pointer_capture) {
                    bridge.DispatchMouse(NativeMouseEvent::Type::Up, wparam, lparam, 1);
                    bridge.primary_drag_active = false;
                    bridge.SetPointerCapture(false);
                    return 0;
                }
                break;
            case WM_KILLFOCUS:
                bridge.primary_drag_active = false;
                break;
            case WM_CAPTURECHANGED:
                // Capture can be reaffirmed re-entrantly while dispatching
                // pointer-down. Only cancel the drag when another HWND (or
                // no HWND) actually owns capture.
                if (GetCapture() != window) {
                    bridge.primary_drag_active = false;
                    bridge.owns_pointer_capture = false;
                }
                break;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                bridge.DispatchWheel(message, wparam);
                // Do not let SDL synthesize a second, normalized wheel event.
                return 0;
            case WM_TIMER:
                if (wparam == bridge.subclass_id) {
                    bridge.EndPreciseScrollGesture();
                    return 0;
                }
                break;
            case WM_SIZE: {
                // Let SDL update its cached logical/pixel window state, then
                // force the corresponding WM_PAINT before returning to DWM.
                const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
                if (wparam != SIZE_MINIMIZED) {
                    RedrawWindow(
                        window,
                        nullptr,
                        nullptr,
                        RDW_INVALIDATE | RDW_INTERNALPAINT | RDW_UPDATENOW | RDW_NOERASE);
                }
                return result;
            }
            case WM_ERASEBKGND:
                // EffinDOM paints the complete client surface. Letting the
                // class background erase between resize frames causes a flash.
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT paint{};
                BeginPaint(window, &paint);
                bridge.on_resize();
                EndPaint(window, &paint);
                return 0;
            }
            case WM_NCDESTROY: {
                bridge.EndPreciseScrollGesture();
                bridge.primary_drag_active = false;
                RemoveWindowSubclass(window, &Impl::SubclassProcedure, bridge.subclass_id);
                bridge.hwnd = nullptr;
                break;
            }
            default:
                break;
        }
        return DefSubclassProc(window, message, wparam, lparam);
    }

    SDL_Window* window = nullptr;
    Handler on_wheel;
    MouseHandler on_mouse;
    ResizeHandler on_resize;
    HWND hwnd = nullptr;
    UINT_PTR subclass_id = 0U;
    std::uint32_t wake_event_type = 0U;
    SDL_WindowID window_id = 0U;
    bool wake_pending = false;
    bool primary_drag_active = false;
    bool owns_pointer_capture = false;
    bool precise_scrolling = false;
};

WindowsScrollWheelBridge::WindowsScrollWheelBridge(
    SDL_Window* window,
    Handler wheel_handler,
    MouseHandler mouse_handler,
    ResizeHandler resize_handler)
    : impl_(std::make_unique<Impl>(
          window, std::move(wheel_handler), std::move(mouse_handler), std::move(resize_handler))) {}

WindowsScrollWheelBridge::~WindowsScrollWheelBridge() = default;

bool WindowsScrollWheelBridge::HandleEvent(const SDL_Event& event) {
    if (event.type != impl_->wake_event_type) return false;
    impl_->wake_pending = false;
    return true;
}

void WindowsScrollWheelBridge::SetPointerCapture(bool captured) {
    impl_->SetPointerCapture(captured);
}

} // namespace effindom::v2::native
