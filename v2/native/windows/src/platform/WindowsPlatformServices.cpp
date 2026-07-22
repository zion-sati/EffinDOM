#include "WindowsPlatformServices.h"

#include "Engine.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"

#include <windows.h>
#include <dwrite.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace effindom::v2::native {
namespace {

using Microsoft::WRL::ComPtr;

std::filesystem::path ExecutableDirectory() {
    std::vector<wchar_t> buffer(512U);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0U) return {};
        if (length < buffer.size() - 1U) return std::filesystem::path(buffer.data()).parent_path();
        buffer.resize(buffer.size() * 2U);
    }
}

std::filesystem::path PathFromUtf8(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), length) != length) {
        return {};
    }
    return std::filesystem::path(wide);
}

std::uint32_t FirstScalar(std::string_view text) {
    if (text.empty()) return 0U;
    const auto first = static_cast<std::uint8_t>(text[0]);
    if ((first & 0xF8U) == 0xF0U && text.size() >= 4U) {
        return ((first & 0x07U) << 18U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 12U) |
            ((static_cast<std::uint8_t>(text[2]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[3]) & 0x3FU);
    }
    if ((first & 0xF0U) == 0xE0U && text.size() >= 3U) {
        return ((first & 0x0FU) << 12U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[2]) & 0x3FU);
    }
    if ((first & 0xE0U) == 0xC0U && text.size() >= 2U) {
        return ((first & 0x1FU) << 6U) | (static_cast<std::uint8_t>(text[1]) & 0x3FU);
    }
    return first;
}

NativeSystemFontSource FindSystemFontForScalar(std::uint32_t scalar) {
    if (scalar == 0U) return {};
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    ComPtr<IDWriteFactory> factory;
    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &factory)) ||
        FAILED(factory->GetSystemFontCollection(&collection, FALSE))) {
        if (uninitialize) CoUninitialize();
        return {};
    }
    const UINT32 family_count = collection->GetFontFamilyCount();
    for (UINT32 family_index = 0U; family_index < family_count; ++family_index) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(family_index, &family))) continue;
        const UINT32 font_count = family->GetFontCount();
        for (UINT32 font_index = 0U; font_index < font_count; ++font_index) {
            ComPtr<IDWriteFont> font;
            ComPtr<IDWriteFontFace> face;
            if (FAILED(family->GetFont(font_index, &font)) || font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE ||
                FAILED(font->CreateFontFace(&face))) continue;
            UINT16 glyph = 0U;
            if (FAILED(face->GetGlyphIndices(&scalar, 1U, &glyph)) || glyph == 0U) continue;
            UINT32 file_count = 0U;
            if (FAILED(face->GetFiles(&file_count, nullptr)) || file_count == 0U) continue;
            std::vector<IDWriteFontFile*> raw_files(file_count, nullptr);
            if (FAILED(face->GetFiles(&file_count, raw_files.data()))) continue;
            ComPtr<IDWriteFontFile> file;
            file.Attach(raw_files[0]);
            for (UINT32 index = 1U; index < file_count; ++index) if (raw_files[index] != nullptr) raw_files[index]->Release();
            const void* key = nullptr;
            UINT32 key_size = 0U;
            ComPtr<IDWriteFontFileLoader> loader;
            ComPtr<IDWriteLocalFontFileLoader> local_loader;
            if (FAILED(file->GetReferenceKey(&key, &key_size)) || FAILED(file->GetLoader(&loader)) ||
                FAILED(loader.As(&local_loader))) continue;
            UINT32 path_length = 0U;
            if (FAILED(local_loader->GetFilePathLengthFromKey(key, key_size, &path_length))) continue;
            std::wstring path(path_length + 1U, L'\0');
            if (FAILED(local_loader->GetFilePathFromKey(key, key_size, path.data(), path_length + 1U))) continue;
            path.resize(path_length);
            if (uninitialize) CoUninitialize();
            return {std::filesystem::path(path), {}, scalar};
        }
    }
    if (uninitialize) CoUninitialize();
    return {};
}

NativeAssetEnvironment CreateWindowsAssetEnvironment() {
    NativeAssetEnvironment environment;
    const std::filesystem::path executable = ExecutableDirectory();
    if (!executable.empty()) {
        environment.search_roots.push_back(executable);
        environment.search_roots.push_back(executable.parent_path() / "resources");
        environment.search_roots.push_back(executable.parent_path() / "resources" / "effindom");
    }
    std::error_code error;
    environment.search_roots.push_back(std::filesystem::current_path(error));
    environment.path_from_utf8 = [](std::string_view value) {
        std::string normalized(value);
        if (normalized.size() >= 3U && normalized[0] == '/' && normalized[2] == ':') {
            normalized.erase(normalized.begin());
        }
        return PathFromUtf8(normalized);
    };
    environment.resolve_system_font = [](std::string_view text) {
        return FindSystemFontForScalar(FirstScalar(text));
    };
    environment.use_symbol_font_for_non_emoji_supplemental = false;
    return environment;
}

} // namespace

