#include "LinuxResizeSyncBridge.h"

#include "SDL3/SDL.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#if defined(EFFINDOM_LINUX_X11_RESIZE_SYNC)

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/sync.h>
#include <poll.h>

namespace effindom::v2::native {

struct LinuxResizeSyncBridge::Impl {
    static Bool IsResizeSyncRequest(Display*, XEvent* event, XPointer argument) {
        const auto* bridge = reinterpret_cast<const Impl*>(argument);
        return bridge != nullptr && event->type == ClientMessage &&
            event->xclient.window == bridge->window && event->xclient.format == 32 &&
            event->xclient.message_type == bridge->wm_protocols &&
            static_cast<Atom>(event->xclient.data.l[0]) == bridge->sync_request;
    }

    Display* display = nullptr;
    Window window = 0;
    Atom wm_protocols = None;
    Atom sync_request = None;
    Atom counter_property = None;
    static XSyncValue CounterValue(std::uint64_t value) {
        XSyncValue result;
        XSyncIntsToValue(&result, static_cast<unsigned int>(value & 0xffffffffU),
            static_cast<int>(value >> 32U));
        return result;
    }

    static std::uint64_t RequestValue(const XClientMessageEvent& event) {
        return static_cast<std::uint64_t>(
                   static_cast<std::uint32_t>(event.data.l[2])) |
            (static_cast<std::uint64_t>(
                 static_cast<std::uint32_t>(event.data.l[3])) << 32U);
    }

    std::uint64_t BeginExtendedFrame(std::uint64_t requested_value) {
        // An extended sync reply must be greater than the WM's requested
        // value. Mutter normally requests last_seen + 240, so an unrelated
        // client-local 3,4,7,8 sequence never satisfies its first wait.
        const std::uint64_t floor = std::max(extended_serial, requested_value);
        std::uint64_t increment = (3U - (floor & 3U)) & 3U;
        if (increment == 0U) increment = 4U;
        extended_serial = floor + increment; // urgent odd: value % 4 == 3
        return extended_serial;
    }

