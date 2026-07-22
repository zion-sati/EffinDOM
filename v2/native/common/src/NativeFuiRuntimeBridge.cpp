#include "NativeFuiRuntimeBridge.h"

#include "NativeFuiBridge.h"
#include "NativeHostCore.h"
#include "NativeInputRouter.h"
#include "NativePlatformHost.h"
#include "NativeUtf8.h"
#include "fui_host_abi.h"

#include "effindom_ui.h"

#include "SDL3/SDL.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace effindom::v2::native {
namespace {

NativePlatformHost* active_host = nullptr;

} // namespace

void SetActiveNativePlatformHost(NativePlatformHost* host) { active_host = host; }
NativePlatformHost* ActiveNativePlatformHost() { return active_host; }

} // namespace effindom::v2::native

namespace {

effindom::v2::native::NativePlatformHost* Host() {
    return effindom::v2::native::ActiveNativePlatformHost();
}

} // namespace

extern "C" {

void request_render() { if (Host() != nullptr) Host()->Core().RequestFrame(); }
bool fui_dispatch_to_ui(std::uint64_t callback_id) {
    return Host() != nullptr && Host()->PostUiDispatch(callback_id);
}
bool fui_cancel_ui_dispatch_async(std::uint64_t callback_id) {
    return Host() != nullptr && Host()->CancelUiDispatch(callback_id);
}
bool fui_native_clipboard_write(const std::uint8_t* text, std::uint32_t length) {
    if (Host() == nullptr) return false;
    Host()->SetClipboardText(effindom::v2::native::Utf8(text, length));
    return true;
}
std::uint32_t fui_native_clipboard_text_length() {
    return Host() == nullptr ? 0U : static_cast<std::uint32_t>(Host()->ClipboardText().size());
}
std::uint32_t fui_native_clipboard_copy(std::uint8_t* destination, std::uint32_t capacity) {
    if (Host() == nullptr) return 0U;
    const std::string text = Host()->ClipboardText();
    const auto copied = std::min(capacity, static_cast<std::uint32_t>(text.size()));
    if (destination != nullptr && copied > 0U) std::copy_n(text.data(), copied, destination);
    return copied;
}
bool fui_native_open_external_url(const std::uint8_t* value, std::uint32_t length) {
    return Host() != nullptr && Host()->OpenExternalUrl(effindom::v2::native::Utf8(value, length));
}
bool fui_native_open_file(const std::uint8_t* value, std::uint32_t length) {
    return Host() != nullptr && Host()->OpenFile(effindom::v2::native::Utf8(value, length));
}
bool fui_native_reveal_file(const std::uint8_t* value, std::uint32_t length) {
    return Host() != nullptr && Host()->RevealFile(effindom::v2::native::Utf8(value, length));
}
bool fui_native_show_file_dialog(
    std::uint32_t kind,
    std::uint64_t request_id,
    const std::uint8_t* filters,
    std::uint32_t filters_length,
    const std::uint8_t* default_location,
    std::uint32_t default_location_length,
    bool allow_multiple) {
    return Host() != nullptr && Host()->ShowFileDialog(
        kind,
        request_id,
        effindom::v2::native::Utf8(filters, filters_length),
        effindom::v2::native::Utf8(default_location, default_location_length),
        allow_multiple);
}
void fui_native_commit_ready() { if (Host() != nullptr) Host()->Core().ApplyManagedCommittedCommands(); }
void fui_native_commit_frame() { ui_commit_frame(Host() == nullptr ? -1.0 : Host()->Core().NowMilliseconds()); }
float get_viewport_width() { return Host() == nullptr ? 0.0f : Host()->Core().LogicalWidth(); }
float get_viewport_height() { return Host() == nullptr ? 0.0f : Host()->Core().LogicalHeight(); }
float get_device_pixel_ratio() { return Host() == nullptr ? 1.0f : Host()->Core().PixelDensity(); }
void fui_set_application_caption(std::uintptr_t pointer, std::uint32_t length) {
    if (Host() != nullptr) Host()->SetApplicationCaption(effindom::v2::native::Utf8(pointer, length));
}
double fui_now_ms() { return Host() == nullptr ? 0.0 : Host()->Core().NowMilliseconds(); }
bool fui_is_dark_mode() { return Host() != nullptr && Host()->IsDarkMode(); }
std::uint32_t fui_get_accent_color() { return Host() == nullptr ? 0x0A84FFFFU : Host()->AccentColor(); }
std::uint32_t fui_get_platform_family() {
    return Host() == nullptr
        ? static_cast<std::uint32_t>(FUI_PLATFORM_UNKNOWN)
        : Host()->PlatformFamily();
}
std::uint32_t fui_get_host_environment() { return FUI_HOST_ENVIRONMENT_DESKTOP; }
std::uint32_t fui_get_host_capabilities() {
    return Host() == nullptr
        ? FUI_HOST_CAPABILITY_OPEN_EXTERNAL_URI |
              FUI_HOST_CAPABILITY_CLIPBOARD_READ |
              FUI_HOST_CAPABILITY_CLIPBOARD_WRITE |
              FUI_HOST_CAPABILITY_FILE_DIALOGS
        : Host()->HostCapabilities();
}
bool fui_is_coarse_pointer() { return Host() != nullptr && Host()->IsCoarsePointer(); }
void fui_set_pointer_capture(std::uint64_t handle) {
    if (Host() == nullptr) return;
    Host()->Core().InputRouter().Capture(handle);
    Host()->SetNativePointerCapture(true);
}
void fui_release_pointer_capture() {
    if (Host() == nullptr) return;
    Host()->Core().InputRouter().ReleaseCapture();
    Host()->SetNativePointerCapture(false);
}
void fui_copy_text(std::uintptr_t pointer, std::uint32_t length) {
    if (Host() != nullptr) Host()->SetClipboardText(effindom::v2::native::Utf8(pointer, length));
}
void fui_set_cursor(std::uint32_t style) { if (Host() != nullptr) Host()->SetCursor(style); }
void fui_load_font(std::uint32_t font_id, std::uintptr_t pointer, std::uint32_t length) {
    if (Host() != nullptr) Host()->RequestFontLoad(font_id, effindom::v2::native::Utf8(pointer, length));
}
void fui_load_svg(std::uint32_t svg_id, std::uintptr_t pointer, std::uint32_t length) {
    if (Host() != nullptr) Host()->LoadSvg(svg_id, effindom::v2::native::Utf8(pointer, length));
}
void fui_release_svg(std::uint32_t svg_id) { if (Host() != nullptr) Host()->ReleaseSvg(svg_id); }
void fui_load_texture(std::uint32_t texture_id, std::uintptr_t pointer, std::uint32_t length) {
    if (Host() != nullptr) Host()->LoadTexture(texture_id, effindom::v2::native::Utf8(pointer, length));
}
void fui_release_texture(std::uint32_t texture_id) { if (Host() != nullptr) Host()->ReleaseTexture(texture_id); }
void fui_reload_page() {}
bool fui_can_navigate_back() { return false; }
bool fui_can_navigate_forward() { return false; }
void fui_navigate_back() {}
void fui_navigate_forward() {}
void fui_show_url_preview(std::uintptr_t, std::uint32_t) {}
void fui_hide_url_preview() {}
void fui_navigate_to(std::uintptr_t pointer, std::uint32_t length, bool) {
    if (Host() != nullptr) Host()->OpenExternalUrl(effindom::v2::native::Utf8(pointer, length));
}
void fui_log(std::uintptr_t, std::uint32_t, std::uintptr_t message, std::uint32_t length) {
    SDL_Log("%s", effindom::v2::native::Utf8(message, length).c_str());
}
bool fui_logs_enabled() { return true; }

void as_on_focus_changed(ui_handle_t handle, bool focused) { __fui_on_focus_changed(handle, focused); }
bool as_on_pointer_event(ui_handle_t handle, UiEvent event) {
    if (event < UI_EVENT_POINTER_DOWN || event > UI_EVENT_POINTER_CANCEL || Host() == nullptr) return false;
    auto metadata = Host()->Core().InputRouter().PointerMetadata();
    metadata.event_type = static_cast<std::uint32_t>(event);
    metadata.handle = handle;
    return __fui_on_pointer_event_with_metadata(
        metadata.event_type, handle, metadata.x, metadata.y, metadata.modifiers,
        metadata.pointer_id, metadata.pointer_type, metadata.button, metadata.buttons,
        metadata.pressure, metadata.width, metadata.height, metadata.click_count);
}
void as_on_text_changed(ui_handle_t handle, const std::uint8_t* text, std::uint32_t len) {
    __fui_on_text_changed(handle, text, len);
}
void as_on_text_replaced(
    ui_handle_t handle,
    std::uint32_t start,
    std::uint32_t end,
    const std::uint8_t* text,
    std::uint32_t len) {
    __fui_on_text_replaced(handle, start, end, text, len);
}
void as_on_scroll(
    ui_handle_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height) {
    __fui_on_scroll(handle, offset_x, offset_y, content_width, content_height, viewport_width, viewport_height);
}
void as_on_selection_changed(ui_handle_t handle, std::uint32_t start, std::uint32_t end) {
    __fui_on_selection_changed(handle, start, end);
}
void as_on_cross_selection_changed(ui_handle_t handle, const std::uint8_t* text, std::uint32_t len) {
    __fui_on_cross_selection_changed(handle, text, len);
}
void as_on_clipboard_write(
    const std::uint8_t* text,
    std::uint32_t length,
    const std::uint8_t*,
    std::uint32_t) {
    if (Host() != nullptr) Host()->SetClipboardText(effindom::v2::native::Utf8(text, length));
}
void as_on_request_clipboard_read(ui_handle_t handle) {
    if (Host() != nullptr) Host()->RequestClipboardRead(handle);
}
void as_on_request_font_load(std::uint32_t font_id, const std::uint8_t* url, std::uint32_t length) {
    if (Host() != nullptr) Host()->RequestFontLoad(font_id, effindom::v2::native::Utf8(url, length));
}
void as_on_missing_font_coverage(std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t) {}
void as_on_request_semantic_announcement(ui_handle_t) {}

} // extern "C"
