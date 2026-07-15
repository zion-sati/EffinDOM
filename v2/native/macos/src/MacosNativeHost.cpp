#include "MacosNativeHost.h"
#include "graphics/MacosMetalSurface.h"
#include "input/MacosInputRouter.h"
#include "input/MacosScrollWheelBridge.h"
#include "platform/MacosUiDispatcher.h"
#include "platform/MacosPlatformServices.h"
#include "platform/MacosFileDialogs.h"
#include "platform/MacosDropTarget.h"

#include "Engine.h"
#include "UiPlatformHost.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/encode/SkPngEncoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string_view>

extern "C" {
void __runApp();
void __disposeApp();
void __flushRenders();
void __fui_clear_ui_dispatches();
void __fui_clear_native_file_dialog_callbacks();
bool __fui_complete_native_file_dialog(
    std::uint64_t request_id,
    std::uint32_t status,
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::int32_t selected_filter);
std::uint64_t __fui_native_action_handle();
std::uint64_t __fui_native_body_text_handle();
std::uint32_t __fui_native_activation_count();

bool __fui_on_pointer_event_with_metadata(
    std::uint32_t event_type, std::uint64_t handle, float x, float y,
    std::uint32_t modifiers, std::int32_t pointer_id, std::uint32_t pointer_type,
    std::int32_t button, std::uint32_t buttons, float pressure, float width,
    float height, std::int32_t click_count);
bool __fui_on_key_event(std::uint32_t event_type, const std::uint8_t* key, std::uint32_t len, std::uint32_t modifiers);
void __fui_on_focus_changed(std::uint64_t handle, bool focused);
void __fui_on_text_changed(std::uint64_t handle, const std::uint8_t* text, std::uint32_t len);
void __fui_on_text_replaced(std::uint64_t handle, std::uint32_t start, std::uint32_t end, const std::uint8_t* text, std::uint32_t len);
void __fui_on_selection_changed(std::uint64_t handle, std::uint32_t start, std::uint32_t end);
void __fui_on_cross_selection_changed(std::uint64_t handle, const std::uint8_t* text, std::uint32_t len);
void __fui_on_font_loaded(std::uint32_t font_id);
void __fui_on_svg_loaded(std::uint32_t svg_id, float width, float height);
void __fui_on_viewport_changed(float width, float height);
void __fui_on_system_dark_mode_changed(bool dark_mode);
void __fui_on_scroll(
    std::uint64_t handle, float offset_x, float offset_y, float content_width,
    float content_height, float viewport_width, float viewport_height);
}

