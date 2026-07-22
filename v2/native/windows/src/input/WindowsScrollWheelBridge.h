#pragma once

#include <cstdint>
#include <functional>
#include <memory>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

namespace detail {

inline constexpr float WindowsHorizontalWheelDeltaToLogicalPixels(std::int16_t delta) {
    return static_cast<float>(delta) * (96.0f / 120.0f);
}

inline constexpr float WindowsVerticalWheelDeltaToLogicalPixels(std::int16_t delta) {
    return -static_cast<float>(delta) * (96.0f / 120.0f);
}

inline constexpr bool IsWindowsPreciseWheelDelta(std::int16_t delta) {
    return delta % 120 != 0;
}

} // namespace detail

struct NativeWheelEvent {
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    bool precise = false;
    bool begins_gesture = false;
    bool ends_gesture = false;
};

struct NativeMouseEvent {
    enum class Type { Down, Move, Up };
    Type type = Type::Move;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t modifiers = 0U;
    std::uint32_t buttons = 0U;
    std::int32_t click_count = 0;
};

class WindowsScrollWheelBridge final {
public:
    using Handler = std::function<void(const NativeWheelEvent&)>;
    using MouseHandler = std::function<void(const NativeMouseEvent&)>;
    using ResizeHandler = std::function<void()>;

    WindowsScrollWheelBridge(SDL_Window*, Handler, MouseHandler, ResizeHandler);
    ~WindowsScrollWheelBridge();

    bool HandleEvent(const SDL_Event&);
    void SetPointerCapture(bool captured);

    WindowsScrollWheelBridge(const WindowsScrollWheelBridge&) = delete;
    WindowsScrollWheelBridge& operator=(const WindowsScrollWheelBridge&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
