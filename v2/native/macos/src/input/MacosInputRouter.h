#pragma once

#include <cstdint>
#include <string>

union SDL_Event;

namespace effindom::v2 {
class Engine;
}

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

class MacosInputRouter final {
public:
    explicit MacosInputRouter(Engine& engine);

    bool HandleEvent(const SDL_Event& event, double timestamp_ms);
    void DispatchPointer(
        float x,
        float y,
        bool down,
        std::int32_t button,
        std::uint32_t buttons,
        std::int32_t click_count,
        double timestamp_ms);
    void DispatchPointerMove(float x, float y, std::uint32_t modifiers, double timestamp_ms);
    void DispatchWheel(float delta_x, float delta_y, double timestamp_ms);
    void DispatchPreciseWheel(
        float delta_x,
        float delta_y,
        bool begins_gesture,
        bool ends_gesture,
        double timestamp_ms);
    void DispatchKey(const std::string& key, bool down, std::uint32_t modifiers, double timestamp_ms);

    void Capture(std::uint64_t handle);
    void ReleaseCapture();
    bool ConsumeCommitRequest();
    const NativePointerMetadata& PointerMetadata() const;

private:
    std::uint64_t ResolvePointerTarget(float x, float y) const;

    Engine& engine_;
    NativePointerMetadata pointer_metadata_{};
    std::uint64_t captured_handle_ = 0U;
    bool commit_requested_ = false;
};

} // namespace effindom::v2::native
