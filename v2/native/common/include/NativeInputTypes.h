#pragma once

#include <cstdint>
#include <string>

namespace effindom::v2::native {

namespace detail {

inline constexpr float WheelDeltaToLogicalPixels(float delta) {
    // Match the browser bridge's DOM_DELTA_LINE normalization so one coarse
    // wheel step travels the same logical distance on web and native hosts.
    return delta * 16.0f;
}

} // namespace detail

enum class NativeWheelDeltaMode : std::uint32_t {
    Pixel = 0U,
    Line = 1U,
    Page = 2U,
};

struct NativeWheelInput {
    std::uint64_t handle = 0U;
    float x = 0.0f;
    float y = 0.0f;
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    NativeWheelDeltaMode delta_mode = NativeWheelDeltaMode::Pixel;
    std::uint32_t modifiers = 0U;
    bool precise = false;
    bool begins_gesture = false;
    bool ends_gesture = false;
    double timestamp_ms = 0.0;
};

struct NativeFrameInput {
    double timestamp_ms = 0.0;
};

enum class NativeTextCompositionPhase : std::uint32_t {
    Start = 0U,
    Update = 1U,
    Commit = 2U,
    Cancel = 3U,
};

inline constexpr std::uint32_t kNativeTextRangeUnspecified = 0xFFFFFFFFU;

struct NativeTextCompositionInput {
    NativeTextCompositionPhase phase = NativeTextCompositionPhase::Start;
    std::string text;
    std::uint32_t replacement_start = kNativeTextRangeUnspecified;
    std::uint32_t replacement_end = kNativeTextRangeUnspecified;
    std::uint32_t selection_start = kNativeTextRangeUnspecified;
    std::uint32_t selection_end = kNativeTextRangeUnspecified;
    double timestamp_ms = 0.0;
};

struct NativeTextInputTarget {
    std::uint64_t handle = 0U;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct NativePointerContactInput {
    float x = 0.0f;
    float y = 0.0f;
    std::int32_t pointer_id = 0;
    std::uint32_t pointer_type = 0U;
    bool primary = false;
    std::int32_t button = -1;
    std::uint32_t buttons = 0U;
    float pressure = 0.0f;
    float tangential_pressure = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
    float twist = 0.0f;
    std::uint32_t modifiers = 0U;
    double timestamp_ms = 0.0;
};

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
    bool primary = true;
    float tangential_pressure = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
    float twist = 0.0f;
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
