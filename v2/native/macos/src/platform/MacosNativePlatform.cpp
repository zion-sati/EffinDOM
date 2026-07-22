#include "MacosNativePlatform.h"

#include "NativeFuiBridge.h"
#include "NativeGraphicsCoordinator.h"
#include "NativeHostCore.h"
#include "NativeUtf8.h"
#include "platform/MacosInputSettings.h"
#include "platform/MacosSystemThemeBridge.h"
#include "platform/MacosAccessibilityAdapter.h"
#include "fui_host_abi.h"
#include "graphics/MacosMetalSurface.h"
#include "NativeInputRouter.h"
#include "NativeRasterSurface.h"
#include "SdlDropTarget.h"
#include "SdlEventAdapter.h"
#include "SdlFileDialogs.h"
#include "SdlUiDispatcher.h"
#include "input/MacosScrollWheelBridge.h"
#include "platform/MacosPlatformServices.h"

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

namespace effindom::v2::native {
struct MacosNativePlatform::Impl {
    explicit Impl(bool visible)
        : core(NativeInputRouterOptions{true, false, true}, NativeHostCoreCallbacks{
              [this] { return platform_services.ProcessPendingAssets(); },
              [this] {
                  ui_dispatcher->Clear();
                  __fui_clear_ui_dispatches();
                  file_dialogs->Clear();
                  drop_target->Clear();
                  __fui_clear_native_file_dialog_callbacks();
              },
          }),
          input_adapter(core.InputRouter(), SdlEventAdapterOptions{false, false, true}),
          platform_services(core.GetEngine(), [this] { RequestFrame(); },
              [this](std::uint64_t handle) { core.AnnounceSemantic(handle); }, visible) {
        // SDL registers this as AppKit's AppleMomentumScrollSupported default.
        // Precise wheel events bypass SDL below, but AppKit still needs this set
        // before initialization so it produces the native momentum event tail.
        SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");
        ConfigureMacosInputSettings();
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        core.SetSystemDarkMode(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_METAL;
        if (!visible) flags |= SDL_WINDOW_HIDDEN;
        window = SDL_CreateWindow("EffinDOM Native FUI-RS", 800, 560, flags);
        if (window == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
        core.AttachAccessibility(CreateMacosAccessibilityAdapter(window,
            [this](NativeAccessibilityAction action, std::uint64_t handle) {
                core.Accessibility().PerformAction(action, handle);
            }));
        scroll_wheel_bridge = std::make_unique<MacosScrollWheelBridge>(
            window,
            [this](const NativePreciseScrollEvent& event) {
                core.InputRouter().DispatchPreciseWheel(
                    event.delta_x,
                    event.delta_y,
                    event.begins_gesture,
                    event.ends_gesture,
                    NowMilliseconds());
                RequestFrame();
            });
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
                NativePixelDensitySource::WindowPixelDensity,
                NativeRasterSurfaceOptions{true, true},
            },
            visible ? MacosMetalSurface::Create(window) : nullptr);
        if (graphics == nullptr) {
            throw std::runtime_error(std::string("native graphics initialization failed: ") + SDL_GetError());
        }
        core.AttachGraphics(std::move(graphics));
        ui::SetGlobalUiPlatformHost(platform_services);
        core.InitializeEngine();
        platform_services.LoadDefaultFont(1U, "NotoSans-Regular.ttf");
        platform_services.LoadDefaultFont(2U, "NotoSans-Bold.ttf");
        platform_services.LoadDefaultFont(7U, "NotoSansMono-Regular.ttf");
        system_theme_bridge = std::make_unique<MacosSystemThemeBridge>(
            [this](std::uint32_t color) {
                __fui_on_system_accent_color_changed(color);
                RequestFrame();
            });
        if (!SDL_AddEventWatch(&Impl::WatchEvent, this)) {
            throw std::runtime_error(std::string("SDL_AddEventWatch failed: ") + SDL_GetError());
        }
    }

    ~Impl() {
        system_theme_bridge.reset();
        ui::ClearGlobalUiPlatformHost(platform_services);
        SDL_RemoveEventWatch(&Impl::WatchEvent, this);
        __disposeApp();
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
            host.core.Stop();
        }
        host.live_resize_rendering = false;
        return true;
    }

    SDL_Window* window = nullptr;
    NativeHostCore core;
    std::unique_ptr<SdlUiDispatcher> ui_dispatcher;
    std::unique_ptr<SdlDropTarget> drop_target;
    std::unique_ptr<SdlFileDialogs> file_dialogs;
    SdlEventAdapter input_adapter;
    std::unique_ptr<MacosScrollWheelBridge> scroll_wheel_bridge;
    std::unique_ptr<MacosSystemThemeBridge> system_theme_bridge;
    MacosPlatformServices platform_services;
    bool live_resize_rendering = false;
};

MacosNativePlatform::MacosNativePlatform(bool visible) : impl_(std::make_unique<Impl>(visible)) {}
MacosNativePlatform::~MacosNativePlatform() = default;