namespace effindom::v2::native {
namespace {

using Clock = std::chrono::steady_clock;

MacosNativeHost::Impl* g_host = nullptr;

std::string Utf8(const std::uint8_t* bytes, std::uint32_t length) {
    return bytes == nullptr || length == 0U
        ? std::string{}
        : std::string(reinterpret_cast<const char*>(bytes), length);
}

std::string Utf8(std::uintptr_t pointer, std::uint32_t length) {
    return Utf8(reinterpret_cast<const std::uint8_t*>(pointer), length);
}

} // namespace

struct MacosNativeHost::Impl {
    explicit Impl(bool visible)
        : input_router(engine),
          platform_services(engine, [this] { RequestFrame(); }, visible),
          start_time(Clock::now()) {
        // SDL registers this as AppKit's AppleMomentumScrollSupported default.
        // Precise wheel events bypass SDL below, but AppKit still needs this set
        // before initialization so it produces the native momentum event tail.
        SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_METAL;
        if (!visible) flags |= SDL_WINDOW_HIDDEN;
        window = SDL_CreateWindow("EffinDOM Native FUI-RS", 800, 560, flags);
        if (window == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
        scroll_wheel_bridge = std::make_unique<MacosScrollWheelBridge>(
            window,
            [this](const NativePreciseScrollEvent& event) {
                input_router.DispatchPreciseWheel(
                    event.delta_x,
                    event.delta_y,
                    event.begins_gesture,
                    event.ends_gesture,
                    NowMilliseconds());
                RequestFrame();
            });
        ui_dispatcher = std::make_unique<MacosUiDispatcher>(window);
        drop_target = std::make_unique<MacosDropTarget>(window, engine);
        file_dialogs = std::make_unique<MacosFileDialogs>(window, visible, [](const NativeFileDialogCompletion& completion) {
            std::string payload;
            if (completion.status == NativeFileDialogStatus::Selected) {
                for (const std::string& path : completion.paths) {
                    payload.append(path);
                    payload.push_back('\0');
                }
            } else if (completion.status == NativeFileDialogStatus::Error) {
                payload = completion.error;
            }
            __fui_complete_native_file_dialog(
                completion.request_id,
                static_cast<std::uint32_t>(completion.status),
                reinterpret_cast<const std::uint8_t*>(payload.data()),
                static_cast<std::uint32_t>(payload.size()),
                completion.selected_filter);
        });
        metal_surface = visible ? MacosMetalSurface::Create(window) : nullptr;
        use_metal = metal_surface != nullptr;
        if (!use_metal) InitializeRaster();
        ui::SetGlobalUiPlatformHost(platform_services);
        g_host = this;
        RefreshSurface();
        engine.Init(physical_width, physical_height, pixel_density);
        engine.SetViewportSize(logical_width, logical_height);
        platform_services.LoadDefaultFont(1U, "NotoSans-Regular.ttf");
        platform_services.LoadDefaultFont(2U, "NotoSans-Bold.ttf");
        platform_services.LoadDefaultFont(7U, "NotoSansMono-Regular.ttf");
        if (!SDL_AddEventWatch(&Impl::WatchEvent, this)) {
            throw std::runtime_error(std::string("SDL_AddEventWatch failed: ") + SDL_GetError());
        }
    }

    ~Impl() {
        SDL_RemoveEventWatch(&Impl::WatchEvent, this);
        __disposeApp();
        ui_dispatcher->Clear();
        __fui_clear_ui_dispatches();
        file_dialogs->Clear();
        __fui_clear_native_file_dialog_callbacks();
        scroll_wheel_bridge.reset();
        metal_surface.reset();
        surface.reset();
        if (texture != nullptr) SDL_DestroyTexture(texture);
        if (renderer != nullptr) SDL_DestroyRenderer(renderer);
        if (window != nullptr) SDL_DestroyWindow(window);
        g_host = nullptr;
        SDL_Quit();
    }

    double NowMilliseconds() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_time).count();
    }

    void InitializeRaster() {
        renderer = SDL_CreateRenderer(window, nullptr);
        if (renderer == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        }
        SDL_SetRenderVSync(renderer, 0);
        graphics_generation += 1U;
    }

    void FallBackToRaster() {
        metal_surface.reset();
        use_metal = false;
        InitializeRaster();
    }

