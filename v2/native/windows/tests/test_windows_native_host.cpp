#include "NativeHost.h"
#include "NativeHostCharacterization.h"
#include "NativeFuiBridge.h"
#include "NativeInputTypes.h"
#include "UiRuntime.h"
#include "input/WindowsScrollWheelBridge.h"
#include "platform/WindowsSystemThemeBridge.h"
#include "effindom_ui.h"
#include "SDL3/SDL.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using effindom::v2::native::NativeHost;

extern "C" std::uint32_t fui_get_platform_family();
extern "C" std::uint32_t fui_get_accent_color();
extern "C" void fui_load_font(std::uint32_t, std::uintptr_t, std::uint32_t);
extern "C" void fui_load_svg(std::uint32_t, std::uintptr_t, std::uint32_t);
extern "C" void fui_release_svg(std::uint32_t);

namespace {

struct LiveResizePaintObserver {
    NativeHost* host = nullptr;
    bool sizing = false;
    std::uint32_t live_paint_count = 0U;
    float maximum_live_width = 0.0f;

    static LRESULT CALLBACK Procedure(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        UINT_PTR subclass_id,
        DWORD_PTR reference) {
        auto& observer = *reinterpret_cast<LiveResizePaintObserver*>(reference);
        if (message == WM_ENTERSIZEMOVE) observer.sizing = true;
        if (message == WM_PAINT) {
            const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
            if (observer.sizing) {
                ++observer.live_paint_count;
                observer.maximum_live_width = std::max(
                    observer.maximum_live_width,
                    observer.host->State().logical_width);
            }
            return result;
        }
        if (message == WM_EXITSIZEMOVE) {
            const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
            observer.sizing = false;
            return result;
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, &Procedure, subclass_id);
        }
        return DefSubclassProc(window, message, wparam, lparam);
    }
};

