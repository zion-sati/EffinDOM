#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CommandBuilder.h"
#include "Engine.h"
#include "EngineInternal.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <include/core/SkData.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkImage.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>

using Catch::Approx;

#ifndef EFFINDOM_SOURCE_DIR
#define EFFINDOM_SOURCE_DIR "."
#endif

namespace {

using effindom::v2::CommandBufferStats;
using effindom::v2::ColoredRect;
using effindom::v2::Engine;
using effindom::v2::GlyphPlacement;
using effindom::v2::GradientStop;
using effindom::v2::PathVerbRecord;
using effindom::v2::Rect;
using effindom::v2::SceneInstructionDebugView;
namespace detail = effindom::v2::detail;
using effindom::v2::test::CommandBuilder;
using effindom::v2::test::Handle;

std::vector<std::uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> SnapshotRgba(sk_sp<SkSurface> surface, int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const bool ok = surface->readPixels(
        info,
        pixels.data(),
        static_cast<size_t>(width) * 4U,
        0,
        0);
    REQUIRE(ok);
    return pixels;
}

std::uint64_t HashBytes(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::uint8_t byte : bytes) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::filesystem::path GoldensDir() {
    return std::filesystem::path(EFFINDOM_SOURCE_DIR) / "v2/core/tests/goldens";
}

std::filesystem::path StyledSceneGoldenPath() {
#if defined(__linux__)
    return GoldensDir() / "styled-scene-linux.png";
#else
    return GoldensDir() / "styled-scene-macos.png";
#endif
}

std::vector<std::uint8_t> ReadPngRgba(const std::filesystem::path& path, int expected_width, int expected_height) {
    const sk_sp<SkData> data = SkData::MakeFromFileName(path.string().c_str());
    REQUIRE(data);
    const sk_sp<SkImage> image = SkImages::DeferredFromEncodedData(data);
    REQUIRE(image);
    REQUIRE(image->width() == expected_width);
    REQUIRE(image->height() == expected_height);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(expected_width) * static_cast<std::size_t>(expected_height) * 4U);
    const SkImageInfo info = SkImageInfo::Make(expected_width, expected_height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    REQUIRE(image->readPixels(info, pixels.data(), static_cast<size_t>(expected_width) * 4U, 0, 0));
    return pixels;
}

struct SsimComparison {
    double score = 0.0;
    double mean_abs_luma_delta = 0.0;
    std::uint8_t max_luma_delta = 0U;
};

double CompositeLumaOnWhite(const std::vector<std::uint8_t>& pixels, int width, int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
    const double alpha = static_cast<double>(pixels[offset + 3]) / 255.0;
    const double red = alpha * static_cast<double>(pixels[offset + 0]) + (1.0 - alpha) * 255.0;
    const double green = alpha * static_cast<double>(pixels[offset + 1]) + (1.0 - alpha) * 255.0;
    const double blue = alpha * static_cast<double>(pixels[offset + 2]) + (1.0 - alpha) * 255.0;
    return (0.2126 * red) + (0.7152 * green) + (0.0722 * blue);
}

double ComputeWindowSsim(
    const std::vector<std::uint8_t>& lhs,
    const std::vector<std::uint8_t>& rhs,
    int width,
    int min_x,
    int min_y,
    int max_x,
    int max_y) {
    constexpr double kL = 255.0;
    constexpr double kC1 = (0.01 * kL) * (0.01 * kL);
    constexpr double kC2 = (0.03 * kL) * (0.03 * kL);

    const int window_width = max_x - min_x;
    const int window_height = max_y - min_y;
    const int sample_count = window_width * window_height;
    if (sample_count <= 0) {
        return 1.0;
    }

    double sum_lhs = 0.0;
    double sum_rhs = 0.0;
    for (int y = min_y; y < max_y; y += 1) {
        for (int x = min_x; x < max_x; x += 1) {
            sum_lhs += CompositeLumaOnWhite(lhs, width, x, y);
            sum_rhs += CompositeLumaOnWhite(rhs, width, x, y);
        }
    }

    const double mean_lhs = sum_lhs / static_cast<double>(sample_count);
    const double mean_rhs = sum_rhs / static_cast<double>(sample_count);

    double variance_lhs = 0.0;
    double variance_rhs = 0.0;
    double covariance = 0.0;
    for (int y = min_y; y < max_y; y += 1) {
        for (int x = min_x; x < max_x; x += 1) {
            const double centered_lhs = CompositeLumaOnWhite(lhs, width, x, y) - mean_lhs;
            const double centered_rhs = CompositeLumaOnWhite(rhs, width, x, y) - mean_rhs;
            variance_lhs += centered_lhs * centered_lhs;
            variance_rhs += centered_rhs * centered_rhs;
            covariance += centered_lhs * centered_rhs;
        }
    }

    variance_lhs /= static_cast<double>(sample_count);
    variance_rhs /= static_cast<double>(sample_count);
    covariance /= static_cast<double>(sample_count);

    const double numerator = (2.0 * mean_lhs * mean_rhs + kC1) * (2.0 * covariance + kC2);
    const double denominator =
        (mean_lhs * mean_lhs + mean_rhs * mean_rhs + kC1) * (variance_lhs + variance_rhs + kC2);
    if (denominator <= 0.0) {
        return 1.0;
    }
    return numerator / denominator;
}

SsimComparison CompareSnapshotSsim(
    const std::vector<std::uint8_t>& actual,
    const std::vector<std::uint8_t>& expected,
    int width,
    int height) {
    REQUIRE(actual.size() == expected.size());

    constexpr int kWindowSize = 8;
    double ssim_sum = 0.0;
    std::size_t ssim_count = 0U;
    double total_abs_delta = 0.0;
    std::uint8_t max_luma_delta = 0U;

    for (int y = 0; y < height; y += 1) {
        for (int x = 0; x < width; x += 1) {
            const double lhs = CompositeLumaOnWhite(actual, width, x, y);
            const double rhs = CompositeLumaOnWhite(expected, width, x, y);
            const double delta = std::abs(lhs - rhs);
            total_abs_delta += delta;
            max_luma_delta = std::max(
                max_luma_delta,
                static_cast<std::uint8_t>(std::min(delta, 255.0)));
        }
    }

    for (int min_y = 0; min_y < height; min_y += kWindowSize) {
        const int max_y = std::min(min_y + kWindowSize, height);
        for (int min_x = 0; min_x < width; min_x += kWindowSize) {
            const int max_x = std::min(min_x + kWindowSize, width);
            ssim_sum += ComputeWindowSsim(actual, expected, width, min_x, min_y, max_x, max_y);
            ssim_count += 1U;
        }
    }

    return SsimComparison{
        ssim_count == 0U ? 1.0 : (ssim_sum / static_cast<double>(ssim_count)),
        (width <= 0 || height <= 0) ? 0.0 : (total_abs_delta / static_cast<double>(width * height)),
        max_luma_delta,
    };
}

std::array<std::uint8_t, 4> PixelAt(const std::vector<std::uint8_t>& pixels, int width, int x, int y) {
    const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
    return {
        pixels[offset + 0],
        pixels[offset + 1],
        pixels[offset + 2],
        pixels[offset + 3],
    };
}

std::array<std::uint8_t, 4> MaxPixelInBox(
    const std::vector<std::uint8_t>& pixels,
    int width,
    int min_x,
    int min_y,
    int max_x,
    int max_y) {
    std::array<std::uint8_t, 4> best = {0U, 0U, 0U, 0U};
    for (int y = min_y; y <= max_y; y += 1) {
        for (int x = min_x; x <= max_x; x += 1) {
            const auto pixel = PixelAt(pixels, width, x, y);
            if (pixel[3] > best[3] || (pixel[3] == best[3] && pixel[1] > best[1])) {
                best = pixel;
            }
        }
    }
    return best;
}

void CheckSnapshotAgainstGolden(
    const std::vector<std::uint8_t>& actual,
    int width,
    int height,
    const std::filesystem::path& golden_path,
    double min_ssim) {
    INFO("Golden: " << golden_path.string());
    const std::vector<std::uint8_t> expected = ReadPngRgba(golden_path, width, height);
    const SsimComparison comparison = CompareSnapshotSsim(actual, expected, width, height);
    INFO("SSIM: " << comparison.score);
    INFO("Mean absolute luma delta: " << comparison.mean_abs_luma_delta);
    INFO("Max luma delta: " << static_cast<int>(comparison.max_luma_delta));
    CHECK(comparison.score >= min_ssim);
}

std::vector<std::uint8_t> MakeSvgBytes() {
    static constexpr char kSvg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
        "<rect x=\"8\" y=\"8\" width=\"48\" height=\"48\" rx=\"10\" fill=\"#ffffff\"/>"
        "<circle cx=\"32\" cy=\"32\" r=\"10\" fill=\"#000000\"/>"
        "</svg>";
    return std::vector<std::uint8_t>(kSvg, kSvg + sizeof(kSvg) - 1);
}

std::vector<std::uint8_t> MakeNinePatchPixels() {
    constexpr std::uint32_t width = 6;
    constexpr std::uint32_t height = 6;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U, 0U);
    for (std::uint32_t y = 0; y < height; y += 1) {
        for (std::uint32_t x = 0; x < width; x += 1) {
            const bool corner = (x < 2 || x >= 4) && (y < 2 || y >= 4);
            const bool edge = !corner && (x < 2 || x >= 4 || y < 2 || y >= 4);
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            pixels[offset + 0] = static_cast<std::uint8_t>(corner ? 255 : edge ? 0 : 32);
            pixels[offset + 1] = static_cast<std::uint8_t>(corner ? 64 : edge ? 220 : 64);
            pixels[offset + 2] = static_cast<std::uint8_t>(corner ? 64 : edge ? 96 : 255);
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

std::vector<GlyphPlacement> MakeGlyphPlacements(std::size_t count, float baseline_y = 13.0f) {
    std::vector<GlyphPlacement> glyphs;
    glyphs.reserve(count);
    for (std::size_t index = 0; index < count; index += 1U) {
        glyphs.push_back(GlyphPlacement{
            36U,
            static_cast<float>(index),
            baseline_y,
        });
    }
    return glyphs;
}

} // namespace

TEST_CASE("v2 detail helpers and lifecycle handle invalid inputs", "[v2][unit]") {
    Engine engine;
    engine.Init(40, 24, 0.0f);
    CHECK(engine.physical_width() == 40U);
    CHECK(engine.physical_height() == 24U);
    CHECK(engine.dpr() == Approx(1.0f));

    engine.Resize(96, 72, 2.0f);
    CHECK(engine.physical_width() == 96U);
    CHECK(engine.physical_height() == 72U);
    CHECK(engine.dpr() == Approx(2.0f));

    CHECK_FALSE(engine.GetNodeForTesting(Handle(999)).has_value());

    const std::array<std::uint8_t, 4> pixel = {255, 0, 0, 255};
    const std::array<std::uint8_t, 3> bad_font = {0, 1, 2};
    const std::array<std::uint8_t, 1> bad_svg = {'x'};
    engine.RegisterFont(0U, nullptr, 0U);
    engine.RegisterFont(9U, bad_font.data(), static_cast<std::uint32_t>(bad_font.size()));
    engine.RegisterSvg(0U, nullptr, 0U);
    engine.RegisterSvg(7U, bad_svg.data(), static_cast<std::uint32_t>(bad_svg.size()));
    engine.RegisterTextureRgba(0U, pixel.data(), 1U, 1U, pixel.size());
    engine.RegisterTextureRgba(1U, nullptr, 1U, 1U, pixel.size());
    engine.RegisterTextureRgba(1U, pixel.data(), 0U, 1U, pixel.size());
    engine.RegisterTextureRgba(1U, pixel.data(), 1U, 0U, pixel.size());
    engine.RegisterTextureRgba(1U, pixel.data(), 1U, 1U, pixel.size() - 1U);

    CHECK(detail::ClampNonNegative(-4.0f) == Approx(0.0f));
    CHECK(detail::ClampNonNegative(3.5f) == Approx(3.5f));
    CHECK(detail::ClampOpacity(-1.0f) == Approx(0.0f));
    CHECK(detail::ClampOpacity(2.0f) == Approx(1.0f));
    CHECK(detail::VerbArgCount(ED_PATH_MOVE_TO) == 2U);
    CHECK(detail::VerbArgCount(ED_PATH_LINE_TO) == 2U);
    CHECK(detail::VerbArgCount(ED_PATH_QUAD_TO) == 4U);
    CHECK(detail::VerbArgCount(ED_PATH_CUBIC_TO) == 6U);
    CHECK(detail::VerbArgCount(ED_PATH_CLOSE) == 0U);
    CHECK(detail::VerbArgCount(999U) == 0U);

    const std::uint64_t handle = detail::DecodeHandleWords(5U, 9U);
    const detail::HandleParts parts = detail::DecodeHandle(handle);
    CHECK(parts.index == 5U);
    CHECK(parts.generation == 9U);

    detail::DisplayNode node;
    node.gradient_stops = {GradientStop{0.0f, 1U}};
    node.path = {PathVerbRecord{}};
    node.glyphs = {GlyphPlacement{1U, 2.0f, 3.0f}};
    node.highlights = {Rect{1.0f, 2.0f, 3.0f, 4.0f}};
    node.colored_highlights = {ColoredRect{Rect{1.0f, 1.0f, 2.0f, 2.0f}, 0xff112233U}};
    node.ResetForCreate(7U);
    CHECK(node.alive);
    CHECK(node.generation == 7U);
    CHECK(node.gradient_stops.empty());
    CHECK(node.path.empty());
    CHECK(node.glyphs.empty());
    CHECK(node.highlights.empty());
    CHECK(node.colored_highlights.empty());

    node.alive = true;
    node.has_box_style = true;
    node.gradient_stops = {GradientStop{0.5f, 2U}};
    node.path = {PathVerbRecord{}};
    node.glyphs = {GlyphPlacement{2U, 4.0f, 5.0f}};
    node.highlights = {Rect{4.0f, 5.0f, 6.0f, 7.0f}};
    node.colored_highlights = {ColoredRect{Rect{2.0f, 2.0f, 3.0f, 3.0f}, 0xff445566U}};
    node.ResetForDelete();
    CHECK_FALSE(node.alive);
    CHECK_FALSE(node.has_box_style);
    CHECK(node.gradient_stops.empty());
    CHECK(node.path.empty());
    CHECK(node.glyphs.empty());
    CHECK(node.highlights.empty());
    CHECK(node.colored_highlights.empty());
}

TEST_CASE("v2 render scales logical fills to physical pixels exactly once", "[v2][unit]") {
    Engine engine;
    engine.Init(8, 8, 2.0f);

    const std::uint64_t filled = Handle(3110);

    CommandBuilder builder;
    builder.CreateNode(filled);
    builder.SetBounds(filled, 1.0f, 1.0f, 1.0f, 1.0f, false);
    builder.SetBoxStyle(filled, 0xff0000ffU, 0.0f, 0.0f, 0.0f, 0.0f);
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, filled},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(8, 8));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    const auto pixels = SnapshotRgba(surface, 8, 8);
    const auto fill_band = MaxPixelInBox(pixels, 8, 2, 2, 3, 3);
    const auto left_band = MaxPixelInBox(pixels, 8, 1, 2, 1, 3);
    const auto top_band = MaxPixelInBox(pixels, 8, 2, 1, 3, 1);
    const auto right_band = MaxPixelInBox(pixels, 8, 4, 2, 4, 3);
    const auto bottom_band = MaxPixelInBox(pixels, 8, 2, 4, 3, 4);
    CHECK(fill_band[0] > 200U);
    CHECK(fill_band[3] > 200U);
    CHECK(left_band[3] < 32U);
    CHECK(top_band[3] < 32U);
    CHECK(right_band[3] < 32U);
    CHECK(bottom_band[3] < 32U);
}

