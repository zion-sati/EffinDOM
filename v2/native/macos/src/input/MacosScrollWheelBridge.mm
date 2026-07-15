#include "MacosScrollWheelBridge.h"

#include "SDL3/SDL.h"

#import <AppKit/AppKit.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace effindom::v2::native {

struct MacosScrollWheelBridge::Impl {
    explicit Impl(SDL_Window* sdl_window, Callback event_callback)
        : callback(std::move(event_callback)) {
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
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
            handler:^NSEvent*(NSEvent* event) {
                if (event.window != bridge->window || !event.hasPreciseScrollingDeltas) return event;

                const NSEventPhase phase = event.phase;
                const NSEventPhase momentum_phase = event.momentumPhase;
                const bool inverted = event.isDirectionInvertedFromDevice;
                bridge->callback(NativePreciseScrollEvent{
                    detail::AppKitPreciseDelta(
                        static_cast<float>(-event.scrollingDeltaX),
                        inverted),
                    detail::AppKitPreciseDelta(
                        static_cast<float>(event.scrollingDeltaY),
                        inverted),
                    (phase & NSEventPhaseBegan) != 0U,
                    (phase & NSEventPhaseCancelled) != 0U ||
                        (momentum_phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) != 0U,
                });
                if (!bridge->wake_pending) {
                    SDL_Event wake_event{};
                    wake_event.type = bridge->wake_event_type;
                    wake_event.user.windowID = bridge->window_id;
                    if (SDL_PushEvent(&wake_event)) {
                        bridge->wake_pending = true;
                    } else {
                        SDL_LogError(
                            SDL_LOG_CATEGORY_INPUT,
                            "EffinDOM could not wake the SDL event loop for precise scrolling: %s",
                            SDL_GetError());
                    }
                }
                return nil;
            }];
        if (monitor == nil) throw std::runtime_error("AppKit scroll-wheel monitor could not be installed");
    }

    ~Impl() {
        if (monitor != nil) [NSEvent removeMonitor:monitor];
    }

    Callback callback;
    __unsafe_unretained NSWindow* window = nil;
    id monitor = nil;
    std::uint32_t wake_event_type = 0U;
    SDL_WindowID window_id = 0U;
    bool wake_pending = false;
};

MacosScrollWheelBridge::MacosScrollWheelBridge(SDL_Window* window, Callback callback)
    : impl_(std::make_unique<Impl>(window, std::move(callback))) {}

MacosScrollWheelBridge::~MacosScrollWheelBridge() = default;

bool MacosScrollWheelBridge::HandleEvent(const SDL_Event& event) {
    if (event.type != impl_->wake_event_type) return false;
    impl_->wake_pending = false;
    return true;
}

} // namespace effindom::v2::native
