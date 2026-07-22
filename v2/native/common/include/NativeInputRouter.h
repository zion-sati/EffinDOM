#pragma once

#include "NativeContextMenuCoordinator.h"
#include "NativeInputTypes.h"

#include <cstdint>
#include <string>

namespace effindom::v2 {
class Engine;
}

namespace effindom::v2::native {

struct NativeInputRouterOptions {
    bool control_click_as_secondary = false;
    bool cancel_pointer_on_focus_lost = true;
    bool default_buttons_follow_button = false;
};

class NativeInputRouter final : private NativeContextMenuGateway {
public:
    NativeInputRouter(Engine& engine, NativeInputRouterOptions options);

    bool DispatchPointer(const NativePointerInput& input);
    void DispatchPointerMove(const NativePointerMoveInput& input);
    void DispatchWheel(float delta_x, float delta_y, double timestamp_ms);
    void DispatchPreciseWheel(float delta_x, float delta_y, bool begins_gesture,
        bool ends_gesture, double timestamp_ms);
    void DispatchKey(const std::string& key, bool down, std::uint32_t modifiers,
        double timestamp_ms);
    void HandleWindowFocusLost(double timestamp_ms);

    void Capture(std::uint64_t handle);
    void ReleaseCapture();
    void CancelPointer(double timestamp_ms);
    bool ConsumeCommitRequest();
    const NativePointerMetadata& PointerMetadata() const;

private:
    bool DispatchRawPointer(const NativePointerInput& input);
    bool DispatchRawSecondaryPointer(const NativeSecondaryPointerEvent& event) override;
    void HideActiveContextMenu() override;
    void FlushRetainedChanges() override;
    std::uint64_t HitTest(float x, float y) const override;
    bool CanShowContextMenu(std::uint64_t handle) const override;
    void ShowContextMenu(std::uint64_t handle, float x, float y) override;
    void RequestFrame() override;
    bool ShowContextMenuForFocusedControl();
    std::uint64_t ResolvePointerTarget(float x, float y) const;

    Engine& engine_;
    NativeInputRouterOptions options_;
    NativeContextMenuCoordinator context_menu_coordinator_;
    NativePointerMetadata pointer_metadata_{};
    std::uint64_t captured_handle_ = 0U;
    bool commit_requested_ = false;
};

} // namespace effindom::v2::native
