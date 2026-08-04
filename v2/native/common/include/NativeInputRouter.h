#pragma once

#include "NativeContextMenuCoordinator.h"
#include "NativeInputTypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace effindom::v2 {
class Engine;
}

namespace effindom::v2::native {

class NativePageZoomController;

struct NativeInputRouterOptions {
    bool control_click_as_secondary = false;
    bool cancel_pointer_on_focus_lost = true;
    bool default_buttons_follow_button = false;
};

class NativeInputRouter final : private NativeContextMenuGateway {
public:
    NativeInputRouter(Engine& engine, NativeInputRouterOptions options,
        const NativePageZoomController* page_zoom = nullptr);

    bool DispatchPointer(const NativePointerInput& input);
    void DispatchPointerMove(const NativePointerMoveInput& input);
    bool DispatchPointerContact(std::uint32_t event_type, const NativePointerContactInput& input);
    bool DispatchWheel(const NativeWheelInput& input);
    bool DispatchPreciseWheel(const NativeWheelInput& input);
    void DispatchKey(const std::string& key, bool down, std::uint32_t modifiers,
        double timestamp_ms, bool text_input_active = false);
    bool DispatchTextComposition(const NativeTextCompositionInput& input);
    void HandleWindowFocusLost(double timestamp_ms);

    void Capture(std::uint64_t handle);
    void ReleaseCapture();
    void CancelPointer(double timestamp_ms);
    bool ConsumeCommitRequest();
    const NativePointerMetadata& PointerMetadata() const;
    std::uint64_t HitTestAt(float x, float y) const;
    NativePointerMoveInput ProjectToScene(float x, float y) const;
    float ProjectLengthToScene(float length) const;
    std::optional<NativeTextInputTarget> FocusedTextInputTarget() const;

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
    bool BeginTextComposition(const NativeTextCompositionInput& input);
    void ClearTextComposition();

    struct TextCompositionState {
        bool active = false;
        std::uint64_t handle = 0U;
        std::string original_text;
        std::uint32_t replacement_start = 0U;
        std::uint32_t replacement_end = 0U;
    };

    Engine& engine_;
    const NativePageZoomController* page_zoom_;
    NativeInputRouterOptions options_;
    NativeContextMenuCoordinator context_menu_coordinator_;
    NativePointerMetadata pointer_metadata_{};
    std::uint64_t captured_handle_ = 0U;
    bool commit_requested_ = false;
    bool precise_wheel_runtime_active_ = false;
    TextCompositionState text_composition_{};
};

} // namespace effindom::v2::native
