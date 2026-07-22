#pragma once

#include <cstdint>

namespace effindom::v2::native {

namespace detail {

inline constexpr float WheelDeltaToLogicalPixels(float delta) {
    return delta * 96.0f;
}

} // namespace detail

struct NativePointerMetadata {
    std::uint32_t event_type = 0U;
    std::uint64_t handle = 0U;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t modifiers = 0U;
    std::int32_t pointer_id = 1;
    std::uint32_t pointer_type = 0U;
    std::int32_t button = -1;
    std::uint32_t buttons = 0U;
    float pressure = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    std::int32_t click_count = 1;
};

struct NativePointerInput {
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;
    std::int32_t button = -1;
    std::uint32_t buttons = 0U;
    std::int32_t click_count = 1;
    std::uint32_t modifiers = 0U;
    double timestamp_ms = 0.0;
};

struct NativePointerMoveInput {
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t buttons = 0U;
    std::uint32_t modifiers = 0U;
    double timestamp_ms = 0.0;
};

} // namespace effindom::v2::native