HWND FindEffinDomWindow() {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (windows == nullptr) return nullptr;

    HWND result = nullptr;
    for (int index = 0; index < count && result == nullptr; ++index) {
        const SDL_PropertiesID properties = SDL_GetWindowProperties(windows[index]);
        result = static_cast<HWND>(SDL_GetPointerProperty(
            properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    }
    SDL_free(windows);
    return result;
}



TEST_CASE("Windows colorization colors are packed as opaque EffinDOM RGBA",
          "[v2][native][windows][theme]") {
    using effindom::v2::native::detail::PackWindowsColorizationColor;
    CHECK(PackWindowsColorizationColor(0xAA112233U) == 0x112233FFU);
    CHECK(PackWindowsColorizationColor(0x00123456U) == 0x123456FFU);
    CHECK(PackWindowsColorizationColor(0xFFFFFFFFU) == 0xFFFFFFFFU);
}

TEST_CASE("Windows accent state falls back and suppresses duplicate changes",
          "[v2][native][windows][theme]") {
    using effindom::v2::native::detail::kWindowsAccentColorFallback;
    using effindom::v2::native::detail::WindowsAccentColorState;

    std::optional<std::uint32_t> source;
    std::vector<std::uint32_t> changes;
    WindowsAccentColorState state(
        [&source] { return source; },
        [&changes](std::uint32_t color) { changes.push_back(color); });

    CHECK(state.Current() == kWindowsAccentColorFallback);
    CHECK_FALSE(state.Refresh());
    source = kWindowsAccentColorFallback;
    CHECK_FALSE(state.Refresh());
    source = 0x123456FFU;
    CHECK(state.Refresh());
    CHECK(state.Current() == 0x123456FFU);
    REQUIRE(changes.size() == 1U);
    CHECK(changes.front() == 0x123456FFU);
    CHECK_FALSE(state.Refresh());
    CHECK(changes.size() == 1U);
    source.reset();
    CHECK_FALSE(state.Refresh());
    CHECK(state.Current() == 0x123456FFU);
}





std::uint64_t FindEditableTextDescendant(std::uint64_t handle) {
    const auto* node = effindom::v2::ui::GetRuntime().Resolve(handle);
    if (node == nullptr) return UI_INVALID_HANDLE;
    if (node->is_text_node && node->is_editable) return handle;
    for (const auto child : node->children) {
        const auto result = FindEditableTextDescendant(child);
        if (result != UI_INVALID_HANDLE) return result;
    }
    return UI_INVALID_HANDLE;
}

std::uint64_t FindSemanticDescendant(std::uint64_t handle, const std::string& label) {
    const auto* node = effindom::v2::ui::GetRuntime().Resolve(handle);
    if (node == nullptr) return UI_INVALID_HANDLE;
    if (node->semantic_label == label) return handle;
    for (const auto child : node->children) {
        const auto result = FindSemanticDescendant(child, label);
        if (result != UI_INVALID_HANDLE) return result;
    }
    return UI_INVALID_HANDLE;
}

} // namespace

TEST_CASE("Windows native FUI-RS mounts remounts and disposes one application", "[v2][native][windows][w5]") {
    NativeHost host(false);
    const auto baseline = host.State();
    host.MountApplication();
    host.DrainFrames();
    const auto first = host.State();
    CHECK(first.mount_count == baseline.mount_count + 1U);
    CHECK(first.dispose_count == baseline.dispose_count);

    host.MountApplication();
    host.DrainFrames();
    const auto second = host.State();
    CHECK(second.mount_count == baseline.mount_count + 2U);
    CHECK(second.dispose_count == baseline.dispose_count + 1U);

    host.Unmount();
    CHECK(host.State().dispose_count == baseline.dispose_count + 2U);
    CHECK(host.IsIdle());
}

TEST_CASE("Windows SDL3 raster presentation is demand driven", "[v2][native][windows][w5]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto rendered = host.State();
    CHECK_FALSE(rendered.gpu_backed);
    CHECK(rendered.frame_count > 0U);
    CHECK(host.IsIdle());
    const auto pixels = host.SnapshotRgba();
    REQUIRE_FALSE(pixels.empty());
    CHECK(std::any_of(pixels.begin(), pixels.end(), [](std::uint8_t value) {
        return value != 0U && value != 255U;
    }));

    const auto idle_frames = host.State().frame_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK_FALSE(host.RunNextFrame());
    CHECK(host.State().frame_count == idle_frames);
}

TEST_CASE("Windows wheel units use one device-independent UI scroll step", "[v2][native][windows][w6]") {
    using effindom::v2::native::detail::WheelDeltaToLogicalPixels;
    using effindom::v2::native::detail::IsWindowsPreciseWheelDelta;
    using effindom::v2::native::detail::WindowsHorizontalWheelDeltaToLogicalPixels;
    using effindom::v2::native::detail::WindowsVerticalWheelDeltaToLogicalPixels;
    using effindom::v2::native::detail::WindowsZoomDistanceMultiplier;
    CHECK(WheelDeltaToLogicalPixels(1.0f) == Catch::Approx(16.0f));
    CHECK(WheelDeltaToLogicalPixels(-1.0f) == Catch::Approx(-16.0f));
    CHECK(WheelDeltaToLogicalPixels(0.1f) == Catch::Approx(1.6f));
    CHECK(WheelDeltaToLogicalPixels(-0.5f) == Catch::Approx(-8.0f));
    CHECK(WindowsHorizontalWheelDeltaToLogicalPixels(120) == Catch::Approx(96.0f));
    CHECK(WindowsHorizontalWheelDeltaToLogicalPixels(-120) == Catch::Approx(-96.0f));
    CHECK(WindowsVerticalWheelDeltaToLogicalPixels(120) == Catch::Approx(-96.0f));
    CHECK(WindowsVerticalWheelDeltaToLogicalPixels(-120) == Catch::Approx(96.0f));
    CHECK(WindowsVerticalWheelDeltaToLogicalPixels(30) == Catch::Approx(-24.0f));
    CHECK_FALSE(IsWindowsPreciseWheelDelta(120));
    CHECK_FALSE(IsWindowsPreciseWheelDelta(-240));
    CHECK(IsWindowsPreciseWheelDelta(30));
    CHECK(WindowsZoomDistanceMultiplier(150U, 100U) == Catch::Approx(1.5f));
    CHECK(WindowsZoomDistanceMultiplier(50U, 100U) == Catch::Approx(0.5f));
    CHECK(WindowsZoomDistanceMultiplier(50U, 0U) == Catch::Approx(1.0f));
}

TEST_CASE("Win32 pointer capture is released after a native drag",
          "[v2][native][windows][w6][win32][capture]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    struct SdlCleanup final {
        ~SdlCleanup() { SDL_Quit(); }
    } cleanup;
    SDL_Window* window = SDL_CreateWindow("EffinDOM capture test", 320, 200, SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);
    struct WindowCleanup final {
        SDL_Window* window;
        ~WindowCleanup() { SDL_DestroyWindow(window); }
    } window_cleanup{window};

    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    REQUIRE(hwnd != nullptr);

    std::uint32_t down_count = 0U;
    std::uint32_t move_count = 0U;
    std::uint32_t up_count = 0U;
    effindom::v2::native::WindowsScrollWheelBridge* capture_bridge = nullptr;
    auto bridge = std::make_unique<effindom::v2::native::WindowsScrollWheelBridge>(
        window,
        [](const effindom::v2::native::NativeWheelEvent&) {},
        [&](const effindom::v2::native::NativeMouseEvent& event) {
            using MouseType = effindom::v2::native::NativeMouseEvent::Type;
            if (event.type == MouseType::Down) {
                ++down_count;
                capture_bridge->SetPointerCapture(true);
            } else if (event.type == MouseType::Move) {
                ++move_count;
            } else {
                ++up_count;
                capture_bridge->SetPointerCapture(false);
            }
        },
        [] {});
    capture_bridge = bridge.get();

    REQUIRE(GetCapture() == nullptr);
    REQUIRE(SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 60)) == 0);
    REQUIRE(GetCapture() == hwnd);
    // Windows can report a transient capture transition during activation.
    // The actual HWND owner remains authoritative for the following drag/up.
    REQUIRE(SendMessageW(hwnd, WM_CAPTURECHANGED, 0U, 0U) == 0);
    REQUIRE(GetCapture() == hwnd);
    REQUIRE(SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 60)) == 0);
    REQUIRE(SendMessageW(hwnd, WM_LBUTTONUP, 0U, MAKELPARAM(180, 60)) == 0);
    CHECK(GetCapture() == nullptr);
    CHECK(down_count == 1U);
    CHECK(move_count == 1U);
    CHECK(up_count == 1U);

    REQUIRE(SendMessageW(hwnd, WM_MOUSEMOVE, 0U, MAKELPARAM(40, 60)) == 0);
    CHECK(move_count == 1U);
}

