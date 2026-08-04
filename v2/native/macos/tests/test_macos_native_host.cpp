#include "NativeHost.h"
#include "NativeHostCharacterization.h"
#include "SdlUiDispatcher.h"
#include "fui_host_abi.h"
#include "NativeInputTypes.h"
#include "graphics/MacosMetalSurface.h"
#include "input/MacosScrollWheelBridge.h"
#include "platform/MacosDisplayLink.h"
#include "platform/MacosSystemThemeBridge.h"
#include "platform/MacosAccessibilityAdapter.h"
#include "effindom_ui.h"
#include "SDL3/SDL.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

using effindom::v2::native::NativeHost;

TEST_CASE("macOS display refresh dispatches only during active live resize",
    "[v2][native][macos][resize]") {
    using effindom::v2::native::detail::ShouldRunMacosDisplayLink;
    CHECK_FALSE(ShouldRunMacosDisplayLink(false, false, false));
    CHECK(ShouldRunMacosDisplayLink(true, false, false));
    CHECK(ShouldRunMacosDisplayLink(false, true, false));
    CHECK(ShouldRunMacosDisplayLink(false, false, true));
    CHECK(ShouldRunMacosDisplayLink(true, true, true));

    using effindom::v2::native::detail::ShouldDispatchMacosLiveResizeFrame;
    CHECK_FALSE(ShouldDispatchMacosLiveResizeFrame(false, false));
    CHECK_FALSE(ShouldDispatchMacosLiveResizeFrame(false, true));
    CHECK_FALSE(ShouldDispatchMacosLiveResizeFrame(true, false));
    CHECK(ShouldDispatchMacosLiveResizeFrame(true, true));
}

