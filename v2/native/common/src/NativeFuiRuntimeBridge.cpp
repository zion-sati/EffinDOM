#include "NativeFuiRuntimeBridge.h"

#include "NativeFuiBridge.h"
#include "NativeHostCore.h"
#include "NativeInputRouter.h"
#include "NativePlatformHost.h"
#include "NativeUtf8.h"
#include "NativeWorkerHost.h"
#include "fui_host_abi.h"

#include "effindom_ui.h"

#include "SDL3/SDL.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace effindom::v2::native {
namespace {

NativePlatformHost* active_host = nullptr;
NativeFuiDrawingMetrics drawing_metrics{};

} // namespace

void SetActiveNativePlatformHost(NativePlatformHost* host) { active_host = host; }
NativePlatformHost* ActiveNativePlatformHost() { return active_host; }
NativeFuiDrawingMetrics NativeFuiDrawingMetricsForTesting() { return drawing_metrics; }
void ResetNativeFuiDrawingMetricsForTesting() { drawing_metrics = {}; }

bool DrawCanvasBatch(
    NativeHostCore& host,
    std::uintptr_t canvas_pointer,
    std::uintptr_t words_pointer,
    std::uint32_t word_count) {
    if (word_count == 0U) return true;
    if (canvas_pointer == 0U || words_pointer == 0U) return false;
    if ((words_pointer % alignof(std::uint32_t)) != 0U) return false;

    const bool drawn = host.GetEngine().CanvasDrawBatch(
        reinterpret_cast<SkCanvas*>(canvas_pointer),
        reinterpret_cast<const std::uint32_t*>(words_pointer),
        word_count);
    if (drawn) {
        ++drawing_metrics.batch_count;
        drawing_metrics.batch_bytes += static_cast<std::uint64_t>(word_count) * sizeof(std::uint32_t);
    }
    return drawn;
}

std::uint32_t CreatePath(NativeHostCore& host) { return host.GetEngine().CreatePath(); }
bool DestroyPath(NativeHostCore& host, std::uint32_t path_id) {
    return host.GetEngine().DestroyPath(path_id);
}
bool PathMoveTo(NativeHostCore& host, std::uint32_t path_id, float x, float y) {
    return host.GetEngine().PathMoveTo(path_id, x, y);
}
bool PathLineTo(NativeHostCore& host, std::uint32_t path_id, float x, float y) {
    return host.GetEngine().PathLineTo(path_id, x, y);
}
bool PathQuadTo(NativeHostCore& host, std::uint32_t path_id, float cx, float cy, float x, float y) {
    return host.GetEngine().PathQuadTo(path_id, cx, cy, x, y);
}
bool PathCubicTo(
    NativeHostCore& host,
    std::uint32_t path_id,
    float cx1,
    float cy1,
    float cx2,
    float cy2,
    float x,
    float y) {
    return host.GetEngine().PathCubicTo(path_id, cx1, cy1, cx2, cy2, x, y);
}
bool PathClose(NativeHostCore& host, std::uint32_t path_id) {
    return host.GetEngine().PathClose(path_id);
}
bool PathAddRect(
    NativeHostCore& host,
    std::uint32_t path_id,
    float x,
    float y,
    float width,
    float height) {
    return host.GetEngine().PathAddRect(path_id, x, y, width, height);
}
bool PathAddCircle(
    NativeHostCore& host,
    std::uint32_t path_id,
    float cx,
    float cy,
    float radius) {
    return host.GetEngine().PathAddCircle(path_id, cx, cy, radius);
}

bool CommitBitmap(
    NativeHostCore& host,
    std::uint32_t texture_id,
    std::uintptr_t pixels_pointer,
    std::uint32_t byte_length,
    std::uint32_t width,
    std::uint32_t height) {
    const bool committed = host.GetEngine().RegisterTextureRgba(
        texture_id,
        reinterpret_cast<const std::uint8_t*>(pixels_pointer),
        width,
        height,
        byte_length);
    if (committed) {
        ++drawing_metrics.bitmap_upload_count;
        drawing_metrics.bitmap_upload_bytes += byte_length;
        host.RequestFrame();
    }
    return committed;
}

