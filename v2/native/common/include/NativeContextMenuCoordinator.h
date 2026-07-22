#pragma once

#include <cstdint>

namespace effindom::v2::native {

enum class NativePointerPhase : std::uint32_t {
    Down,
    Up,
    Cancel,
};

struct NativeSecondaryPointerEvent {
    NativePointerPhase phase = NativePointerPhase::Cancel;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t modifiers = 0U;
    std::uint32_t buttons = 0U;
    std::int32_t pointer_id = 1;
    std::uint32_t pointer_type = 0U;
    float pressure = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    std::int32_t click_count = 1;
    double timestamp_ms = 0.0;
};

struct NativeContextMenuDispatchResult {
    bool raw_event_handled = false;
    bool fallback_shown = false;
};

class NativeContextMenuGateway {
public:
    virtual ~NativeContextMenuGateway() = default;

    virtual bool DispatchRawSecondaryPointer(const NativeSecondaryPointerEvent& event) = 0;
    virtual void HideActiveContextMenu() = 0;
    virtual void FlushRetainedChanges() = 0;
    virtual std::uint64_t HitTest(float x, float y) const = 0;
    virtual bool CanShowContextMenu(std::uint64_t handle) const = 0;
    virtual void ShowContextMenu(std::uint64_t handle, float x, float y) = 0;
    virtual void RequestFrame() = 0;
};

class NativeContextMenuCoordinator final {
public:
    explicit NativeContextMenuCoordinator(NativeContextMenuGateway& gateway);

    NativeContextMenuDispatchResult Dispatch(const NativeSecondaryPointerEvent& event);
    bool ShowAt(float x, float y);
    void ResetGesture();

private:
    NativeContextMenuGateway& gateway_;
    bool secondary_gesture_active_ = false;
    bool secondary_gesture_handled_ = false;
};

} // namespace effindom::v2::native