    void RefreshSurface() {
        float next_pixel_density = SDL_GetWindowPixelDensity(window);
        if (!std::isfinite(next_pixel_density) || next_pixel_density <= 0.0f) next_pixel_density = 1.0f;
        int output_width = 0;
        int output_height = 0;
        const bool size_available = use_metal
            ? SDL_GetWindowSizeInPixels(window, &output_width, &output_height)
            : SDL_GetRenderOutputSize(renderer, &output_width, &output_height);
        if (!size_available) {
            throw std::runtime_error(std::string("SDL drawable size query failed: ") + SDL_GetError());
        }
        const auto next_physical_width = static_cast<std::uint32_t>(std::max(output_width, 1));
        const auto next_physical_height = static_cast<std::uint32_t>(std::max(output_height, 1));
        const bool storage_changed = next_physical_width != physical_width ||
            next_physical_height != physical_height || (!use_metal && (surface == nullptr || texture == nullptr));
        physical_width = next_physical_width;
        physical_height = next_physical_height;
        pixel_density = next_pixel_density;
        logical_width = static_cast<float>(physical_width) / pixel_density;
        logical_height = static_cast<float>(physical_height) / pixel_density;
        if (!storage_changed) {
            return;
        }

        if (use_metal) return;

        const std::size_t row_bytes = static_cast<std::size_t>(physical_width) * 4U;
        pixels.resize(row_bytes * physical_height);
        const SkImageInfo info = SkImageInfo::MakeN32Premul(physical_width, physical_height);
        surface = SkSurfaces::WrapPixels(info, pixels.data(), row_bytes);
        if (surface == nullptr) {
            throw std::runtime_error("Skia could not wrap the native raster buffer");
        }

        if (texture != nullptr) {
            SDL_DestroyTexture(texture);
        }
        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_BGRA32,
            SDL_TEXTUREACCESS_STREAMING,
            static_cast<int>(physical_width),
            static_cast<int>(physical_height));
        if (texture == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        }
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }

    void RequestFrame() { frame_pending = mounted; }

    bool RefreshWindowGeometry() {
        const auto previous_physical_width = physical_width;
        const auto previous_physical_height = physical_height;
        const auto previous_logical_width = logical_width;
        const auto previous_logical_height = logical_height;
        const auto previous_pixel_density = pixel_density;
        RefreshSurface();
        if (physical_width == previous_physical_width &&
            physical_height == previous_physical_height &&
            logical_width == previous_logical_width &&
            logical_height == previous_logical_height &&
            pixel_density == previous_pixel_density) {
            return false;
        }
        engine.Resize(physical_width, physical_height, pixel_density);
        engine.SetViewportSize(logical_width, logical_height);
        // Match the managed browser harness: notify Tier 3 so responsive
        // signals and the retained shell update, not just Tier 2 layout.
        __fui_on_viewport_changed(logical_width, logical_height);
        return true;
    }

    void ApplyCommittedCommands() {
        std::uint32_t command_count = 0U;
        const std::uint32_t* commands = ui_get_command_buffer(&command_count);
        engine.ExecuteCommandBuffer(commands, command_count);
    }

    bool RunNextFrame() {
        if (!mounted || !frame_pending) return false;
        if (presentation_suspended) {
            frame_pending = false;
            return false;
        }
        frame_pending = false;
        const bool pending_assets = platform_services.ProcessPendingAssets();
        const bool viewport_changed = RefreshWindowGeometry();
        if (use_metal && !metal_surface->PrepareFrame(physical_width, physical_height, pixel_density)) {
            SDL_Log("EffinDOM Metal recovery failed; switching to raster diagnostics");
            FallBackToRaster();
            RefreshSurface();
        }
        __flushRenders();
        if (viewport_changed || input_router.ConsumeCommitRequest() || ui_has_pending_visual_work()) {
            // ResizeWindow dirties Tier 2 even when no Tier 3 viewport signal
            // listener produced a scheduled FUI commit. Pointer-driven text
            // selection also dirties Tier 2 directly and must be committed
            // before pointer-up, matching the browser bridge frame pipeline.
            ui_commit_frame(NowMilliseconds());
            ApplyCommittedCommands();
        }
        SkCanvas* canvas = use_metal ? metal_surface->Canvas() : surface->getCanvas();
        if (canvas == nullptr) {
            frame_pending = true;
            return false;
        }
        canvas->clear(SK_ColorWHITE);
        engine.RenderToCanvas(canvas, NowMilliseconds());
        // Tier 1 is transparent by design for browser composition. Native SDL
        // windows need an opaque compositor backdrop behind retained content.
        canvas->drawColor(SK_ColorWHITE, SkBlendMode::kDstOver);
        if (use_metal) {
            if (!metal_surface->Present()) {
                metal_surface->RequestRecovery();
                frame_pending = true;
                return false;
            }
        } else if (!SDL_UpdateTexture(
                texture,
                nullptr,
                pixels.data(),
                static_cast<int>(static_cast<std::size_t>(physical_width) * 4U))) {
            throw std::runtime_error(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
        }
        if (!use_metal && (!SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) || !SDL_RenderClear(renderer))) {
            throw std::runtime_error(std::string("SDL_RenderClear failed: ") + SDL_GetError());
        }
        const SDL_FRect destination{
            0.0f,
            0.0f,
            static_cast<float>(physical_width),
            static_cast<float>(physical_height),
        };
        if (!use_metal && !SDL_RenderTexture(renderer, texture, nullptr, &destination)) {
            throw std::runtime_error(std::string("SDL_RenderTexture failed: ") + SDL_GetError());
        }
        if (!use_metal) SDL_RenderPresent(renderer);
        frame_count += 1U;
        if (ui_needs_animation_frame() || pending_assets) frame_pending = true;
        return true;
    }

    static bool WatchEvent(void* userdata, SDL_Event* event) {
        auto& host = *static_cast<Impl*>(userdata);
        if (event->type != SDL_EVENT_WINDOW_EXPOSED || event->window.data1 != 1 || host.live_resize_rendering) {
            return true;
        }
        host.live_resize_rendering = true;
        try {
            host.RequestFrame();
            host.RunNextFrame();
        } catch (const std::exception& error) {
            SDL_Log("EffinDOM live resize failed: %s", error.what());
            host.running = false;
        }
        host.live_resize_rendering = false;
        return true;
    }

    SDL_Window* window = nullptr;
    bool use_metal = false;
    std::unique_ptr<MacosMetalSurface> metal_surface;
    std::unique_ptr<MacosUiDispatcher> ui_dispatcher;
    std::unique_ptr<MacosDropTarget> drop_target;
    std::unique_ptr<MacosFileDialogs> file_dialogs;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    std::vector<std::uint8_t> pixels{};
    sk_sp<SkSurface> surface{};
    Engine engine{};
    MacosInputRouter input_router;
    std::unique_ptr<MacosScrollWheelBridge> scroll_wheel_bridge;
    MacosPlatformServices platform_services;
    Clock::time_point start_time;
    float logical_width = 0.0f;
    float logical_height = 0.0f;
    float pixel_density = 1.0f;
    std::uint32_t physical_width = 0U;
    std::uint32_t physical_height = 0U;
    std::uint64_t frame_count = 0U;
    std::uint64_t graphics_generation = 0U;
    std::uint64_t graphics_recovery_count = 0U;
    std::uint32_t mount_count = 0U;
    std::uint32_t dispose_count = 0U;
    bool frame_pending = false;
    bool mounted = false;
    bool running = true;
    bool live_resize_rendering = false;
    bool presentation_suspended = false;
};

