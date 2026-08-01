#include "NativeFuiBridge.h"
#include "NativeFuiRuntimeBridge.h"
#include "NativeHost.h"
#include "effindom_ui.h"

#include <catch2/catch_test_macros.hpp>
#include <include/core/SkCanvas.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

bool HasVisiblePixels(std::uint64_t handle) {
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(640, 180));
    if (!surface) {
        return false;
    }
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    fui_dispatch_custom_draw(handle, reinterpret_cast<std::uintptr_t>(surface->getCanvas()));
    SkPixmap pixmap;
    if (!surface->peekPixels(&pixmap)) {
        return false;
    }
    for (int y = 0; y < pixmap.height(); ++y) {
        const auto* row = static_cast<const std::uint8_t*>(pixmap.addr(0, y));
        for (int x = 0; x < pixmap.width(); ++x) {
            if (row[x * 4 + 3] != 0U) {
                return true;
            }
        }
    }
    return false;
}

bool HasSemanticLabel(
    const effindom::v2::native::NativeAccessibilitySnapshot& snapshot,
    std::string_view label) {
    return std::any_of(snapshot.nodes.begin(), snapshot.nodes.end(), [label](const auto& node) {
        return node.label == label;
    });
}

} // namespace

TEST_CASE("native drawing showcase mounts every panel and produces stable visible output", "[v2][native][demo][drawing]") {
    effindom::v2::native::NativeHost host(false);
    REQUIRE(host.LoadFontForTesting(
        1U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSans-Regular.ttf"));
    host.MountApplication();
    host.Resize(1000U, 10000U);
    host.DrainFrames();
    __fui_native_rasterize_retained();
    host.DrainFrames();

    const std::array handles{
        __fui_native_custom_draw_handle(),
        __fui_native_bitmap_draw_handle(),
        __fui_native_offscreen_draw_handle(),
        __fui_native_retained_draw_handle(),
    };
    for (const std::uint64_t handle : handles) {
        REQUIRE(handle != 0U);
        CHECK(HasVisiblePixels(handle));
    }
    CHECK(host.TextureSizeForTesting(__fui_native_waveform_texture_id()) == std::pair{96U, 40U});
    CHECK(host.TextureSizeForTesting(__fui_native_offscreen_texture_id()) == std::pair{40U, 40U});
    CHECK(host.TextureSizeForTesting(__fui_native_retained_texture_id()) == std::pair{320U, 96U});
    CHECK(__fui_native_bitmap_full_upload_count() >= 1U);
    CHECK(__fui_native_offscreen_composition_count() >= 1U);
    CHECK(__fui_native_offscreen_sample_rgba() != 0U);
    CHECK(__fui_native_retained_raster_count() >= 1U);

    const auto& semantics = host.AccessibilitySnapshotForTesting();
    CHECK(HasSemanticLabel(semantics, "Native custom drawing"));
    CHECK(HasSemanticLabel(semantics, "Dynamic bitmap"));
    CHECK(HasSemanticLabel(semantics, "Offscreen composition"));
    CHECK(HasSemanticLabel(semantics, "Retained rasterization"));
    CHECK(HasSemanticLabel(semantics, "Start animation"));
    CHECK(HasSemanticLabel(semantics, "Pause animation"));
    CHECK(HasSemanticLabel(semantics, "Single step"));
    CHECK(HasSemanticLabel(semantics, "Reset animation"));
    CHECK(HasSemanticLabel(semantics, "Animation: paused | frame 0 | timer fires 0"));
}

TEST_CASE("native drawing showcase interactions cover animation uploads reset resize and remount", "[v2][native][demo][drawing]") {
    effindom::v2::native::NativeHost host(false);
    host.MountApplication();
    host.Resize(1000U, 900U);
    host.DrainFrames();
    __fui_native_rasterize_retained();
    host.DrainFrames();

    const std::uint32_t dirty_before = __fui_native_bitmap_dirty_upload_count();
    __fui_native_step_drawing_animation();
    host.DrainFrames();
    CHECK(__fui_native_animation_step_count() == 1U);
    CHECK(__fui_native_bitmap_dirty_upload_count() == dirty_before + 1U);

    const std::uint32_t compositions_before = __fui_native_offscreen_composition_count();
    __fui_native_recompose_offscreen();
    host.DrainFrames();
    CHECK(__fui_native_offscreen_composition_count() == compositions_before + 1U);

    const std::uint32_t rasters_before = __fui_native_retained_raster_count();
    __fui_native_rasterize_retained();
    host.DrainFrames();
    CHECK(__fui_native_retained_raster_count() == rasters_before + 1U);

    __fui_native_start_drawing_animation();
    REQUIRE(__fui_native_animation_running());
    const std::uint32_t timer_before = __fui_native_timer_fire_count();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (__fui_native_timer_fire_count() == timer_before &&
           std::chrono::steady_clock::now() < deadline) {
        host.PumpEvent(true);
        host.DrainFrames();
    }
    CHECK(__fui_native_timer_fire_count() > timer_before);
    __fui_native_pause_drawing_animation();
    CHECK_FALSE(__fui_native_animation_running());

    __fui_native_reset_drawing_animation();
    host.DrainFrames();
    CHECK(__fui_native_animation_step_count() == 0U);
    CHECK(__fui_native_timer_fire_count() == 0U);
    CHECK(__fui_native_bitmap_full_upload_count() == 1U);

    host.Resize(540U, 420U);
    host.DrainFrames();
    CHECK(host.State().logical_width == 540.0F);
    CHECK(host.State().logical_height == 420.0F);
    CHECK(__fui_native_scroll_handle() != 0U);

    host.MountApplication();
    host.Resize(1000U, 10000U);
    host.DrainFrames();
    __fui_native_rasterize_retained();
    host.DrainFrames();
    CHECK(__fui_native_bitmap_draw_handle() != 0U);
    CHECK(__fui_native_offscreen_draw_handle() != 0U);
    CHECK(__fui_native_retained_draw_handle() != 0U);
    CHECK(__fui_native_retained_raster_count() >= 1U);
    CHECK(host.OffscreenSurfaceCountForTesting() == 3U);

    const auto first_focus = __fui_native_animation_start_handle();
    const auto second_focus = __fui_native_animation_pause_handle();
    REQUIRE(first_focus != 0U);
    REQUIRE(second_focus != 0U);
    ui_request_focus(first_focus);
    host.RequestFrame();
    host.DrainFrames();
    CHECK(host.AccessibilitySnapshotForTesting().focused_handle == first_focus);
    host.DispatchKey("Tab", true);
    host.DispatchKey("Tab", false);
    host.DrainFrames();
    CHECK(host.AccessibilitySnapshotForTesting().focused_handle == second_focus);

    host.Unmount();
    CHECK(host.OffscreenSurfaceCountForTesting() == 0U);
    CHECK(host.TextureCountForTesting() == 0U);
}

TEST_CASE("native drawing showcase records backend-neutral performance evidence", "[v2][native][demo][drawing][performance]") {
    using effindom::v2::native::NativeFuiDrawingMetricsForTesting;
    using effindom::v2::native::ResetNativeFuiDrawingMetricsForTesting;

    effindom::v2::native::NativeHost host(false);
    REQUIRE(host.LoadFontForTesting(
        1U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSans-Regular.ttf"));
    host.MountApplication();
    host.Resize(1000U, 10000U);
    host.DrainFrames();
    __fui_native_rasterize_retained();
    host.DrainFrames();

    constexpr std::uint32_t measured_callbacks = 24U;
    const std::uint64_t drawable = __fui_native_custom_draw_handle();
    REQUIRE(drawable != 0U);
    const std::uint32_t callbacks_before = __fui_native_custom_draw_calls();
    ResetNativeFuiDrawingMetricsForTesting();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t iteration = 0U; iteration < measured_callbacks; ++iteration) {
        REQUIRE(HasVisiblePixels(drawable));
    }
    const auto stopped = std::chrono::steady_clock::now();

    __fui_native_reset_drawing_animation();
    __fui_native_step_drawing_animation();
    host.DrainFrames();

    const auto metrics = NativeFuiDrawingMetricsForTesting();
    const std::uint64_t callback_count =
        static_cast<std::uint64_t>(__fui_native_custom_draw_calls() - callbacks_before);
    const double frame_time_ms =
        std::chrono::duration<double, std::milli>(stopped - started).count() /
        static_cast<double>(measured_callbacks);
    const std::size_t path_count = host.PathCountForTesting();
    const std::size_t texture_count = host.TextureCountForTesting();
    const std::size_t offscreen_count = host.OffscreenSurfaceCountForTesting();

    CHECK(callback_count >= measured_callbacks);
    CHECK(metrics.batch_count >= measured_callbacks);
    CHECK(metrics.batch_bytes > 0U);
    CHECK(metrics.bitmap_upload_count >= 1U);
    CHECK(metrics.bitmap_upload_bytes >= 96U * 40U * 4U);
    CHECK(metrics.dirty_upload_count >= 1U);
    CHECK(metrics.dirty_upload_bytes > 0U);
    CHECK(path_count == 1U);
    CHECK(texture_count >= 3U);
    CHECK(offscreen_count == 3U);
    CHECK(frame_time_ms >= 0.0);

    std::cout << std::fixed << std::setprecision(3)
              << "EFFINDOM_NATIVE_DRAWING_METRICS"
              << " callback_count=" << callback_count
              << " batch_bytes=" << metrics.batch_bytes
              << " bitmap_upload_bytes=" << metrics.bitmap_upload_bytes
              << " dirty_upload_bytes=" << metrics.dirty_upload_bytes
              << " path_count=" << path_count
              << " texture_count=" << texture_count
              << " offscreen_count=" << offscreen_count
              << " frame_time_ms=" << frame_time_ms << '\n';

    host.Unmount();
    CHECK(host.PathCountForTesting() == 0U);
    CHECK(host.TextureCountForTesting() == 0U);
    CHECK(host.OffscreenSurfaceCountForTesting() == 0U);
}