bool CommitBitmapDirty(
    NativeHostCore& host,
    std::uint32_t texture_id,
    std::uintptr_t pixels_pointer,
    std::uint32_t byte_length,
    std::uint32_t full_width,
    std::uint32_t full_height,
    std::uint32_t sub_x,
    std::uint32_t sub_y,
    std::uint32_t sub_width,
    std::uint32_t sub_height) {
    const bool committed = host.GetEngine().RegisterTextureSubRgba(
        texture_id,
        reinterpret_cast<const std::uint8_t*>(pixels_pointer),
        sub_x,
        sub_y,
        sub_width,
        sub_height,
        full_width,
        full_height,
        byte_length);
    if (committed) {
        ++drawing_metrics.dirty_upload_count;
        drawing_metrics.dirty_upload_bytes += byte_length;
        host.RequestFrame();
    }
    return committed;
}

bool ReleaseBitmap(NativeHostCore& host, std::uint32_t texture_id) {
    const bool released = host.GetEngine().UnregisterTexture(texture_id);
    if (released) host.RequestFrame();
    return released;
}

std::uint32_t CreateOffscreenSurface(NativeHostCore& host, std::uint32_t width, std::uint32_t height) {
    return host.GetEngine().CreateOffscreenSurface(width, height);
}

std::uintptr_t GetOffscreenCanvas(NativeHostCore& host, std::uint32_t offscreen_id) {
    return reinterpret_cast<std::uintptr_t>(host.GetEngine().GetOffscreenCanvas(offscreen_id));
}

bool ReadOffscreenPixels(
    NativeHostCore& host,
    std::uint32_t offscreen_id,
    std::uintptr_t output_pointer,
    std::uint32_t width,
    std::uint32_t height) {
    return host.GetEngine().ReadOffscreenPixels(
        offscreen_id, reinterpret_cast<std::uint8_t*>(output_pointer), width, height);
}

bool DestroyOffscreenSurface(NativeHostCore& host, std::uint32_t offscreen_id) {
    return host.GetEngine().DestroyOffscreenSurface(offscreen_id);
}