MacosNativeHost::MacosNativeHost(bool visible) : impl_(std::make_unique<Impl>(visible)) {}
MacosNativeHost::~MacosNativeHost() { Unmount(); }

void MacosNativeHost::MountApplication() {
    if (impl_->mounted) {
        ++impl_->dispose_count;
    }
    impl_->mounted = true;
    __runApp();
    ++impl_->mount_count;
    ui_resize_window(impl_->logical_width, impl_->logical_height);
    impl_->engine.SetViewportSize(impl_->logical_width, impl_->logical_height);
    RequestFrame();
}

void MacosNativeHost::Unmount() {
    if (!impl_->mounted) return;
    __disposeApp();
    ++impl_->dispose_count;
    impl_->ui_dispatcher->Clear();
    __fui_clear_ui_dispatches();
    impl_->file_dialogs->Clear();
    impl_->drop_target->Clear();
    __fui_clear_native_file_dialog_callbacks();
    impl_->mounted = false;
    impl_->frame_pending = false;
}

void MacosNativeHost::RequestFrame() { impl_->RequestFrame(); }

bool MacosNativeHost::RunNextFrame() {
    return impl_->RunNextFrame();
}

void MacosNativeHost::DrainFrames(std::uint32_t maximum_frames) {
    std::uint32_t count = 0U;
    while (impl_->frame_pending && count < maximum_frames) {
        RunNextFrame();
        count += 1U;
    }
    if (impl_->frame_pending) throw std::runtime_error("native frame queue did not become idle");
}

