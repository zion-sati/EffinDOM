#include "Engine.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <webgpu/webgpu.h>
#include <webgpu/webgpu_cpp.h>

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
#include <include/gpu/graphite/BackendTexture.h>
#include <include/gpu/graphite/Context.h>
#include <include/gpu/graphite/ContextOptions.h>
#include <include/gpu/graphite/GraphiteTypes.h>
#include <include/gpu/graphite/Recorder.h>
#include <include/gpu/graphite/Surface.h>
#include <include/gpu/graphite/dawn/DawnBackendContext.h>
#include <include/gpu/graphite/dawn/DawnGraphiteTypes.h>

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
    std::uint32_t webgpu_attempt_serial = 0;

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Surface wgpu_surface;
    WGPUTextureFormat surface_format = WGPUTextureFormat_BGRA8Unorm;
    std::unique_ptr<skgpu::graphite::Context> graphite_ctx;
    std::unique_ptr<skgpu::graphite::Recorder> recorder;

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

EM_JS(int, effindom_v2_get_preferred_canvas_format, (), {
    try {
        if (typeof navigator !== 'undefined' &&
            navigator.gpu &&
            typeof navigator.gpu.getPreferredCanvasFormat === 'function' &&
            navigator.gpu.getPreferredCanvasFormat() === 'bgra8unorm') {
            return 0x1B;
        }
    } catch (error) {
    }
    return 0x16;
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

void CancelPendingWebGpuAttempts() {
    g_state.webgpu_attempt_serial += 1;
}

void ResetGraphiteState() {
    g_state.recorder.reset();
    g_state.graphite_ctx.reset();
    g_state.wgpu_surface = wgpu::Surface();
    g_state.device = wgpu::Device();
    g_state.adapter = wgpu::Adapter();
    g_state.instance = wgpu::Instance();
    g_state.surface_format = WGPUTextureFormat_BGRA8Unorm;
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
    ResetGraphiteState();
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
    if (backend == ED_BACKEND_WEBGPU) {
        ResetGraphiteState();
    } else if (backend == ED_BACKEND_WEBGL2) {
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
    CancelPendingWebGpuAttempts();
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

void ConfigureGraphiteSurface() {
    g_state.surface_format = static_cast<WGPUTextureFormat>(effindom_v2_get_preferred_canvas_format());
    wgpu::SurfaceConfiguration config = {};
    config.device = g_state.device;
    config.format = static_cast<wgpu::TextureFormat>(g_state.surface_format);
    config.usage = wgpu::TextureUsage::RenderAttachment;
    config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
    config.width = g_state.engine.physical_width();
    config.height = g_state.engine.physical_height();
    config.presentMode = wgpu::PresentMode::Fifo;
    g_state.wgpu_surface.Configure(&config);
}

void StartWebGpuInit() {
    PrepareForInit(g_state.engine.physical_width(), g_state.engine.physical_height(), g_state.engine.dpr(), ED_BACKEND_WEBGPU);
    g_state.device_state = ED_DEVICE_RECOVERING;
    const std::uint32_t attempt_serial = ++g_state.webgpu_attempt_serial;

    WGPUInstanceDescriptor instance_descriptor = {};
    g_state.instance = wgpu::Instance::Acquire(wgpuCreateInstance(&instance_descriptor));
    if (!g_state.instance) {
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source = {};
    canvas_source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas_source.selector = WGPUStringView{ kCanvasSelector, WGPU_STRLEN };
    WGPUSurfaceDescriptor surface_descriptor = {};
    surface_descriptor.nextInChain = &canvas_source.chain;
    g_state.wgpu_surface = wgpu::Surface::Acquire(
        wgpuInstanceCreateSurface(g_state.instance.Get(), &surface_descriptor));
    if (!g_state.wgpu_surface) {
        ResetGraphiteState();
        g_state.device_state = ED_DEVICE_LOST;
        return;
    }

    wgpu::RequestAdapterOptions adapter_options{};
    adapter_options.compatibleSurface = g_state.wgpu_surface;
    adapter_options.powerPreference = wgpu::PowerPreference::HighPerformance;
    g_state.instance.RequestAdapter(
        &adapter_options,
        wgpu::CallbackMode::AllowSpontaneous,
        [attempt_serial](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView) {
            if (attempt_serial != g_state.webgpu_attempt_serial) {
                return;
            }
            if (status != wgpu::RequestAdapterStatus::Success) {
                ResetGraphiteState();
                g_state.device_state = ED_DEVICE_LOST;
                return;
            }

            g_state.adapter = std::move(adapter);
            wgpu::DeviceDescriptor device_descriptor{};
            device_descriptor.SetDeviceLostCallback(
                wgpu::CallbackMode::AllowSpontaneous,
                [](const wgpu::Device&, wgpu::DeviceLostReason, wgpu::StringView) {
                    if (g_state.active_backend == ED_BACKEND_WEBGPU || g_state.device_state == ED_DEVICE_RECOVERING) {
                        MarkDeviceLost(ED_BACKEND_WEBGPU);
                    }
                });
            device_descriptor.SetUncapturedErrorCallback(
                [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView) {
                });
            g_state.adapter.RequestDevice(
                &device_descriptor,
                wgpu::CallbackMode::AllowSpontaneous,
                [attempt_serial](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView) {
                    if (attempt_serial != g_state.webgpu_attempt_serial) {
                        return;
                    }
                    if (status != wgpu::RequestDeviceStatus::Success) {
                        ResetGraphiteState();
                        g_state.device_state = ED_DEVICE_LOST;
                        return;
                    }

                    g_state.device = std::move(device);
                    ConfigureGraphiteSurface();

                    skgpu::graphite::DawnBackendContext dawn_context;
                    dawn_context.fInstance = g_state.instance;
                    dawn_context.fDevice = g_state.device;
                    dawn_context.fQueue = g_state.device.GetQueue();

                    skgpu::graphite::ContextOptions context_options;
                    g_state.graphite_ctx = skgpu::graphite::ContextFactory::MakeDawn(dawn_context, context_options);
                    if (!g_state.graphite_ctx) {
                        ResetGraphiteState();
                        g_state.device_state = ED_DEVICE_LOST;
                        return;
                    }

                    g_state.recorder = g_state.graphite_ctx->makeRecorder();
                    if (!g_state.recorder) {
                        ResetGraphiteState();
                        g_state.device_state = ED_DEVICE_LOST;
                        return;
                    }

                    SetActiveBackend(ED_BACKEND_WEBGPU);
                });
        });
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
    case ED_BACKEND_WEBGPU:
        if (g_state.device && g_state.wgpu_surface) {
            ConfigureGraphiteSurface();
        }
        return;
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

void RenderWebGpuFrame(double current_time_ms) {
    if (!g_state.recorder || !g_state.graphite_ctx || !g_state.wgpu_surface || !g_state.device) {
        return;
    }

    WGPUSurfaceTexture surface_texture = {};
    wgpuSurfaceGetCurrentTexture(g_state.wgpu_surface.Get(), &surface_texture);
    if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        MarkDeviceLost(ED_BACKEND_WEBGPU);
        return;
    }
    if (!surface_texture.texture) {
        MarkDeviceLost(ED_BACKEND_WEBGPU);
        return;
    }

    const SkColorType color_type =
        g_state.surface_format == WGPUTextureFormat_BGRA8Unorm
        ? kBGRA_8888_SkColorType
        : kRGBA_8888_SkColorType;
    auto backend_texture = skgpu::graphite::BackendTextures::MakeDawn(surface_texture.texture);
    auto surface = SkSurfaces::WrapBackendTexture(
        g_state.recorder.get(),
        backend_texture,
        color_type,
        SkColorSpace::MakeSRGB(),
        nullptr);
    if (!surface) {
        wgpuTextureRelease(surface_texture.texture);
        MarkDeviceLost(ED_BACKEND_WEBGPU);
        return;
    }

    g_state.engine.RenderToCanvas(surface->getCanvas(), current_time_ms);
    auto recording = g_state.recorder->snap();
    if (!recording) {
        surface.reset();
        wgpuTextureRelease(surface_texture.texture);
        MarkDeviceLost(ED_BACKEND_WEBGPU);
        return;
    }

    skgpu::graphite::InsertRecordingInfo info;
    info.fRecording = recording.get();
    if (!g_state.graphite_ctx->insertRecording(info)) {
        surface.reset();
        wgpuTextureRelease(surface_texture.texture);
        MarkDeviceLost(ED_BACKEND_WEBGPU);
        return;
    }

    g_state.graphite_ctx->submit(skgpu::graphite::SyncToCpu::kNo);
    surface.reset();
    wgpuTextureRelease(surface_texture.texture);
    effindom_v2_frame_presented();
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
    StartWebGpuInit();
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
    case ED_BACKEND_WEBGPU:
        RenderWebGpuFrame(current_time_ms);
        return;
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
    case ED_BACKEND_WEBGPU:
        ed_init(g_state.engine.physical_width(), g_state.engine.physical_height(), g_state.engine.dpr());
        return;
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
void ed_debug_simulate_device_lost(void) {
    if (g_state.active_backend != ED_BACKEND_NONE) {
        MarkDeviceLost(g_state.active_backend);
    }
}

} // extern "C"
