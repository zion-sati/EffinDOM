#pragma once

#include <functional>
#include <memory>

struct SDL_Window;

namespace effindom::v2::native {

namespace detail {

inline constexpr bool ShouldRunMacosDisplayLink(
    bool frame_requested, bool live_resize_active, bool event_tracking_active) {
    return frame_requested || live_resize_active || event_tracking_active;
}

inline constexpr bool ShouldDispatchMacosLiveResizeFrame(
    bool refresh_active, bool window_in_live_resize) {
    return refresh_active && window_in_live_resize;
}

} // namespace detail

class MacosDisplayLink final {
public:
    using LiveResizeRefresh = std::function<void()>;

    explicit MacosDisplayLink(SDL_Window* window, LiveResizeRefresh live_resize_refresh);
    ~MacosDisplayLink();

    void WaitForRefresh();
    void SetActive(bool active);

    MacosDisplayLink(const MacosDisplayLink&) = delete;
    MacosDisplayLink& operator=(const MacosDisplayLink&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
