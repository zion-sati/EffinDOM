#include "WindowsSystemThemeBridge.h"

#include "SDL3/SDL.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace effindom::v2::native {

namespace detail {

std::optional<std::uint32_t> ReadWindowsAccentColor() {
    DWORD colorization = 0U;
    BOOL opaque_blend = FALSE;
    if (FAILED(DwmGetColorizationColor(&colorization, &opaque_blend))) return std::nullopt;
    return PackWindowsColorizationColor(static_cast<std::uint32_t>(colorization));
}

WindowsAccentColorState::WindowsAccentColorState(Reader reader, ChangedHandler changed_handler)
    : reader_(std::move(reader)), changed_handler_(std::move(changed_handler)) {
    if (const std::optional<std::uint32_t> color = reader_()) current_ = *color;
}

std::uint32_t WindowsAccentColorState::Current() const { return current_; }

bool WindowsAccentColorState::Refresh() {
    const std::optional<std::uint32_t> color = reader_();
    if (!color.has_value() || *color == current_) return false;
    current_ = *color;
    changed_handler_(current_);
    return true;
}

} // namespace detail

struct WindowsSystemThemeBridge::Impl {
    Impl(SDL_Window* sdl_window, AccentChangedHandler changed_handler)
        : state(&detail::ReadWindowsAccentColor, std::move(changed_handler)) {
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
            throw std::runtime_error("Win32 system theme subclass could not be installed");
        }
    }

    ~Impl() {
        if (hwnd != nullptr) {
            RemoveWindowSubclass(hwnd, &Impl::SubclassProcedure, subclass_id);
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
                SDL_LOG_CATEGORY_APPLICATION,
                "EffinDOM could not wake the SDL event loop for a system accent change: %s",
                SDL_GetError());
        }
    }

    static LRESULT CALLBACK SubclassProcedure(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        UINT_PTR,
        DWORD_PTR reference) {
        auto& bridge = *reinterpret_cast<Impl*>(reference);
        if (message == WM_DWMCOLORIZATIONCOLORCHANGED) {
            if (bridge.state.Refresh()) bridge.WakeEventLoop();
        } else if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, &Impl::SubclassProcedure, bridge.subclass_id);
            bridge.hwnd = nullptr;
        }
        return DefSubclassProc(window, message, wparam, lparam);
    }

    detail::WindowsAccentColorState state;
    HWND hwnd = nullptr;
    UINT_PTR subclass_id = 0U;
    std::uint32_t wake_event_type = 0U;
    SDL_WindowID window_id = 0U;
    bool wake_pending = false;
};

WindowsSystemThemeBridge::WindowsSystemThemeBridge(SDL_Window* window, AccentChangedHandler changed_handler)
    : impl_(std::make_unique<Impl>(window, std::move(changed_handler))) {}

WindowsSystemThemeBridge::~WindowsSystemThemeBridge() = default;

std::uint32_t WindowsSystemThemeBridge::AccentColor() const { return impl_->state.Current(); }

bool WindowsSystemThemeBridge::HandleEvent(const SDL_Event& event) {
    if (event.type != impl_->wake_event_type) return false;
    impl_->wake_pending = false;
    return true;
}

} // namespace effindom::v2::native
