#include "WindowsNativePlatform.h"
#include "NativeFuiBridge.h"
#include "NativeGraphicsCoordinator.h"
#include "NativeHostCore.h"
#include "NativeUtf8.h"
#include "graphics/WindowsGpuSurface.h"
#include "NativeInputRouter.h"
#include "NativeRasterSurface.h"
#include "SdlDropTarget.h"
#include "SdlEventAdapter.h"
#include "SdlFileDialogs.h"
#include "SdlUiDispatcher.h"
#include "input/WindowsScrollWheelBridge.h"
#include "platform/WindowsAccessibilityAdapter.h"
#include "platform/WindowsPlatformServices.h"
#include "platform/WindowsSystemThemeBridge.h"

#include "Engine.h"
#include "UiPlatformHost.h"
#include "UiRuntime.h"
#include "effindom_ui.h"
#include "fui_host_abi.h"

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

namespace effindom::v2::native {
struct WindowsNativePlatform::Impl {
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
          input_adapter(core.InputRouter(), SdlEventAdapterOptions{true, false, false}),
          platform_services(core.GetEngine(), [this] { RequestFrame(); },
              [this](std::uint64_t handle) { core.AnnounceSemantic(handle); }, visible) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        core.SetSystemDarkMode(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (!visible) flags |= SDL_WINDOW_HIDDEN;
        window = SDL_CreateWindow("EffinDOM Native FUI-RS", 800, 560, flags);
        if (window == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
        core.AttachAccessibility(CreateWindowsAccessibilityAdapter(window,
            [this](NativeAccessibilityAction action, std::uint64_t handle) {
                core.Accessibility().PerformAction(action, handle);
            }));
        const float initial_display_scale = SDL_GetWindowDisplayScale(window);
        if (std::isfinite(initial_display_scale) && initial_display_scale > 0.0f &&
            initial_display_scale != 1.0f) {
            SDL_SetWindowSize(
                window,
                static_cast<int>(std::lround(800.0f * initial_display_scale)),
                static_cast<int>(std::lround(560.0f * initial_display_scale)));
            SDL_SyncWindow(window);
        }
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
        auto graphics = NativeGraphicsCoordinator::Create(
            window,
            NativeGraphicsOptions{
                NativePixelDensitySource::DisplayScale,
                NativeRasterSurfaceOptions{true},
            },
            visible ? WindowsGpuSurface::Create(window) : nullptr);
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
        system_theme_bridge = std::make_unique<WindowsSystemThemeBridge>(
            window,
            [this](std::uint32_t color) {
                __fui_on_system_accent_color_changed(color);
                RequestFrame();
            });
        scroll_wheel_bridge = std::make_unique<WindowsScrollWheelBridge>(
            window,
            [this](const NativeWheelEvent& event) {
                if (event.precise) {
                    core.InputRouter().DispatchPreciseWheel(
                        event.delta_x,
                        event.delta_y,
                        event.begins_gesture,
                        event.ends_gesture,
                        NowMilliseconds());
                    RequestFrame();
                } else if (event.delta_x != 0.0f || event.delta_y != 0.0f) {
                    core.InputRouter().DispatchWheel(event.delta_x, event.delta_y, NowMilliseconds());
                    RequestFrame();
                }
            },
            [this](const NativeMouseEvent& event) {
                if (event.type == NativeMouseEvent::Type::Move) {
                    core.InputRouter().DispatchPointerMove(NativePointerMoveInput{
                        event.x, event.y, event.buttons, event.modifiers, NowMilliseconds(),
                    });
                } else {
                    core.InputRouter().DispatchPointer(NativePointerInput{
                        event.x, event.y, event.type == NativeMouseEvent::Type::Down,
                        0, event.buttons, event.click_count, event.modifiers,
                        NowMilliseconds(),
                    });
                }
                RequestFrame();
            },
            [this] { RenderNativePaint(); });
    }

    ~Impl() {
        system_theme_bridge.reset();
        ui::ClearGlobalUiPlatformHost(platform_services);
        ui_dispatcher->Clear();
        __fui_clear_ui_dispatches();
        file_dialogs->Clear();
        __fui_clear_native_file_dialog_callbacks();
        scroll_wheel_bridge.reset();
        core.AttachAccessibility(nullptr);
        core.ReleaseGraphics();
        if (window != nullptr) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    double NowMilliseconds() const { return core.NowMilliseconds(); }
    void RequestFrame() { core.RequestFrame(); }
    void ApplyManagedCommittedCommands() { core.ApplyManagedCommittedCommands(); }
    bool RunNextFrame() { return core.RunNextFrame(); }

    void RenderNativePaint() {
        if (live_resize_rendering || !core.IsMounted()) return;
        live_resize_rendering = true;
        core.Graphics().SetSuspended(false);
        try {
            RequestFrame();
            RunNextFrame();
            // A viewport commit can schedule a retained follow-up frame, and
            // ResizeBuffers can schedule graphics recovery. BeginPaint has
            // already validated this paint region, so consume one bounded
            // follow-up here instead of waiting until the modal sizing loop
            // exits. Never drain continuously-active animations in WM_PAINT.
            if (core.IsFramePending()) {
                RunNextFrame();
            }
        } catch (const std::exception& error) {
            SDL_Log("EffinDOM live resize failed: %s", error.what());
            core.Stop();
        }
        live_resize_rendering = false;
    }

    SDL_Window* window = nullptr;
    NativeHostCore core;
    std::unique_ptr<SdlUiDispatcher> ui_dispatcher;
    std::unique_ptr<SdlDropTarget> drop_target;
    std::unique_ptr<SdlFileDialogs> file_dialogs;
    SdlEventAdapter input_adapter;
    std::unique_ptr<WindowsSystemThemeBridge> system_theme_bridge;
    std::unique_ptr<WindowsScrollWheelBridge> scroll_wheel_bridge;
    WindowsPlatformServices platform_services;
    bool live_resize_rendering = false;
};

WindowsNativePlatform::WindowsNativePlatform(bool visible) : impl_(std::make_unique<Impl>(visible)) {}
WindowsNativePlatform::~WindowsNativePlatform() = default;

NativeHostCore& WindowsNativePlatform::Core() { return impl_->core; }
const NativeHostCore& WindowsNativePlatform::Core() const { return impl_->core; }
std::uint32_t WindowsNativePlatform::CurrentPointerButtons() const {
    return impl_->input_adapter.CurrentButtons();
}
std::uint32_t WindowsNativePlatform::CurrentModifiers() const {
    return impl_->input_adapter.CurrentModifiers();
}
bool WindowsNativePlatform::PostUiDispatch(std::uint64_t callback_id) {
    return impl_->ui_dispatcher->Post(callback_id);
}
bool WindowsNativePlatform::CancelUiDispatch(std::uint64_t callback_id) {
    return impl_->ui_dispatcher->Cancel(callback_id);
}
void WindowsNativePlatform::RequestFrame() { impl_->core.RequestFrame(); }

 bool WindowsNativePlatform::PumpEvent(bool wait_when_idle) {
    // Tier 2 animations can become active before a queued wake event is
    // observed. Promote that state before entering the idle wait; D3D
    // Present(1) provides display-rate pacing for the render loop.
    if (impl_->core.IsMounted() && !impl_->core.Graphics().IsSuspended() && ui_needs_animation_frame()) {
        RequestFrame();
    }
    SDL_Event event{};
    const bool has_event = wait_when_idle && !impl_->core.IsFramePending()
        ? SDL_WaitEventTimeout(&event, 50)
        : SDL_PollEvent(&event);
    if (!has_event) return false;
    if (impl_->core.Graphics().HandleRecoveryEvent(event)) {
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
    if (impl_->system_theme_bridge->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->scroll_wheel_bridge->HandleEvent(event)) {
        RequestFrame();
        return true;
    }
    if (impl_->input_adapter.HandleEvent(event, impl_->NowMilliseconds())) {
        RequestFrame();
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
            impl_->scroll_wheel_bridge->SetPointerCapture(false);
            impl_->core.Graphics().SetSuspended(true);
            impl_->core.CancelPendingFrame();
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // An unfocused desktop window remains visible and may still
            // receive native wheel input. Cancel active pointer interaction,
            // but do not suspend its presentation lifecycle.
            impl_->core.InputRouter().HandleWindowFocusLost(impl_->NowMilliseconds());
            impl_->scroll_wheel_bridge->SetPointerCapture(false);
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
    return true;
}

void WindowsNativePlatform::Resize(std::uint32_t logical_width, std::uint32_t logical_height) {
    const auto frames_before_resize = impl_->core.FrameCount();
    const float display_scale = SDL_GetWindowDisplayScale(impl_->window);
    const float valid_scale = std::isfinite(display_scale) && display_scale > 0.0f ? display_scale : 1.0f;
    SDL_SetWindowSize(
        impl_->window,
        static_cast<int>(std::lround(static_cast<float>(logical_width) * valid_scale)),
        static_cast<int>(std::lround(static_cast<float>(logical_height) * valid_scale)));
    SDL_SyncWindow(impl_->window);
    if (impl_->core.FrameCount() == frames_before_resize) RequestFrame();
}

void WindowsNativePlatform::RecreateGraphicsSurface() {
    impl_->core.Graphics().RequestRecovery();
    RequestFrame();
}

 void WindowsNativePlatform::DispatchWindowFocusLost() {
    impl_->core.InputRouter().HandleWindowFocusLost(impl_->NowMilliseconds());
    impl_->scroll_wheel_bridge->SetPointerCapture(false);
    RequestFrame();
}

void WindowsNativePlatform::SetClipboardText(const std::string& text) { impl_->platform_services.SetClipboardText(text); }

std::string WindowsNativePlatform::ClipboardText() const {
    return impl_->platform_services.ClipboardText();
}
void WindowsNativePlatform::RequestClipboardRead(std::uint64_t handle) {
    impl_->platform_services.RequestClipboardRead(handle);
}

bool WindowsNativePlatform::OpenExternalUrl(const std::string& url) const { return impl_->platform_services.OpenExternalUrl(url); }
bool WindowsNativePlatform::OpenFile(const std::filesystem::path& path) const { return impl_->platform_services.OpenFile(path); }
bool WindowsNativePlatform::RevealFile(const std::filesystem::path& path) const { return impl_->platform_services.RevealFile(path); }
bool WindowsNativePlatform::ShowFileDialog(
    std::uint32_t kind,
    std::uint64_t request_id,
    const std::string& filters,
    const std::string& default_location,
    bool allow_multiple) {
    if (kind > 2U) return false;
    return impl_->file_dialogs->Show(
        static_cast<NativeFileDialogKind>(kind), request_id, filters, default_location, allow_multiple);
}
bool WindowsNativePlatform::IsDarkMode() const { return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK; }
std::uint32_t WindowsNativePlatform::AccentColor() const {
    return impl_->system_theme_bridge == nullptr
        ? detail::kWindowsAccentColorFallback
        : impl_->system_theme_bridge->AccentColor();
}
std::uint32_t WindowsNativePlatform::PlatformFamily() const { return FUI_PLATFORM_WINDOWS; }
std::uint32_t WindowsNativePlatform::HostCapabilities() const {
    return FUI_HOST_CAPABILITY_OPEN_EXTERNAL_URI |
           FUI_HOST_CAPABILITY_CLIPBOARD_READ |
           FUI_HOST_CAPABILITY_CLIPBOARD_WRITE |
           FUI_HOST_CAPABILITY_FILE_DIALOGS;
}
bool WindowsNativePlatform::IsCoarsePointer() const { return false; }
void WindowsNativePlatform::SetApplicationCaption(const std::string& caption) {
    SDL_SetWindowTitle(impl_->window, caption.c_str());
}
void WindowsNativePlatform::SetNativePointerCapture(bool captured) {
    // The Win32 subclass consumes primary-button messages before SDL sees
    // them, so it must also be the owner of capture acquired for UI drags.
    impl_->scroll_wheel_bridge->SetPointerCapture(captured);
}
void WindowsNativePlatform::SetCursor(std::uint32_t style) { impl_->platform_services.SetCursor(style); }
void WindowsNativePlatform::RequestFontLoad(std::uint32_t font_id, const std::string& source) {
    impl_->platform_services.RequestFontLoad(font_id, source);
}
void WindowsNativePlatform::LoadSvg(std::uint32_t svg_id, const std::string& source) {
    impl_->platform_services.LoadSvg(svg_id, source);
}
void WindowsNativePlatform::ReleaseSvg(std::uint32_t svg_id) { impl_->platform_services.ReleaseSvg(svg_id); }
void WindowsNativePlatform::LoadTexture(std::uint32_t texture_id, const std::string& source) {
    impl_->platform_services.LoadTexture(texture_id, source);
}
void WindowsNativePlatform::ReleaseTexture(std::uint32_t texture_id) {
    impl_->platform_services.ReleaseTexture(texture_id);
}
void WindowsNativePlatform::CompleteFileDialogForTesting(
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

void WindowsNativePlatform::DispatchDropEventForTesting(
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

 std::size_t WindowsNativePlatform::FallbackFontCountForTesting() const {
    return impl_->platform_services.FallbackFontCountForTesting();
}

void WindowsNativePlatform::RequestMissingFontCoverageForTesting(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    const std::string& sample_text) {
    impl_->platform_services.ReportMissingFontCoverage(primary_font_id, coverage_kind, sample_text);
}


} // namespace effindom::v2::native