bool MacosNativeHost::PumpEvent(bool wait_when_idle) {
    SDL_Event event{};
    const bool has_event = wait_when_idle && !impl_->frame_pending
        ? SDL_WaitEventTimeout(&event, 50)
        : SDL_PollEvent(&event);
    if (!has_event) return false;
    if (impl_->metal_surface != nullptr && impl_->metal_surface->HandleRecoveryEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->ui_dispatcher->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->file_dialogs->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->drop_target->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->scroll_wheel_bridge->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->input_router.HandleEvent(event, impl_->NowMilliseconds())) {
        RequestFrame();
        return true;
    }
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            impl_->running = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            RequestFrame();
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            RecreateGraphicsSurface();
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_OCCLUDED:
            impl_->presentation_suspended = true;
            impl_->frame_pending = false;
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
            impl_->presentation_suspended = false;
            RequestFrame();
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            impl_->presentation_suspended = false;
            // Live-resize exposes are rendered synchronously by WatchEvent
            // while Cocoa's modal resize loop blocks ordinary event pumping.
            if (event.window.data1 != 1) RequestFrame();
            break;
        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            __fui_on_system_dark_mode_changed(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
            RequestFrame();
            break;
        default:
            break;
    }
    return true;
}

void MacosNativeHost::Resize(std::uint32_t logical_width, std::uint32_t logical_height) {
    SDL_SetWindowSize(impl_->window, static_cast<int>(logical_width), static_cast<int>(logical_height));
    SDL_SyncWindow(impl_->window);
    RequestFrame();
}

void MacosNativeHost::RecreateGraphicsSurface() {
    if (impl_->metal_surface != nullptr) impl_->metal_surface->RequestRecovery();
    RequestFrame();
}

void MacosNativeHost::DispatchPointer(
    float x,
    float y,
    bool down,
    std::int32_t button,
    std::uint32_t buttons,
    std::int32_t click_count) {
    impl_->input_router.DispatchPointer(
        x, y, down, button, buttons, click_count, impl_->NowMilliseconds());
    RequestFrame();
}

void MacosNativeHost::DispatchPointerMove(float x, float y, std::uint32_t modifiers) {
    impl_->input_router.DispatchPointerMove(x, y, modifiers, impl_->NowMilliseconds());
    RequestFrame();
}

void MacosNativeHost::DispatchWheel(float delta_x, float delta_y) {
    impl_->input_router.DispatchWheel(delta_x, delta_y, impl_->NowMilliseconds());
    RequestFrame();
}

void MacosNativeHost::DispatchKey(const std::string& key, bool down, std::uint32_t modifiers) {
    impl_->input_router.DispatchKey(key, down, modifiers, impl_->NowMilliseconds());
    RequestFrame();
}

void MacosNativeHost::SetClipboardText(const std::string& text) { impl_->platform_services.SetClipboardText(text); }

std::string MacosNativeHost::ClipboardText() const {
    return impl_->platform_services.ClipboardText();
}

bool MacosNativeHost::OpenExternalUrl(const std::string& url) const { return impl_->platform_services.OpenExternalUrl(url); }
bool MacosNativeHost::OpenFile(const std::filesystem::path& path) const { return impl_->platform_services.OpenFile(path); }
bool MacosNativeHost::RevealFile(const std::filesystem::path& path) const { return impl_->platform_services.RevealFile(path); }
void MacosNativeHost::CompleteFileDialogForTesting(
    std::uint64_t request_id,
    std::uint32_t status,
    std::vector<std::string> paths,
    std::string error,
    std::int32_t selected_filter) {
    impl_->file_dialogs->CompleteForTesting(NativeFileDialogCompletion{
        request_id,
        static_cast<NativeFileDialogStatus>(status),
        std::move(paths),
        std::move(error),
        selected_filter,
    });
}

void MacosNativeHost::DispatchDropEventForTesting(
    std::uint32_t event_type,
    float x,
    float y,
    const std::string& data) {
    SDL_Event event{};
    event.type = event_type;
    event.drop.windowID = impl_->window == nullptr ? 0U : SDL_GetWindowID(impl_->window);
    event.drop.x = x;
    event.drop.y = y;
    event.drop.data = data.empty() ? nullptr : data.c_str();
    if (impl_->drop_target->HandleEvent(event)) RequestFrame();
}

