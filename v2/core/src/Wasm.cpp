#include "Engine.h"
#include "EngineInternal.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <include/core/SkColorSpace.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>
#include <include/gpu/ganesh/GrBackendSurface.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <include/gpu/ganesh/gl/GrGLDirectContext.h>
#include <include/gpu/ganesh/gl/GrGLInterface.h>
#include <include/gpu/ganesh/gl/GrGLMakeWebGLInterface.h>
#include <include/gpu/ganesh/gl/GrGLTypes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr char kCanvasSelector[] = "#fui-canvas";
constexpr GrGLenum kGLRGBA8 = 0x8058;

struct RenderState {
    effindom::v2::Engine engine;
    EdBackendType active_backend = ED_BACKEND_NONE;
    EdDeviceState device_state = ED_DEVICE_OK;
    EdBackendType recovery_backend = ED_BACKEND_NONE;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl_ctx = 0;
    bool gl_explicit_swap_control = false;
    sk_sp<GrDirectContext> ganesh_ctx;
    sk_sp<SkSurface> gl_surface;

    std::vector<std::uint8_t> software_pixels;
    sk_sp<SkSurface> software_surface;
};

RenderState g_state;
bool g_pending_resize = false;
std::uint32_t g_pending_width = 0;
std::uint32_t g_pending_height = 0;
float g_pending_dpr = 1.0f;

EM_JS(void, effindom_v2_frame_presented, (), {
    if (typeof Module !== 'undefined' && typeof Module['effindomV2FramePresented'] === 'function') {
        Module['effindomV2FramePresented']();
    }
});

EM_JS(void, effindom_v2_backend_changed, (int backend_type), {
    if (typeof Module === 'undefined') {
        return;
    }
    Module.__effindomV2BackendType = backend_type;
    if (typeof Module['effindomV2BackendChanged'] === 'function') {
        Module['effindomV2BackendChanged'](backend_type);
    }
});

EM_JS(int, effindom_v2_should_use_explicit_swap_control, (), {
    if (typeof Module === 'undefined' || !Module || !Module.canvas) {
        return 0;
    }
    if (typeof OffscreenCanvas !== 'undefined' && Module.canvas instanceof OffscreenCanvas) {
        return 1;
    }
    return 0;
});

EM_JS(void, effindom_v2_install_webgl_context_lost_handler, (), {
    if (typeof Module === 'undefined' || !Module) {
        return;
    }
    if (Module.__effindomV2WebGlContextLostInstalled === true) {
        return;
    }
    const canvas = Module.canvas instanceof HTMLCanvasElement
        ? Module.canvas
        : document.querySelector('#fui-canvas');
    if (!(canvas instanceof HTMLCanvasElement)) {
        return;
    }
    canvas.addEventListener('webglcontextlost', (event) => {
        event.preventDefault();
        if (typeof Module._ed_notify_webgl_context_lost === 'function') {
            Module._ed_notify_webgl_context_lost();
        }
    }, false);
    Module.__effindomV2WebGlContextLostInstalled = true;
});

void NotifyBackendChanged() {
    effindom_v2_backend_changed(static_cast<int>(g_state.active_backend));
}

void ResetGaneshState() {
    g_state.gl_surface.reset();
    g_state.ganesh_ctx.reset();
    g_state.gl_explicit_swap_control = false;
    if (g_state.gl_ctx != 0) {
        emscripten_webgl_destroy_context(g_state.gl_ctx);
        g_state.gl_ctx = 0;
    }
}

void ResetSoftwareState() {
    g_state.software_surface.reset();
    g_state.software_pixels.clear();
}

void ResetAllBackends() {
    ResetGaneshState();
    ResetSoftwareState();
    g_state.active_backend = ED_BACKEND_NONE;
    NotifyBackendChanged();
}

void SetActiveBackend(EdBackendType backend) {
    g_state.active_backend = backend;
    g_state.device_state = ED_DEVICE_OK;
    g_state.recovery_backend = backend;
    NotifyBackendChanged();
}