std::uint32_t RenderNodeToRgba(
    NativeHostCore& host,
    std::uint64_t handle,
    std::uint32_t width,
    std::uint32_t height,
    std::uintptr_t output_pointer,
    std::uint32_t output_capacity,
    float scale,
    float x,
    float y) {
    return host.GetEngine().RenderNodeToRgba(
        handle, width, height, reinterpret_cast<std::uint8_t*>(output_pointer),
        output_capacity, scale, x, y);
}

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
void fui_worker_start_string(
    std::uint32_t worker_id,
    std::uintptr_t artifact_pointer,
    std::uint32_t artifact_length,
    std::uintptr_t entry_pointer,
    std::uint32_t entry_length,
    std::uintptr_t input_pointer,
    std::uint32_t input_length) {
    auto* workers = effindom::v2::native::ActiveNativeWorkerHost();
    if (workers == nullptr) return;
    workers->Start(
        worker_id,
        effindom::v2::native::Utf8(artifact_pointer, artifact_length),
        effindom::v2::native::Utf8(entry_pointer, entry_length),
        effindom::v2::native::Utf8(input_pointer, input_length));
}
void fui_worker_cancel(std::uint32_t worker_id) {
    auto* workers = effindom::v2::native::ActiveNativeWorkerHost();
    if (workers != nullptr) workers->Cancel(worker_id);
}
void fui_start_timer(std::uint32_t timer_id, std::int32_t delay_ms) {
    if (Host() != nullptr) Host()->StartTimer(timer_id, delay_ms);
}
void fui_cancel_timer(std::uint32_t timer_id) {
    if (Host() != nullptr) Host()->CancelTimer(timer_id);
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
void fui_bitmap_commit(
    std::uint32_t texture_id,
    std::uintptr_t pixels_pointer,
    std::uint32_t byte_length,
    std::uint32_t width,
    std::uint32_t height) {
    if (Host() != nullptr) {
        effindom::v2::native::CommitBitmap(
            Host()->Core(), texture_id, pixels_pointer, byte_length, width, height);
    }
}
void fui_bitmap_commit_dirty(
    std::uint32_t texture_id,
    std::uintptr_t pixels_pointer,
    std::uint32_t byte_length,
    std::uint32_t full_width,
    std::uint32_t full_height,
    std::uint32_t sub_x,
    std::uint32_t sub_y,
    std::uint32_t sub_width,
    std::uint32_t sub_height) {
    if (Host() != nullptr) {
        effindom::v2::native::CommitBitmapDirty(
            Host()->Core(), texture_id, pixels_pointer, byte_length,
            full_width, full_height, sub_x, sub_y, sub_width, sub_height);
    }
}
void fui_bitmap_release(std::uint32_t texture_id) {
    if (Host() != nullptr) effindom::v2::native::ReleaseBitmap(Host()->Core(), texture_id);
}
std::uint32_t fui_render_node_to_rgba(
    std::uint64_t handle,
    std::uint32_t width,
    std::uint32_t height,
    std::uintptr_t output_pointer,
    std::uint32_t output_capacity,
    float scale,
    float x,
    float y) {
    return Host() == nullptr ? 0U : effindom::v2::native::RenderNodeToRgba(
        Host()->Core(), handle, width, height, output_pointer, output_capacity, scale, x, y);
}
std::uint32_t fui_path_create() {
    return Host() == nullptr ? 0U : effindom::v2::native::CreatePath(Host()->Core());
}
void fui_path_destroy(std::uint32_t path_id) {
    if (Host() != nullptr) effindom::v2::native::DestroyPath(Host()->Core(), path_id);
}
void fui_path_move_to(std::uint32_t path_id, float x, float y) {
    if (Host() != nullptr) effindom::v2::native::PathMoveTo(Host()->Core(), path_id, x, y);
}
void fui_path_line_to(std::uint32_t path_id, float x, float y) {
    if (Host() != nullptr) effindom::v2::native::PathLineTo(Host()->Core(), path_id, x, y);
}
void fui_path_quad_to(std::uint32_t path_id, float cx, float cy, float x, float y) {
    if (Host() != nullptr) effindom::v2::native::PathQuadTo(Host()->Core(), path_id, cx, cy, x, y);
}
void fui_path_cubic_to(
    std::uint32_t path_id,
    float cx1,
    float cy1,
    float cx2,
    float cy2,
    float x,
    float y) {
    if (Host() != nullptr) {
        effindom::v2::native::PathCubicTo(Host()->Core(), path_id, cx1, cy1, cx2, cy2, x, y);
    }
}
void fui_path_close(std::uint32_t path_id) {
    if (Host() != nullptr) effindom::v2::native::PathClose(Host()->Core(), path_id);
}
void fui_path_add_rect(std::uint32_t path_id, float x, float y, float width, float height) {
    if (Host() != nullptr) {
        effindom::v2::native::PathAddRect(Host()->Core(), path_id, x, y, width, height);
    }
}
void fui_path_add_circle(std::uint32_t path_id, float cx, float cy, float radius) {
    if (Host() != nullptr) {
        effindom::v2::native::PathAddCircle(Host()->Core(), path_id, cx, cy, radius);
    }
}
std::uint32_t fui_canvas_create_offscreen(std::uint32_t width, std::uint32_t height) {
    return Host() == nullptr ? 0U :
        effindom::v2::native::CreateOffscreenSurface(Host()->Core(), width, height);
}
std::uintptr_t fui_canvas_get_offscreen_ptr(std::uint32_t offscreen_id) {
    return Host() == nullptr ? 0U :
        effindom::v2::native::GetOffscreenCanvas(Host()->Core(), offscreen_id);
}
void fui_canvas_read_offscreen_pixels(
    std::uint32_t offscreen_id,
    std::uintptr_t output_pointer,
    std::uint32_t width,
    std::uint32_t height) {
    if (Host() != nullptr) {
        effindom::v2::native::ReadOffscreenPixels(
            Host()->Core(), offscreen_id, output_pointer, width, height);
    }
}
void fui_canvas_destroy_offscreen(std::uint32_t offscreen_id) {
    if (Host() != nullptr) {
        effindom::v2::native::DestroyOffscreenSurface(Host()->Core(), offscreen_id);
    }
}
void fui_canvas_draw_batch(
    std::uintptr_t canvas_pointer,
    std::uintptr_t words_pointer,
    std::uint32_t word_count) {
    if (Host() != nullptr) {
        effindom::v2::native::DrawCanvasBatch(
            Host()->Core(), canvas_pointer, words_pointer, word_count);
    }
}
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
void as_on_missing_font_coverage(
    std::uint32_t font_id,
    std::uint32_t coverage_kind,
    const std::uint8_t* sample,
    std::uint32_t length) {
    if (Host() != nullptr) {
        Host()->ReportMissingFontCoverage(
            font_id,
            coverage_kind,
            effindom::v2::native::Utf8(sample, length));
    }
}
void as_on_request_semantic_announcement(ui_handle_t) {}

} // extern "C"
