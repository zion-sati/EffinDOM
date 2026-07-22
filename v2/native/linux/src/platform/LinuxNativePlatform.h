#pragma once

#include "NativePlatformHost.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace effindom::v2::native {

class LinuxNativePlatform final : public NativePlatformHost {
public:
    struct Impl;

    explicit LinuxNativePlatform(bool visible = true);
    ~LinuxNativePlatform() override;

    LinuxNativePlatform(const LinuxNativePlatform&) = delete;
    LinuxNativePlatform& operator=(const LinuxNativePlatform&) = delete;

    NativeHostCore& Core() override;
    const NativeHostCore& Core() const override;
    bool PumpEvent(bool wait_when_idle) override;
    bool ShouldPresentAfterLastEvent() const override;
    void Resize(std::uint32_t logical_width, std::uint32_t logical_height) override;
    void RecreateGraphicsSurface() override;
    void DispatchWindowFocusLost() override;
    std::uint32_t CurrentPointerButtons() const override;
    std::uint32_t CurrentModifiers() const override;
    bool PostUiDispatch(std::uint64_t callback_id) override;
    bool CancelUiDispatch(std::uint64_t callback_id) override;
    void SetClipboardText(const std::string& text) override;
    std::string ClipboardText() const override;
    void RequestClipboardRead(std::uint64_t handle) override;
    bool OpenExternalUrl(const std::string& url) const override;
    bool OpenFile(const std::filesystem::path& path) const override;
    bool RevealFile(const std::filesystem::path& path) const override;
    bool ShowFileDialog(
        std::uint32_t kind,
        std::uint64_t request_id,
        const std::string& filters,
        const std::string& default_location,
        bool allow_multiple) override;
    bool IsDarkMode() const override;
    std::uint32_t AccentColor() const override;
    std::uint32_t PlatformFamily() const override;
    std::uint32_t HostCapabilities() const override;
    bool IsCoarsePointer() const override;
    void SetApplicationCaption(const std::string& caption) override;
    void SetNativePointerCapture(bool captured) override;
    void SetCursor(std::uint32_t style) override;
    void RequestFontLoad(std::uint32_t font_id, const std::string& source) override;
    void LoadSvg(std::uint32_t svg_id, const std::string& source) override;
    void ReleaseSvg(std::uint32_t svg_id) override;
    void LoadTexture(std::uint32_t texture_id, const std::string& source) override;
    void ReleaseTexture(std::uint32_t texture_id) override;
    void CompleteFileDialogForTesting(
        std::uint64_t request_id,
        std::uint32_t status,
        std::vector<std::string> paths = {},
        std::string error = {},
        std::int32_t selected_filter = -1) override;
    void DispatchDropEventForTesting(
        std::uint32_t event_type,
        float x,
        float y,
        const std::string& data = {}) override;
    std::size_t FallbackFontCountForTesting() const override;
    void RequestMissingFontCoverageForTesting(
        std::uint32_t primary_font_id,
        std::uint32_t coverage_kind,
        const std::string& sample_text) override;

private:
    void RequestFrame();
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
