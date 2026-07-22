#include "LinuxNativePlatform.h"
#include "LinuxResizeSyncBridge.h"
#include "LinuxSystemThemeBridge.h"
#include "NativeFuiBridge.h"
#include "NativeGraphicsCoordinator.h"
#include "NativeHostCore.h"
#include "NativeInputRouter.h"
#include "NativeRasterSurface.h"
#include "SdlDropTarget.h"
#include "SdlEventAdapter.h"
#include "SdlFileDialogs.h"
#include "SdlUiDispatcher.h"
#include "graphics/LinuxVulkanSurface.h"
#include "platform/LinuxPlatformServices.h"

#include "Engine.h"
#include "UiPlatformHost.h"
#include "effindom_ui.h"
#include "fui_host_abi.h"

#include "SDL3/SDL.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>

namespace effindom::v2::native {
struct LinuxNativePlatform::Impl {
    explicit Impl(bool visible)
        : core(NativeInputRouterOptions{false, true, false}, NativeHostCoreCallbacks{
              [this] { return platform_services.ProcessPendingAssets(); },
              [this] {
                  ui_dispatcher->Clear();
                  __fui_clear_ui_dispatches();
                  file_dialogs->Clear();
                  drop_target->Clear();
                  __fui_clear_native_file_dialog_callbacks();
              },
          }),
          input_adapter(core.InputRouter(), SdlEventAdapterOptions{false, true, false}),
          platform_services(core.GetEngine(), [this] { RequestFrame(); },
              [this](std::uint64_t handle) { core.AnnounceSemantic(handle); }, visible) {
        // Use the staged Cairo backend deterministically. Some distro plugin
        // directories also contain a GTK backend whose selection and ABI are
        // outside the application's control.
        SDL_Environment* environment = SDL_GetEnvironment();
        if (SDL_GetEnvironmentVariable(environment, "LIBDECOR_PLUGIN_DIR") == nullptr) {
            const std::filesystem::path plugin_directory =
                (std::filesystem::path(SDL_GetBasePath()) /
                    "../lib/libdecor/plugins-1").lexically_normal();
            SDL_SetEnvironmentVariable(
                environment, "LIBDECOR_PLUGIN_DIR", plugin_directory.c_str(), false);
        }
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        core.SetSystemDarkMode(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
        // Install backend-specific native window protocols before the window
        // manager sees the initial map. In particular, Mutter must discover
        // the X11 resize-sync counter before the first interactive resize.
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE |
            SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
#if defined(EFFINDOM_SKIA_VULKAN)
        if (visible) flags |= SDL_WINDOW_VULKAN;
#endif
        window = SDL_CreateWindow("EffinDOM Native FUI-RS", 800, 560, flags);
        if (window == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
        // SDL's own X11 backend deliberately excludes Vulkan from XSync
        // resize synchronization because the compositor wait can withhold the
        // swapchain image needed to produce its acknowledgement frame. Vulkan
        // uses WSI presentation scaling/top-left gravity instead.
#if defined(EFFINDOM_SKIA_VULKAN)
        resize_sync = LinuxResizeSyncBridge::Create(nullptr);
#else
        resize_sync = LinuxResizeSyncBridge::Create(visible ? window : nullptr);
#endif
        const float initial_content_scale = SdlEventAdapter::DisplayContentScale(
            SDL_GetWindowDisplayScale(window), SDL_GetWindowPixelDensity(window));
        if (initial_content_scale != 1.0f) {
            SDL_SetWindowSize(
                window,
                static_cast<int>(std::lround(800.0f * initial_content_scale)),
                static_cast<int>(std::lround(560.0f * initial_content_scale)));
        }
        if (visible && (!SDL_ShowWindow(window) || !SDL_SyncWindow(window))) {
            throw std::runtime_error(std::string("SDL_ShowWindow failed: ") + SDL_GetError());
        }
        core.AttachAccessibility(nullptr);
        SDL_Log("EffinDOM Linux video driver: %s", SDL_GetCurrentVideoDriver());
        ui_dispatcher = std::make_unique<SdlUiDispatcher>(window);
        drop_target = std::make_unique<SdlDropTarget>(window, core.GetEngine());
        file_dialogs = std::make_unique<SdlFileDialogs>(window, visible, [](const NativeFileDialogCompletion& completion) {
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
        std::unique_ptr<NativeGraphicsSurface> preferred_surface;
#if defined(EFFINDOM_SKIA_VULKAN)
        if (visible) preferred_surface = LinuxVulkanSurface::Create(window);
#endif
        auto graphics = NativeGraphicsCoordinator::Create(
            window,
            NativeGraphicsOptions{
                NativePixelDensitySource::DisplayScale,
                // Hidden snapshot hosts and Vulkan recovery use the shared
                // top-left raster surface. It is an initialization fallback
                // only; a live Vulkan host always recovers back to Vulkan.
                NativeRasterSurfaceOptions{true, !visible},
            },
            std::move(preferred_surface));
        if (graphics == nullptr) {
            throw std::runtime_error(std::string("native graphics initialization failed: ") + SDL_GetError());
        }
        core.AttachGraphics(std::move(graphics));
        ui::SetGlobalUiPlatformHost(platform_services);
        UiHostCallbacks callbacks{};
        callbacks.on_focus_changed = &as_on_focus_changed;
        callbacks.on_pointer_event = &as_on_pointer_event;
        callbacks.on_text_changed = &as_on_text_changed;
        callbacks.on_text_replaced = &as_on_text_replaced;
        callbacks.on_scroll = &as_on_scroll;
        callbacks.on_selection_changed = &as_on_selection_changed;
        callbacks.on_cross_selection_changed = &as_on_cross_selection_changed;
        callbacks.on_clipboard_write = &as_on_clipboard_write;
        callbacks.on_request_clipboard_read = &as_on_request_clipboard_read;
        callbacks.on_request_font_load = &as_on_request_font_load;
        callbacks.on_missing_font_coverage = &as_on_missing_font_coverage;
        callbacks.on_request_semantic_announcement = &as_on_request_semantic_announcement;
        ui_set_host_callbacks(&callbacks);
        core.InitializeEngine();
        platform_services.LoadDefaultFont(1U, "NotoSans-Regular.ttf");
        platform_services.LoadDefaultFont(2U, "NotoSans-Bold.ttf");
        platform_services.LoadDefaultFont(7U, "NotoSansMono-Regular.ttf");
        system_theme_bridge = std::make_unique<LinuxSystemThemeBridge>(
            window,
            [this](std::uint32_t color) {
                __fui_on_system_accent_color_changed(color);
                RequestFrame();
            });

        const char* diagnostic = SDL_GetEnvironmentVariable(
            SDL_GetEnvironment(), "EFFINDOM_LINUX_CHARACTERIZE");
        characterize = diagnostic != nullptr && diagnostic[0] != '\0' && diagnostic[0] != '0';
    }

    ~Impl() {
        system_theme_bridge.reset();
        ui::ClearGlobalUiPlatformHost(platform_services);
        ui_dispatcher->Clear();
        __fui_clear_ui_dispatches();
        file_dialogs->Clear();
        __fui_clear_native_file_dialog_callbacks();
        core.AttachAccessibility(nullptr);
        core.ReleaseGraphics();
        resize_sync.reset();
        if (window != nullptr) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    double NowMilliseconds() const { return core.NowMilliseconds(); }
    void RequestFrame() { core.RequestFrame(); }
    void ApplyManagedCommittedCommands() { core.ApplyManagedCommittedCommands(); }
    bool RunNextFrame() {
        const bool presented = core.RunNextFrame();
        if (presented) resize_sync->DidPresentFrame();
        return presented;
    }

    static bool IsResizeGeometryEvent(const SDL_Event& event) {
        switch (event.type) {
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                return true;
            default:
                return false;
        }
    }

    bool NextEvent(SDL_Event& event, bool wait_when_idle) {
        if (deferred_event.has_value()) {
            event = *deferred_event;
            deferred_event.reset();
            return true;
        }
        if (!wait_when_idle || core.IsFramePending()) return SDL_PollEvent(&event);
        if (!resize_sync->IsEnabled()) return SDL_WaitEventTimeout(&event, 50);

        // SDL intentionally disables XSync for Vulkan, but still consumes its
        // raw ClientMessage while inside SDL_WaitEventTimeout. Keep that wait
        // outside SDL so the native bridge always gets first refusal.
        if (SDL_PollEvent(&event)) return true;
        resize_sync->WaitForNativeEvents(8);
        resize_sync->PumpNativeEvents();
        return SDL_PollEvent(&event);
    }

    void CoalesceResizeGeometry(SDL_Event& event) {
        if (!IsResizeGeometryEvent(event)) return;
        const std::uint32_t window_id = event.window.windowID;
        SDL_Event next{};
        while (SDL_PollEvent(&next)) {
            if (IsResizeGeometryEvent(next) && next.window.windowID == window_id) {
                event = next;
                continue;
            }
            deferred_event = next;
            break;
        }
    }

    void LogCharacterization(const SDL_Event& event) {
        if (!characterize) return;
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            const double interval_ms = last_wheel_timestamp_ns == 0U
                ? 0.0
                : static_cast<double>(event.wheel.timestamp - last_wheel_timestamp_ns) / 1'000'000.0;
            last_wheel_timestamp_ns = event.wheel.timestamp;
            SDL_Log("EffinDOM Linux wheel: driver=%s x=%.6f y=%.6f integer_x=%d "
                    "integer_y=%d direction=%d mouse=(%.2f,%.2f) timestamp_ns=%llu "
                    "interval_ms=%.3f animation=%d",
                SDL_GetCurrentVideoDriver(), event.wheel.x, event.wheel.y,
                event.wheel.integer_x, event.wheel.integer_y,
                static_cast<int>(event.wheel.direction), event.wheel.mouse_x,
                event.wheel.mouse_y,
                static_cast<unsigned long long>(event.wheel.timestamp), interval_ms,
                ui_needs_animation_frame() ? 1 : 0);
            return;
        }
        switch (event.type) {
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_EXPOSED: {
                int logical_width = 0;
                int logical_height = 0;
                int pixel_width = 0;
                int pixel_height = 0;
                SDL_GetWindowSize(window, &logical_width, &logical_height);
                SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
                const auto state = core.State();
                const std::uint64_t now_ns = SDL_GetTicksNS();
                const double queue_age_ms = event.common.timestamp <= now_ns
                    ? static_cast<double>(now_ns - event.common.timestamp) / 1'000'000.0
                    : 0.0;
                SDL_Log("EffinDOM Linux resize: driver=%s event=%u logical=%dx%d "
                        "pixels=%dx%d density=%.3f frames=%llu suspended=%d timestamp_ns=%llu "
                        "queue_age_ms=%.3f",
                    SDL_GetCurrentVideoDriver(), event.type, logical_width, logical_height,
                    pixel_width, pixel_height, SDL_GetWindowPixelDensity(window),
                    static_cast<unsigned long long>(state.frame_count),
                    state.presentation_suspended ? 1 : 0,
                    static_cast<unsigned long long>(event.common.timestamp), queue_age_ms);
                break;
            }
            default:
                break;
        }
    }

    SDL_Window* window = nullptr;
    NativeHostCore core;
    std::unique_ptr<LinuxResizeSyncBridge> resize_sync;
    std::unique_ptr<SdlUiDispatcher> ui_dispatcher;
    std::unique_ptr<SdlDropTarget> drop_target;
    std::unique_ptr<SdlFileDialogs> file_dialogs;
    std::unique_ptr<LinuxSystemThemeBridge> system_theme_bridge;
    SdlEventAdapter input_adapter;
    LinuxPlatformServices platform_services;
    bool characterize = false;
    bool present_after_last_event = false;
    std::optional<SDL_Event> deferred_event;
    std::uint64_t last_wheel_timestamp_ns = 0U;
};

LinuxNativePlatform::LinuxNativePlatform(bool visible) : impl_(std::make_unique<Impl>(visible)) {}
LinuxNativePlatform::~LinuxNativePlatform() = default;

NativeHostCore& LinuxNativePlatform::Core() { return impl_->core; }
const NativeHostCore& LinuxNativePlatform::Core() const { return impl_->core; }
std::uint32_t LinuxNativePlatform::CurrentPointerButtons() const {
    return impl_->input_adapter.CurrentButtons();
}
std::uint32_t LinuxNativePlatform::CurrentModifiers() const {
    return impl_->input_adapter.CurrentModifiers();
}
bool LinuxNativePlatform::PostUiDispatch(std::uint64_t callback_id) {
    return impl_->ui_dispatcher->Post(callback_id);
}
bool LinuxNativePlatform::CancelUiDispatch(std::uint64_t callback_id) {
    return impl_->ui_dispatcher->Cancel(callback_id);
}
void LinuxNativePlatform::RequestFrame() { impl_->core.RequestFrame(); }

bool LinuxNativePlatform::PumpEvent(bool wait_when_idle) {
    impl_->present_after_last_event = false;
    impl_->resize_sync->PumpNativeEvents();
    // Tier 2 animations can become active before a queued wake event is
    // observed. Promote that state before entering the bounded idle wait.
    if (impl_->core.IsMounted() && !impl_->core.Graphics().IsSuspended() && ui_needs_animation_frame()) {
        RequestFrame();
    }
    SDL_Event event{};
    const bool has_event = impl_->NextEvent(event, wait_when_idle);
    if (!has_event) return false;
    impl_->CoalesceResizeGeometry(event);
    impl_->resize_sync->HandleSdlEvent(event);
    impl_->present_after_last_event = SdlEventAdapter::EndsInputBatch(event.type);
    if (impl_->core.Graphics().HandleRecoveryEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->ui_dispatcher->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->system_theme_bridge->HandleEvent(event)) return true;
    if (impl_->file_dialogs->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->drop_target->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->input_adapter.HandleEvent(event, impl_->NowMilliseconds())) {
        RequestFrame();
        impl_->LogCharacterization(event);
        return true;
    }
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            impl_->core.Stop();
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            RequestFrame();
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            RecreateGraphicsSurface();
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_OCCLUDED:
            impl_->core.InputRouter().HandleWindowFocusLost(impl_->NowMilliseconds());
            SDL_CaptureMouse(false);
            impl_->core.Graphics().SetSuspended(true);
            impl_->core.CancelPendingFrame();
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // An unfocused desktop window remains visible and may still
            // receive native wheel input. Cancel active pointer interaction,
            // but do not suspend its presentation lifecycle.
            impl_->core.InputRouter().HandleWindowFocusLost(impl_->NowMilliseconds());
            SDL_CaptureMouse(false);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            impl_->core.Graphics().SetSuspended(false);
            RequestFrame();
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
            impl_->core.Graphics().SetSuspended(false);
            RequestFrame();
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            impl_->core.Graphics().SetSuspended(false);
            RequestFrame();
            break;
        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            impl_->core.SetSystemDarkMode(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
            __fui_on_system_dark_mode_changed(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
            RequestFrame();
            break;
        default:
            break;
    }
    impl_->LogCharacterization(event);
    return true;
}

bool LinuxNativePlatform::ShouldPresentAfterLastEvent() const {
    return impl_->present_after_last_event;
}

void LinuxNativePlatform::Resize(std::uint32_t logical_width, std::uint32_t logical_height) {
    const float content_scale = SdlEventAdapter::DisplayContentScale(
        SDL_GetWindowDisplayScale(impl_->window),
        SDL_GetWindowPixelDensity(impl_->window));
    SDL_SetWindowSize(
        impl_->window,
        static_cast<int>(std::lround(static_cast<float>(logical_width) * content_scale)),
        static_cast<int>(std::lround(static_cast<float>(logical_height) * content_scale)));
    SDL_SyncWindow(impl_->window);
    RequestFrame();
}

void LinuxNativePlatform::RecreateGraphicsSurface() {
    impl_->core.Graphics().RequestRecovery();
    RequestFrame();
}

void LinuxNativePlatform::DispatchWindowFocusLost() {
    impl_->core.InputRouter().HandleWindowFocusLost(impl_->NowMilliseconds());
    SDL_CaptureMouse(false);
    RequestFrame();
}

void LinuxNativePlatform::SetClipboardText(const std::string& text) { impl_->platform_services.SetClipboardText(text); }

std::string LinuxNativePlatform::ClipboardText() const {
    return impl_->platform_services.ClipboardText();
}
void LinuxNativePlatform::RequestClipboardRead(std::uint64_t handle) {
    impl_->platform_services.RequestClipboardRead(handle);
}

bool LinuxNativePlatform::OpenExternalUrl(const std::string& url) const { return impl_->platform_services.OpenExternalUrl(url); }
bool LinuxNativePlatform::OpenFile(const std::filesystem::path& path) const { return impl_->platform_services.OpenFile(path); }
bool LinuxNativePlatform::RevealFile(const std::filesystem::path& path) const { return impl_->platform_services.RevealFile(path); }
bool LinuxNativePlatform::ShowFileDialog(
    std::uint32_t kind,
    std::uint64_t request_id,
    const std::string& filters,
    const std::string& default_location,
    bool allow_multiple) {
    if (kind > 2U) return false;
    return impl_->file_dialogs->Show(
        static_cast<NativeFileDialogKind>(kind), request_id, filters, default_location, allow_multiple);
}
bool LinuxNativePlatform::IsDarkMode() const { return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK; }
std::uint32_t LinuxNativePlatform::AccentColor() const {
    return impl_->system_theme_bridge == nullptr
        ? detail::kLinuxAccentColorFallback
        : impl_->system_theme_bridge->AccentColor();
}
std::uint32_t LinuxNativePlatform::PlatformFamily() const { return FUI_PLATFORM_LINUX; }
std::uint32_t LinuxNativePlatform::HostCapabilities() const {
    return FUI_HOST_CAPABILITY_OPEN_EXTERNAL_URI |
           FUI_HOST_CAPABILITY_CLIPBOARD_READ |
           FUI_HOST_CAPABILITY_CLIPBOARD_WRITE |
           FUI_HOST_CAPABILITY_FILE_DIALOGS;
}
bool LinuxNativePlatform::IsCoarsePointer() const { return false; }
void LinuxNativePlatform::SetApplicationCaption(const std::string& caption) {
    SDL_SetWindowTitle(impl_->window, caption.c_str());
}
void LinuxNativePlatform::SetNativePointerCapture(bool captured) { SDL_CaptureMouse(captured); }
void LinuxNativePlatform::SetCursor(std::uint32_t style) { impl_->platform_services.SetCursor(style); }
void LinuxNativePlatform::RequestFontLoad(std::uint32_t font_id, const std::string& source) {
    impl_->platform_services.RequestFontLoad(font_id, source);
}
void LinuxNativePlatform::LoadSvg(std::uint32_t svg_id, const std::string& source) {
    impl_->platform_services.LoadSvg(svg_id, source);
}
void LinuxNativePlatform::ReleaseSvg(std::uint32_t svg_id) { impl_->platform_services.ReleaseSvg(svg_id); }
void LinuxNativePlatform::LoadTexture(std::uint32_t texture_id, const std::string& source) {
    impl_->platform_services.LoadTexture(texture_id, source);
}
void LinuxNativePlatform::ReleaseTexture(std::uint32_t texture_id) {
    impl_->platform_services.ReleaseTexture(texture_id);
}
void LinuxNativePlatform::CompleteFileDialogForTesting(
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

void LinuxNativePlatform::DispatchDropEventForTesting(
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

std::size_t LinuxNativePlatform::FallbackFontCountForTesting() const {
    return impl_->platform_services.FallbackFontCountForTesting();
}

void LinuxNativePlatform::RequestMissingFontCoverageForTesting(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    const std::string& sample_text) {
    impl_->platform_services.ReportMissingFontCoverage(primary_font_id, coverage_kind, sample_text);
}


} // namespace effindom::v2::native
