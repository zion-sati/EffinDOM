#pragma once

#include <functional>
#include <memory>

struct SDL_Window;
union SDL_Event;

namespace effindom::v2::native {

namespace detail {

inline constexpr float AppKitPreciseDelta(float delta, bool direction_inverted) {
    return direction_inverted ? -delta : delta;
}

} // namespace detail

struct NativePreciseScrollEvent {
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    bool begins_gesture = false;
    bool ends_gesture = false;
};

class MacosScrollWheelBridge final {
public:
    using Callback = std::function<void(const NativePreciseScrollEvent&)>;

    MacosScrollWheelBridge(SDL_Window* window, Callback callback);
    ~MacosScrollWheelBridge();

    bool HandleEvent(const SDL_Event& event);

    MacosScrollWheelBridge(const MacosScrollWheelBridge&) = delete;
    MacosScrollWheelBridge& operator=(const MacosScrollWheelBridge&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
