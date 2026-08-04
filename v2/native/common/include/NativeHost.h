#pragma once

#include "NativeAccessibility.h"
#include "NativeHostState.h"
#include "NativePageZoomController.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace effindom::v2::native {

class NativePlatformHost;
class NativeWorkerHost;

class NativeHost final {
public:
    explicit NativeHost(bool visible = true);
    ~NativeHost();

    NativeHost(const NativeHost&) = delete;
    NativeHost& operator=(const NativeHost&) = delete;

    void MountApplication();
    void Unmount();
    void RequestFrame();
    bool RunNextFrame();
    void DrainFrames(std::uint32_t maximum_frames = 120U);
    bool PumpEvent(bool wait_when_idle);
    bool ShouldPresentAfterLastEvent() const;
    void WaitForAnimationFrame();
    void SetAnimationFrameActive(bool active);
    void Resize(std::uint32_t logical_width, std::uint32_t logical_height);
    void RecreateGraphicsSurface();
    void DispatchPointer(
        float x,
        float y,
        bool down,
        std::int32_t button = 0,
        std::uint32_t buttons = 0xFFFFFFFFU,
        std::int32_t click_count = 1);
    void DispatchPointerMove(
        float x,
        float y,
        std::uint32_t modifiers = 0U,
        std::uint32_t buttons = 0xFFFFFFFFU);
    void SetPointerCaptureForTesting(std::uint64_t handle);
    std::uint64_t LastPointerTargetForTesting() const;
    void DispatchWheel(float delta_x, float delta_y);
    void DispatchKey(const std::string& key, bool down, std::uint32_t modifiers = 0U);
    void DispatchWindowFocusLost();
    void SetSystemDarkModeForTesting(bool dark_mode);
    void SetClipboardText(const std::string& text);
    std::string ClipboardText() const;
    bool OpenExternalUrl(const std::string& url) const;
    bool OpenFile(const std::filesystem::path& path) const;
    bool RevealFile(const std::filesystem::path& path) const;
    void CompleteFileDialogForTesting(
        std::uint64_t request_id,
        std::uint32_t status,
        std::vector<std::string> paths = {},
        std::string error = {},
        std::int32_t selected_filter = -1);
    void DispatchDropEventForTesting(
        std::uint32_t event_type,
        float x,
        float y,
        const std::string& data = {});
    std::uint64_t HitTest(float x, float y) const;
    bool HasFontForTesting(std::uint32_t font_id) const;
    bool LoadFontForTesting(std::uint32_t font_id, const std::filesystem::path& path);
    bool FontHasGlyphForTesting(std::uint32_t font_id, std::uint32_t codepoint) const;
    std::optional<std::pair<float, float>> SvgSizeForTesting(std::uint32_t svg_id) const;
    void RegisterSvgForTesting(std::uint32_t svg_id, std::string_view source);
    std::optional<std::pair<std::uint32_t, std::uint32_t>> TextureSizeForTesting(std::uint32_t texture_id) const;
    std::size_t TextureCountForTesting() const;
    std::size_t PathCountForTesting() const;
    std::size_t OffscreenSurfaceCountForTesting() const;
    std::size_t FallbackFontCountForTesting() const;
    void RequestMissingFontCoverageForTesting(
        std::uint32_t primary_font_id,
        std::uint32_t coverage_kind,
        const std::string& sample_text);
    NativeHostState State() const;
    const NativeAccessibilitySnapshot& AccessibilitySnapshotForTesting() const;
    std::vector<std::uint8_t> SnapshotRgba() const;
    bool WriteScreenshot(const std::filesystem::path& path, std::string& error) const;
    bool IsIdle() const;
    bool IsRunning() const;
    bool IsPageZoomEnabledForTesting() const;
    bool SetPageZoomForTesting(float scale, float screen_x, float screen_y);
    NativePageZoomState PageZoomStateForTesting() const;

private:
    std::unique_ptr<NativePlatformHost> platform_;
    std::unique_ptr<NativeWorkerHost> worker_host_;
    std::uint64_t worker_session_generation_ = 0U;
    std::optional<bool> packaged_page_zoom_enabled_;
};

} // namespace effindom::v2::native
