#pragma once

#include "NativeGraphicsCoordinator.h"
#include "NativeAccessibility.h"
#include "NativeHostState.h"
#include "NativeInputRouter.h"

#include "Engine.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace effindom::v2::native {

struct NativeHostCoreCallbacks {
    std::function<bool()> process_pending_assets;
    std::function<void()> clear_application_state;
};

class NativeHostCore final {
public:
    NativeHostCore(NativeInputRouterOptions input_options,
        NativeHostCoreCallbacks callbacks);

    void AttachGraphics(std::unique_ptr<NativeGraphicsCoordinator> graphics);
    void AttachAccessibility(std::unique_ptr<NativeAccessibilityAdapter> adapter);
    void ReleaseGraphics();
    void InitializeEngine();
    void MountApplication();
    void UnmountApplication();

    void RequestFrame();
    void SetSystemDarkMode(bool dark_mode);
    bool RunNextFrame();
    void DrainFrames(std::uint32_t maximum_frames);
    void ApplyManagedCommittedCommands();

    double NowMilliseconds() const;
    bool RefreshWindowGeometry();
    NativeHostState State() const;
    std::vector<std::uint8_t> SnapshotRgba() const;
    bool WriteScreenshot(const std::filesystem::path& path, std::string& error) const;

    Engine& GetEngine();
    const Engine& GetEngine() const;
    NativeInputRouter& InputRouter();
    const NativeInputRouter& InputRouter() const;
    NativeGraphicsCoordinator& Graphics();
    const NativeGraphicsCoordinator& Graphics() const;
    NativeAccessibilityCoordinator& Accessibility();
    const NativeAccessibilityCoordinator& Accessibility() const;
    void AnnounceSemantic(std::uint64_t handle);

    float LogicalWidth() const;
    float LogicalHeight() const;
    float PixelDensity() const;
    std::uint32_t PhysicalWidth() const;
    std::uint32_t PhysicalHeight() const;
    std::uint64_t FrameCount() const;
    bool IsFramePending() const;
    void CancelPendingFrame();
    bool IsMounted() const;
    bool IsRunning() const;
    void Stop();

private:
    using Clock = std::chrono::steady_clock;

    void RefreshSurface();
    void ApplyCommittedCommands();

    Engine engine_{};
    NativeInputRouter input_router_;
    NativeAccessibilityCoordinator accessibility_;
    NativeHostCoreCallbacks callbacks_;
    std::unique_ptr<NativeGraphicsCoordinator> graphics_;
    Clock::time_point start_time_;
    float logical_width_ = 0.0f;
    float logical_height_ = 0.0f;
    float pixel_density_ = 1.0f;
    std::uint32_t physical_width_ = 0U;
    std::uint32_t physical_height_ = 0U;
    std::uint64_t frame_count_ = 0U;
    std::uint32_t mount_count_ = 0U;
    std::uint32_t dispose_count_ = 0U;
    bool frame_pending_ = false;
    bool mounted_ = false;
    bool running_ = true;
    bool managed_commit_applied_ = false;
    std::uint32_t backdrop_color_ = 0xFFFFFFFFU;
};

} // namespace effindom::v2::native