TEST_CASE("Windows native wheel bypass wakes retained rendering", "[v2][native][windows][w6][win32]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const auto& semantics = host.AccessibilitySnapshotForTesting();
    const auto advanced = std::find_if(
        semantics.nodes.begin(), semantics.nodes.end(), [](const auto& node) {
            return node.role == effindom::v2::native::NativeAccessibilityRole::Button &&
                node.label == "Advanced";
        });
    REQUIRE(advanced != semantics.nodes.end());
    const float tab_x = advanced->bounds.x + advanced->bounds.width * 0.5f;
    const float tab_y = advanced->bounds.y + advanced->bounds.height * 0.5f;
    host.DispatchPointer(tab_x, tab_y, true, 0, 1U, 1);
    host.DispatchPointer(tab_x, tab_y, false, 0, 0U, 1);
    host.DrainFrames();
    REQUIRE(host.IsIdle());

    const HWND window = FindEffinDomWindow();
    REQUIRE(window != nullptr);
    REQUIRE(SetWindowPos(
        window, nullptr, 0, 0, 480, 320,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    while (host.PumpEvent(false)) {}
    host.DrainFrames();
    REQUIRE(host.IsIdle());
    // A negative Win32 wheel delta means wheel-down. The demo starts at the
    // top of its scroll range, so use the direction that can start scrolling.
    CHECK(SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0) == 0);
    CHECK_FALSE(host.IsIdle());
    REQUIRE(host.RunNextFrame());
}

TEST_CASE("unfocused Windows wheel input does not suspend presentation", "[v2][native][windows][w6][lifecycle]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    SDL_Event focus_lost{};
    focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    REQUIRE(SDL_PushEvent(&focus_lost));
    REQUIRE(host.PumpEvent(false));
    CHECK_FALSE(host.State().presentation_suspended);

    const auto frames_before_wheel = host.State().frame_count;
    const HWND window = FindEffinDomWindow();
    REQUIRE(window != nullptr);
    CHECK(SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0) == 0);
    host.DrainFrames();
    CHECK(host.State().frame_count > frames_before_wheel);

    SDL_Event focus_gained{};
    focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    REQUIRE(SDL_PushEvent(&focus_gained));
    REQUIRE(host.PumpEvent(false));
    host.DrainFrames();
    CHECK_FALSE(host.State().presentation_suspended);
}

