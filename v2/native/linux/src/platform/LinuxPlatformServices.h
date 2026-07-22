#pragma once

#include "UiPlatformHost.h"
#include "NativeAssetService.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Cursor;

namespace effindom::v2 {
class Engine;
namespace native {

class LinuxPlatformServices final : public ui::UiPlatformHost {
public:
    LinuxPlatformServices(Engine& engine, std::function<void()> request_frame,
        std::function<void(std::uint64_t)> announce, bool allow_external_launch);
    ~LinuxPlatformServices() override;

    void WriteClipboard(std::string_view plain_text, std::string_view rich_json) override;
    void RequestClipboardRead(std::uint64_t handle) override;
    void RequestFontLoad(std::uint32_t font_id, std::string_view url) override;
    void ReportMissingFontCoverage(std::uint32_t, std::uint32_t, std::string_view) override;
    void RequestSemanticAnnouncement(std::uint64_t) override;

    bool SetClipboardText(std::string_view text);
    std::string ClipboardText() const;
    bool OpenExternalUrl(std::string_view url) const;
    bool OpenFile(const std::filesystem::path& path) const;
    bool RevealFile(const std::filesystem::path& path) const;
    void SetCursor(std::uint32_t style);
    bool LoadDefaultFont(std::uint32_t font_id, const char* name);
    bool LoadFont(std::uint32_t font_id, const std::filesystem::path& path);
    bool LoadSvg(std::uint32_t svg_id, std::string_view source);
    bool LoadTexture(std::uint32_t texture_id, std::string_view source);
    void ReleaseSvg(std::uint32_t svg_id);
    void ReleaseTexture(std::uint32_t texture_id);
    bool ProcessPendingAssets();
    std::size_t FallbackFontCountForTesting() const;

private:
    static bool IsSupportedExternalUrl(std::string_view url);

    Engine& engine_;
    std::function<void()> request_frame_;
    std::function<void(std::uint64_t)> announce_;
    bool allow_external_launch_;
    SDL_Cursor* cursor_ = nullptr;
    NativeAssetService assets_;
};

} // namespace native
} // namespace effindom::v2