void MarkDeviceLost(EdBackendType backend) {
    if (backend == ED_BACKEND_WEBGL2) {
        ResetGaneshState();
    } else if (backend == ED_BACKEND_CPU) {
        ResetSoftwareState();
    }
    g_state.active_backend = ED_BACKEND_NONE;
    g_state.device_state = ED_DEVICE_LOST;
    g_state.recovery_backend = backend;
    NotifyBackendChanged();
}

void PrepareForInit(std::uint32_t physical_w, std::uint32_t physical_h, float dpr, EdBackendType backend) {
    ResetAllBackends();
    g_state.engine.Init(physical_w, physical_h, dpr);
    g_state.recovery_backend = backend;
}

bool RecreateSoftwareSurface() {
    ResetSoftwareState();
    const std::uint32_t width = g_state.engine.physical_width();
    const std::uint32_t height = g_state.engine.physical_height();
    if (width == 0 || height == 0) {
        return false;
    }

    g_state.software_pixels.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U,
        0U);
    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(width),
        static_cast<int>(height),
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType,
        SkColorSpace::MakeSRGB());
    g_state.software_surface = SkSurfaces::WrapPixels(
        info,
        g_state.software_pixels.data(),
        static_cast<std::size_t>(width) * 4U);
    return static_cast<bool>(g_state.software_surface);
}

bool CreateWebGlSurface() {
    GrGLFramebufferInfo framebuffer_info;
    framebuffer_info.fFBOID = 0;
    framebuffer_info.fFormat = kGLRGBA8;

    const auto backend_render_target = GrBackendRenderTargets::MakeGL(
        static_cast<int>(g_state.engine.physical_width()),
        static_cast<int>(g_state.engine.physical_height()),
        0,
        8,
        framebuffer_info);

    g_state.gl_surface = SkSurfaces::WrapBackendRenderTarget(
        g_state.ganesh_ctx.get(),
        backend_render_target,
        kBottomLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType,
        SkColorSpace::MakeSRGB(),
        nullptr);
    
    return static_cast<bool>(g_state.gl_surface);
}