TEST_CASE("v2 command parsing stores node state", "[v2][unit]") {
    Engine engine;
    const std::uint64_t handle = Handle(42);

    CommandBuilder builder;
    builder.CreateNode(handle);
    builder.SetBounds(
        handle,
        10.0f,
        20.0f,
        100.0f,
        60.0f,
        12.0f,
        24.0f,
        80.0f,
        40.0f,
        true,
        ED_CLIP_MODE_STRICT_CONTENT);
    builder.SetBoxStyle(handle, 0x336699ffU, 4.0f, 8.0f, 12.0f, 16.0f, 3.0f, 0xffcc00ffU, ED_BORDER_DASHED, 6.0f, 3.0f);
    builder.SetLinearGradient(handle, 10.0f, 20.0f, 110.0f, 80.0f, {
        GradientStop{0.0f, 0xff0000ffU},
        GradientStop{1.0f, 0x0000ffffU},
    });
    builder.SetLayerEffect(handle, 0.75f, 2.5f, ED_BLEND_MULTIPLY);
    builder.SetBackgroundBlur(handle, 5.5f);
    builder.SetDropShadow(handle, 0x00000044U, 2.0f, 6.0f, 18.0f, 4.0f);

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));

    CHECK(stats.parsed_commands == 7U);
    CHECK(stats.ignored_commands == 0U);
    CHECK(stats.truncated_buffers == 0U);
    CHECK(stats.unknown_commands == 0U);

    const auto node = engine.GetNodeForTesting(handle);
    REQUIRE(node.has_value());
    CHECK(node->visual_bounds.x == Approx(10.0f));
    CHECK(node->visual_bounds.y == Approx(20.0f));
    CHECK(node->visual_bounds.width == Approx(100.0f));
    CHECK(node->visual_bounds.height == Approx(60.0f));
    CHECK(node->hit_bounds.x == Approx(12.0f));
    CHECK(node->hit_bounds.y == Approx(24.0f));
    CHECK(node->hit_bounds.width == Approx(80.0f));
    CHECK(node->hit_bounds.height == Approx(40.0f));
    CHECK(node->clip_bounds.x == Approx(10.0f));
    CHECK(node->clip_bounds.y == Approx(20.0f));
    CHECK(node->clip_bounds.width == Approx(100.0f));
    CHECK(node->clip_bounds.height == Approx(60.0f));
    CHECK(node->interactive);
    CHECK(node->clip_mode == ED_CLIP_MODE_STRICT_CONTENT);
    CHECK(node->has_box_style);
    CHECK(node->bg_color == 0x336699ffU);
    CHECK(node->corner_radii[0] == Approx(4.0f));
    CHECK(node->corner_radii[3] == Approx(16.0f));
    CHECK(node->has_border);
    CHECK(node->border_style == ED_BORDER_DASHED);
    CHECK(node->has_gradient);
    REQUIRE(node->gradient_stops.size() == 2U);
    CHECK(node->has_layer_effect);
    CHECK(node->opacity == Approx(0.75f));
    CHECK(node->blur_sigma == Approx(2.5f));
    CHECK(node->background_blur_sigma == Approx(5.5f));
    CHECK(node->drop_shadow_color == 0x00000044U);
    CHECK(node->drop_shadow_offset_x == Approx(2.0f));
    CHECK(node->drop_shadow_offset_y == Approx(6.0f));
    CHECK(node->drop_shadow_blur_sigma == Approx(18.0f));
    CHECK(node->drop_shadow_spread == Approx(4.0f));
    CHECK(node->blend_mode == ED_BLEND_MULTIPLY);
}

TEST_CASE("v2 duplicate create preserves existing retained node state", "[v2][unit]") {
    Engine engine;
    const std::uint64_t handle = Handle(77);

    CommandBuilder initial;
    initial.CreateNode(handle);
    initial.SetBounds(
        handle,
        10.0f,
        20.0f,
        100.0f,
        60.0f,
        12.0f,
        24.0f,
        80.0f,
        40.0f,
        true,
        ED_CLIP_MODE_STRICT_CONTENT);
    initial.SetBoxStyle(handle, 0x336699ffU, 4.0f, 8.0f, 12.0f, 16.0f, 3.0f, 0xffcc00ffU, ED_BORDER_DASHED, 6.0f, 3.0f);

    const CommandBufferStats initial_stats = engine.ExecuteCommandBuffer(
        initial.words().data(),
        static_cast<std::uint32_t>(initial.words().size()));
    CHECK(initial_stats.parsed_commands == 3U);

    CommandBuilder duplicate_create;
    duplicate_create.CreateNode(handle);

    const CommandBufferStats duplicate_stats = engine.ExecuteCommandBuffer(
        duplicate_create.words().data(),
        static_cast<std::uint32_t>(duplicate_create.words().size()));
    CHECK(duplicate_stats.parsed_commands == 1U);
    CHECK(duplicate_stats.ignored_commands == 0U);

    const auto node = engine.GetNodeForTesting(handle);
    REQUIRE(node.has_value());
    CHECK(node->visual_bounds.x == Approx(10.0f));
    CHECK(node->visual_bounds.y == Approx(20.0f));
    CHECK(node->visual_bounds.width == Approx(100.0f));
    CHECK(node->visual_bounds.height == Approx(60.0f));
    CHECK(node->hit_bounds.x == Approx(12.0f));
    CHECK(node->hit_bounds.y == Approx(24.0f));
    CHECK(node->hit_bounds.width == Approx(80.0f));
    CHECK(node->hit_bounds.height == Approx(40.0f));
    CHECK(node->clip_bounds.x == Approx(10.0f));
    CHECK(node->clip_bounds.y == Approx(20.0f));
    CHECK(node->clip_bounds.width == Approx(100.0f));
    CHECK(node->clip_bounds.height == Approx(60.0f));
    CHECK(node->interactive);
    CHECK(node->clip_mode == ED_CLIP_MODE_STRICT_CONTENT);
    CHECK(node->has_box_style);
    CHECK(node->bg_color == 0x336699ffU);
    CHECK(node->corner_radii[0] == Approx(4.0f));
    CHECK(node->corner_radii[3] == Approx(16.0f));
    CHECK(node->has_border);
    CHECK(node->border_style == ED_BORDER_DASHED);
    CHECK(node->border_width == Approx(3.0f));
    CHECK(node->border_color == 0xffcc00ffU);
}

