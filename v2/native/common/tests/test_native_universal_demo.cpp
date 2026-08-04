#include "NativeHost.h"
#include "NativeFuiRuntimeBridge.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using effindom::v2::native::NativeAccessibilityNode;
using effindom::v2::native::NativeAccessibilitySnapshot;
using effindom::v2::native::NativeHost;
using effindom::v2::native::NativeFuiDrawingMetricsForTesting;
using effindom::v2::native::ResetNativeFuiDrawingMetricsForTesting;

const NativeAccessibilityNode* FindLabel(
    const NativeAccessibilitySnapshot& snapshot,
    std::string_view label) {
    const auto found = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(), [label](const auto& node) {
        return node.label == label;
    });
    return found == snapshot.nodes.end() ? nullptr : &*found;
}

const NativeAccessibilityNode* FindLabelPrefix(
    const NativeAccessibilitySnapshot& snapshot,
    std::string_view prefix) {
    const auto found = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(), [prefix](const auto& node) {
        return node.label.size() >= prefix.size() &&
            node.label.compare(0U, prefix.size(), prefix) == 0;
    });
    return found == snapshot.nodes.end() ? nullptr : &*found;
}

const NativeAccessibilityNode* FindNthLabelPrefix(
    const NativeAccessibilitySnapshot& snapshot,
    std::string_view prefix,
    std::size_t index) {
    for (const auto& node : snapshot.nodes) {
        if (node.label.size() >= prefix.size() &&
            node.label.compare(0U, prefix.size(), prefix) == 0U) {
            if (index == 0U) return &node;
            --index;
        }
    }
    return nullptr;
}

bool ScrollUntilLabelPrefix(
    NativeHost& host,
    std::string_view prefix) {
    for (float x = 1570.0F; x <= 1598.0F; x += 2.0F) {
        host.DispatchPointer(x, 58.0F, true, 0, 1U, 1);
        for (float y = 100.0F; y <= 940.0F; y += 40.0F) {
            host.DispatchPointerMove(x, y, 0U, 1U);
            host.DrainFrames();
            if (FindLabelPrefix(host.AccessibilitySnapshotForTesting(), prefix) != nullptr) {
                const float settled_y = std::min(940.0F, y + 160.0F);
                host.DispatchPointerMove(x, settled_y, 0U, 1U);
                host.DrainFrames();
                host.DispatchPointer(x, settled_y, false, 0, 0U, 1);
                host.DrainFrames();
                return true;
            }
        }
        host.DispatchPointer(x, 940.0F, false, 0, 0U, 1);
        host.DrainFrames();
    }
    return false;
}

std::size_t CountGreenPixels(
    const NativeHost& host,
    const NativeAccessibilityNode& node) {
    const auto pixels = host.SnapshotRgba();
    const auto state = host.State();
    const auto width = static_cast<std::uint32_t>(std::lround(state.logical_width * state.pixel_density));
    const auto height = static_cast<std::uint32_t>(std::lround(state.logical_height * state.pixel_density));
    REQUIRE(pixels.size() == static_cast<std::size_t>(width) * height * 4U);
    const auto left = static_cast<std::uint32_t>(std::max(0.0F, std::floor(node.bounds.x * state.pixel_density)));
    const auto top = static_cast<std::uint32_t>(std::max(0.0F, std::floor(node.bounds.y * state.pixel_density)));
    const auto right = std::min(width, static_cast<std::uint32_t>(
        std::ceil((node.bounds.x + node.bounds.width) * state.pixel_density)));
    const auto bottom = std::min(height, static_cast<std::uint32_t>(
        std::ceil((node.bounds.y + node.bounds.height) * state.pixel_density)));
    std::size_t count = 0U;
    for (std::uint32_t y = top; y < bottom; ++y) {
        for (std::uint32_t x = left; x < right; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            const auto red = pixels[offset];
            const auto green = pixels[offset + 1U];
            const auto blue = pixels[offset + 2U];
            if (green > 120U && green > red + 35U && green > blue + 20U) ++count;
        }
    }
    return count;
}

std::size_t CountChangedPixels(
    const NativeHost& host,
    const std::vector<std::uint8_t>& before,
    const std::vector<std::uint8_t>& after,
    float left,
    float top,
    float right,
    float bottom) {
    const auto state = host.State();
    const auto width = static_cast<std::uint32_t>(std::lround(state.logical_width * state.pixel_density));
    const auto height = static_cast<std::uint32_t>(std::lround(state.logical_height * state.pixel_density));
    REQUIRE(before.size() == after.size());
    REQUIRE(after.size() == static_cast<std::size_t>(width) * height * 4U);
    const auto x0 = static_cast<std::uint32_t>(std::max(0.0F, std::floor(left * state.pixel_density)));
    const auto y0 = static_cast<std::uint32_t>(std::max(0.0F, std::floor(top * state.pixel_density)));
    const auto x1 = std::min(width, static_cast<std::uint32_t>(std::ceil(right * state.pixel_density)));
    const auto y1 = std::min(height, static_cast<std::uint32_t>(std::ceil(bottom * state.pixel_density)));
    std::size_t count = 0U;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            if (!std::equal(before.begin() + static_cast<std::ptrdiff_t>(offset),
                    before.begin() + static_cast<std::ptrdiff_t>(offset + 4U),
                    after.begin() + static_cast<std::ptrdiff_t>(offset))) {
                ++count;
            }
        }
    }
    return count;
}