void StartWebGlInit() {
    PrepareForInit(g_state.engine.physical_width(), g_state.engine.physical_height(), g_state.engine.dpr(), ED_BACKEND_WEBGL2);

    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.alpha = 1;
    attributes.stencil = 1;
    attributes.antialias = 0;
    attributes.premultipliedAlpha = 0;
    attributes.preserveDrawingBuffer = 1;
    attributes.enableExtensionsByDefault = 0;
    attributes.explicitSwapControl = effindom_v2_should_use_explicit_swap_control() != 0;
    g_state.gl_explicit_swap_control = attributes.explicitSwapControl;

    g_state.gl_ctx = emscripten_webgl_create_context(kCanvasSelector, &attributes);
    if (g_state.gl_ctx == 0) {
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }

    if (emscripten_webgl_make_context_current(g_state.gl_ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
        ResetGaneshState();
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }

    effindom_v2_install_webgl_context_lost_handler();
    emscripten_webgl_enable_extension(g_state.gl_ctx, "EXT_texture_filter_anisotropic");
    emscripten_webgl_enable_extension(g_state.gl_ctx, "WEBGL_debug_renderer_info");

    sk_sp<const GrGLInterface> interface = GrGLInterfaces::MakeWebGL();
    if (!interface) {
        ResetGaneshState();
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }

    g_state.ganesh_ctx = GrDirectContexts::MakeGL(interface);
    if (!g_state.ganesh_ctx) {
        ResetGaneshState();
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }

    if (!CreateWebGlSurface()) {
        ResetGaneshState();
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }

    SetActiveBackend(ED_BACKEND_WEBGL2);
}

void StartSoftwareInit() {
    PrepareForInit(g_state.engine.physical_width(), g_state.engine.physical_height(), g_state.engine.dpr(), ED_BACKEND_CPU);
    if (!RecreateSoftwareSurface()) {
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }
    SetActiveBackend(ED_BACKEND_CPU);
}

void ResizeActiveBackend() {
    switch (g_state.active_backend) {
    case ED_BACKEND_WEBGL2:
        if (g_state.ganesh_ctx) {
            g_state.gl_surface.reset();
            if (!CreateWebGlSurface()) {
                MarkDeviceLost(ED_BACKEND_WEBGL2);
            }
        }
        return;
    case ED_BACKEND_CPU:
        if (!RecreateSoftwareSurface()) {
            MarkDeviceLost(ED_BACKEND_CPU);
        }
        return;
    default:
        return;
    }
}

void RenderWebGlFrame(double current_time_ms) {
    if (!g_state.gl_ctx || !g_state.gl_surface || !g_state.ganesh_ctx) {
        return;
    }
    if (emscripten_webgl_make_context_current(g_state.gl_ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
        MarkDeviceLost(ED_BACKEND_WEBGL2);
        return;
    }

    g_state.engine.RenderToCanvas(g_state.gl_surface->getCanvas(), current_time_ms);
    g_state.ganesh_ctx->flushAndSubmit();
    if (g_state.gl_explicit_swap_control) {
        const EMSCRIPTEN_RESULT present_status = emscripten_webgl_commit_frame();
        if (present_status != EMSCRIPTEN_RESULT_SUCCESS) {
            MarkDeviceLost(ED_BACKEND_WEBGL2);
            return;
        }
    }
    effindom_v2_frame_presented();
}

void RenderSoftwareFrame(double current_time_ms) {
    if (!g_state.software_surface) {
        return;
    }
    g_state.engine.RenderToCanvas(g_state.software_surface->getCanvas(), current_time_ms);
    effindom_v2_frame_presented();
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void ed_init(std::uint32_t physical_w, std::uint32_t physical_h, float dpr) {
    g_state.engine.Init(physical_w, physical_h, dpr);
    StartWebGlInit();
}

EMSCRIPTEN_KEEPALIVE
void ed_init_webgl(std::uint32_t physical_w, std::uint32_t physical_h, float dpr) {
    g_state.engine.Init(physical_w, physical_h, dpr);
    StartWebGlInit();
}

EMSCRIPTEN_KEEPALIVE
void ed_init_sw(std::uint32_t physical_w, std::uint32_t physical_h, float dpr) {
    g_state.engine.Init(physical_w, physical_h, dpr);
    StartSoftwareInit();
}

EMSCRIPTEN_KEEPALIVE
void ed_resize(std::uint32_t physical_w, std::uint32_t physical_h, float dpr) {
    g_pending_resize = true;
    g_pending_width = physical_w;
    g_pending_height = physical_h;
    g_pending_dpr = dpr;
}

EMSCRIPTEN_KEEPALIVE
void ed_register_font(std::uint32_t font_id, const std::uint8_t* bytes, std::uint32_t len) {
    g_state.engine.RegisterFont(font_id, bytes, len);
}

EMSCRIPTEN_KEEPALIVE
void ed_unregister_font(std::uint32_t font_id) {
    g_state.engine.UnregisterFont(font_id);
}

EMSCRIPTEN_KEEPALIVE
void ed_register_svg(std::uint32_t svg_id, const std::uint8_t* bytes, std::uint32_t len) {
    g_state.engine.RegisterSvg(svg_id, bytes, len);
}

EMSCRIPTEN_KEEPALIVE
void ed_register_texture_rgba(
    std::uint32_t texture_id,
    const std::uint8_t* rgba,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t byte_length) {
    g_state.engine.RegisterTextureRgba(texture_id, rgba, width, height, static_cast<std::size_t>(byte_length));
}

EMSCRIPTEN_KEEPALIVE
void ed_unregister_texture(std::uint32_t texture_id) {
    g_state.engine.UnregisterTexture(texture_id);
}

EMSCRIPTEN_KEEPALIVE
void ed_execute_command_buffer(const std::uint32_t* buffer, std::uint32_t length) {
    g_state.engine.ExecuteCommandBuffer(buffer, length);
}

EMSCRIPTEN_KEEPALIVE
void ed_render_frame(double current_time_ms) {
    if (g_pending_resize) {
        g_pending_resize = false;
        g_state.engine.Resize(g_pending_width, g_pending_height, g_pending_dpr);
        ResizeActiveBackend();
    }

    switch (g_state.active_backend) {
    case ED_BACKEND_WEBGL2:
        RenderWebGlFrame(current_time_ms);
        return;
    case ED_BACKEND_CPU:
        RenderSoftwareFrame(current_time_ms);
        return;
    default:
        return;
    }
}

EMSCRIPTEN_KEEPALIVE
void ed_recover_device(void) {
    if (g_state.device_state != ED_DEVICE_LOST) {
        return;
    }
    switch (g_state.recovery_backend) {
    case ED_BACKEND_WEBGL2:
        ed_init_webgl(g_state.engine.physical_width(), g_state.engine.physical_height(), g_state.engine.dpr());
        return;
    case ED_BACKEND_CPU:
        ed_init_sw(g_state.engine.physical_width(), g_state.engine.physical_height(), g_state.engine.dpr());
        return;
    default:
        return;
    }
}

EMSCRIPTEN_KEEPALIVE
std::uint64_t ed_hit_test(float logical_x, float logical_y) {
    return g_state.engine.HitTest(logical_x, logical_y);
}

EMSCRIPTEN_KEEPALIVE
ed_ptr_t ed_get_sw_framebuffer(void) {
    if (g_state.software_pixels.empty()) {
        return 0;
    }
    return reinterpret_cast<ed_ptr_t>(g_state.software_pixels.data());
}

EMSCRIPTEN_KEEPALIVE
EdBackendType ed_get_backend_type(void) {
    return g_state.active_backend;
}

EMSCRIPTEN_KEEPALIVE
EdDeviceState ed_get_device_state(void) {
    return g_state.device_state;
}

EMSCRIPTEN_KEEPALIVE
void ed_notify_webgl_context_lost(void) {
    if (g_state.active_backend == ED_BACKEND_WEBGL2) {
        MarkDeviceLost(ED_BACKEND_WEBGL2);
    }
}

EMSCRIPTEN_KEEPALIVE
void effindom_v2_custom_draw(std::uint32_t handle_lo, std::uint32_t handle_hi, std::uint32_t canvas_ptr) {
    EM_ASM({
        if (typeof window !== 'undefined' && typeof window['__effindomV2CustomDraw'] === 'function') {
            window['__effindomV2CustomDraw']($0, $1, $2 >>> 0);
        }
    }, handle_lo, handle_hi, canvas_ptr);
}

EMSCRIPTEN_KEEPALIVE
void ed_debug_simulate_device_lost(void) {
    if (g_state.active_backend != ED_BACKEND_NONE) {
        MarkDeviceLost(g_state.active_backend);
    }
}

/* ── Canvas drawing API ─────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
void ed_canvas_save(void* canvas) {
    effindom::v2::EdCanvasSave(static_cast<SkCanvas*>(canvas));
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_restore(void* canvas) {
    effindom::v2::EdCanvasRestore(static_cast<SkCanvas*>(canvas));
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_translate(void* canvas, float x, float y) {
    effindom::v2::EdCanvasTranslate(static_cast<SkCanvas*>(canvas), x, y);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_scale(void* canvas, float sx, float sy) {
    effindom::v2::EdCanvasScale(static_cast<SkCanvas*>(canvas), sx, sy);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_rotate(void* canvas, float degrees) {
    effindom::v2::EdCanvasRotate(static_cast<SkCanvas*>(canvas), degrees);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_clip_rect(void* canvas, float x, float y, float w, float h) {
    effindom::v2::EdCanvasClipRect(static_cast<SkCanvas*>(canvas), x, y, w, h);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_rect(void* canvas, float x, float y, float w, float h,
                         uint32_t fill_color, uint32_t stroke_color, float stroke_width) {
    effindom::v2::EdCanvasDrawRect(
        static_cast<SkCanvas*>(canvas), x, y, w, h, fill_color, stroke_color, stroke_width);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_circle(void* canvas, float cx, float cy, float radius,
                           uint32_t fill_color, uint32_t stroke_color, float stroke_width) {
    effindom::v2::EdCanvasDrawCircle(
        static_cast<SkCanvas*>(canvas), cx, cy, radius, fill_color, stroke_color, stroke_width);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_line(void* canvas, float x1, float y1, float x2, float y2,
                         uint32_t color, float stroke_width) {
    effindom::v2::EdCanvasDrawLine(
        static_cast<SkCanvas*>(canvas), x1, y1, x2, y2, color, stroke_width);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_round_rect(void* canvas, float x, float y, float w, float h,
                               float rx, float ry,
                               uint32_t fill_color, uint32_t stroke_color, float stroke_width) {
    effindom::v2::EdCanvasDrawRoundRect(
        static_cast<SkCanvas*>(canvas), x, y, w, h, rx, ry, fill_color, stroke_color, stroke_width);
}

EMSCRIPTEN_KEEPALIVE
uint32_t ed_path_create(void) {
    return g_state.engine.CreatePath();
}

EMSCRIPTEN_KEEPALIVE
void ed_path_destroy(uint32_t path_id) {
    g_state.engine.DestroyPath(path_id);
}

EMSCRIPTEN_KEEPALIVE
void ed_path_move_to(uint32_t path_id, float x, float y) {
    g_state.engine.PathMoveTo(path_id, x, y);
}

EMSCRIPTEN_KEEPALIVE
void ed_path_line_to(uint32_t path_id, float x, float y) {
    g_state.engine.PathLineTo(path_id, x, y);
}

EMSCRIPTEN_KEEPALIVE
void ed_path_quad_to(uint32_t path_id, float cx, float cy, float x, float y) {
    g_state.engine.PathQuadTo(path_id, cx, cy, x, y);
}

EMSCRIPTEN_KEEPALIVE
void ed_path_cubic_to(uint32_t path_id, float cx1, float cy1, float cx2, float cy2, float x, float y) {
    g_state.engine.PathCubicTo(path_id, cx1, cy1, cx2, cy2, x, y);
}

EMSCRIPTEN_KEEPALIVE
void ed_path_close(uint32_t path_id) {
    g_state.engine.PathClose(path_id);
}

EMSCRIPTEN_KEEPALIVE
void ed_path_add_rect(uint32_t path_id, float x, float y, float w, float h) {
    g_state.engine.PathAddRect(path_id, x, y, w, h);
}

EMSCRIPTEN_KEEPALIVE
void ed_path_add_circle(uint32_t path_id, float cx, float cy, float r) {
    g_state.engine.PathAddCircle(path_id, cx, cy, r);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_path(void* canvas, uint32_t path_id,
                         uint32_t fill_color, uint32_t stroke_color, float stroke_width) {
    g_state.engine.CanvasDrawPath(
        static_cast<SkCanvas*>(canvas), path_id, fill_color, stroke_color, stroke_width);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_text(void* canvas, const uint8_t* utf8, uint32_t len,
                         float x, float y, uint32_t font_id, float font_size, uint32_t color) {
    g_state.engine.CanvasDrawText(
        static_cast<SkCanvas*>(canvas), utf8, len, x, y, font_id, font_size, color);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_image(void* canvas, uint32_t texture_id,
                          float x, float y, float w, float h) {
    g_state.engine.CanvasDrawImage(
        static_cast<SkCanvas*>(canvas), texture_id, x, y, w, h);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_draw_svg(void* canvas, uint32_t svg_id,
                        float x, float y, float w, float h) {
    g_state.engine.CanvasDrawSvg(
        static_cast<SkCanvas*>(canvas), svg_id, x, y, w, h);
}

EMSCRIPTEN_KEEPALIVE
uint32_t ed_canvas_create_offscreen(uint32_t width, uint32_t height) {
    return g_state.engine.CreateOffscreenSurface(width, height);
}

EMSCRIPTEN_KEEPALIVE
void* ed_canvas_get_offscreen_canvas(uint32_t offscreen_id) {
    return g_state.engine.GetOffscreenCanvas(offscreen_id);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_read_offscreen_pixels(uint32_t offscreen_id, uint8_t* out_rgba) {
    g_state.engine.ReadOffscreenPixels(offscreen_id, out_rgba);
}

EMSCRIPTEN_KEEPALIVE
void ed_canvas_destroy_offscreen(uint32_t offscreen_id) {
    g_state.engine.DestroyOffscreenSurface(offscreen_id);
}

} // extern "C"