TEST_CASE("v2 command parsing covers success ignored truncated and unknown paths", "[v2][unit]") {
    Engine engine;
    engine.Init(128, 96, 1.0f);

    const std::uint64_t valid = Handle(40);
    const std::uint64_t stale = Handle(41);
    const std::uint64_t image_node = Handle(42);
    const std::uint64_t nine_node = Handle(43);
    const std::uint64_t svg_node = Handle(44);
    const std::uint64_t text_node = Handle(45);

    const std::vector<std::uint8_t> svg = MakeSvgBytes();
    engine.RegisterSvg(17U, svg.data(), static_cast<std::uint32_t>(svg.size()));

    CommandBuilder builder;
    builder.CreateNode(valid);
    builder.CreateNode(stale);
    builder.CreateNode(image_node);
    builder.CreateNode(nine_node);
    builder.CreateNode(svg_node);
    builder.CreateNode(text_node);
    builder.DeleteNode(stale);
    builder.DeleteNode(stale);
    builder.PushRaw(CMD_CREATE_NODE);
    builder.PushRaw(detail::kMaxNodes);
    builder.PushRaw(1U);

    builder.SetBounds(valid, 1.0f, 2.0f, 30.0f, 20.0f, 2.0f, 3.0f, 18.0f, 12.0f, true);
    builder.SetBounds(stale, 0.0f, 0.0f, 10.0f, 10.0f, false);
    builder.SetBoxStyle(valid, 0xff1010ffU, -1.0f, -2.0f, 3.0f, 4.0f, -3.0f, 0xff00ffffU, ED_BORDER_DASHED, -2.0f, -1.0f);
    builder.SetBoxStyle(stale, 0xff0000ffU, 1.0f, 1.0f, 1.0f, 1.0f);
    builder.SetLinearGradient(valid, 0.0f, 0.0f, 10.0f, 10.0f, {
        GradientStop{1.5f, 0x111111ffU},
        GradientStop{-0.5f, 0x222222ffU},
    });
    builder.SetLinearGradient(stale, 0.0f, 0.0f, 4.0f, 4.0f, {
        GradientStop{0.0f, 0xffffffffU},
    });
    builder.SetLayerEffect(valid, 1.5f, -2.0f, ED_BLEND_LIGHTEN);
    builder.SetLayerEffect(stale, 0.5f, 1.0f, ED_BLEND_SCREEN);
    builder.SetImage(image_node, 9U, ED_OBJECT_FIT_NONE);
    builder.SetImage(stale, 9U, ED_OBJECT_FIT_FILL);
    builder.SetImageNine(nine_node, 21U, -1.0f, -2.0f, 3.0f, 4.0f);
    builder.SetImageNine(stale, 21U, 1.0f, 1.0f, 1.0f, 1.0f);

    PathVerbRecord move{};
    move.verb = ED_PATH_MOVE_TO;
    move.arg_count = 2;
    move.args = {0.0f, 0.0f, 0, 0, 0, 0};
    PathVerbRecord quad{};
    quad.verb = ED_PATH_QUAD_TO;
    quad.arg_count = 4;
    quad.args = {4.0f, 2.0f, 6.0f, 4.0f, 0, 0};
    PathVerbRecord cubic{};
    cubic.verb = ED_PATH_CUBIC_TO;
    cubic.arg_count = 6;
    cubic.args = {6.0f, 5.0f, 7.0f, 6.0f, 8.0f, 7.0f};
    PathVerbRecord unknown{};
    unknown.verb = 999U;
    unknown.arg_count = 0;
    PathVerbRecord close{};
    close.verb = ED_PATH_CLOSE;
    close.arg_count = 0;
    builder.SetPath(valid, 0x00000000U, 0xff00ffffU, -4.0f, {move, quad, cubic, unknown, close});
    builder.SetPath(stale, 0xffffffffU, 0xffffffffU, 1.0f, {move});

    builder.SetSvg(svg_node, 17U, 0U);
    builder.SetSvg(stale, 17U, 0xff00ffffU);
    builder.SetGlyphRun(text_node, 7U, -12.0f, 0xff334455U, {
        GlyphPlacement{40U, 4.0f, 12.0f, 7U},
        GlyphPlacement{41U, 16.0f, 12.0f, 9U},
    });
    builder.SetGlyphRun(stale, 7U, 12.0f, 0xffffffffU, {
        GlyphPlacement{40U, 1.0f, 1.0f},
    });
    builder.SetTextFade(text_node, ED_FADE_BOTTOM);
    builder.SetTextFade(stale, ED_FADE_TOP);
    builder.SetCaret(text_node, 8.0f, 9.0f, -5.0f, 0xffabcdefU, 44U);
    builder.SetCaret(stale, 0.0f, 0.0f, 3.0f, 0xffffffffU, 1U);
    builder.SetHighlights(text_node, 0x5500ffffU, {
        Rect{1.0f, 2.0f, 3.0f, 4.0f},
    });
    builder.SetHighlights(stale, 0xffffffffU, {
        Rect{0.0f, 0.0f, 1.0f, 1.0f},
    });
    builder.CommitPaintOrder({valid, stale, image_node, nine_node, svg_node, text_node});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, valid},
        SceneInstructionDebugView{OP_DRAW_NODE, text_node},
        SceneInstructionDebugView{OP_PUSH_CLIP, stale},
        SceneInstructionDebugView{OP_PUSH_LAYER, valid},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));

    CHECK(stats.parsed_commands == 21U);
    CHECK(stats.ignored_commands == 14U);
    CHECK(stats.truncated_buffers == 0U);
    CHECK(stats.unknown_commands == 0U);

    const auto valid_node = engine.GetNodeForTesting(valid);
    REQUIRE(valid_node.has_value());
    CHECK(valid_node->interactive);
    CHECK(valid_node->corner_radii[0] == Approx(0.0f));
    CHECK(valid_node->corner_radii[1] == Approx(0.0f));
    CHECK(valid_node->border_width == Approx(0.0f));
    CHECK_FALSE(valid_node->has_border);
    REQUIRE(valid_node->gradient_stops.size() == 2U);
    CHECK(valid_node->gradient_stops[0].offset == Approx(0.0f));
    CHECK(valid_node->gradient_stops[1].offset == Approx(1.0f));
    CHECK(valid_node->opacity == Approx(1.0f));
    CHECK(valid_node->blur_sigma == Approx(0.0f));
    CHECK(valid_node->blend_mode == ED_BLEND_LIGHTEN);
    CHECK(valid_node->has_path);
    CHECK(valid_node->path.size() == 5U);
    CHECK(valid_node->path_stroke_width == Approx(0.0f));

    const auto image_debug = engine.GetNodeForTesting(image_node);
    REQUIRE(image_debug.has_value());
    CHECK(image_debug->has_image);
    CHECK(image_debug->object_fit == ED_OBJECT_FIT_NONE);

    const auto nine_debug = engine.GetNodeForTesting(nine_node);
    REQUIRE(nine_debug.has_value());
    CHECK(nine_debug->has_image_nine);
    CHECK(nine_debug->image_nine_insets.left == Approx(0.0f));
    CHECK(nine_debug->image_nine_insets.top == Approx(0.0f));

    const auto svg_debug = engine.GetNodeForTesting(svg_node);
    REQUIRE(svg_debug.has_value());
    CHECK(svg_debug->has_svg);
    CHECK(svg_debug->svg_tint_color == 0U);

    const auto text_debug = engine.GetNodeForTesting(text_node);
    REQUIRE(text_debug.has_value());
    CHECK(text_debug->has_glyph_run);
    CHECK(text_debug->font_size == Approx(1.0f));
    REQUIRE(text_debug->glyphs.size() == 2U);
    CHECK(text_debug->glyphs[0].font_id == 7U);
    CHECK(text_debug->glyphs[1].font_id == 9U);
    CHECK(text_debug->fade_edge == ED_FADE_BOTTOM);
    CHECK(text_debug->caret_height == Approx(0.0f));
    REQUIRE(text_debug->highlights.size() == 1U);

    CHECK_FALSE(engine.GetNodeForTesting(stale).has_value());
    const auto paint_order = engine.GetPaintOrderForTesting();
    REQUIRE(paint_order.size() == 5U);
    CHECK(paint_order[0] == valid);
    CHECK(paint_order[4] == text_node);
    REQUIRE(engine.GetSceneInstructionsForTesting().size() == 5U);

    const auto truncated = [&](const std::vector<std::uint32_t>& words) {
        return engine.ExecuteCommandBuffer(words.data(), static_cast<std::uint32_t>(words.size()));
    };

    CHECK(engine.ExecuteCommandBuffer(nullptr, 0U).parsed_commands == 0U);

    CHECK(truncated({CMD_CREATE_NODE, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_DELETE_NODE, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_BOUNDS, 1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_BOX_STYLE, 1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_LINEAR_GRADIENT, 1U, 1U, 0U, 0U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_LINEAR_GRADIENT, 1U, 1U, 0U, 0U, 0U, 0U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_LAYER_EFFECT, 1U, 1U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_IMAGE, 1U, 1U, 9U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_IMAGE_NINE, 1U, 1U, 9U, 0U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_PATH, 1U, 1U, 0U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_PATH, 1U, 1U, 0U, 0U, 0U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_PATH, 1U, 1U, 0U, 0U, 0U, 1U, ED_PATH_LINE_TO}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_SVG, 1U, 1U, 7U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_GLYPH_RUN, 1U, 1U, 7U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_GLYPH_RUN, 1U, 1U, 7U, 0U, 0U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_GLYPH_RUN, 1U, 1U, 7U, 0U, 0U, 1U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_GLYPH_RUN_COLORED, 1U, 1U, 7U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_GLYPH_RUN_COLORED, 1U, 1U, 7U, 0U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_GLYPH_RUN_COLORED, 1U, 1U, 7U, 0U, 1U, 0U, 0U, 0U, 7U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_TEXT_FADE, 1U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_CARET, 1U, 1U, 0U, 0U, 0U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_HIGHLIGHTS, 1U, 1U, 0U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_HIGHLIGHTS, 1U, 1U, 0U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_HIGHLIGHTS_COLORED, 1U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_SET_HIGHLIGHTS_COLORED, 1U, 1U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_COMMIT_PAINT_ORDER}).truncated_buffers == 1U);
    CHECK(truncated({CMD_COMMIT_PAINT_ORDER, 1U, 1U}).truncated_buffers == 1U);
    CHECK(truncated({CMD_COMMIT_SCENE}).truncated_buffers == 1U);
    CHECK(truncated({CMD_COMMIT_SCENE, 1U, OP_DRAW_NODE, 1U}).truncated_buffers == 1U);

    const CommandBufferStats unknown_stats = truncated({777U});
    CHECK(unknown_stats.unknown_commands == 1U);
}