TEST_CASE("macOS live resize can drain timer work without the outer SDL event loop",
    "[v2][native][macos][resize][timer]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    SDL_Window* window = SDL_CreateWindow(
        "EffinDOM live resize dispatcher test", 64, 64, SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);
    {
        effindom::v2::native::SdlUiDispatcher dispatcher(window);
        bool timer_fired = false;
        REQUIRE(dispatcher.PostTask([&timer_fired] {
            timer_fired = true;
            return true;
        }));

        CHECK(dispatcher.DrainPending());
        CHECK(timer_fired);
        CHECK_FALSE(dispatcher.DrainPending());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("macOS native host satisfies the shared characterization contract", "[v2][native][macos][characterization]") {
    effindom::v2::native::tests::CharacterizeNativeHost<NativeHost>();
}

extern "C" void fui_load_font(std::uint32_t font_id, std::uintptr_t source, std::uint32_t length);
extern "C" void fui_load_svg(std::uint32_t svg_id, std::uintptr_t source, std::uint32_t length);
extern "C" void fui_release_svg(std::uint32_t svg_id);
extern "C" std::uint32_t fui_get_host_environment();
extern "C" std::uint32_t fui_get_host_capabilities();
extern "C" std::uint32_t fui_get_accent_color();

namespace {







} // namespace

TEST_CASE("macOS sRGB accent components are packed as opaque EffinDOM RGBA",
    "[v2][native][macos][theme]") {
    using effindom::v2::native::detail::PackMacosAccentColor;

    CHECK(PackMacosAccentColor(0.0, 0.5, 1.0) == 0x0080FFFFU);
    CHECK(PackMacosAccentColor(-1.0, 2.0, 0.25) == 0x00FF40FFU);
    CHECK_FALSE(PackMacosAccentColor(
        std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0).has_value());
}

TEST_CASE("macOS accent state falls back and suppresses duplicate changes",
    "[v2][native][macos][theme]") {
    using effindom::v2::native::detail::kMacosAccentColorFallback;
    using effindom::v2::native::detail::MacosAccentColorState;

    std::optional<std::uint32_t> source;
    std::vector<std::uint32_t> changes;
    MacosAccentColorState state(
        [&source] { return source; },
        [&changes](std::uint32_t color) { changes.push_back(color); });

    CHECK(state.Current() == kMacosAccentColorFallback);
    CHECK_FALSE(state.Refresh());
    source = kMacosAccentColorFallback;
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

TEST_CASE("macOS native accent host service reflects the AppKit source",
    "[v2][native][macos][theme]") {
    using effindom::v2::native::detail::kMacosAccentColorFallback;
    using effindom::v2::native::detail::ReadMacosAccentColor;

    NativeHost host(false);
    const std::uint32_t expected = ReadMacosAccentColor().value_or(kMacosAccentColorFallback);
    CHECK(fui_get_accent_color() == expected);
    CHECK((fui_get_accent_color() & 0xFFU) == 0xFFU);
}

TEST_CASE("macOS native host reports desktop operation capabilities", "[v2][native][macos][host]") {
    CHECK(fui_get_host_environment() == FUI_HOST_ENVIRONMENT_DESKTOP);
    const auto capabilities = fui_get_host_capabilities();
    CHECK((capabilities & FUI_HOST_CAPABILITY_OPEN_EXTERNAL_URI) != 0U);
    CHECK((capabilities & FUI_HOST_CAPABILITY_CLIPBOARD_READ) != 0U);
    CHECK((capabilities & FUI_HOST_CAPABILITY_CLIPBOARD_WRITE) != 0U);
    CHECK((capabilities & FUI_HOST_CAPABILITY_FILE_DIALOGS) != 0U);
    CHECK((capabilities & FUI_HOST_CAPABILITY_BROWSER_HISTORY) == 0U);
    CHECK((capabilities & (1U << 1U)) == 0U);
    CHECK((capabilities & (1U << 2U)) == 0U);
}

TEST_CASE("macOS accessibility converts Unicode scalar ranges to AX UTF-16 ranges",
    "[v2][native][macos][accessibility][text]") {
    using namespace effindom::v2::native::detail;
    const std::string text = "A\xF0\x9F\x98\x80\xE4\xBD\xA0" "e\xCC\x81";
    CHECK(MacosAccessibilityUtf16Length(text) == 6U);

    std::uint32_t location = 0U;
    std::uint32_t length = 0U;
    REQUIRE(MacosAccessibilityCharacterRangeToUtf16Range(text, 1U, 3U, location, length));
    CHECK(location == 1U);
    CHECK(length == 3U);

    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
    REQUIRE(MacosAccessibilityUtf16RangeToCharacterRange(text, location, length, start, end));
    CHECK(start == 1U);
    CHECK(end == 3U);
    CHECK_FALSE(MacosAccessibilityUtf16RangeToCharacterRange(text, 2U, 1U, start, end));
    CHECK_FALSE(MacosAccessibilityCharacterRangeToUtf16Range(text, 0U, 6U, location, length));
}

TEST_CASE("native FUI-RS mounts remounts and disposes one application", "[v2][native][macos][n3a]") {
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
    const auto disposed = host.State();
    CHECK(disposed.dispose_count == baseline.dispose_count + 2U);
    CHECK(host.IsIdle());
}

TEST_CASE("SDL3 raster presentation is demand driven", "[v2][native][macos][n3b]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto rendered = host.State();
    CHECK(rendered.frame_count > 0U);
    CHECK(host.IsIdle());
    const auto pixels = host.SnapshotRgba();
    REQUIRE_FALSE(pixels.empty());
    CHECK(std::any_of(pixels.begin(), pixels.end(), [](std::uint8_t value) { return value != 0U && value != 255U; }));

    const auto idle_frames = host.State().frame_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK_FALSE(host.RunNextFrame());
    CHECK(host.State().frame_count == idle_frames);
}

TEST_CASE("visible macOS presentation uses demand-driven Skia Metal", "[v2][native][macos][n4]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!host.State().gpu_backed) {
        SKIP("Metal is unavailable; visible software fallback is covered separately");
    }
    CHECK(host.State().frame_count > 0U);
    host.DrainFrames();
    CHECK(host.IsIdle());
}

TEST_CASE("macOS Metal presentation recreates graphics state", "[v2][native][macos][n4]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto before = host.State();
    if (!before.gpu_backed) {
        SKIP("Metal is unavailable; visible software fallback is covered separately");
    }
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

TEST_CASE("macOS Metal lifecycle suspends and recovers through SDL window events", "[v2][native][macos][n4]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto initial = host.State();
    if (!initial.gpu_backed) {
        SKIP("Metal is unavailable; visible software fallback is covered separately");
    }

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

TEST_CASE("visible macOS presentation falls back to explicit software rendering when Metal initialization fails",
    "[v2][native][macos][fallback]") {
    effindom::v2::native::MacosMetalSurface::FailNextInitializationForTesting();
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    CHECK_FALSE(host.State().gpu_backed);
    CHECK(host.State().frame_count > 0U);
    CHECK_FALSE(host.SnapshotRgba().empty());

    host.Resize(640U, 420U);
    host.DrainFrames();
    CHECK(host.State().logical_width == 640.0f);
    CHECK(host.State().logical_height == 420.0f);
    CHECK(host.IsIdle());
}

TEST_CASE("macOS retries Metal when a runtime recovery attempt fails",
    "[v2][native][macos][recovery]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!host.State().gpu_backed) {
        SKIP("Metal is unavailable; startup fallback is covered separately");
    }

    const auto before = host.State();
    effindom::v2::native::MacosMetalSurface::FailNextRecoveryForTesting();
    host.RecreateGraphicsSurface();
    host.DrainFrames();

    const auto recovered = host.State();
    CHECK(recovered.gpu_backed);
    CHECK(recovered.graphics_recovery_count > before.graphics_recovery_count);
    CHECK(recovered.frame_count > before.frame_count);
    CHECK_FALSE(host.SnapshotRgba().empty());
    CHECK(host.IsIdle());
}

TEST_CASE("SDL wheel units use one device-independent UI scroll step", "[v2][native][macos][input]") {
    using effindom::v2::native::detail::WheelDeltaToLogicalPixels;
    CHECK(WheelDeltaToLogicalPixels(1.0f) == Catch::Approx(16.0f));
    CHECK(WheelDeltaToLogicalPixels(-1.0f) == Catch::Approx(-16.0f));
    CHECK(WheelDeltaToLogicalPixels(0.1f) == Catch::Approx(1.6f));
    CHECK(WheelDeltaToLogicalPixels(-0.5f) == Catch::Approx(-8.0f));

    using effindom::v2::native::detail::AppKitPreciseDelta;
    CHECK(AppKitPreciseDelta(3.25f, false) == Catch::Approx(3.25f));
    CHECK(AppKitPreciseDelta(3.25f, true) == Catch::Approx(-3.25f));

    using effindom::v2::native::detail::AppKitCoarseDelta;
    CHECK(AppKitCoarseDelta(1.0f, false) == Catch::Approx(16.0f));
    CHECK(AppKitCoarseDelta(4.0f, false) == Catch::Approx(64.0f));
    CHECK(AppKitCoarseDelta(4.0f, true) == Catch::Approx(-64.0f));
}

TEST_CASE("native application remount is deterministic and lifecycle remains idle", "[v2][native][macos][n3d]") {
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