bool MacosNativeHost::HasFontForTesting(std::uint32_t font_id) const {
    return impl_->engine.HasFontForTesting(font_id);
}

bool MacosNativeHost::FontHasGlyphForTesting(std::uint32_t font_id, std::uint32_t codepoint) const {
    return impl_->engine.FontHasGlyphForTesting(font_id, codepoint);
}

std::optional<std::pair<float, float>> MacosNativeHost::SvgSizeForTesting(std::uint32_t svg_id) const {
    return impl_->engine.GetSvgSizeForTesting(svg_id);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> MacosNativeHost::TextureSizeForTesting(
    std::uint32_t texture_id) const {
    return impl_->engine.GetTextureSizeForTesting(texture_id);
}

std::size_t MacosNativeHost::TextureCountForTesting() const { return impl_->engine.TextureCountForTesting(); }
std::size_t MacosNativeHost::FallbackFontCountForTesting() const {
    return impl_->platform_services.FallbackFontCountForTesting();
}

void MacosNativeHost::RequestMissingFontCoverageForTesting(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    const std::string& sample_text) {
    impl_->platform_services.ReportMissingFontCoverage(primary_font_id, coverage_kind, sample_text);
}

std::uint64_t MacosNativeHost::HitTest(float x, float y) const { return impl_->engine.HitTest(x, y); }

NativeHostState MacosNativeHost::State() const {
    const std::uint64_t graphics_generation = impl_->metal_surface != nullptr
        ? impl_->metal_surface->Generation()
        : impl_->graphics_generation;
    const std::uint64_t graphics_recovery_count = impl_->metal_surface != nullptr
        ? impl_->metal_surface->RecoveryCount()
        : impl_->graphics_recovery_count;
    return NativeHostState{
        __fui_native_activation_count(), impl_->mount_count, impl_->dispose_count,
        impl_->frame_count, impl_->logical_width,
        impl_->logical_height, impl_->pixel_density, impl_->frame_pending,
        impl_->use_metal, graphics_generation, graphics_recovery_count,
        impl_->presentation_suspended,
    };
}

std::vector<std::uint8_t> MacosNativeHost::SnapshotRgba() const {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(impl_->physical_width) * impl_->physical_height * 4U);
    const SkImageInfo info = SkImageInfo::Make(impl_->physical_width, impl_->physical_height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    SkSurface* active_surface = impl_->metal_surface != nullptr ? impl_->metal_surface->Surface() : impl_->surface.get();
    if (active_surface == nullptr || !active_surface->readPixels(info, pixels.data(), static_cast<std::size_t>(impl_->physical_width) * 4U, 0, 0)) return {};
    return pixels;
}

bool MacosNativeHost::WriteScreenshot(const std::filesystem::path& path, std::string& error) const {
    SkSurface* active_surface = impl_->metal_surface != nullptr ? impl_->metal_surface->Surface() : impl_->surface.get();
    const sk_sp<SkImage> image = active_surface == nullptr ? nullptr : active_surface->makeImageSnapshot();
    if (image == nullptr) {
        error = "failed to snapshot native surface";
        return false;
    }
    SkPixmap pixmap;
    if (!image->peekPixels(&pixmap)) {
        error = "failed to access native surface pixels";
        return false;
    }
    SkFILEWStream output(path.string().c_str());
    if (!output.isValid() || !SkPngEncoder::Encode(&output, pixmap, SkPngEncoder::Options{})) {
        error = "failed to encode native screenshot";
        return false;
    }
    return true;
}

bool MacosNativeHost::IsIdle() const { return !impl_->frame_pending; }
bool MacosNativeHost::IsRunning() const { return impl_->running; }

} // namespace effindom::v2::native