TEST_CASE("v2 core reuses cached glyph blobs until glyph payload changes", "[v2][unit][text]") {
    Engine engine;
    engine.Init(64, 64, 1.0f);

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    engine.RegisterFont(7U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t text = Handle(4100);
    CommandBuilder builder;
    builder.CreateNode(text);
    builder.SetBounds(text, 4.0f, 6.0f, 40.0f, 20.0f, false);
    builder.SetGlyphRun(text, 7U, 16.0f, 0xff112233U, {
        GlyphPlacement{36U, 0.0f, 13.0f},
        GlyphPlacement{37U, 10.0f, 13.0f},
    });
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, text},
    });
    CHECK(engine.ExecuteCommandBuffer(
              builder.words().data(),
              static_cast<std::uint32_t>(builder.words().size()))
          .parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    REQUIRE(surface);

    engine.RenderToCanvas(surface->getCanvas());
    auto node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK(node->glyph_blob_build_count == 1U);
    CHECK(node->glyph_blob_cached);

    engine.RenderToCanvas(surface->getCanvas());
    node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK(node->glyph_blob_build_count == 1U);
    CHECK(node->glyph_blob_cached);

    CommandBuilder update;
    update.SetGlyphRun(text, 7U, 16.0f, 0xff112233U, {
        GlyphPlacement{38U, 0.0f, 13.0f},
    });
    update.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, text},
    });
    CHECK(engine.ExecuteCommandBuffer(
              update.words().data(),
              static_cast<std::uint32_t>(update.words().size()))
          .parsed_commands == 2U);

    engine.RenderToCanvas(surface->getCanvas());
    node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK(node->glyph_blob_build_count == 2U);
    CHECK(node->glyph_blob_cached);
}

TEST_CASE("v2 core parses and renders colored glyph runs without glyph-blob caching", "[v2][unit][text]") {
    Engine engine;
    engine.Init(96, 64, 1.0f);

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    engine.RegisterFont(7U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t text = Handle(4110);
    CommandBuilder builder;
    builder.CreateNode(text);
    builder.SetBounds(text, 6.0f, 8.0f, 72.0f, 28.0f, false);
    builder.SetGlyphRunColored(text, 7U, 18.0f, {
        GlyphPlacement{36U, 0.0f, 15.0f, 7U, 0xff0000ffU},
        GlyphPlacement{37U, 12.0f, 15.0f, 7U, 0x00ff00ffU},
    });
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, text},
    });
    CHECK(engine.ExecuteCommandBuffer(
              builder.words().data(),
              static_cast<std::uint32_t>(builder.words().size()))
          .parsed_commands == 4U);

    auto node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK(node->has_glyph_run);
    CHECK(node->glyph_color == 0U);
    REQUIRE(node->glyphs.size() == 2U);
    CHECK(node->glyphs[0].color == 0xff0000ffU);
    CHECK(node->glyphs[1].color == 0x00ff00ffU);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(96, 64));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK_FALSE(node->glyph_blob_cached);
    CHECK(node->glyph_blob_build_count == 0U);
}

TEST_CASE("v2 core evicts cached glyph blobs after they go unused for too many renders", "[v2][unit][text]") {
    Engine engine;
    engine.Init(64, 64, 1.0f);

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    engine.RegisterFont(7U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t text = Handle(4200);
    CommandBuilder builder;
    builder.CreateNode(text);
    builder.SetBounds(text, 4.0f, 6.0f, 40.0f, 20.0f, false);
    builder.SetGlyphRun(text, 7U, 16.0f, 0xff112233U, MakeGlyphPlacements(32U));
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, text},
    });
    CHECK(engine.ExecuteCommandBuffer(
              builder.words().data(),
              static_cast<std::uint32_t>(builder.words().size()))
          .parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    REQUIRE(surface);

    engine.RenderToCanvas(surface->getCanvas());
    auto node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK(node->glyph_blob_build_count == 1U);
    CHECK(node->glyph_blob_cached);

    CommandBuilder hide;
    hide.CommitScene({});
    CHECK(engine.ExecuteCommandBuffer(
              hide.words().data(),
              static_cast<std::uint32_t>(hide.words().size()))
          .parsed_commands == 1U);

    for (std::uint64_t frame = 0; frame <= detail::kGlyphBlobMaxUnusedGenerations; frame += 1U) {
        engine.RenderToCanvas(surface->getCanvas());
    }

    node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK_FALSE(node->glyph_blob_cached);

    CommandBuilder reveal;
    reveal.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, text},
    });
    CHECK(engine.ExecuteCommandBuffer(
              reveal.words().data(),
              static_cast<std::uint32_t>(reveal.words().size()))
          .parsed_commands == 1U);

    engine.RenderToCanvas(surface->getCanvas());
    node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK(node->glyph_blob_build_count == 2U);
    CHECK(node->glyph_blob_cached);
}

TEST_CASE("v2 core glyph blob budget evicts the least recently used cached text blobs", "[v2][unit][text]") {
    Engine engine;
    engine.Init(64, 64, 1.0f);

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    engine.RegisterFont(7U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    constexpr std::size_t kBudgetStressGlyphCount = 12000U;
    const std::uint64_t first = Handle(4300);
    const std::uint64_t second = Handle(4301);
    const std::uint64_t third = Handle(4302);
    const std::vector<GlyphPlacement> glyphs = MakeGlyphPlacements(kBudgetStressGlyphCount);

    CommandBuilder builder;
    for (const std::uint64_t handle : {first, second, third}) {
        builder.CreateNode(handle);
        builder.SetBounds(handle, 4.0f, 6.0f, 40.0f, 20.0f, false);
        builder.SetGlyphRun(handle, 7U, 16.0f, 0xff112233U, glyphs);
    }
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, first},
        SceneInstructionDebugView{OP_DRAW_NODE, second},
        SceneInstructionDebugView{OP_DRAW_NODE, third},
    });
    CHECK(engine.ExecuteCommandBuffer(
              builder.words().data(),
              static_cast<std::uint32_t>(builder.words().size()))
          .parsed_commands == 10U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    REQUIRE(surface);

    engine.RenderToCanvas(surface->getCanvas());
    auto first_node = engine.GetNodeForTesting(first);
    auto second_node = engine.GetNodeForTesting(second);
    auto third_node = engine.GetNodeForTesting(third);
    REQUIRE(first_node.has_value());
    REQUIRE(second_node.has_value());
    REQUIRE(third_node.has_value());
    CHECK(first_node->glyph_blob_build_count == 1U);
    CHECK(second_node->glyph_blob_build_count == 1U);
    CHECK(third_node->glyph_blob_build_count == 1U);
    CHECK_FALSE(first_node->glyph_blob_cached);
    CHECK(second_node->glyph_blob_cached);
    CHECK(third_node->glyph_blob_cached);
    CHECK(second_node->glyph_blob_estimated_bytes * 2U <= detail::kGlyphBlobBudgetBytes);

    engine.RenderToCanvas(surface->getCanvas());
    first_node = engine.GetNodeForTesting(first);
    second_node = engine.GetNodeForTesting(second);
    third_node = engine.GetNodeForTesting(third);
    REQUIRE(first_node.has_value());
    REQUIRE(second_node.has_value());
    REQUIRE(third_node.has_value());
    CHECK(first_node->glyph_blob_build_count == 2U);
    CHECK(second_node->glyph_blob_build_count == 1U);
    CHECK(third_node->glyph_blob_build_count == 1U);
}

TEST_CASE("v2 command parsing stays aligned after ignored gradients and highlights", "[v2][unit]") {
    Engine engine;
    const std::uint64_t valid = Handle(100);
    const std::uint64_t stale = Handle(101);
    const std::uint64_t text = Handle(102);
    const std::uint64_t image = Handle(103);

    CommandBuilder builder;
    builder.CreateNode(valid);
    builder.CreateNode(stale);
    builder.CreateNode(text);
    builder.CreateNode(image);
    builder.DeleteNode(stale);
    builder.SetLinearGradient(stale, 0.0f, 0.0f, 10.0f, 10.0f, {
        GradientStop{0.0f, 0xff0000ffU},
        GradientStop{1.0f, 0x00ff00ffU},
    });
    builder.SetBounds(valid, 4.0f, 5.0f, 30.0f, 20.0f, false);
    builder.SetHighlights(stale, 0x5500ffffU, {
        Rect{1.0f, 2.0f, 3.0f, 4.0f},
        Rect{5.0f, 6.0f, 7.0f, 8.0f},
    });
    builder.SetTextFade(text, ED_FADE_BOTTOM);
    builder.SetImage(image, 12U, ED_OBJECT_FIT_FILL);

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));

    CHECK(stats.parsed_commands == 8U);
    CHECK(stats.ignored_commands == 2U);
    CHECK(stats.truncated_buffers == 0U);
    CHECK(stats.unknown_commands == 0U);

    const auto valid_node = engine.GetNodeForTesting(valid);
    REQUIRE(valid_node.has_value());
    CHECK(valid_node->visual_bounds.x == Approx(4.0f));
    CHECK(valid_node->visual_bounds.y == Approx(5.0f));
    CHECK(valid_node->visual_bounds.width == Approx(30.0f));
    CHECK(valid_node->visual_bounds.height == Approx(20.0f));

    const auto text_node = engine.GetNodeForTesting(text);
    REQUIRE(text_node.has_value());
    CHECK(text_node->fade_edge == ED_FADE_BOTTOM);

    const auto image_node = engine.GetNodeForTesting(image);
    REQUIRE(image_node.has_value());
    CHECK(image_node->has_image);
    CHECK(image_node->texture_id == 12U);
    CHECK(image_node->object_fit == ED_OBJECT_FIT_FILL);
}

