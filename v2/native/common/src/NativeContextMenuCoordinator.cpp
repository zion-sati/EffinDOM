#include "NativeContextMenuCoordinator.h"

namespace effindom::v2::native {

NativeContextMenuCoordinator::NativeContextMenuCoordinator(NativeContextMenuGateway& gateway)
    : gateway_(gateway) {}

NativeContextMenuDispatchResult NativeContextMenuCoordinator::Dispatch(
    const NativeSecondaryPointerEvent& event) {
    const bool handled = gateway_.DispatchRawSecondaryPointer(event);

    switch (event.phase) {
        case NativePointerPhase::Down:
            secondary_gesture_active_ = true;
            secondary_gesture_handled_ = handled;
            return NativeContextMenuDispatchResult{handled, false};
        case NativePointerPhase::Cancel:
            ResetGesture();
            return NativeContextMenuDispatchResult{handled, false};
        case NativePointerPhase::Up: {
            const bool suppress_fallback = handled ||
                (secondary_gesture_active_ && secondary_gesture_handled_);
            ResetGesture();
            return NativeContextMenuDispatchResult{
                handled,
                !suppress_fallback && ShowAt(event.x, event.y),
            };
        }
    }
    return NativeContextMenuDispatchResult{handled, false};
}

bool NativeContextMenuCoordinator::ShowAt(float x, float y) {
    gateway_.HideActiveContextMenu();
    gateway_.FlushRetainedChanges();
    const std::uint64_t handle = gateway_.HitTest(x, y);
    const bool can_show = handle != 0U && gateway_.CanShowContextMenu(handle);
    if (can_show) {
        gateway_.ShowContextMenu(handle, x, y);
    }
    gateway_.RequestFrame();
    return can_show;
}

void NativeContextMenuCoordinator::ResetGesture() {
    secondary_gesture_active_ = false;
    secondary_gesture_handled_ = false;
}

} // namespace effindom::v2::native
