#pragma once

#include "NativeInputTypes.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

namespace effindom::v2::native {

struct NativeTouchGestureCallbacks {
    std::function<std::uint64_t(float, float)> hit_test;
    std::function<std::uint64_t(std::uint64_t)> resolve_gesture_owner;
    std::function<std::uint32_t(std::uint64_t)> gesture_intent;
    std::function<bool(std::uint64_t, std::uint32_t, std::uint32_t, float, float,
        float, float, float, std::int32_t)> dispatch_gesture;
    std::function<std::uint64_t(std::uint64_t)> resolve_long_press_owner;
    std::function<std::int32_t(std::uint64_t)> long_press_duration_ms;
    std::function<float(std::uint64_t)> long_press_movement_tolerance;
    std::function<bool(std::uint64_t)> long_press_continues_pointer_events;
    std::function<bool(std::uint64_t, float, float, std::int32_t, std::uint32_t,
        std::uint32_t, std::int32_t)> dispatch_long_press;
    std::function<void(const NativePointerContactInput&)> cancel_pointer;
    std::function<void()> clear_scroll_momentum;
    std::function<void(std::uint64_t, float, float, double)> begin_touch_scroll;
    std::function<bool(float, float)> touch_scroll_can_consume;
    std::function<void(float, float, double)> update_touch_scroll;
    std::function<void(double)> end_touch_scroll;
    std::function<bool()> page_zoom_enabled;
    std::function<bool(float, float)> begin_page_zoom;
    std::function<bool(float, float, float)> update_page_zoom;
    std::function<void()> end_page_zoom;
    std::function<void()> flush_retained_changes;
};

class NativeTouchGestureController final {
public:
    explicit NativeTouchGestureController(NativeTouchGestureCallbacks callbacks);

    bool HandlePointer(std::uint32_t event_type, const NativePointerContactInput& input,
        std::uint64_t hit_handle, bool pointer_handled);
    bool ConsumesTerminal(std::int32_t pointer_id) const;
    bool Advance(double timestamp_ms);
    bool Cancel(double timestamp_ms);

private:
    struct Contact {
        NativePointerContactInput input;
        float start_x = 0.0f;
        float start_y = 0.0f;
        float last_x = 0.0f;
        float last_y = 0.0f;
        std::uint64_t hit_handle = 0U;
        bool moved = false;
    };

    struct ScrollState {
        std::int32_t pointer_id = 0;
        std::uint64_t start_handle = 0U;
        float start_x = 0.0f;
        float start_y = 0.0f;
        float last_x = 0.0f;
        float last_y = 0.0f;
        std::uint32_t axis = 0U;
        bool started = false;
    };

    struct ControlGestureState {
        std::int32_t first_pointer_id = 0;
        std::int32_t second_pointer_id = 0;
        std::uint64_t owner_handle = 0U;
        std::uint32_t intent = 0U;
        std::uint32_t kind = 0U;
        float initial_distance = 1.0f;
        float last_midpoint_x = 0.0f;
        float last_midpoint_y = 0.0f;
        bool first_moved = false;
        bool second_moved = false;
        bool started = false;
        bool page_zoom_only = false;
        bool page_zoom_started = false;
    };

    struct LongPressState {
        std::int32_t pointer_id = 0;
        std::uint64_t owner_handle = 0U;
        float start_x = 0.0f;
        float start_y = 0.0f;
        std::uint32_t pointer_type = 0U;
        std::uint32_t modifiers = 0U;
        std::int32_t duration_ms = 500;
        float movement_tolerance = 10.0f;
        double deadline_ms = 0.0;
        bool fired = false;
        bool handled = false;
        bool continues_pointer_events = false;
    };

    void BeginContact(const NativePointerContactInput& input, std::uint64_t hit_handle);
    bool MoveContact(const NativePointerContactInput& input, bool pointer_handled);
    bool EndContact(const NativePointerContactInput& input, bool cancelled);
    void ResolveControlGesture();
    bool UpdateControlGesture(std::int32_t moved_pointer_id);
    bool FinishControlGesture(bool cancelled);
    void CancelLongPress(std::optional<std::int32_t> pointer_id = std::nullopt);
    bool FireLongPress(double timestamp_ms);
    static float Distance(float first_x, float first_y, float second_x, float second_y);

    NativeTouchGestureCallbacks callbacks_;
    std::unordered_map<std::int32_t, Contact> contacts_;
    std::optional<ScrollState> scroll_;
    std::optional<ControlGestureState> control_gesture_;
    std::optional<LongPressState> long_press_;
};

} // namespace effindom::v2::native
