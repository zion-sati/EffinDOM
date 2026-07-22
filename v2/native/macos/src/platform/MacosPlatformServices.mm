#include "MacosPlatformServices.h"
#include "MacosAssetEnvironment.h"

#include "Engine.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"

#import <AppKit/NSWorkspace.h>
#import <Foundation/NSURL.h>

#include <algorithm>
#include <cctype>

namespace effindom::v2::native {
namespace {

NSString* PathString(const std::filesystem::path& path) {
    const std::string utf8 = path.string();
    return [NSString stringWithUTF8String:utf8.c_str()];
}

} // namespace

MacosPlatformServices::MacosPlatformServices(
    Engine& engine,
    std::function<void()> request_frame,
    std::function<void(std::uint64_t)> announce,
    bool allow_external_launch)
    : engine_(engine),
      request_frame_(std::move(request_frame)),
      announce_(std::move(announce)),
      allow_external_launch_(allow_external_launch),
      assets_(engine_, request_frame_, CreateMacosAssetEnvironment()) {}

MacosPlatformServices::~MacosPlatformServices() {
    if (cursor_ != nullptr) SDL_DestroyCursor(cursor_);
}

void MacosPlatformServices::WriteClipboard(std::string_view plain_text, std::string_view) {
    SetClipboardText(plain_text);
}

void MacosPlatformServices::RequestClipboardRead(std::uint64_t handle) {
    const std::string text = ClipboardText();
    ui_on_paste_text(
        handle,
        reinterpret_cast<const std::uint8_t*>(text.data()),
        static_cast<std::uint32_t>(text.size()));
    request_frame_();
}

void MacosPlatformServices::RequestFontLoad(std::uint32_t font_id, std::string_view url) {
    assets_.LoadFont(font_id, url);
}

void MacosPlatformServices::ReportMissingFontCoverage(
    std::uint32_t font_id,
    std::uint32_t coverage_kind,
    std::string_view sample_text) {
    assets_.QueueMissingFontCoverage(font_id, coverage_kind, sample_text);
}
void MacosPlatformServices::RequestSemanticAnnouncement(std::uint64_t handle) {
    if (announce_) announce_(handle);
}

bool MacosPlatformServices::SetClipboardText(std::string_view text) {
    return SDL_SetClipboardText(std::string(text).c_str());
}

std::string MacosPlatformServices::ClipboardText() const {
    char* text = SDL_GetClipboardText();
    if (text == nullptr) return {};
    std::string result(text);
    SDL_free(text);
    return result;
}

bool MacosPlatformServices::OpenExternalUrl(std::string_view url) const {
    if (!IsSupportedExternalUrl(url)) return false;
    return !allow_external_launch_ || SDL_OpenURL(std::string(url).c_str());
}

bool MacosPlatformServices::OpenFile(const std::filesystem::path& path) const {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    if (!allow_external_launch_) return true;
    NSString* native_path = PathString(path);
    return native_path != nil && [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:native_path]];
}

bool MacosPlatformServices::RevealFile(const std::filesystem::path& path) const {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    if (!allow_external_launch_) return true;
    NSString* native_path = PathString(path);
    if (native_path == nil) return false;
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:native_path]]];
    return true;
}

void MacosPlatformServices::SetCursor(std::uint32_t style) {
    if (cursor_ != nullptr) SDL_DestroyCursor(cursor_);
    const SDL_SystemCursor cursor = style == 1U ? SDL_SYSTEM_CURSOR_POINTER
        : style == 2U ? SDL_SYSTEM_CURSOR_TEXT
        : style >= 3U && style <= 5U ? SDL_SYSTEM_CURSOR_MOVE
        : style == 6U ? SDL_SYSTEM_CURSOR_NS_RESIZE
        : style == 7U ? SDL_SYSTEM_CURSOR_EW_RESIZE
        : SDL_SYSTEM_CURSOR_DEFAULT;
    cursor_ = SDL_CreateSystemCursor(cursor);
    SDL_SetCursor(cursor_);
}

bool MacosPlatformServices::LoadDefaultFont(std::uint32_t font_id, const char* name) {
    return assets_.LoadDefaultFont(font_id, name);
}

bool MacosPlatformServices::LoadFont(std::uint32_t font_id, const std::filesystem::path& path) {
    return assets_.LoadFont(font_id, path);
}

bool MacosPlatformServices::LoadSvg(std::uint32_t svg_id, std::string_view source) {
    return assets_.LoadSvg(svg_id, source);
}

bool MacosPlatformServices::LoadTexture(std::uint32_t texture_id, std::string_view source) {
    return assets_.LoadTexture(texture_id, source);
}

void MacosPlatformServices::ReleaseSvg(std::uint32_t svg_id) { assets_.ReleaseSvg(svg_id); }
void MacosPlatformServices::ReleaseTexture(std::uint32_t texture_id) { assets_.ReleaseTexture(texture_id); }
bool MacosPlatformServices::ProcessPendingAssets() { return assets_.ProcessPendingFontCoverage(); }
std::size_t MacosPlatformServices::FallbackFontCountForTesting() const {
    return assets_.FallbackFontCountForTesting();
}

bool MacosPlatformServices::IsSupportedExternalUrl(std::string_view url) {
    const auto separator = url.find(':');
    if (separator == std::string_view::npos) return false;
    std::string scheme(url.substr(0, separator));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return scheme == "http" || scheme == "https";
}

} // namespace effindom::v2::native