TEST_CASE("visible Windows WM_PAINT presents before UpdateWindow returns", "[v2][native][windows][w7][win32]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const HWND window = FindEffinDomWindow();
    REQUIRE(window != nullptr);
    const auto frames_before_paint = host.State().frame_count;
    REQUIRE(InvalidateRect(window, nullptr, FALSE));
    REQUIRE(UpdateWindow(window));
    CHECK(host.State().frame_count > frames_before_paint);
}

TEST_CASE("Windows sizing modal loop services pending frames while the pointer is stationary",
          "[v2][native][windows][resize][animation]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const HWND window = FindEffinDomWindow();
    REQUIRE(window != nullptr);
    SendMessageW(window, WM_ENTERSIZEMOVE, 0U, 0);
    for (std::uint32_t sample = 0U; sample < 3U; ++sample) {
        const auto frame_before_tick = host.State().frame_count;
        host.RequestFrame();
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(250);
        while (host.State().frame_count == frame_before_tick &&
               std::chrono::steady_clock::now() < deadline) {
            host.PumpEvent(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(host.State().frame_count > frame_before_tick);
    }
    SendMessageW(window, WM_EXITSIZEMOVE, 0U, 0);
}

TEST_CASE("Windows outward border drags move the frame and repaint the live canvas",
          "[v2][native][windows][resize]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const HWND window = FindEffinDomWindow();
    REQUIRE(window != nullptr);
    REQUIRE(SetForegroundWindow(window));
    REQUIRE(SetWindowPos(
        window, nullptr, 200, 160, 800, 600,
        SWP_NOZORDER | SWP_NOACTIVATE));

    LiveResizePaintObserver observer{&host};
    const UINT_PTR observer_id = reinterpret_cast<UINT_PTR>(&observer);
    REQUIRE(SetWindowSubclass(
        window,
        &LiveResizePaintObserver::Procedure,
        observer_id,
        reinterpret_cast<DWORD_PTR>(&observer)));
    struct ObserverCleanup {
        HWND window;
        UINT_PTR id;
        ~ObserverCleanup() {
            RemoveWindowSubclass(window, &LiveResizePaintObserver::Procedure, id);
        }
    } observer_cleanup{window, observer_id};

    const auto check_outward_drag = [&](bool right_edge) {
        RECT rect{};
        REQUIRE(GetWindowRect(window, &rect));
        const LONG initial_frame_width = rect.right - rect.left;
        const float initial_canvas_width = host.State().logical_width;
        const int edge_x = right_edge ? rect.right - 1 : rect.left;
        const int edge_y = rect.top + (rect.bottom - rect.top) / 2;
        const int direction = right_edge ? 1 : -1;
        observer.live_paint_count = 0U;
        observer.maximum_live_width = initial_canvas_width;
        std::atomic_bool input_finished = false;
        std::thread drag([&] {
            SetCursorPos(edge_x, edge_y);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0U, 0U, 0U, 0U);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (int step = 1; step <= 4; ++step) {
                SetCursorPos(edge_x + direction * step * 40, edge_y);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            mouse_event(MOUSEEVENTF_LEFTUP, 0U, 0U, 0U, 0U);
            input_finished = true;
        });

        while (!input_finished) host.PumpEvent(true);
        drag.join();
        while (host.PumpEvent(false)) {}

        REQUIRE(GetWindowRect(window, &rect));
        CHECK(rect.right - rect.left >= initial_frame_width + 120);
        CHECK(observer.live_paint_count >= 2U);
        CHECK(observer.maximum_live_width > initial_canvas_width);
    };

    check_outward_drag(true);
    REQUIRE(SetWindowPos(
        window, nullptr, 200, 160, 800, 600,
        SWP_NOZORDER | SWP_NOACTIVATE));
    check_outward_drag(false);
}

#if defined(EFFINDOM_TEST_DIRECT3D)
TEST_CASE("visible Windows presentation uses demand-driven Skia Direct3D", "[v2][native][windows][w7]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(host.State().gpu_backed);
    CHECK(host.State().frame_count > 0U);
    host.DrainFrames();
    CHECK(host.IsIdle());
}

TEST_CASE("Windows Direct3D presentation recreates graphics state", "[v2][native][windows][w7]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto before = host.State();
    REQUIRE(before.gpu_backed);
    REQUIRE(before.frame_count > 0U);

    host.RecreateGraphicsSurface();
    host.DrainFrames();
    const auto recovered = host.State();
    CHECK(recovered.gpu_backed);
    CHECK(recovered.graphics_generation == before.graphics_generation + 1U);
    CHECK(recovered.graphics_recovery_count == before.graphics_recovery_count + 1U);
    CHECK(recovered.frame_count > before.frame_count);
    CHECK(host.IsIdle());
}

TEST_CASE("Windows Direct3D lifecycle suspends and recovers through SDL window events", "[v2][native][windows][w7]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto initial = host.State();
    REQUIRE(initial.gpu_backed);

    SDL_Event minimized{};
    minimized.type = SDL_EVENT_WINDOW_MINIMIZED;
    REQUIRE(SDL_PushEvent(&minimized));
    for (std::uint32_t attempt = 0U; attempt < 120U && !host.State().presentation_suspended; ++attempt) {
        host.PumpEvent(false);
    }
    CHECK(host.State().presentation_suspended);
    host.RequestFrame();
    CHECK_FALSE(host.RunNextFrame());
    CHECK(host.IsIdle());

    SDL_Event restored{};
    restored.type = SDL_EVENT_WINDOW_RESTORED;
    REQUIRE(SDL_PushEvent(&restored));
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().presentation_suspended; ++attempt) {
        host.PumpEvent(false);
    }
    host.DrainFrames();
    CHECK_FALSE(host.State().presentation_suspended);
    CHECK(host.State().frame_count > initial.frame_count);
}
#endif

TEST_CASE("Windows native application remount is deterministic and lifecycle remains idle", "[v2][native][windows][w10]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto first = host.SnapshotRgba();
    host.MountApplication();
    host.DrainFrames();
    const auto remounted = host.SnapshotRgba();
    REQUIRE(first.size() == remounted.size());
    CHECK(first == remounted);
    CHECK(host.IsIdle());
}