void Activate(NativeHost& host, const NativeAccessibilityNode& node) {
    const float x = node.bounds.x + node.bounds.width * 0.5F;
    const float y = node.bounds.y + node.bounds.height * 0.5F;
    REQUIRE(node.bounds.width > 0.0F);
    REQUIRE(node.bounds.height > 0.0F);
    REQUIRE(host.HitTest(x, y) == node.handle);
    host.DispatchPointer(x, y, true, 0, 1U, 1);
    REQUIRE(host.HitTest(x, y) == node.handle);
    host.DispatchPointer(x, y, false, 0, 0U, 1);
    host.DrainFrames();
}

} // namespace

TEST_CASE("native application mounts and switches the six universal demo pages",
    "[v2][native][demo][universal]") {
    NativeHost host(false);
    host.Resize(1600U, 1000U);
    host.MountApplication();
    host.DrainFrames();

    const std::array page_labels{
        "Dashboard",
        "Basic controls",
        "Text and fonts",
        "Advanced",
        "Immediate drawing",
        "Platform",
    };
    const auto& dashboard = host.AccessibilitySnapshotForTesting();
    for (const auto* label : page_labels) {
        CAPTURE(label);
        REQUIRE(FindLabel(dashboard, label) != nullptr);
    }
    CHECK(FindLabel(dashboard, "Dashboard tab panel") != nullptr);
    CHECK(FindLabel(dashboard, "Dashboard horizontal slider") != nullptr);

    for (const auto* label : std::array{
             "Text and fonts",
             "Basic controls",
             "Advanced",
             "Immediate drawing",
             "Platform",
         }) {
        const auto& before = host.AccessibilitySnapshotForTesting();
        const auto* tab = FindLabel(before, label);
        REQUIRE(tab != nullptr);
        Activate(host, *tab);
        const auto& selected = host.AccessibilitySnapshotForTesting();
        CHECK(FindLabel(selected, std::string(label) + " tab panel") != nullptr);
        if (std::string_view(label) == "Immediate drawing") {
            CHECK(FindLabel(selected, "Animated gauge drawing sample") != nullptr);
            CHECK(FindLabel(selected, "Paint canvas - drag to draw") != nullptr);
        } else if (std::string_view(label) == "Platform") {
            CHECK(FindLabel(selected, "Enable page zoom") != nullptr);
        }
    }
    CHECK(host.IsIdle());
}

TEST_CASE("native pointer capture preserves in-viewport hit testing for drag targets",
    "[v2][native][input][capture][drag-drop]") {
    NativeHost host(false);
    host.Resize(1600U, 1000U);
    host.MountApplication();
    host.DrainFrames();

    const auto& dashboard = host.AccessibilitySnapshotForTesting();
    const auto* source = FindLabel(dashboard, "Dashboard");
    const auto* target = FindLabel(dashboard, "Basic controls");
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);
    const float target_x = target->bounds.x + target->bounds.width * 0.5F;
    const float target_y = target->bounds.y + target->bounds.height * 0.5F;

    host.SetPointerCaptureForTesting(source->handle);
    host.DispatchPointerMove(target_x, target_y);
    CHECK(host.LastPointerTargetForTesting() == target->handle);
    host.SetPointerCaptureForTesting(0U);
}

