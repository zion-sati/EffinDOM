#include "LinuxPlatformServices.h"

#include "LinuxAssetEnvironment.h"
#include "Engine.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"
#include <dbus/dbus.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace effindom::v2::native {
namespace {

std::string FileUri(const std::filesystem::path& input) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(input, error);
    if (error) return {};
    const std::string path = absolute.generic_string();
    std::ostringstream uri;
    uri << "file://";
    uri << std::uppercase << std::hex;
    for (const unsigned char character : path) {
        if (std::isalnum(character) || character == '/' || character == '-' ||
            character == '_' || character == '.' || character == '~') {
            uri << static_cast<char>(character);
        } else {
            uri << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(character);
        }
    }
    return uri.str();
}

bool RevealWithFileManager(const std::string& uri) {
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (connection == nullptr) {
        dbus_error_free(&error);
        return false;
    }
    DBusMessage* message = dbus_message_new_method_call(
        "org.freedesktop.FileManager1",
        "/org/freedesktop/FileManager1",
        "org.freedesktop.FileManager1",
        "ShowItems");
    if (message == nullptr) {
        dbus_connection_unref(connection);
        return false;
    }

    const char* uri_value = uri.c_str();
    const char* startup_id = "";
    DBusMessageIter arguments;
    DBusMessageIter items;
    dbus_message_iter_init_append(message, &arguments);
    bool appended = dbus_message_iter_open_container(
        &arguments, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &items);
    appended = appended && dbus_message_iter_append_basic(&items, DBUS_TYPE_STRING, &uri_value);
    appended = appended && dbus_message_iter_close_container(&arguments, &items);
    appended = appended && dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &startup_id);
    const bool sent = appended && dbus_connection_send(connection, message, nullptr);
    if (sent) dbus_connection_flush(connection);
    dbus_message_unref(message);
    dbus_connection_unref(connection);
    return sent;
}

} // namespace

LinuxPlatformServices::LinuxPlatformServices(
    Engine& engine,
    std::function<void()> request_frame,
    std::function<void(std::uint64_t)> announce,
    bool allow_external_launch)
    : engine_(engine),
      request_frame_(std::move(request_frame)),
      announce_(std::move(announce)),
      allow_external_launch_(allow_external_launch),
      assets_(engine_, request_frame_, CreateLinuxAssetEnvironment()) {}

LinuxPlatformServices::~LinuxPlatformServices() {
    if (cursor_ != nullptr) SDL_DestroyCursor(cursor_);
}

void LinuxPlatformServices::WriteClipboard(std::string_view plain_text, std::string_view) {
    SetClipboardText(plain_text);
}

void LinuxPlatformServices::RequestClipboardRead(std::uint64_t handle) {
    const std::string text = ClipboardText();
    ui_on_paste_text(handle, reinterpret_cast<const std::uint8_t*>(text.data()), static_cast<std::uint32_t>(text.size()));
    request_frame_();
}

void LinuxPlatformServices::RequestFontLoad(std::uint32_t font_id, std::string_view url) {
    assets_.LoadFont(font_id, url);
}

void LinuxPlatformServices::ReportMissingFontCoverage(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    std::string_view sample_text) {
    assets_.QueueMissingFontCoverage(primary_font_id, coverage_kind, sample_text);
}

void LinuxPlatformServices::RequestSemanticAnnouncement(std::uint64_t handle) {
    if (announce_) announce_(handle);
}

bool LinuxPlatformServices::SetClipboardText(std::string_view text) {
    return SDL_SetClipboardText(std::string(text).c_str());
}

std::string LinuxPlatformServices::ClipboardText() const {
    char* value = SDL_GetClipboardText();
    if (value == nullptr) return {};
    std::string result(value);
    SDL_free(value);
    return result;
}

bool LinuxPlatformServices::OpenExternalUrl(std::string_view url) const {
    if (!IsSupportedExternalUrl(url)) return false;
    return !allow_external_launch_ || SDL_OpenURL(std::string(url).c_str());
}

bool LinuxPlatformServices::OpenFile(const std::filesystem::path& path) const {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    if (!allow_external_launch_) return true;
    const std::string uri = FileUri(path);
    return !uri.empty() && SDL_OpenURL(uri.c_str());
}

bool LinuxPlatformServices::RevealFile(const std::filesystem::path& path) const {
    if (path.empty() || !std::filesystem::exists(path)) return false;
    if (!allow_external_launch_) return true;
    const std::string uri = FileUri(path);
    if (!uri.empty() && RevealWithFileManager(uri)) return true;
    const std::string parent_uri = FileUri(path.parent_path());
    return !parent_uri.empty() && SDL_OpenURL(parent_uri.c_str());
}

void LinuxPlatformServices::SetCursor(std::uint32_t style) {
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

bool LinuxPlatformServices::LoadDefaultFont(std::uint32_t font_id, const char* name) {
    return assets_.LoadDefaultFont(font_id, name);
}

bool LinuxPlatformServices::LoadFont(std::uint32_t font_id, const std::filesystem::path& path) {
    return assets_.LoadFont(font_id, path);
}

bool LinuxPlatformServices::LoadSvg(std::uint32_t svg_id, std::string_view source) {
    return assets_.LoadSvg(svg_id, source);
}

bool LinuxPlatformServices::LoadTexture(std::uint32_t texture_id, std::string_view source) {
    return assets_.LoadTexture(texture_id, source);
}

void LinuxPlatformServices::ReleaseSvg(std::uint32_t svg_id) { assets_.ReleaseSvg(svg_id); }
void LinuxPlatformServices::ReleaseTexture(std::uint32_t texture_id) { assets_.ReleaseTexture(texture_id); }
bool LinuxPlatformServices::ProcessPendingAssets() { return assets_.ProcessPendingFontCoverage(); }
std::size_t LinuxPlatformServices::FallbackFontCountForTesting() const {
    return assets_.FallbackFontCountForTesting();
}

bool LinuxPlatformServices::IsSupportedExternalUrl(std::string_view url) {
    const std::size_t separator = url.find(':');
    if (separator == std::string_view::npos) return false;
    std::string scheme(url.substr(0U, separator));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return scheme == "http" || scheme == "https";
}

} // namespace effindom::v2::native