    XSyncCounter basic_counter = None;
    XSyncCounter extended_counter = None;
    XSyncValue pending_value{};
    std::uint64_t request_started_ns = 0U;
    std::uint64_t request_sequence = 0U;
    std::uint64_t extended_serial = 0U;
    bool request_pending = false;
    bool request_is_extended = false;
    bool configure_observed = false;
};

std::unique_ptr<LinuxResizeSyncBridge> LinuxResizeSyncBridge::Create(SDL_Window* window) {
    auto impl = std::make_unique<Impl>();
    if (window == nullptr || SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") != 0) {
        return std::unique_ptr<LinuxResizeSyncBridge>(
            new LinuxResizeSyncBridge(std::move(impl)));
    }

    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    impl->display = static_cast<Display*>(SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
    impl->window = static_cast<Window>(SDL_GetNumberProperty(
        properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    if (impl->display == nullptr || impl->window == 0) {
        impl->display = nullptr;
        impl->window = 0;
        return std::unique_ptr<LinuxResizeSyncBridge>(
            new LinuxResizeSyncBridge(std::move(impl)));
    }

    int event_base = 0;
    int error_base = 0;
    int major = 3;
    int minor = 0;
    if (!XSyncQueryExtension(impl->display, &event_base, &error_base) ||
        !XSyncInitialize(impl->display, &major, &minor) || major < 3) {
        impl->display = nullptr;
        impl->window = 0;
        return std::unique_ptr<LinuxResizeSyncBridge>(
            new LinuxResizeSyncBridge(std::move(impl)));
    }

    impl->wm_protocols = XInternAtom(impl->display, "WM_PROTOCOLS", False);
    impl->sync_request = XInternAtom(impl->display, "_NET_WM_SYNC_REQUEST", False);
    impl->counter_property = XInternAtom(
        impl->display, "_NET_WM_SYNC_REQUEST_COUNTER", False);
    XSyncValue zero;
    XSyncIntToValue(&zero, 0);
    impl->basic_counter = XSyncCreateCounter(impl->display, zero);
    impl->extended_counter = XSyncCreateCounter(impl->display, zero);
    if (impl->basic_counter == None || impl->extended_counter == None) {
        if (impl->basic_counter != None) {
            XSyncDestroyCounter(impl->display, impl->basic_counter);
        }
        if (impl->extended_counter != None) {
            XSyncDestroyCounter(impl->display, impl->extended_counter);
        }
        impl->basic_counter = None;
        impl->extended_counter = None;
        impl->display = nullptr;
        impl->window = 0;
        return std::unique_ptr<LinuxResizeSyncBridge>(
            new LinuxResizeSyncBridge(std::move(impl)));
    }

    const unsigned long counter_ids[] = {
        static_cast<unsigned long>(impl->basic_counter),
        static_cast<unsigned long>(impl->extended_counter),
    };
    XChangeProperty(impl->display, impl->window, impl->counter_property,
        XA_CARDINAL, 32, PropModeReplace,
        reinterpret_cast<const unsigned char*>(counter_ids), 2);

    Atom* existing = nullptr;
    int existing_count = 0;
    std::vector<Atom> protocols;
    if (XGetWMProtocols(impl->display, impl->window, &existing, &existing_count)) {
        protocols.assign(existing, existing + existing_count);
        XFree(existing);
    }
    if (std::find(protocols.begin(), protocols.end(), impl->sync_request) == protocols.end()) {
        protocols.push_back(impl->sync_request);
        XSetWMProtocols(impl->display, impl->window,
            protocols.data(), static_cast<int>(protocols.size()));
    }
    XFlush(impl->display);
    SDL_Log("EffinDOM Linux X11 synchronized resize: enabled");
    return std::unique_ptr<LinuxResizeSyncBridge>(
        new LinuxResizeSyncBridge(std::move(impl)));
}

LinuxResizeSyncBridge::LinuxResizeSyncBridge(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

LinuxResizeSyncBridge::~LinuxResizeSyncBridge() {
    if (!IsEnabled()) return;
    XDeleteProperty(impl_->display, impl_->window, impl_->counter_property);
    XSyncDestroyCounter(impl_->display, impl_->extended_counter);
    XSyncDestroyCounter(impl_->display, impl_->basic_counter);
    XFlush(impl_->display);
}

void LinuxResizeSyncBridge::PumpNativeEvents() {
    if (!IsEnabled()) return;
    XEvent event{};
    bool counter_changed = false;
    while (XCheckIfEvent(impl_->display, &event, Impl::IsResizeSyncRequest,
               reinterpret_cast<XPointer>(impl_.get()))) {
        const std::uint64_t requested_value = Impl::RequestValue(event.xclient);
        XSyncIntsToValue(&impl_->pending_value,
            static_cast<unsigned int>(event.xclient.data.l[2]),
            static_cast<int>(event.xclient.data.l[3]));
        impl_->request_is_extended = event.xclient.data.l[4] != 0;
        if (impl_->request_is_extended) {
            // Extended synchronization leaves the basic counter unused. Mark
            // an urgent frame whose value is beyond the WM's requested
            // serial; the matching even value is published after present.
            const std::uint64_t frame_value =
                impl_->BeginExtendedFrame(requested_value);
            XSyncSetCounter(impl_->display, impl_->extended_counter,
                Impl::CounterValue(frame_value));
        }
        impl_->request_pending = true;
        impl_->configure_observed = false;
        impl_->request_started_ns = SDL_GetTicksNS();
        ++impl_->request_sequence;
        counter_changed = true;
    }
    if (counter_changed) XFlush(impl_->display);
    if (impl_->request_pending &&
        SDL_GetTicksNS() - impl_->request_started_ns >= 100'000'000U) {
        // Never let a temporarily unavailable swapchain image freeze Mutter's
        // interactive edge. The next native request will establish a fresh
        // synchronized frame boundary.
        if (impl_->request_is_extended) {
            ++impl_->extended_serial;
            XSyncSetCounter(impl_->display, impl_->extended_counter,
                Impl::CounterValue(impl_->extended_serial));
        } else {
            XSyncSetCounter(
                impl_->display, impl_->basic_counter, impl_->pending_value);
        }
        XFlush(impl_->display);
        SDL_Log("EffinDOM Linux X11 resize sync: request %llu "
                "*** ESCAPE ACK AFTER 100 MS ***",
            static_cast<unsigned long long>(impl_->request_sequence));
        impl_->request_pending = false;
        impl_->configure_observed = false;
    }
}

void LinuxResizeSyncBridge::WaitForNativeEvents(int timeout_ms) {
    if (!IsEnabled() || timeout_ms <= 0) return;
    pollfd descriptor{};
    descriptor.fd = ConnectionNumber(impl_->display);
    descriptor.events = POLLIN;
    poll(&descriptor, 1, timeout_ms);
}

void LinuxResizeSyncBridge::HandleSdlEvent(const SDL_Event& event) {
    if (!impl_->request_pending) return;
    switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            impl_->configure_observed = true;
            break;
        default:
            break;
    }
}

void LinuxResizeSyncBridge::DidPresentFrame() {
    if (!IsEnabled() || !impl_->request_pending || !impl_->configure_observed) return;
    const double duration_ms =
        static_cast<double>(SDL_GetTicksNS() - impl_->request_started_ns) / 1'000'000.0;
    if (impl_->request_is_extended) {
        ++impl_->extended_serial;
        XSyncSetCounter(impl_->display, impl_->extended_counter,
            Impl::CounterValue(impl_->extended_serial));
    } else {
        XSyncSetCounter(impl_->display, impl_->basic_counter, impl_->pending_value);
    }
    XFlush(impl_->display);
    if (duration_ms > 30.0) {
        SDL_Log("EffinDOM Linux X11 resize sync: request %llu ack duration_ms=%.3f "
                "*** OVER 30 MS ***",
            static_cast<unsigned long long>(impl_->request_sequence), duration_ms);
    }
    impl_->request_pending = false;
    impl_->configure_observed = false;
    impl_->request_started_ns = 0U;
}

bool LinuxResizeSyncBridge::IsEnabled() const {
    return impl_ != nullptr && impl_->display != nullptr && impl_->window != 0 &&
        impl_->basic_counter != None && impl_->extended_counter != None;
}

} // namespace effindom::v2::native

#else

namespace effindom::v2::native {

struct LinuxResizeSyncBridge::Impl {};

std::unique_ptr<LinuxResizeSyncBridge> LinuxResizeSyncBridge::Create(SDL_Window*) {
    return std::unique_ptr<LinuxResizeSyncBridge>(
        new LinuxResizeSyncBridge(std::make_unique<Impl>()));
}
LinuxResizeSyncBridge::LinuxResizeSyncBridge(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
LinuxResizeSyncBridge::~LinuxResizeSyncBridge() = default;
void LinuxResizeSyncBridge::PumpNativeEvents() {}
void LinuxResizeSyncBridge::WaitForNativeEvents(int) {}
void LinuxResizeSyncBridge::HandleSdlEvent(const SDL_Event&) {}
void LinuxResizeSyncBridge::DidPresentFrame() {}
bool LinuxResizeSyncBridge::IsEnabled() const { return false; }

} // namespace effindom::v2::native

#endif