extern "C" {

void request_render() { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->RequestFrame(); }
bool fui_dispatch_to_ui(std::uint64_t callback_id) {
    return effindom::v2::native::g_host != nullptr && effindom::v2::native::g_host->ui_dispatcher->Post(callback_id);
}
bool fui_cancel_ui_dispatch_async(std::uint64_t callback_id) {
    return effindom::v2::native::g_host != nullptr && effindom::v2::native::g_host->ui_dispatcher->Cancel(callback_id);
}
bool fui_native_clipboard_write(const std::uint8_t* text, std::uint32_t length) {
    return effindom::v2::native::g_host != nullptr && effindom::v2::native::g_host->platform_services.SetClipboardText(effindom::v2::native::Utf8(text, length));
}
std::uint32_t fui_native_clipboard_text_length() {
    return effindom::v2::native::g_host == nullptr
        ? 0U
        : static_cast<std::uint32_t>(effindom::v2::native::g_host->platform_services.ClipboardText().size());
}
std::uint32_t fui_native_clipboard_copy(std::uint8_t* destination, std::uint32_t capacity) {
    if (effindom::v2::native::g_host == nullptr) return 0U;
    const std::string text = effindom::v2::native::g_host->platform_services.ClipboardText();
    const auto copied = std::min(capacity, static_cast<std::uint32_t>(text.size()));
    if (destination != nullptr && copied > 0U) std::copy_n(text.data(), copied, destination);
    return copied;
}
bool fui_native_open_external_url(const std::uint8_t* value, std::uint32_t length) {
    return effindom::v2::native::g_host != nullptr && effindom::v2::native::g_host->platform_services.OpenExternalUrl(effindom::v2::native::Utf8(value, length));
}
bool fui_native_open_file(const std::uint8_t* value, std::uint32_t length) {
    return effindom::v2::native::g_host != nullptr && effindom::v2::native::g_host->platform_services.OpenFile(effindom::v2::native::Utf8(value, length));
}
bool fui_native_reveal_file(const std::uint8_t* value, std::uint32_t length) {
    return effindom::v2::native::g_host != nullptr && effindom::v2::native::g_host->platform_services.RevealFile(effindom::v2::native::Utf8(value, length));
}
bool fui_native_show_file_dialog(
    std::uint32_t kind,
    std::uint64_t request_id,
    const std::uint8_t* filters,
    std::uint32_t filters_length,
    const std::uint8_t* default_location,
    std::uint32_t default_location_length,
    bool allow_multiple) {
    if (effindom::v2::native::g_host == nullptr || kind > 2U) return false;
    return effindom::v2::native::g_host->file_dialogs->Show(
        static_cast<effindom::v2::native::NativeFileDialogKind>(kind),
        request_id,
        effindom::v2::native::Utf8(filters, filters_length),
        effindom::v2::native::Utf8(default_location, default_location_length),
        allow_multiple);
}
void fui_native_commit_ready() { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->ApplyCommittedCommands(); }
float get_viewport_width() { return effindom::v2::native::g_host == nullptr ? 0.0f : effindom::v2::native::g_host->logical_width; }
float get_viewport_height() { return effindom::v2::native::g_host == nullptr ? 0.0f : effindom::v2::native::g_host->logical_height; }
float get_device_pixel_ratio() { return effindom::v2::native::g_host == nullptr ? 1.0f : effindom::v2::native::g_host->pixel_density; }
double fui_now_ms() { return effindom::v2::native::g_host == nullptr ? 0.0 : effindom::v2::native::g_host->NowMilliseconds(); }
bool fui_is_dark_mode() { return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK; }
std::uint32_t fui_get_accent_color() { return 0x0A84FFFFU; }
std::uint32_t fui_get_platform_family() { return 1U; }
bool fui_is_coarse_pointer() { return false; }
void fui_set_pointer_capture(std::uint64_t handle) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->input_router.Capture(handle); }
void fui_release_pointer_capture() { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->input_router.ReleaseCapture(); }
void fui_copy_text(std::uintptr_t pointer, std::uint32_t length) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.SetClipboardText(effindom::v2::native::Utf8(pointer, length)); }
void fui_set_cursor(std::uint32_t style) {
    if (effindom::v2::native::g_host == nullptr) return;
    effindom::v2::native::g_host->platform_services.SetCursor(style);
}
void fui_load_font(std::uint32_t font_id, std::uintptr_t pointer, std::uint32_t length) {
    if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.RequestFontLoad(font_id, effindom::v2::native::Utf8(pointer, length));
}
void fui_load_svg(std::uint32_t svg_id, std::uintptr_t pointer, std::uint32_t length) {
    if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.LoadSvg(svg_id, effindom::v2::native::Utf8(pointer, length));
}
void fui_release_svg(std::uint32_t svg_id) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.ReleaseSvg(svg_id); }
void fui_load_texture(std::uint32_t texture_id, std::uintptr_t pointer, std::uint32_t length) {
    if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.LoadTexture(texture_id, effindom::v2::native::Utf8(pointer, length));
}
void fui_release_texture(std::uint32_t texture_id) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.ReleaseTexture(texture_id); }
void fui_reload_page() {}
bool fui_can_navigate_back() { return false; }
bool fui_can_navigate_forward() { return false; }
void fui_navigate_back() {}
void fui_navigate_forward() {}
void fui_show_url_preview(std::uintptr_t, std::uint32_t) {}
void fui_hide_url_preview() {}
void fui_navigate_to(std::uintptr_t pointer, std::uint32_t length, bool) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.OpenExternalUrl(effindom::v2::native::Utf8(pointer, length)); }
void fui_log(std::uintptr_t, std::uint32_t, std::uintptr_t message, std::uint32_t length) { SDL_Log("%s", effindom::v2::native::Utf8(message, length).c_str()); }
bool fui_logs_enabled() { return true; }