TEST_CASE("v2 core preserves combined text fade masks", "[v2][unit][text]") {
    Engine engine;
    const std::uint64_t text = Handle(220);

    CommandBuilder builder;
    builder.CreateNode(text);
    builder.SetTextFade(text, ED_FADE_RIGHT | ED_FADE_BOTTOM);

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));

    CHECK(stats.parsed_commands == 2U);
    const auto text_node = engine.GetNodeForTesting(text);
    REQUIRE(text_node.has_value());
    CHECK(text_node->fade_edge == (ED_FADE_RIGHT | ED_FADE_BOTTOM));
}

TEST_CASE("v2 core parses colored highlight commands", "[v2][unit][text]") {
    Engine engine;
    engine.Init(64, 64, 1.0f);

    const std::uint64_t text = Handle(8800);
    const std::uint64_t stale = Handle(8801);
    CommandBuilder builder;
    builder.CreateNode(text);
    builder.SetHighlightsColored(text, {
        ColoredRect{Rect{1.0f, 2.0f, 10.0f, 12.0f}, 0xff112233U},
        ColoredRect{Rect{3.0f, 5.0f, 6.0f, 7.0f}, 0xff445566U},
    });
    builder.SetHighlightsColored(stale, {
        ColoredRect{Rect{0.0f, 0.0f, 1.0f, 1.0f}, 0xffffffffU},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 2U);
    CHECK(stats.ignored_commands == 1U);

    const auto node = engine.GetNodeForTesting(text);
    REQUIRE(node.has_value());
    CHECK(node->highlights.empty());
    REQUIRE(node->colored_highlights.size() == 2U);
    CHECK(node->colored_highlights[0].rect.x == Approx(1.0f));
    CHECK(node->colored_highlights[0].rect.width == Approx(10.0f));
    CHECK(node->colored_highlights[0].color == 0xff112233U);
    CHECK(node->colored_highlights[1].color == 0xff445566U);
}

TEST_CASE("v2 hit testing walks top-most paint order backwards", "[v2][unit]") {
    Engine engine;
    const std::uint64_t back = Handle(1);
    const std::uint64_t front = Handle(2);

    CommandBuilder builder;
    builder.CreateNode(back);
    builder.CreateNode(front);
    builder.SetBounds(back, 0.0f, 0.0f, 100.0f, 100.0f, 0.0f, 0.0f, 100.0f, 100.0f, true);
    builder.SetBounds(front, 10.0f, 10.0f, 100.0f, 100.0f, 40.0f, 40.0f, 20.0f, 20.0f, true);
    builder.CommitPaintOrder({back, front});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, back},
        SceneInstructionDebugView{OP_DRAW_NODE, front},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 6U);
    CHECK(engine.HitTest(15.0f, 15.0f) == back);
    CHECK(engine.HitTest(45.0f, 45.0f) == front);
    CHECK(engine.HitTest(5.0f, 5.0f) == back);
    CHECK(engine.HitTest(250.0f, 250.0f) == ED_INVALID_HANDLE);
}

TEST_CASE("v2 render paths cover object fit fades blends and scene stack edge cases", "[v2][unit]") {
    Engine engine;
    engine.Init(360, 280, 1.0f);

    const std::vector<std::uint8_t> font = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    engine.RegisterFont(7U, font.data(), static_cast<std::uint32_t>(font.size()));

    const std::vector<std::uint8_t> svg = MakeSvgBytes();
    engine.RegisterSvg(3U, svg.data(), static_cast<std::uint32_t>(svg.size()));

    const std::array<std::uint8_t, 4 * 2 * 4> wide_pixels = {
        255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 255, 0, 255,
    };
    const std::array<std::uint8_t, 2 * 4 * 4> tall_pixels = {
        255, 0, 255, 255, 255, 0, 255, 255,
        255, 128, 0, 255, 255, 128, 0, 255,
        0, 255, 255, 255, 0, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    engine.RegisterTextureRgba(1U, wide_pixels.data(), 4, 2, wide_pixels.size());
    engine.RegisterTextureRgba(2U, tall_pixels.data(), 2, 4, tall_pixels.size());

    const std::uint64_t image_fill = Handle(1001);
    const std::uint64_t image_none = Handle(1002);
    const std::uint64_t image_cover_wide = Handle(1003);
    const std::uint64_t image_cover_tall = Handle(1004);
    const std::uint64_t image_contain_wide = Handle(1005);
    const std::uint64_t image_contain_tall = Handle(1006);
    const std::uint64_t image_scale_down = Handle(1007);
    const std::uint64_t image_missing = Handle(1008);
    const std::uint64_t nine_node = Handle(1009);
    const std::uint64_t svg_tinted = Handle(1010);
    const std::uint64_t svg_plain = Handle(1011);
    const std::uint64_t svg_empty = Handle(1012);
    const std::uint64_t svg_missing = Handle(1013);
    const std::uint64_t path_node = Handle(1014);
    const std::uint64_t dotted_node = Handle(1015);
    const std::uint64_t dashed_node = Handle(1016);
    const std::uint64_t blend_screen = Handle(1017);
    const std::uint64_t blend_overlay = Handle(1018);
    const std::uint64_t blend_darken = Handle(1019);
    const std::uint64_t blend_lighten = Handle(1020);
    const std::uint64_t fade_left = Handle(1021);
    const std::uint64_t fade_top = Handle(1022);
    const std::uint64_t fade_bottom = Handle(1023);
    const std::uint64_t fade_empty = Handle(1024);
    const std::uint64_t caret_node = Handle(1025);
    const std::uint64_t clip_node = Handle(1026);
    const std::uint64_t layer_node = Handle(1027);
    const std::uint64_t fade_invalid = Handle(1028);
    const std::uint64_t missing_handle = Handle(60000);

    CommandBuilder builder;
    for (std::uint64_t handle : {
             image_fill, image_none, image_cover_wide, image_cover_tall, image_contain_wide, image_contain_tall,
             image_scale_down, image_missing, nine_node, svg_tinted, svg_plain, svg_empty, svg_missing, path_node,
             dotted_node, dashed_node, blend_screen, blend_overlay, blend_darken, blend_lighten, fade_left, fade_top,
             fade_bottom, fade_empty, caret_node, clip_node, layer_node, fade_invalid }) {
        builder.CreateNode(handle);
    }

    builder.SetBounds(image_fill, 8.0f, 8.0f, 40.0f, 20.0f, false);
    builder.SetImage(image_fill, 1U, ED_OBJECT_FIT_FILL);
    builder.SetBounds(image_none, 56.0f, 8.0f, 20.0f, 20.0f, false);
    builder.SetImage(image_none, 1U, ED_OBJECT_FIT_NONE);
    builder.SetBounds(image_cover_wide, 88.0f, 8.0f, 20.0f, 40.0f, false);
    builder.SetImage(image_cover_wide, 1U, ED_OBJECT_FIT_COVER);
    builder.SetBounds(image_cover_tall, 116.0f, 8.0f, 40.0f, 20.0f, false);
    builder.SetImage(image_cover_tall, 2U, ED_OBJECT_FIT_COVER);
    builder.SetBounds(image_contain_wide, 8.0f, 40.0f, 40.0f, 20.0f, false);
    builder.SetImage(image_contain_wide, 1U, ED_OBJECT_FIT_CONTAIN);
    builder.SetBounds(image_contain_tall, 56.0f, 40.0f, 20.0f, 40.0f, false);
    builder.SetImage(image_contain_tall, 2U, ED_OBJECT_FIT_CONTAIN);
    builder.SetBounds(image_scale_down, 88.0f, 56.0f, 80.0f, 80.0f, false);
    builder.SetImage(image_scale_down, 1U, ED_OBJECT_FIT_SCALE_DOWN);
    builder.SetBounds(image_missing, 176.0f, 8.0f, 30.0f, 30.0f, false);
    builder.SetImage(image_missing, 77U, ED_OBJECT_FIT_FILL);

    builder.SetBounds(nine_node, 216.0f, 8.0f, 60.0f, 50.0f, false);
    builder.SetImageNine(nine_node, 1U, 1.0f, 1.0f, 1.0f, 1.0f);

    builder.SetBounds(svg_tinted, 8.0f, 96.0f, 40.0f, 40.0f, false);
    builder.SetSvg(svg_tinted, 3U, 0xff00ffffU);
    builder.SetBounds(svg_plain, 56.0f, 96.0f, 40.0f, 40.0f, false);
    builder.SetSvg(svg_plain, 3U, 0U);
    builder.SetBounds(svg_empty, 104.0f, 96.0f, 0.0f, 0.0f, false);
    builder.SetSvg(svg_empty, 3U, 0U);
    builder.SetBounds(svg_missing, 120.0f, 96.0f, 40.0f, 40.0f, false);
    builder.SetSvg(svg_missing, 999U, 0U);

    PathVerbRecord move{};
    move.verb = ED_PATH_MOVE_TO;
    move.arg_count = 2;
    move.args = {180.0f, 120.0f, 0, 0, 0, 0};
    PathVerbRecord line{};
    line.verb = ED_PATH_LINE_TO;
    line.arg_count = 2;
    line.args = {200.0f, 90.0f, 0, 0, 0, 0};
    PathVerbRecord quad{};
    quad.verb = ED_PATH_QUAD_TO;
    quad.arg_count = 4;
    quad.args = {220.0f, 80.0f, 240.0f, 120.0f, 0, 0};
    PathVerbRecord cubic{};
    cubic.verb = ED_PATH_CUBIC_TO;
    cubic.arg_count = 6;
    cubic.args = {245.0f, 130.0f, 250.0f, 140.0f, 255.0f, 100.0f};
    PathVerbRecord unknown{};
    unknown.verb = 555U;
    unknown.arg_count = 0;
    PathVerbRecord close{};
    close.verb = ED_PATH_CLOSE;
    close.arg_count = 0;
    builder.SetBounds(path_node, 160.0f, 72.0f, 100.0f, 80.0f, false);
    builder.SetPath(path_node, 0x55ff00ffU, 0xff0000ffU, 2.0f, {move, line, quad, cubic, unknown, close});

    builder.SetBounds(dotted_node, 8.0f, 152.0f, 40.0f, 30.0f, false);
    builder.SetBoxStyle(dotted_node, 0x220000ffU, 4.0f, 4.0f, 4.0f, 4.0f, 2.0f, 0xffffffffU, ED_BORDER_DOTTED);
    builder.SetBounds(dashed_node, 56.0f, 152.0f, 40.0f, 30.0f, false);
    builder.SetBoxStyle(dashed_node, 0x220000ffU, 4.0f, 4.0f, 4.0f, 4.0f, 2.0f, 0xffffffffU, ED_BORDER_DASHED, 0.0f, 0.0f);

    builder.SetBounds(blend_screen, 104.0f, 152.0f, 24.0f, 24.0f, false);
    builder.SetBoxStyle(blend_screen, 0xff0000ffU, 2.0f, 2.0f, 2.0f, 2.0f);
    builder.SetLayerEffect(blend_screen, 0.5f, 0.0f, ED_BLEND_SCREEN);
    builder.SetBounds(blend_overlay, 136.0f, 152.0f, 24.0f, 24.0f, false);
    builder.SetBoxStyle(blend_overlay, 0x00ff00ffU, 2.0f, 2.0f, 2.0f, 2.0f);
    builder.SetLayerEffect(blend_overlay, 0.5f, 0.0f, ED_BLEND_OVERLAY);
    builder.SetBounds(blend_darken, 168.0f, 152.0f, 24.0f, 24.0f, false);
    builder.SetBoxStyle(blend_darken, 0x0000ffffU, 2.0f, 2.0f, 2.0f, 2.0f);
    builder.SetLayerEffect(blend_darken, 0.5f, 0.0f, ED_BLEND_DARKEN);
    builder.SetBounds(blend_lighten, 200.0f, 152.0f, 24.0f, 24.0f, false);
    builder.SetBoxStyle(blend_lighten, 0xffffffffU, 2.0f, 2.0f, 2.0f, 2.0f);
    builder.SetLayerEffect(blend_lighten, 0.5f, 0.0f, ED_BLEND_LIGHTEN);

    builder.SetBounds(fade_left, 8.0f, 200.0f, 50.0f, 24.0f, false);
    builder.SetGlyphRun(fade_left, 7U, 18.0f, 0xffffffffU, {GlyphPlacement{40U, 0.0f, 18.0f}});
    builder.SetTextFade(fade_left, ED_FADE_LEFT);
    builder.SetBounds(fade_top, 72.0f, 200.0f, 50.0f, 24.0f, false);
    builder.SetGlyphRun(fade_top, 7U, 18.0f, 0xffffffffU, {GlyphPlacement{40U, 0.0f, 18.0f}});
    builder.SetTextFade(fade_top, ED_FADE_TOP);
    builder.SetBounds(fade_bottom, 136.0f, 200.0f, 50.0f, 24.0f, false);
    builder.SetGlyphRun(fade_bottom, 7U, 18.0f, 0xffffffffU, {GlyphPlacement{40U, 0.0f, 18.0f}});
    builder.SetTextFade(fade_bottom, ED_FADE_BOTTOM);
    builder.SetBounds(fade_empty, 200.0f, 200.0f, 0.0f, 0.0f, false);
    builder.SetGlyphRun(fade_empty, 7U, 18.0f, 0xffffffffU, {GlyphPlacement{40U, 0.0f, 18.0f}});
    builder.SetTextFade(fade_empty, ED_FADE_LEFT);
    builder.SetBounds(fade_invalid, 232.0f, 200.0f, 50.0f, 24.0f, false);
    builder.SetGlyphRun(fade_invalid, 7U, 18.0f, 0xffffffffU, {GlyphPlacement{40U, 0.0f, 18.0f}});
    builder.SetTextFade(fade_invalid, 999U);

    builder.SetBounds(caret_node, 264.0f, 152.0f, 0.0f, 20.0f, false);
    builder.SetCaret(caret_node, 264.0f, 152.0f, 20.0f, 0xff00ffffU, 1U);

    builder.SetBounds(clip_node, 280.0f, 8.0f, 40.0f, 40.0f, false);
    builder.SetBoxStyle(clip_node, 0x440000ffU, 6.0f, 6.0f, 6.0f, 6.0f);
    builder.SetBounds(layer_node, 280.0f, 56.0f, 50.0f, 40.0f, false);
    builder.SetBoxStyle(layer_node, 0xffaa00ffU, 4.0f, 4.0f, 4.0f, 4.0f);
    builder.SetLayerEffect(layer_node, 0.5f, 1.0f, ED_BLEND_MULTIPLY);

    builder.CommitPaintOrder({image_fill, missing_handle, fade_left});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, image_fill},
        SceneInstructionDebugView{OP_DRAW_NODE, missing_handle},
        SceneInstructionDebugView{OP_DRAW_NODE, image_none},
        SceneInstructionDebugView{OP_DRAW_NODE, image_cover_wide},
        SceneInstructionDebugView{OP_DRAW_NODE, image_cover_tall},
        SceneInstructionDebugView{OP_DRAW_NODE, image_contain_wide},
        SceneInstructionDebugView{OP_DRAW_NODE, image_contain_tall},
        SceneInstructionDebugView{OP_DRAW_NODE, image_scale_down},
        SceneInstructionDebugView{OP_DRAW_NODE, image_missing},
        SceneInstructionDebugView{OP_DRAW_NODE, nine_node},
        SceneInstructionDebugView{OP_DRAW_NODE, svg_tinted},
        SceneInstructionDebugView{OP_DRAW_NODE, svg_plain},
        SceneInstructionDebugView{OP_DRAW_NODE, svg_empty},
        SceneInstructionDebugView{OP_DRAW_NODE, svg_missing},
        SceneInstructionDebugView{OP_DRAW_NODE, path_node},
        SceneInstructionDebugView{OP_DRAW_NODE, dotted_node},
        SceneInstructionDebugView{OP_DRAW_NODE, dashed_node},
        SceneInstructionDebugView{OP_DRAW_NODE, blend_screen},
        SceneInstructionDebugView{OP_DRAW_NODE, blend_overlay},
        SceneInstructionDebugView{OP_DRAW_NODE, blend_darken},
        SceneInstructionDebugView{OP_DRAW_NODE, blend_lighten},
        SceneInstructionDebugView{OP_DRAW_NODE, fade_left},
        SceneInstructionDebugView{OP_DRAW_NODE, fade_top},
        SceneInstructionDebugView{OP_DRAW_NODE, fade_bottom},
        SceneInstructionDebugView{OP_DRAW_NODE, fade_empty},
        SceneInstructionDebugView{OP_DRAW_NODE, fade_invalid},
        SceneInstructionDebugView{OP_DRAW_NODE, caret_node},
        SceneInstructionDebugView{OP_PUSH_CLIP, clip_node},
        SceneInstructionDebugView{OP_DRAW_NODE, svg_plain},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
        SceneInstructionDebugView{OP_PUSH_CLIP, missing_handle},
        SceneInstructionDebugView{OP_PUSH_LAYER, layer_node},
        SceneInstructionDebugView{OP_DRAW_NODE, layer_node},
        SceneInstructionDebugView{OP_PUSH_LAYER, missing_handle},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
        SceneInstructionDebugView{999U, ED_INVALID_HANDLE},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
        SceneInstructionDebugView{OP_PUSH_CLIP, clip_node},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.unknown_commands == 0U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(360, 280));
    REQUIRE(surface);
    engine.RenderToCanvas(nullptr);
    engine.RenderToCanvas(surface->getCanvas());
    CHECK(engine.HitTest(12.0f, 12.0f) == ED_INVALID_HANDLE);
    const std::vector<std::uint8_t> pixels = SnapshotRgba(surface, 360, 280);
    CHECK(HashBytes(pixels) != 0ULL);
}

