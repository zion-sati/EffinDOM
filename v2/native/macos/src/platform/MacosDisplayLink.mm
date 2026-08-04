#include "MacosDisplayLink.h"

#include "SDL3/SDL.h"

#import <AppKit/AppKit.h>
#import <CoreVideo/CVDisplayLink.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace effindom::v2::native {

struct MacosDisplayLink::Impl {
    explicit Impl(SDL_Window* owner_window, LiveResizeRefresh refresh)
        : window(owner_window), live_resize_refresh(std::move(refresh)) {
        if (CVDisplayLinkCreateWithActiveCGDisplays(&display_link) != kCVReturnSuccess ||
            display_link == nullptr) {
            throw std::runtime_error("CVDisplayLink creation failed");
        }
        if (CVDisplayLinkSetOutputCallback(display_link, &Impl::OnRefresh, this) !=
            kCVReturnSuccess) {
            CVDisplayLinkRelease(display_link);
            display_link = nullptr;
            throw std::runtime_error("CVDisplayLink callback registration failed");
        }
        CFRunLoopSourceContext source_context{};
        source_context.info = this;
        source_context.perform = &Impl::OnMainRunLoopRefresh;
        live_resize_source = CFRunLoopSourceCreate(
            kCFAllocatorDefault, 0, &source_context);
        if (live_resize_source == nullptr) {
            CVDisplayLinkRelease(display_link);
            display_link = nullptr;
            throw std::runtime_error("live-resize run-loop source creation failed");
        }
        main_run_loop = CFRunLoopGetMain();
        CFRunLoopObserverContext observer_context{};
        observer_context.info = this;
        event_tracking_observer = CFRunLoopObserverCreate(
            kCFAllocatorDefault,
            kCFRunLoopEntry | kCFRunLoopExit,
            true,
            0,
            &Impl::OnEventTrackingActivity,
            &observer_context);
        if (event_tracking_observer == nullptr) {
            throw std::runtime_error("event-tracking run-loop observer creation failed");
        }
        CFRunLoopAddObserver(
            main_run_loop,
            event_tracking_observer,
            (__bridge CFStringRef)NSEventTrackingRunLoopMode);
        CFRunLoopAddSource(main_run_loop, live_resize_source, kCFRunLoopCommonModes);
        CFRunLoopAddSource(
            main_run_loop,
            live_resize_source,
            (__bridge CFStringRef)NSEventTrackingRunLoopMode);
        NSWindow* native_window = NativeWindow();
        if (native_window == nil) {
            throw std::runtime_error("Cocoa window lookup failed");
        }
        NSNotificationCenter* notifications = NSNotificationCenter.defaultCenter;
        live_resize_started_observer = [notifications
            addObserverForName:NSWindowWillStartLiveResizeNotification
                        object:native_window
                         queue:nil
                    usingBlock:^(__unused NSNotification* notification) {
                        live_resize_active = true;
                        UpdateRunning();
                    }];
        live_resize_ended_observer = [notifications
            addObserverForName:NSWindowDidEndLiveResizeNotification
                        object:native_window
                         queue:nil
                    usingBlock:^(__unused NSNotification* notification) {
                        live_resize_active = false;
                        UpdateRunning();
                    }];
    }

    ~Impl() {
        NSNotificationCenter* notifications = NSNotificationCenter.defaultCenter;
        if (live_resize_started_observer != nil) {
            [notifications removeObserver:live_resize_started_observer];
        }
        if (live_resize_ended_observer != nil) {
            [notifications removeObserver:live_resize_ended_observer];
        }
        live_resize_active = false;
        event_tracking_active = false;
        SetActive(false);
        if (event_tracking_observer != nullptr) {
            CFRunLoopObserverInvalidate(event_tracking_observer);
            CFRelease(event_tracking_observer);
        }
        if (live_resize_source != nullptr) {
            CFRunLoopSourceInvalidate(live_resize_source);
            CFRelease(live_resize_source);
        }
        if (display_link != nullptr) CVDisplayLinkRelease(display_link);
    }

