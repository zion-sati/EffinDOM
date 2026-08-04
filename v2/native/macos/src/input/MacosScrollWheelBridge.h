#pragma once

#include <cstdint>
#include <functional>
#include <memory>

struct SDL_Window;
union SDL_Event;

namespace effindom::v2::native {

namespace detail {

inline constexpr float AppKitPreciseDelta(float delta, bool direction_inverted) {
    return direction_inverted ? -delta : delta;
}

inline constexpr float AppKitCoarseDelta(float delta, bool direction_inverted) {
    return AppKitPreciseDelta(delta, direction_inverted) * 16.0f;
}

inline constexpr float AppKitMagnificationMultiplier(float magnification) {
    return magnification <= -0.99f ? 0.01f : 1.0f + magnification;
}

} // namespace detail

struct NativeMacosScrollEvent {
    float x = 0.0f;
    float y = 0.0f;
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    std::uint32_t modifiers = 0U;
    bool precise = false;
    bool begins_gesture = false;
    bool ends_gesture = false;
};

struct NativeMagnifyEvent {
    float x = 0.0f;
    float y = 0.0f;
    float magnification = 0.0f;
};

class MacosScrollWheelBridge final {
public:
    using Callback = std::function<void(const NativeMacosScrollEvent&)>;
    using MagnifyCallback = std::function<bool(const NativeMagnifyEvent&)>;

    MacosScrollWheelBridge(
        SDL_Window* window, Callback callback, MagnifyCallback magnify_callback);
    ~MacosScrollWheelBridge();

    bool HandleEvent(const SDL_Event& event);

    MacosScrollWheelBridge(const MacosScrollWheelBridge&) = delete;
    MacosScrollWheelBridge& operator=(const MacosScrollWheelBridge&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