TEST_CASE("v2 caret stays solid for 500ms then blinks every 500ms", "[v2][unit]") {
    Engine engine;
    engine.Init(24, 24, 1.0f);

    const std::uint64_t caret = Handle(2001);

    CommandBuilder builder;
    builder.CreateNode(caret);
    builder.SetBounds(caret, 0.0f, 0.0f, 20.0f, 20.0f, false);
    builder.SetCaret(caret, 5.0f, 5.0f, 10.0f, 0x00ff00ffU, 1000U);
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, caret},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(24, 24));
    REQUIRE(surface);

    engine.RenderToCanvas(surface->getCanvas(), 1200.0);
    const auto solid_pixels = SnapshotRgba(surface, 24, 24);
    const auto solid_pixel = PixelAt(solid_pixels, 24, 5, 5);
    CHECK(solid_pixel[1] > 200U);
    CHECK(solid_pixel[3] > 200U);

    engine.RenderToCanvas(surface->getCanvas(), 1600.0);
    const auto hidden_pixels = SnapshotRgba(surface, 24, 24);
    const auto hidden_pixel = PixelAt(hidden_pixels, 24, 5, 5);
    CHECK(hidden_pixel[3] == 0U);

    engine.RenderToCanvas(surface->getCanvas(), 2100.0);
    const auto blinking_pixels = SnapshotRgba(surface, 24, 24);
    const auto blinking_pixel = PixelAt(blinking_pixels, 24, 5, 5);
    CHECK(blinking_pixel[1] > 200U);
    CHECK(blinking_pixel[3] > 200U);

    engine.RenderToCanvas(surface->getCanvas(), 0.0);
    const auto default_pixels = SnapshotRgba(surface, 24, 24);
    const auto default_pixel = PixelAt(default_pixels, 24, 5, 5);
    CHECK(default_pixel[1] > 200U);
    CHECK(default_pixel[3] > 200U);
}

TEST_CASE("v2 caret blink timestamps follow the 200 700 1200 ms contract", "[v2][unit]") {
    Engine engine;
    engine.Init(24, 24, 1.0f);

    const std::uint64_t caret = Handle(2002);
    CommandBuilder builder;
    builder.CreateNode(caret);
    builder.SetBounds(caret, 0.0f, 0.0f, 20.0f, 20.0f, false);
    builder.SetCaret(caret, 5.0f, 5.0f, 10.0f, 0x00ff00ffU, 0U);
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, caret},
    });
    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(24, 24));
    REQUIRE(surface);

    engine.RenderToCanvas(surface->getCanvas(), 200.0);
    CHECK(PixelAt(SnapshotRgba(surface, 24, 24), 24, 5, 5)[3] > 200U);

    engine.RenderToCanvas(surface->getCanvas(), 700.0);
    CHECK(PixelAt(SnapshotRgba(surface, 24, 24), 24, 5, 5)[3] == 0U);

    engine.RenderToCanvas(surface->getCanvas(), 1200.0);
    CHECK(PixelAt(SnapshotRgba(surface, 24, 24), 24, 5, 5)[3] > 200U);
}