    static CVReturn OnRefresh(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
        CVOptionFlags, CVOptionFlags*, void* context) {
        auto& self = *static_cast<Impl*>(context);
        {
            std::lock_guard lock(self.mutex);
            ++self.refresh_generation;
        }
        self.condition.notify_one();
        CFRunLoopSourceSignal(self.live_resize_source);
        CFRunLoopWakeUp(self.main_run_loop);
        return kCVReturnSuccess;
    }

    static void OnMainRunLoopRefresh(void* context) {
        auto& self = *static_cast<Impl*>(context);
        NSWindow* native_window = self.NativeWindow();
        const bool nested_resize_tracking = self.event_tracking_active ||
            (native_window != nil && native_window.inLiveResize);
        if (detail::ShouldDispatchMacosLiveResizeFrame(self.active, nested_resize_tracking) &&
            self.live_resize_refresh) {
            self.live_resize_refresh();
        }
    }

    static void OnEventTrackingActivity(
        CFRunLoopObserverRef, CFRunLoopActivity activity, void* context) {
        auto& self = *static_cast<Impl*>(context);
        if ((activity & kCFRunLoopEntry) != 0U) {
            self.event_tracking_active = true;
        }
        if ((activity & kCFRunLoopExit) != 0U) {
            self.event_tracking_active = false;
        }
        self.UpdateRunning();
    }

    NSWindow* NativeWindow() const {
        return (__bridge NSWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
            nullptr);
    }

    void RefreshDisplay() {
        NSWindow* native_window = (__bridge NSWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        NSNumber* screen_number = native_window.screen.deviceDescription[@"NSScreenNumber"];
        if (screen_number != nil) {
            CVDisplayLinkSetCurrentCGDisplay(
                display_link, static_cast<CGDirectDisplayID>(screen_number.unsignedIntValue));
        }
    }

    void SetActive(bool next_active) {
        requested_active = next_active;
        UpdateRunning();
    }

    void UpdateRunning() {
        const bool next_active = detail::ShouldRunMacosDisplayLink(
            requested_active, live_resize_active, event_tracking_active);
        if (next_active == active) return;
        if (next_active) {
            RefreshDisplay();
            if (CVDisplayLinkStart(display_link) != kCVReturnSuccess) {
                throw std::runtime_error("CVDisplayLink start failed");
            }
        } else if (display_link != nullptr && CVDisplayLinkIsRunning(display_link)) {
            CVDisplayLinkStop(display_link);
        }
        active = next_active;
    }

    void WaitForRefresh() {
        SetActive(true);
        std::unique_lock lock(mutex);
        const std::uint64_t observed = refresh_generation;
        condition.wait(lock, [this, observed] { return refresh_generation != observed; });
    }

    SDL_Window* window = nullptr;
    LiveResizeRefresh live_resize_refresh;
    CVDisplayLinkRef display_link = nullptr;
    CFRunLoopRef main_run_loop = nullptr;
    CFRunLoopSourceRef live_resize_source = nullptr;
    CFRunLoopObserverRef event_tracking_observer = nullptr;
    id live_resize_started_observer = nil;
    id live_resize_ended_observer = nil;
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t refresh_generation = 0U;
    bool requested_active = false;
    bool live_resize_active = false;
    bool event_tracking_active = false;
    bool active = false;
};

MacosDisplayLink::MacosDisplayLink(
    SDL_Window* window, LiveResizeRefresh live_resize_refresh)
    : impl_(std::make_unique<Impl>(window, std::move(live_resize_refresh))) {}
MacosDisplayLink::~MacosDisplayLink() = default;
void MacosDisplayLink::WaitForRefresh() { impl_->WaitForRefresh(); }
void MacosDisplayLink::SetActive(bool active) { impl_->SetActive(active); }

} // namespace effindom::v2::native