TEST_CASE("native universal pages render bitmap rich text and complete app font loading",
    "[v2][native][demo][universal][drawing][fonts]") {
    NativeHost host(false);
    host.Resize(1600U, 1000U);
    REQUIRE(host.LoadFontForTesting(
        1U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSans-Regular.ttf"));
    REQUIRE(host.LoadFontForTesting(
        2U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSans-Bold.ttf"));
    REQUIRE(host.LoadFontForTesting(
        7U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSansMono-Regular.ttf"));
    REQUIRE(host.LoadFontForTesting(
        8U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSansMono-Bold.ttf"));
    REQUIRE(host.LoadFontForTesting(
        3U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSansSymbols2-Regular.ttf"));
    REQUIRE(host.LoadFontForTesting(
        4U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoEmoji-Regular.ttf"));
    REQUIRE(host.LoadFontForTesting(
        5U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSans-Italic.ttf"));
    REQUIRE(host.LoadFontForTesting(
        6U, std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/NotoSans-BoldItalic.ttf"));
    host.MountApplication();
    host.DrainFrames();

    ResetNativeFuiDrawingMetricsForTesting();
    const auto* text_tab = FindLabel(host.AccessibilitySnapshotForTesting(), "Text and fonts");
    REQUIRE(text_tab != nullptr);
    Activate(host, *text_tab);
    const auto font_metrics = NativeFuiDrawingMetricsForTesting();
    CHECK(font_metrics.font_load_request_count >= 5U);
    CHECK(font_metrics.font_load_dispatch_count == font_metrics.font_load_request_count);

    const auto* drawing_tab = FindLabel(host.AccessibilitySnapshotForTesting(), "Immediate drawing");
    REQUIRE(drawing_tab != nullptr);
    Activate(host, *drawing_tab);
    const auto* canvas = FindLabel(host.AccessibilitySnapshotForTesting(), "Paint canvas - drag to draw");
    REQUIRE(canvas != nullptr);
    const auto drawing_metrics = NativeFuiDrawingMetricsForTesting();
    CHECK(drawing_metrics.node_render_request_count > 0U);
    CHECK(drawing_metrics.node_render_pending_visual_count > 0U);
    CHECK(drawing_metrics.node_render_success_count > 0U);
    CHECK(drawing_metrics.node_render_success_count <= drawing_metrics.node_render_request_count);
    CHECK(CountGreenPixels(host, *canvas) > 12U);
}

TEST_CASE("native retained reorder drag paints its target insertion marker and drops on release",
    "[v2][native][demo][universal][drag-drop]") {
    NativeHost host(false);
    host.Resize(1600U, 1000U);
    host.MountApplication();
    host.DrainFrames();

    const auto* text_and_fonts_tab =
        FindLabel(host.AccessibilitySnapshotForTesting(), "Text and fonts");
    REQUIRE(text_and_fonts_tab != nullptr);
    Activate(host, *text_and_fonts_tab);
    REQUIRE(ScrollUntilLabelPrefix(host, "Drag grip for "));
    const auto* source = FindLabelPrefix(host.AccessibilitySnapshotForTesting(), "Drag grip for ");
    REQUIRE(source != nullptr);
    const auto* target = FindNthLabelPrefix(
        host.AccessibilitySnapshotForTesting(), "Reorder item ", 2U);
    REQUIRE(target != nullptr);
    REQUIRE(target->handle != source->handle);

    const float source_x = source->bounds.x + source->bounds.width * 0.5F;
    const float source_y = source->bounds.y + source->bounds.height * 0.5F;
    const float target_x = target->bounds.x + target->bounds.width * 0.5F;
    const float target_y = target->bounds.y + target->bounds.height * 0.5F;
    const auto target_bounds = target->bounds;
    const auto before = host.SnapshotRgba();
    host.DispatchPointer(source_x, source_y, true, 0, 1U, 1);
    host.DispatchPointerMove(source_x + 24.0F, source_y, 0U, 1U);
    host.DrainFrames();
    host.DispatchPointerMove(target_x, target_y, 0U, 1U);
    host.DrainFrames();
    const auto after = host.SnapshotRgba();
    CHECK(CountChangedPixels(
        host,
        before,
        after,
        target_bounds.x,
        target_bounds.y - 8.0F,
        target_bounds.x + target_bounds.width,
        target_bounds.y) > 20U);
    host.DispatchPointer(target_x, target_y, false, 0, 0U, 1);
    host.DrainFrames();

    CHECK(FindLabel(
        host.AccessibilitySnapshotForTesting(),
        "Reorder item 1: Audit font shard cache") != nullptr);
    CHECK(FindLabel(
        host.AccessibilitySnapshotForTesting(),
        "Reorder item 2: Document Core rename") != nullptr);
    CHECK(FindLabel(
        host.AccessibilitySnapshotForTesting(),
        "Reorder item 3: Add drag reorder demo") != nullptr);

    CHECK(FindLabelPrefix(
        host.AccessibilitySnapshotForTesting(), "Reorder drag preview for ") == nullptr);
    host.DispatchPointerMove(target_x, target_y, 0U, 0U);
    host.DrainFrames();
    CHECK(FindLabelPrefix(
        host.AccessibilitySnapshotForTesting(), "Reorder drag preview for ") == nullptr);

    const auto* second_source = FindLabelPrefix(
        host.AccessibilitySnapshotForTesting(), "Drag grip for ");
    const auto* second_target = FindNthLabelPrefix(
        host.AccessibilitySnapshotForTesting(), "Reorder item ", 1U);
    REQUIRE(second_source != nullptr);
    REQUIRE(second_target != nullptr);
    const float second_source_x = second_source->bounds.x + second_source->bounds.width * 0.5F;
    const float second_source_y = second_source->bounds.y + second_source->bounds.height * 0.5F;
    const float second_target_x = second_target->bounds.x + second_target->bounds.width * 0.5F;
    const float second_target_y = second_target->bounds.y + second_target->bounds.height * 0.5F;
    host.DispatchPointer(second_source_x, second_source_y, true, 0, 1U, 1);
    host.DispatchPointerMove(second_source_x + 24.0F, second_source_y, 0U, 1U);
    host.DrainFrames();
    host.DispatchPointerMove(second_target_x, second_target_y, 0U, 1U);
    host.DrainFrames();
    CHECK(FindLabelPrefix(
        host.AccessibilitySnapshotForTesting(), "Reorder drag preview for ") != nullptr);
    host.DispatchPointer(second_target_x, second_target_y, false, 0, 0U, 1);
    host.DrainFrames();
}