TEST_CASE("v2 commit scene preserves strict instruction stride", "[v2][unit]") {
    Engine engine;
    const std::uint64_t clip = Handle(11);
    const std::uint64_t content = Handle(12);
    const std::uint64_t layer = Handle(13);

    CommandBuilder builder;
    builder.CreateNode(clip);
    builder.CreateNode(content);
    builder.CreateNode(layer);
    builder.SetBounds(clip, 0.0f, 0.0f, 200.0f, 200.0f, false);
    builder.SetBounds(content, 20.0f, 20.0f, 40.0f, 40.0f, 20.0f, 20.0f, 40.0f, 40.0f, true);
    builder.SetBounds(layer, 0.0f, 0.0f, 200.0f, 200.0f, false);
    builder.CommitPaintOrder({content});
    builder.CommitScene({
        SceneInstructionDebugView{OP_PUSH_CLIP, clip},
        SceneInstructionDebugView{OP_DRAW_NODE, content},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
        SceneInstructionDebugView{OP_PUSH_LAYER, layer},
        SceneInstructionDebugView{OP_DRAW_NODE, content},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 8U);

    const auto instructions = engine.GetSceneInstructionsForTesting();
    REQUIRE(instructions.size() == 6U);
    CHECK(instructions[0].opcode == OP_PUSH_CLIP);
    CHECK(instructions[1].opcode == OP_DRAW_NODE);
    CHECK(instructions[2].opcode == OP_POP);
    CHECK(instructions[3].opcode == OP_PUSH_LAYER);
    CHECK(instructions[4].opcode == OP_DRAW_NODE);
    CHECK(instructions[5].opcode == OP_POP);

    const auto paint_order = engine.GetPaintOrderForTesting();
    REQUIRE(paint_order.size() == 1U);
    CHECK(paint_order[0] == content);
}

TEST_CASE("v2 push clip uses clip bounds instead of outer visual bounds", "[v2][unit]") {
    Engine engine;
    engine.Init(48, 48, 1.0f);

    const std::uint64_t clip = Handle(14);
    const std::uint64_t content = Handle(15);

    CommandBuilder builder;
    builder.CreateNode(clip);
    builder.CreateNode(content);
    builder.SetBounds(
        clip,
        0.0f,
        0.0f,
        48.0f,
        48.0f,
        0.0f,
        0.0f,
        48.0f,
        48.0f,
        12.0f,
        10.0f,
        24.0f,
        20.0f,
        false);
    builder.SetBounds(content, 0.0f, 0.0f, 48.0f, 48.0f, false);
    builder.SetBoxStyle(content, 0xff0000ffU, 0.0f, 0.0f, 0.0f, 0.0f);
    builder.CommitPaintOrder({content});
    builder.CommitScene({
        SceneInstructionDebugView{OP_PUSH_CLIP, clip},
        SceneInstructionDebugView{OP_DRAW_NODE, content},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 7U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(48, 48));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    const auto pixels = SnapshotRgba(surface, 48, 48);
    CHECK(PixelAt(pixels, 48, 6, 6)[3] == 0U);
    CHECK(PixelAt(pixels, 48, 16, 16)[0] > 200U);
    CHECK(PixelAt(pixels, 48, 16, 16)[3] > 200U);
    CHECK(PixelAt(pixels, 48, 40, 40)[3] == 0U);
}

TEST_CASE("v2 rounded box borders stay visible on the top and bottom centerlines without spilling outside bounds", "[v2][unit]") {
    Engine engine;
    engine.Init(24, 24, 1.0f);

    const std::uint64_t bordered = Handle(3100);

    CommandBuilder builder;
    builder.CreateNode(bordered);
    builder.SetBounds(bordered, 4.0f, 4.0f, 16.0f, 16.0f, false);
    builder.SetBoxStyle(bordered, 0x00000000U, 6.0f, 6.0f, 6.0f, 6.0f, 1.0f, 0x00ff00ffU, ED_BORDER_SOLID, 0.0f, 0.0f);
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, bordered},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(24, 24));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    const auto pixels = SnapshotRgba(surface, 24, 24);
    const auto top_center = PixelAt(pixels, 24, 12, 4);
    const auto top_inside = PixelAt(pixels, 24, 12, 5);
    const auto bottom_inside = PixelAt(pixels, 24, 12, 19);
    const auto below_bottom = PixelAt(pixels, 24, 12, 20);
    const auto below_bounds = PixelAt(pixels, 24, 12, 21);
    CHECK(top_center[1] > 120U);
    CHECK(top_center[3] > 120U);
    CHECK(top_inside[3] < 32U);
    CHECK(bottom_inside[1] > 120U);
    CHECK(bottom_inside[3] > 120U);
    CHECK(below_bottom[3] < 32U);
    CHECK(below_bounds[3] < 32U);
}

TEST_CASE("v2 rounded box borders stay visible when bounds land on fractional pixels", "[v2][unit]") {
    Engine engine;
    engine.Init(24, 24, 1.0f);

    const std::uint64_t bordered = Handle(3101);

    CommandBuilder builder;
    builder.CreateNode(bordered);
    builder.SetBounds(bordered, 4.25f, 4.25f, 15.5f, 15.5f, false);
    builder.SetBoxStyle(bordered, 0x00000000U, 6.0f, 6.0f, 6.0f, 6.0f, 1.0f, 0x00ff00ffU, ED_BORDER_SOLID, 0.0f, 0.0f);
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, bordered},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(24, 24));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    const auto pixels = SnapshotRgba(surface, 24, 24);
    const auto top_band = MaxPixelInBox(pixels, 24, 10, 4, 13, 6);
    const auto bottom_band = MaxPixelInBox(pixels, 24, 10, 17, 13, 19);
    const auto above_band = MaxPixelInBox(pixels, 24, 10, 2, 13, 3);
    const auto below_band = MaxPixelInBox(pixels, 24, 10, 20, 13, 21);
    CHECK(top_band[1] > 120U);
    CHECK(top_band[3] > 120U);
    CHECK(bottom_band[1] > 120U);
    CHECK(bottom_band[3] > 120U);
    CHECK(above_band[3] < 32U);
    CHECK(below_band[3] < 32U);
}

TEST_CASE("v2 large rounded solid borders keep their bottom center segment visible", "[v2][unit]") {
    Engine engine;
    engine.Init(280, 220, 1.0f);

    const std::uint64_t bordered = Handle(3102);

    CommandBuilder builder;
    builder.CreateNode(bordered);
    builder.SetBounds(bordered, 20.0f, 20.0f, 220.0f, 160.0f, false);
    builder.SetBoxStyle(bordered, 0x00000000U, 18.0f, 18.0f, 18.0f, 18.0f, 1.0f, 0xff6a00ffU, ED_BORDER_SOLID, 0.0f, 0.0f);
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, bordered},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 4U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(280, 220));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    const auto pixels = SnapshotRgba(surface, 280, 220);
    const auto top_band = MaxPixelInBox(pixels, 280, 120, 20, 139, 24);
    const auto bottom_band = MaxPixelInBox(pixels, 280, 120, 176, 139, 179);
    const auto above_band = MaxPixelInBox(pixels, 280, 120, 16, 139, 18);
    const auto below_band = MaxPixelInBox(pixels, 280, 120, 181, 139, 184);
    CHECK(top_band[0] > 160U);
    CHECK(top_band[3] > 120U);
    CHECK(bottom_band[0] > 160U);
    CHECK(bottom_band[3] > 120U);
    CHECK(above_band[3] < 32U);
    CHECK(below_band[3] < 32U);
}

TEST_CASE("v2 ancestor clips preserve child bottom borders at dpr 2", "[v2][unit]") {
    Engine engine;
    engine.Init(144, 96, 2.0f);

    const std::uint64_t clipped_parent = Handle(3111);
    const std::uint64_t clipped_child = Handle(3112);
    const std::uint64_t unclipped_child = Handle(3113);

    CommandBuilder builder;
    builder.CreateNode(clipped_parent);
    builder.CreateNode(clipped_child);
    builder.CreateNode(unclipped_child);
    builder.SetBounds(clipped_parent, 4.0f, 4.0f, 14.0f, 12.0f, false);
    builder.SetBounds(clipped_child, 6.0f, 8.0f, 10.0f, 8.0f, false);
    builder.SetBounds(unclipped_child, 24.0f, 8.0f, 10.0f, 8.0f, false);
    builder.SetBoxStyle(clipped_child, 0x00000000U, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xff00ffffU, ED_BORDER_SOLID, 0.0f, 0.0f);
    builder.SetBoxStyle(unclipped_child, 0x00000000U, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xff00ffffU, ED_BORDER_SOLID, 0.0f, 0.0f);
    builder.CommitScene({
        SceneInstructionDebugView{OP_PUSH_CLIP, clipped_parent},
        SceneInstructionDebugView{OP_DRAW_NODE, clipped_child},
        SceneInstructionDebugView{OP_POP, ED_INVALID_HANDLE},
        SceneInstructionDebugView{OP_DRAW_NODE, unclipped_child},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 9U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(144, 96));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    const auto pixels = SnapshotRgba(surface, 144, 96);
    const auto clipped_bottom = MaxPixelInBox(pixels, 144, 16, 30, 31, 31);
    const auto unclipped_bottom = MaxPixelInBox(pixels, 144, 52, 30, 67, 31);
    CHECK(clipped_bottom[0] > 160U);
    CHECK(clipped_bottom[3] > 120U);
    CHECK(unclipped_bottom[0] > 160U);
    CHECK(unclipped_bottom[3] > 120U);
    CHECK(static_cast<int>(clipped_bottom[3]) >= static_cast<int>(unclipped_bottom[3]) - 32);
}

TEST_CASE("v2 ignores out-of-bounds and malformed input without crashing", "[v2][unit]") {
    Engine engine;
    const std::uint64_t valid = Handle(5);
    const std::uint64_t invalid = Handle(9999999U);

    CommandBuilder builder;
    builder.CreateNode(valid);
    builder.SetBounds(valid, 0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.0f, 32.0f, 32.0f, true);
    builder.SetBounds(invalid, 1.0f, 1.0f, 4.0f, 4.0f, 1.0f, 1.0f, 4.0f, 4.0f, true);
    builder.CommitPaintOrder({valid, invalid});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, valid},
    });
    builder.PushRaw(CMD_SET_BOX_STYLE);
    builder.PushRaw(5U);

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));

    CHECK(stats.parsed_commands == 4U);
    CHECK(stats.ignored_commands == 1U);
    CHECK(stats.truncated_buffers == 1U);
    CHECK(engine.HitTest(8.0f, 8.0f) == valid);
}