NativeHostCore& MacosNativePlatform::Core() { return impl_->core; }
const NativeHostCore& MacosNativePlatform::Core() const { return impl_->core; }
std::uint32_t MacosNativePlatform::CurrentPointerButtons() const {
    return impl_->input_adapter.CurrentButtons();
}
std::uint32_t MacosNativePlatform::CurrentModifiers() const {
    return impl_->input_adapter.CurrentModifiers();
}
bool MacosNativePlatform::PostUiDispatch(std::uint64_t callback_id) {
    return impl_->ui_dispatcher->Post(callback_id);
}
bool MacosNativePlatform::CancelUiDispatch(std::uint64_t callback_id) {
    return impl_->ui_dispatcher->Cancel(callback_id);
}
void MacosNativePlatform::RequestFrame() { impl_->core.RequestFrame(); }

 bool MacosNativePlatform::PumpEvent(bool wait_when_idle) {
    SDL_Event event{};
    const bool has_event = wait_when_idle && !impl_->core.IsFramePending()
        ? SDL_WaitEventTimeout(&event, 50)
        : SDL_PollEvent(&event);
    if (!has_event) return false;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        impl_->core.InputRouter().HandleWindowFocusLost(impl_->NowMilliseconds());
    }
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
            impl_->core.Graphics().SetSuspended(true);
            impl_->core.CancelPendingFrame();
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
            impl_->core.Graphics().SetSuspended(false);
            RequestFrame();
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            impl_->core.Graphics().SetSuspended(false);
            // Live-resize exposes are rendered synchronously by WatchEvent
            // while Cocoa's modal resize loop blocks ordinary event pumping.
            if (event.window.data1 != 1) RequestFrame();
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

void MacosNativePlatform::Resize(std::uint32_t logical_width, std::uint32_t logical_height) {
    SDL_SetWindowSize(impl_->window, static_cast<int>(logical_width), static_cast<int>(logical_height));
    SDL_SyncWindow(impl_->window);
    RequestFrame();
}

void MacosNativePlatform::RecreateGraphicsSurface() {
    impl_->core.Graphics().RequestRecovery();
    RequestFrame();
}

 void MacosNativePlatform::DispatchWindowFocusLost() {
    impl_->core.InputRouter().HandleWindowFocusLost(impl_->NowMilliseconds());
    RequestFrame();
}

void MacosNativePlatform::SetClipboardText(const std::string& text) { impl_->platform_services.SetClipboardText(text); }

std::string MacosNativePlatform::ClipboardText() const {
    return impl_->platform_services.ClipboardText();
}
void MacosNativePlatform::RequestClipboardRead(std::uint64_t handle) {
    impl_->platform_services.RequestClipboardRead(handle);
}

bool MacosNativePlatform::OpenExternalUrl(const std::string& url) const { return impl_->platform_services.OpenExternalUrl(url); }
bool MacosNativePlatform::OpenFile(const std::filesystem::path& path) const { return impl_->platform_services.OpenFile(path); }
bool MacosNativePlatform::RevealFile(const std::filesystem::path& path) const { return impl_->platform_services.RevealFile(path); }
bool MacosNativePlatform::ShowFileDialog(
    std::uint32_t kind,
    std::uint64_t request_id,
    const std::string& filters,
    const std::string& default_location,
    bool allow_multiple) {
    if (kind > 2U) return false;
    return impl_->file_dialogs->Show(
        static_cast<NativeFileDialogKind>(kind), request_id, filters, default_location, allow_multiple);
}
bool MacosNativePlatform::IsDarkMode() const { return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK; }
std::uint32_t MacosNativePlatform::AccentColor() const {
    return impl_->system_theme_bridge == nullptr
        ? detail::kMacosAccentColorFallback
        : impl_->system_theme_bridge->AccentColor();
}
std::uint32_t MacosNativePlatform::PlatformFamily() const { return FUI_PLATFORM_APPLE; }
std::uint32_t MacosNativePlatform::HostCapabilities() const {
    return FUI_HOST_CAPABILITY_OPEN_EXTERNAL_URI |
           FUI_HOST_CAPABILITY_CLIPBOARD_READ |
           FUI_HOST_CAPABILITY_CLIPBOARD_WRITE |
           FUI_HOST_CAPABILITY_FILE_DIALOGS;
}
bool MacosNativePlatform::IsCoarsePointer() const { return false; }
void MacosNativePlatform::SetApplicationCaption(const std::string& caption) {
    SDL_SetWindowTitle(impl_->window, caption.c_str());
}
void MacosNativePlatform::SetNativePointerCapture(bool) {}
void MacosNativePlatform::SetCursor(std::uint32_t style) { impl_->platform_services.SetCursor(style); }
void MacosNativePlatform::RequestFontLoad(std::uint32_t font_id, const std::string& source) {
    impl_->platform_services.RequestFontLoad(font_id, source);
}
void MacosNativePlatform::LoadSvg(std::uint32_t svg_id, const std::string& source) {
    impl_->platform_services.LoadSvg(svg_id, source);
}
void MacosNativePlatform::ReleaseSvg(std::uint32_t svg_id) { impl_->platform_services.ReleaseSvg(svg_id); }
void MacosNativePlatform::LoadTexture(std::uint32_t texture_id, const std::string& source) {
    impl_->platform_services.LoadTexture(texture_id, source);
}
void MacosNativePlatform::ReleaseTexture(std::uint32_t texture_id) {
    impl_->platform_services.ReleaseTexture(texture_id);
}
void MacosNativePlatform::CompleteFileDialogForTesting(
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

void MacosNativePlatform::DispatchDropEventForTesting(
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

 std::size_t MacosNativePlatform::FallbackFontCountForTesting() const {
    return impl_->platform_services.FallbackFontCountForTesting();
}

void MacosNativePlatform::RequestMissingFontCoverageForTesting(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    const std::string& sample_text) {
    impl_->platform_services.ReportMissingFontCoverage(primary_font_id, coverage_kind, sample_text);
}


} // namespace effindom::v2::native
