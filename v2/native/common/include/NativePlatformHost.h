#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace effindom::v2::native {

class NativeHostCore;

class NativePlatformHost {
public:
    virtual ~NativePlatformHost() = default;

    virtual NativeHostCore& Core() = 0;
    virtual const NativeHostCore& Core() const = 0;
    virtual bool PumpEvent(bool wait_when_idle) = 0;
    virtual bool ShouldPresentAfterLastEvent() const { return false; }
    virtual void Resize(std::uint32_t logical_width, std::uint32_t logical_height) = 0;
    virtual void RecreateGraphicsSurface() = 0;
    virtual void DispatchWindowFocusLost() = 0;
    virtual std::uint32_t CurrentPointerButtons() const = 0;
    virtual std::uint32_t CurrentModifiers() const = 0;
    virtual bool PostUiDispatch(std::uint64_t callback_id) = 0;
    virtual bool CancelUiDispatch(std::uint64_t callback_id) = 0;
    virtual void SetClipboardText(const std::string& text) = 0;
    virtual std::string ClipboardText() const = 0;
    virtual void RequestClipboardRead(std::uint64_t handle) = 0;
    virtual bool OpenExternalUrl(const std::string& url) const = 0;
    virtual bool OpenFile(const std::filesystem::path& path) const = 0;
    virtual bool RevealFile(const std::filesystem::path& path) const = 0;
    virtual bool ShowFileDialog(
        std::uint32_t kind,
        std::uint64_t request_id,
        const std::string& filters,
        const std::string& default_location,
        bool allow_multiple) = 0;
    virtual bool IsDarkMode() const = 0;
    virtual std::uint32_t AccentColor() const = 0;
    virtual std::uint32_t PlatformFamily() const = 0;
    virtual std::uint32_t HostCapabilities() const = 0;
    virtual bool IsCoarsePointer() const = 0;
    virtual void SetApplicationCaption(const std::string& caption) = 0;
    virtual void SetNativePointerCapture(bool captured) = 0;
    virtual void SetCursor(std::uint32_t style) = 0;
    virtual void RequestFontLoad(std::uint32_t font_id, const std::string& source) = 0;
    virtual void LoadSvg(std::uint32_t svg_id, const std::string& source) = 0;
    virtual void ReleaseSvg(std::uint32_t svg_id) = 0;
    virtual void LoadTexture(std::uint32_t texture_id, const std::string& source) = 0;
    virtual void ReleaseTexture(std::uint32_t texture_id) = 0;
    virtual void CompleteFileDialogForTesting(
        std::uint64_t request_id,
        std::uint32_t status,
        std::vector<std::string> paths,
        std::string error,
        std::int32_t selected_filter) = 0;
    virtual void DispatchDropEventForTesting(
        std::uint32_t event_type,
        float x,
        float y,
        const std::string& data) = 0;
    virtual std::size_t FallbackFontCountForTesting() const = 0;
    virtual void RequestMissingFontCoverageForTesting(
        std::uint32_t primary_font_id,
        std::uint32_t coverage_kind,
        const std::string& sample_text) = 0;
};

} // namespace effindom::v2::native
