#include "MacosScrollWheelBridge.h"

#include "SDL3/SDL.h"
#include "effindom_ui.h"

#import <AppKit/AppKit.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace effindom::v2::native {
namespace {

std::uint32_t AppKitModifiers(NSEventModifierFlags flags) {
    std::uint32_t result = 0U;
    if ((flags & NSEventModifierFlagShift) != 0U) result |= UI_KEY_MOD_SHIFT;
    if ((flags & NSEventModifierFlagControl) != 0U) result |= UI_KEY_MOD_CTRL;
    if ((flags & NSEventModifierFlagOption) != 0U) result |= UI_KEY_MOD_ALT;
    if ((flags & NSEventModifierFlagCommand) != 0U) result |= UI_KEY_MOD_META;
    return result;
}

} // namespace

struct MacosScrollWheelBridge::Impl {
    explicit Impl(SDL_Window* sdl_window, Callback event_callback,
        MagnifyCallback magnify_event_callback)
        : callback(std::move(event_callback)),
          magnify_callback(std::move(magnify_event_callback)) {
        wake_event_type = SDL_RegisterEvents(1);
        if (wake_event_type == 0U) {
            throw std::runtime_error(std::string("SDL_RegisterEvents failed: ") + SDL_GetError());
        }
        window_id = SDL_GetWindowID(sdl_window);
        const SDL_PropertiesID properties = SDL_GetWindowProperties(sdl_window);
        window = (__bridge NSWindow*)SDL_GetPointerProperty(
            properties,
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
            nullptr);
        if (window == nil) throw std::runtime_error("SDL did not expose its macOS NSWindow");

        Impl* bridge = this;
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask:
            (NSEventMaskScrollWheel | NSEventMaskMagnify)
            handler:^NSEvent*(NSEvent* event) {
                if (event.window != bridge->window) return event;

                NSView* content_view = bridge->window.contentView;
                const NSPoint point = [content_view convertPoint:event.locationInWindow fromView:nil];
                const float x = static_cast<float>(point.x);
                const float y = static_cast<float>(NSHeight(content_view.bounds) - point.y);
                if (event.type == NSEventTypeMagnify) {
                    const bool handled = bridge->magnify_callback && bridge->magnify_callback(
                        NativeMagnifyEvent{x, y, static_cast<float>(event.magnification)});
                    if (handled) bridge->Wake();
                    return handled ? nil : event;
                }
                const bool precise = event.hasPreciseScrollingDeltas;
                const NSEventPhase phase = event.phase;
                const NSEventPhase momentum_phase = event.momentumPhase;
                const bool inverted = event.isDirectionInvertedFromDevice;
                const auto convert_delta = [precise, inverted](float delta) {
                    return precise
                        ? detail::AppKitPreciseDelta(delta, inverted)
                        : detail::AppKitCoarseDelta(delta, inverted);
                };
                bridge->callback(NativeMacosScrollEvent{
                    x,
                    y,
                    convert_delta(static_cast<float>(-event.scrollingDeltaX)),
                    convert_delta(static_cast<float>(event.scrollingDeltaY)),
                    AppKitModifiers(event.modifierFlags),
                    precise,
                    precise && (phase & NSEventPhaseBegan) != 0U,
                    precise && ((phase & NSEventPhaseCancelled) != 0U ||
                        (momentum_phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) != 0U),
                });
                bridge->Wake();
                return nil;
            }];
        if (monitor == nil) throw std::runtime_error("AppKit scroll-wheel monitor could not be installed");
    }

    ~Impl() {
        if (monitor != nil) [NSEvent removeMonitor:monitor];
    }

    void Wake() {
        if (wake_pending) return;
        SDL_Event wake_event{};
        wake_event.type = wake_event_type;
        wake_event.user.windowID = window_id;
        if (SDL_PushEvent(&wake_event)) {
            wake_pending = true;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_INPUT,
                "EffinDOM could not wake the SDL event loop for native gestures: %s",
                SDL_GetError());
        }
    }

    Callback callback;
    MagnifyCallback magnify_callback;
    __unsafe_unretained NSWindow* window = nil;
    id monitor = nil;
    std::uint32_t wake_event_type = 0U;
    SDL_WindowID window_id = 0U;
    bool wake_pending = false;
};

MacosScrollWheelBridge::MacosScrollWheelBridge(
    SDL_Window* window, Callback callback, MagnifyCallback magnify_callback)
    : impl_(std::make_unique<Impl>(
          window, std::move(callback), std::move(magnify_callback))) {}

MacosScrollWheelBridge::~MacosScrollWheelBridge() = default;

bool MacosScrollWheelBridge::HandleEvent(const SDL_Event& event) {
    if (event.type != impl_->wake_event_type) return false;
    impl_->wake_pending = false;
    return true;
}

} // namespace effindom::v2::native