WindowsPlatformServices::WindowsPlatformServices(
    Engine& engine,
    std::function<void()> request_frame,
    std::function<void(std::uint64_t)> announce,
    bool allow_external_launch)
    : engine_(engine),
      request_frame_(std::move(request_frame)),
      announce_(std::move(announce)),
      allow_external_launch_(allow_external_launch),
      assets_(engine_, request_frame_, CreateWindowsAssetEnvironment()) {}

WindowsPlatformServices::~WindowsPlatformServices() {
    if (cursor_ != nullptr) SDL_DestroyCursor(cursor_);
}

void WindowsPlatformServices::WriteClipboard(std::string_view plain_text, std::string_view) {
    SetClipboardText(plain_text);
}

void WindowsPlatformServices::RequestClipboardRead(std::uint64_t handle) {
    const std::string text = ClipboardText();
    ui_on_paste_text(handle, reinterpret_cast<const std::uint8_t*>(text.data()), static_cast<std::uint32_t>(text.size()));
    request_frame_();
}

void WindowsPlatformServices::RequestFontLoad(std::uint32_t font_id, std::string_view url) {
    assets_.LoadFont(font_id, url);
}

void WindowsPlatformServices::ReportMissingFontCoverage(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    std::string_view sample_text) {
    assets_.QueueMissingFontCoverage(primary_font_id, coverage_kind, sample_text);
}
void WindowsPlatformServices::RequestSemanticAnnouncement(std::uint64_t handle) {
    if (announce_) announce_(handle);
}

bool WindowsPlatformServices::SetClipboardText(std::string_view text) {
    return SDL_SetClipboardText(std::string(text).c_str());
}

std::string WindowsPlatformServices::ClipboardText() const {
    char* value = SDL_GetClipboardText();
    if (value == nullptr) return {};
    std::string result(value);
    SDL_free(value);
    return result;
}

bool WindowsPlatformServices::OpenExternalUrl(std::string_view url) const {
    if (!IsSupportedExternalUrl(url)) return false;
    return !allow_external_launch_ || SDL_OpenURL(std::string(url).c_str());
}

bool WindowsPlatformServices::OpenFile(const std::filesystem::path& path) const {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    if (!allow_external_launch_) return true;
    return reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool WindowsPlatformServices::RevealFile(const std::filesystem::path& path) const {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    if (!allow_external_launch_) return true;
    const std::wstring arguments = L"/select,\"" + path.wstring() + L"\"";
    return reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
}

void WindowsPlatformServices::SetCursor(std::uint32_t style) {
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

bool WindowsPlatformServices::LoadDefaultFont(std::uint32_t font_id, const char* name) {
    return assets_.LoadDefaultFont(font_id, name);
}

bool WindowsPlatformServices::LoadFont(std::uint32_t font_id, const std::filesystem::path& path) {
    return assets_.LoadFont(font_id, path);
}

bool WindowsPlatformServices::LoadSvg(std::uint32_t svg_id, std::string_view source) {
    return assets_.LoadSvg(svg_id, source);
}

bool WindowsPlatformServices::LoadTexture(std::uint32_t texture_id, std::string_view source) {
    return assets_.LoadTexture(texture_id, source);
}

void WindowsPlatformServices::ReleaseSvg(std::uint32_t svg_id) { assets_.ReleaseSvg(svg_id); }
void WindowsPlatformServices::ReleaseTexture(std::uint32_t texture_id) { assets_.ReleaseTexture(texture_id); }
bool WindowsPlatformServices::ProcessPendingAssets() { return assets_.ProcessPendingFontCoverage(); }
std::size_t WindowsPlatformServices::FallbackFontCountForTesting() const {
    return assets_.FallbackFontCountForTesting();
}

bool WindowsPlatformServices::IsSupportedExternalUrl(std::string_view url) {
    const std::size_t separator = url.find(':');
    if (separator == std::string_view::npos) return false;
    std::string scheme(url.substr(0U, separator));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return scheme == "http" || scheme == "https";
}

} // namespace effindom::v2::native