void as_on_focus_changed(ui_handle_t handle, bool focused) { __fui_on_focus_changed(handle, focused); }
void as_on_pointer_event(ui_handle_t handle, UiEvent event) {
    if (event < UI_EVENT_POINTER_DOWN || event > UI_EVENT_POINTER_CANCEL) return;
    if (effindom::v2::native::g_host == nullptr) return;
    auto metadata = effindom::v2::native::g_host->input_router.PointerMetadata();
    metadata.event_type = static_cast<std::uint32_t>(event);
    metadata.handle = handle;
    __fui_on_pointer_event_with_metadata(metadata.event_type, handle, metadata.x, metadata.y, metadata.modifiers, metadata.pointer_id, metadata.pointer_type, metadata.button, metadata.buttons, metadata.pressure, metadata.width, metadata.height, metadata.click_count);
}
void as_on_text_changed(ui_handle_t handle, const std::uint8_t* text, std::uint32_t len) { __fui_on_text_changed(handle, text, len); }
void as_on_text_replaced(ui_handle_t handle, std::uint32_t start, std::uint32_t end, const std::uint8_t* text, std::uint32_t len) { __fui_on_text_replaced(handle, start, end, text, len); }
void as_on_scroll(ui_handle_t handle, float offset_x, float offset_y, float content_width, float content_height, float viewport_width, float viewport_height) {
    __fui_on_scroll(handle, offset_x, offset_y, content_width, content_height, viewport_width, viewport_height);
}
void as_on_selection_changed(ui_handle_t handle, std::uint32_t start, std::uint32_t end) { __fui_on_selection_changed(handle, start, end); }
void as_on_cross_selection_changed(ui_handle_t handle, const std::uint8_t* text, std::uint32_t len) { __fui_on_cross_selection_changed(handle, text, len); }
void as_on_clipboard_write(const std::uint8_t* text, std::uint32_t length, const std::uint8_t*, std::uint32_t) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.SetClipboardText(effindom::v2::native::Utf8(text, length)); }
void as_on_request_clipboard_read(ui_handle_t handle) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.RequestClipboardRead(handle); }
void as_on_request_font_load(std::uint32_t font_id, const std::uint8_t* url, std::uint32_t length) { if (effindom::v2::native::g_host != nullptr) effindom::v2::native::g_host->platform_services.RequestFontLoad(font_id, effindom::v2::native::Utf8(url, length)); }
void as_on_missing_font_coverage(std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t) {}
void as_on_request_semantic_announcement(ui_handle_t) {}

}