TEST_CASE("v2 stores svg and nine-patch media state", "[v2][unit]") {
    Engine engine;
    const std::uint64_t svg_handle = Handle(31);
    const std::uint64_t image_handle = Handle(32);

    const std::vector<std::uint8_t> svg = MakeSvgBytes();
    engine.RegisterSvg(17U, svg.data(), static_cast<std::uint32_t>(svg.size()));

    CommandBuilder builder;
    builder.CreateNode(svg_handle);
    builder.CreateNode(image_handle);
    builder.SetBounds(svg_handle, 16.0f, 20.0f, 96.0f, 96.0f, false);
    builder.SetSvg(svg_handle, 17U, 0xff66ccffU);
    builder.SetBounds(image_handle, 132.0f, 20.0f, 120.0f, 96.0f, false);
    builder.SetImageNine(image_handle, 9U, 2.0f, 2.0f, 2.0f, 2.0f);

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));

    CHECK(stats.parsed_commands == 6U);

    const auto svg_node = engine.GetNodeForTesting(svg_handle);
    REQUIRE(svg_node.has_value());
    CHECK(svg_node->has_svg);
    CHECK(svg_node->svg_id == 17U);
    CHECK(svg_node->svg_tint_color == 0xff66ccffU);

    const auto image_node = engine.GetNodeForTesting(image_handle);
    REQUIRE(image_node.has_value());
    CHECK(image_node->has_image_nine);
    CHECK(image_node->image_nine_texture_id == 9U);
    CHECK(image_node->image_nine_insets.left == Approx(2.0f));
    CHECK(image_node->image_nine_insets.bottom == Approx(2.0f));
}

TEST_CASE("v2 renders a golden styled scene snapshot", "[v2][snapshot]") {
    Engine engine;
    engine.Init(320, 220, 1.0f);

    const std::uint64_t box = Handle(101);
    const std::uint64_t path = Handle(102);
    const std::uint64_t text = Handle(103);

    const std::vector<std::uint8_t> font = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    engine.RegisterFont(7U, font.data(), static_cast<std::uint32_t>(font.size()));

    CommandBuilder builder;
    builder.CreateNode(box);
    builder.CreateNode(path);
    builder.CreateNode(text);
    builder.SetBounds(box, 24.0f, 24.0f, 272.0f, 172.0f, false);
    builder.SetBoxStyle(box, 0x203040ffU, 18.0f, 18.0f, 18.0f, 18.0f, 4.0f, 0xf0c040ffU, ED_BORDER_DASHED, 10.0f, 5.0f);
    builder.SetLinearGradient(box, 24.0f, 24.0f, 296.0f, 196.0f, {
        GradientStop{0.0f, 0x5eead4ffU},
        GradientStop{1.0f, 0x2563ebffU},
    });
    builder.SetLayerEffect(box, 0.9f, 1.25f, ED_BLEND_SRC_OVER);

    PathVerbRecord move{};
    move.verb = ED_PATH_MOVE_TO;
    move.arg_count = 2;
    move.args = {60.0f, 150.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    PathVerbRecord line_one{};
    line_one.verb = ED_PATH_LINE_TO;
    line_one.arg_count = 2;
    line_one.args = {120.0f, 70.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    PathVerbRecord line_two{};
    line_two.verb = ED_PATH_LINE_TO;
    line_two.arg_count = 2;
    line_two.args = {180.0f, 150.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    PathVerbRecord close{};
    close.verb = ED_PATH_CLOSE;
    close.arg_count = 0;
    builder.SetBounds(path, 0.0f, 0.0f, 320.0f, 220.0f, false);
    builder.SetPath(path, 0xff7f50ccU, 0xffffffffU, 3.0f, {move, line_one, line_two, close});

    builder.SetBounds(text, 56.0f, 66.0f, 180.0f, 48.0f, false);
    builder.SetHighlights(text, 0xffffffffU, {
        Rect{56.0f, 72.0f, 92.0f, 24.0f},
    });
    builder.SetGlyphRun(text, 7U, 30.0f, 0x111827ffU, {
        GlyphPlacement{40U, 0.0f, 28.0f},
        GlyphPlacement{69U, 22.0f, 28.0f},
        GlyphPlacement{70U, 40.0f, 28.0f},
        GlyphPlacement{70U, 58.0f, 28.0f},
        GlyphPlacement{76U, 76.0f, 28.0f},
        GlyphPlacement{81U, 88.0f, 28.0f},
    });
    builder.SetTextFade(text, ED_FADE_RIGHT);
    builder.SetCaret(text, 152.0f, 72.0f, 28.0f, 0xdc2626ffU, 1234U);
    builder.CommitPaintOrder({});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, box},
        SceneInstructionDebugView{OP_DRAW_NODE, path},
        SceneInstructionDebugView{OP_DRAW_NODE, text},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 16U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, 220));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    CheckSnapshotAgainstGolden(
        SnapshotRgba(surface, 320, 220),
        320,
        220,
        StyledSceneGoldenPath(),
        0.94);
}

TEST_CASE("v2 renders a golden background blur snapshot", "[v2][snapshot]") {
    Engine engine;
    engine.Init(220, 180, 1.0f);

    const std::uint64_t stripes[6] = {
        Handle(401),
        Handle(402),
        Handle(403),
        Handle(404),
        Handle(405),
        Handle(406),
    };
    const std::uint64_t overlay = Handle(407);

    CommandBuilder builder;
    for (std::uint64_t stripe : stripes) {
        builder.CreateNode(stripe);
    }
    builder.CreateNode(overlay);

    const std::uint32_t stripe_colors[6] = {
        0xff4f46e5U,
        0x22c55effU,
        0xf59e0bffU,
        0xef4444ffU,
        0x06b6d4ffU,
        0xa855f7ffU,
    };
    for (std::size_t index = 0; index < 6U; index += 1U) {
        builder.SetBounds(stripes[index], static_cast<float>(index) * 36.0f, 0.0f, 40.0f, 180.0f, false);
        builder.SetBoxStyle(stripes[index], stripe_colors[index], 0.0f, 0.0f, 0.0f, 0.0f);
    }

    builder.SetBounds(overlay, 38.0f, 36.0f, 144.0f, 108.0f, false);
    builder.SetBoxStyle(overlay, 0x000000b8U, 0.0f, 0.0f, 0.0f, 0.0f);
    builder.SetBackgroundBlur(overlay, 12.0f);
    builder.CommitPaintOrder({});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, stripes[0]},
        SceneInstructionDebugView{OP_DRAW_NODE, stripes[1]},
        SceneInstructionDebugView{OP_DRAW_NODE, stripes[2]},
        SceneInstructionDebugView{OP_DRAW_NODE, stripes[3]},
        SceneInstructionDebugView{OP_DRAW_NODE, stripes[4]},
        SceneInstructionDebugView{OP_DRAW_NODE, stripes[5]},
        SceneInstructionDebugView{OP_DRAW_NODE, overlay},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 24U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(220, 180));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    CheckSnapshotAgainstGolden(
        SnapshotRgba(surface, 220, 180),
        220,
        180,
        GoldensDir() / "background-blur-scene.png",
        0.94);
}

TEST_CASE("v2 renders images with object-fit into a golden snapshot", "[v2][snapshot]") {
    Engine engine;
    engine.Init(220, 180, 1.0f);

    const std::uint64_t image = Handle(201);
    const std::uint64_t frame = Handle(202);

    const std::array<std::uint8_t, 4 * 4 * 4> pixels = {
        255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 255, 0, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 255, 0, 255,
    };
    engine.RegisterTextureRgba(9U, pixels.data(), 4, 4, pixels.size());

    CommandBuilder builder;
    builder.CreateNode(frame);
    builder.CreateNode(image);
    builder.SetBounds(frame, 16.0f, 16.0f, 188.0f, 148.0f, false);
    builder.SetBoxStyle(frame, 0x0f172affU, 12.0f, 12.0f, 12.0f, 12.0f);
    builder.SetBounds(image, 28.0f, 28.0f, 164.0f, 124.0f, false);
    builder.SetImage(image, 9U, ED_OBJECT_FIT_CONTAIN);
    builder.CommitPaintOrder({});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, frame},
        SceneInstructionDebugView{OP_DRAW_NODE, image},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 8U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(220, 180));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    CheckSnapshotAgainstGolden(
        SnapshotRgba(surface, 220, 180),
        220,
        180,
        GoldensDir() / "image-scene.png",
        0.999);
}

TEST_CASE("v2 unregisters textures from the image registry", "[v2][unit]") {
    Engine engine;
    engine.Init(8, 8, 1.0f);

    const std::uint64_t image = Handle(220);
    const std::array<std::uint8_t, 4> pixels = {0, 255, 0, 255};
    engine.RegisterTextureRgba(9U, pixels.data(), 1U, 1U, pixels.size());

    CommandBuilder builder;
    builder.CreateNode(image);
    builder.SetBounds(image, 0.0f, 0.0f, 8.0f, 8.0f, false);
    builder.SetImage(image, 9U, ED_OBJECT_FIT_FILL);
    builder.CommitPaintOrder({});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, image},
    });

    const auto stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 5U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(8, 8));
    REQUIRE(surface);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    engine.RenderToCanvas(surface->getCanvas());
    const auto before = SnapshotRgba(surface, 8, 8);
    CHECK(PixelAt(before, 8, 4, 4) == std::array<std::uint8_t, 4>{0, 255, 0, 255});

    engine.UnregisterTexture(9U);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    engine.RenderToCanvas(surface->getCanvas());
    const auto after = SnapshotRgba(surface, 8, 8);
    CHECK(PixelAt(after, 8, 4, 4) == std::array<std::uint8_t, 4>{0, 0, 0, 0});
}

TEST_CASE("v2 renders svg and nine-patch media into a golden snapshot", "[v2][snapshot]") {
    Engine engine;
    engine.Init(260, 180, 1.0f);

    const std::vector<std::uint8_t> svg = MakeSvgBytes();
    engine.RegisterSvg(17U, svg.data(), static_cast<std::uint32_t>(svg.size()));

    const std::vector<std::uint8_t> pixels = MakeNinePatchPixels();
    engine.RegisterTextureRgba(21U, pixels.data(), 6, 6, pixels.size());

    const std::uint64_t svg_node = Handle(301);
    const std::uint64_t nine_node = Handle(302);

    CommandBuilder builder;
    builder.CreateNode(svg_node);
    builder.CreateNode(nine_node);
    builder.SetBounds(svg_node, 24.0f, 26.0f, 92.0f, 92.0f, false);
    builder.SetSvg(svg_node, 17U, 0xff6b00ffU);
    builder.SetBounds(nine_node, 132.0f, 24.0f, 100.0f, 112.0f, false);
    builder.SetImageNine(nine_node, 21U, 2.0f, 2.0f, 2.0f, 2.0f);
    builder.CommitPaintOrder({});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, svg_node},
        SceneInstructionDebugView{OP_DRAW_NODE, nine_node},
    });

    const CommandBufferStats stats = engine.ExecuteCommandBuffer(
        builder.words().data(),
        static_cast<std::uint32_t>(builder.words().size()));
    CHECK(stats.parsed_commands == 8U);

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(260, 180));
    REQUIRE(surface);
    engine.RenderToCanvas(surface->getCanvas());

    CheckSnapshotAgainstGolden(
        SnapshotRgba(surface, 260, 180),
        260,
        180,
        GoldensDir() / "svg-nine-scene.png",
        0.999);
}
