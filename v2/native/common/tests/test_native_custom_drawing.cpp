#include <catch2/catch_test_macros.hpp>

#include "CommandBuilder.h"
#include "NativeFuiRuntimeBridge.h"
#include "NativeFuiBridge.h"
#include "NativeHost.h"
#include "NativeHostCore.h"
#include "effindom_ui.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using effindom::v2::SceneInstructionDebugView;
using effindom::v2::native::NativeHostCore;
using effindom::v2::native::NativeHostCoreCallbacks;
using effindom::v2::native::NativeInputRouterOptions;
using effindom::v2::test::CommandBuilder;

namespace {

std::uint32_t FloatWord(float value) {
    std::uint32_t word = 0U;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

std::vector<std::uint8_t> SnapshotRgba(const sk_sp<SkSurface>& surface, int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4));
    const SkImageInfo info = SkImageInfo::Make(
        width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    REQUIRE(surface->readPixels(info, pixels.data(), static_cast<std::size_t>(width * 4), 0, 0));
    return pixels;
}

bool HasAlpha(const std::vector<std::uint8_t>& pixels, int width, int x, int y) {
    return pixels[static_cast<std::size_t>((y * width + x) * 4 + 3)] != 0U;
}

std::array<std::uint8_t, 4> Pixel(const std::vector<std::uint8_t>& pixels, int width, int x, int y) {
    const std::size_t offset = static_cast<std::size_t>((y * width + x) * 4);
    return {pixels[offset], pixels[offset + 1U], pixels[offset + 2U], pixels[offset + 3U]};
}

std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("native host installs the FUI custom draw strategy", "[v2][native][custom-draw]") {
    constexpr std::uint64_t expected_handle = (13ULL << 32U) | 11ULL;
    std::uint64_t actual_handle = 0U;
    std::uintptr_t actual_canvas = 0U;
    NativeHostCore host(
        NativeInputRouterOptions{},
        NativeHostCoreCallbacks{
            {},
            {},
            [&](std::uint64_t handle, std::uintptr_t canvas) {
                actual_handle = handle;
                actual_canvas = canvas;
            },
        });

    CommandBuilder builder;
    builder.CommitScene({SceneInstructionDebugView{OP_DRAW_CUSTOM, expected_handle}});
    host.GetEngine().ExecuteCommandBuffer(
        builder.words().data(), static_cast<std::uint32_t>(builder.words().size()));
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(16, 16));
    REQUIRE(surface);

    host.GetEngine().RenderToCanvas(surface->getCanvas());

    CHECK(actual_handle == expected_handle);
    CHECK(actual_canvas == reinterpret_cast<std::uintptr_t>(surface->getCanvas()));
}

TEST_CASE("native immediate drawing delegates one validated batch to the engine", "[v2][native][custom-draw]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(32, 32));
    REQUIRE(surface);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    constexpr std::uint32_t red = 0xFF0000FFU;
    constexpr std::uint32_t blue = 0x0000FFFFU;
    const std::vector<std::uint32_t> words = {
        1U,
        3U, FloatWord(8.0f), FloatWord(8.0f),
        6U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(8.0f), FloatWord(8.0f),
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(16.0f), FloatWord(16.0f),
        red, 0U, FloatWord(0.0f),
        2U,
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(4.0f), FloatWord(4.0f),
        blue, 0U, FloatWord(0.0f),
    };

    CHECK(effindom::v2::native::DrawCanvasBatch(
        host,
        reinterpret_cast<std::uintptr_t>(canvas),
        reinterpret_cast<std::uintptr_t>(words.data()),
        static_cast<std::uint32_t>(words.size())));

    const auto pixels = SnapshotRgba(surface, 32, 32);
    CHECK(HasAlpha(pixels, 32, 2, 2));
    CHECK(HasAlpha(pixels, 32, 10, 10));
    CHECK_FALSE(HasAlpha(pixels, 32, 18, 18));
}

TEST_CASE("native immediate drawing renders retained rich text directly and into bitmap pixels",
    "[v2][native][custom-draw][text]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    const auto font_bytes = ReadFileBytes(
        std::filesystem::path(EFFINDOM_TEST_SOURCE_ROOT) / "v2/fonts/DejaVuSans.ttf");
    REQUIRE_FALSE(font_bytes.empty());
    host.GetEngine().RegisterFont(
        7U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    constexpr std::uint64_t text_handle = (17ULL << 32U) | 41ULL;
    CommandBuilder builder;
    builder.CreateNode(text_handle);
    builder.SetBounds(text_handle, 0.0f, 0.0f, 96.0f, 40.0f, false);
    builder.SetGlyphRunColored(text_handle, 7U, 24.0f, {
        effindom::v2::GlyphPlacement{36U, 0.0f, 24.0f, 7U, 0xff0000ffU},
        effindom::v2::GlyphPlacement{37U, 18.0f, 24.0f, 7U, 0x00ff00ffU},
    });
    REQUIRE(host.GetEngine().ExecuteCommandBuffer(
        builder.words().data(), static_cast<std::uint32_t>(builder.words().size()))
        .parsed_commands == 3U);

    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(128, 64));
    REQUIRE(surface);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    const std::vector<std::uint32_t> words = {
        30U,
        static_cast<std::uint32_t>(text_handle),
        static_cast<std::uint32_t>(text_handle >> 32U),
        FloatWord(12.0f),
        FloatWord(8.0f),
    };
    REQUIRE(effindom::v2::native::DrawCanvasBatch(
        host,
        reinterpret_cast<std::uintptr_t>(surface->getCanvas()),
        reinterpret_cast<std::uintptr_t>(words.data()),
        static_cast<std::uint32_t>(words.size())));

    const auto direct_pixels = SnapshotRgba(surface, 128, 64);
    CHECK(std::count_if(direct_pixels.begin() + 3, direct_pixels.end(),
              [index = std::size_t{3}](std::uint8_t alpha) mutable {
                  const bool selected = index % 4U == 3U && alpha != 0U;
                  ++index;
                  return selected;
              }) > 20);

    std::vector<std::uint8_t> bitmap_pixels(128U * 64U * 4U, 0U);
    REQUIRE(effindom::v2::native::RenderNodeToRgba(
        host,
        text_handle,
        128U,
        64U,
        reinterpret_cast<std::uintptr_t>(bitmap_pixels.data()),
        static_cast<std::uint32_t>(bitmap_pixels.size()),
        1.0f,
        12.0f,
        8.0f) == bitmap_pixels.size());
    CHECK(std::count_if(bitmap_pixels.begin() + 3, bitmap_pixels.end(),
              [index = std::size_t{3}](std::uint8_t alpha) mutable {
                  const bool selected = index % 4U == 3U && alpha != 0U;
                  ++index;
                  return selected;
              }) > 20);
}

TEST_CASE("native immediate drawing rejects invalid boundaries and malformed streams atomically", "[v2][native][custom-draw]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(16, 16));
    REQUIRE(surface);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    constexpr std::uint32_t red = 0xFF0000FFU;
    const std::array<std::uint32_t, 10> malformed = {
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(8.0f), FloatWord(8.0f),
        red, 0U, FloatWord(0.0f),
        10U, FloatWord(0.0f),
    };
    const auto canvas_pointer = reinterpret_cast<std::uintptr_t>(canvas);
    const auto words_pointer = reinterpret_cast<std::uintptr_t>(malformed.data());

    CHECK(effindom::v2::native::DrawCanvasBatch(host, 0U, 0U, 0U));
    CHECK_FALSE(effindom::v2::native::DrawCanvasBatch(host, 0U, words_pointer, 1U));
    CHECK_FALSE(effindom::v2::native::DrawCanvasBatch(host, canvas_pointer, 0U, 1U));
    CHECK_FALSE(effindom::v2::native::DrawCanvasBatch(host, canvas_pointer, words_pointer + 1U, 1U));
    CHECK_FALSE(effindom::v2::native::DrawCanvasBatch(
        host, canvas_pointer, words_pointer, static_cast<std::uint32_t>(malformed.size())));

    const auto pixels = SnapshotRgba(surface, 16, 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            CHECK_FALSE(HasAlpha(pixels, 16, x, y));
        }
    }
}

TEST_CASE("native paths forward every verb and release engine resources", "[v2][native][custom-draw][path]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    CHECK(host.GetEngine().PathCountForTesting() == 0U);
    const std::uint32_t path_id = effindom::v2::native::CreatePath(host);
    REQUIRE(path_id != 0U);
    CHECK(host.GetEngine().PathCountForTesting() == 1U);

    CHECK(effindom::v2::native::PathMoveTo(host, path_id, 2.0f, 20.0f));
    CHECK(effindom::v2::native::PathLineTo(host, path_id, 8.0f, 2.0f));
    CHECK(effindom::v2::native::PathQuadTo(host, path_id, 16.0f, 0.0f, 20.0f, 8.0f));
    CHECK(effindom::v2::native::PathCubicTo(host, path_id, 28.0f, 8.0f, 30.0f, 18.0f, 20.0f, 24.0f));
    CHECK(effindom::v2::native::PathClose(host, path_id));

    const std::uint32_t shapes_id = effindom::v2::native::CreatePath(host);
    REQUIRE(shapes_id != 0U);
    CHECK(effindom::v2::native::PathAddRect(host, shapes_id, 34.0f, 2.0f, 12.0f, 12.0f));
    CHECK(effindom::v2::native::PathAddCircle(host, shapes_id, 42.0f, 24.0f, 6.0f));

    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(52, 32));
    REQUIRE(surface);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    constexpr std::uint32_t green = 0x00FF00FFU;
    const std::vector<std::uint32_t> words = {
        20U, path_id, green, 0U, FloatWord(0.0f),
        20U, shapes_id, green, 0U, FloatWord(0.0f),
    };
    REQUIRE(effindom::v2::native::DrawCanvasBatch(
        host,
        reinterpret_cast<std::uintptr_t>(canvas),
        reinterpret_cast<std::uintptr_t>(words.data()),
        static_cast<std::uint32_t>(words.size())));

    const auto pixels = SnapshotRgba(surface, 52, 32);
    CHECK(HasAlpha(pixels, 52, 12, 10));
    CHECK(HasAlpha(pixels, 52, 38, 8));
    CHECK(HasAlpha(pixels, 52, 42, 24));

    CHECK(effindom::v2::native::DestroyPath(host, path_id));
    CHECK_FALSE(effindom::v2::native::DestroyPath(host, path_id));
    CHECK(effindom::v2::native::DestroyPath(host, shapes_id));
    CHECK(host.GetEngine().PathCountForTesting() == 0U);
}

TEST_CASE("native paths reject invalid IDs and geometry without mutation", "[v2][native][custom-draw][path]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    const std::uint32_t path_id = effindom::v2::native::CreatePath(host);
    REQUIRE(path_id != 0U);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    CHECK_FALSE(effindom::v2::native::PathMoveTo(host, 0U, 1.0f, 1.0f));
    CHECK_FALSE(effindom::v2::native::PathMoveTo(host, path_id, nan, 1.0f));
    CHECK_FALSE(effindom::v2::native::PathLineTo(host, path_id, infinity, 1.0f));
    CHECK_FALSE(effindom::v2::native::PathQuadTo(host, path_id, 1.0f, nan, 2.0f, 2.0f));
    CHECK_FALSE(effindom::v2::native::PathCubicTo(
        host, path_id, 1.0f, 1.0f, 2.0f, 2.0f, infinity, 3.0f));
    CHECK_FALSE(effindom::v2::native::PathAddRect(host, path_id, 0.0f, 0.0f, -1.0f, 2.0f));
    CHECK_FALSE(effindom::v2::native::PathAddCircle(host, path_id, 1.0f, 1.0f, 0.0f));
    CHECK_FALSE(effindom::v2::native::PathClose(host, 999U));
    CHECK(effindom::v2::native::DestroyPath(host, path_id));
}

TEST_CASE("native bitmap commits render full and dirty pixel updates", "[v2][native][custom-draw][bitmap]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    std::vector<std::uint8_t> red_pixels(4U * 4U * 4U, 0U);
    for (std::size_t index = 0U; index < red_pixels.size(); index += 4U) {
        red_pixels[index] = 255U;
        red_pixels[index + 3U] = 255U;
    }
    REQUIRE(effindom::v2::native::CommitBitmap(
        host, 51U, reinterpret_cast<std::uintptr_t>(red_pixels.data()),
        static_cast<std::uint32_t>(red_pixels.size()), 4U, 4U));

    const std::array<std::uint8_t, 4> blue = {0U, 0U, 255U, 255U};
    REQUIRE(effindom::v2::native::CommitBitmapDirty(
        host, 51U, reinterpret_cast<std::uintptr_t>(blue.data()),
        static_cast<std::uint32_t>(blue.size()), 4U, 4U, 2U, 1U, 1U, 1U));

    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(4, 4));
    REQUIRE(surface);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    const std::vector<std::uint32_t> words = {
        31U, 51U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(4.0f), FloatWord(4.0f),
        ED_IMAGE_SAMPLING_NEAREST, 0U,
    };
    REQUIRE(effindom::v2::native::DrawCanvasBatch(
        host,
        reinterpret_cast<std::uintptr_t>(surface->getCanvas()),
        reinterpret_cast<std::uintptr_t>(words.data()),
        static_cast<std::uint32_t>(words.size())));
    const auto pixels = SnapshotRgba(surface, 4, 4);
    CHECK(Pixel(pixels, 4, 1, 1) == std::array<std::uint8_t, 4>{255U, 0U, 0U, 255U});
    CHECK(Pixel(pixels, 4, 2, 1) == blue);

    CHECK(effindom::v2::native::ReleaseBitmap(host, 51U));
    CHECK_FALSE(effindom::v2::native::ReleaseBitmap(host, 51U));
    CHECK(host.GetEngine().TextureCountForTesting() == 0U);
}

TEST_CASE("native bitmap validation is atomic and first dirty commits are supported", "[v2][native][custom-draw][bitmap]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    const std::array<std::uint8_t, 4> green = {0U, 255U, 0U, 255U};

    CHECK_FALSE(effindom::v2::native::CommitBitmap(host, 0U, 0U, 0U, 0U, 0U));
    CHECK_FALSE(effindom::v2::native::CommitBitmap(
        host, 3U, reinterpret_cast<std::uintptr_t>(green.data()), 3U, 1U, 1U));
    CHECK_FALSE(effindom::v2::native::CommitBitmap(
        host, 3U, reinterpret_cast<std::uintptr_t>(green.data()), 5U, 1U, 1U));
    CHECK(host.GetEngine().TextureCountForTesting() == 0U);

    REQUIRE(effindom::v2::native::CommitBitmapDirty(
        host, 3U, reinterpret_cast<std::uintptr_t>(green.data()), 4U,
        2U, 2U, 0U, 0U, 1U, 1U));
    CHECK_FALSE(effindom::v2::native::CommitBitmapDirty(
        host, 3U, reinterpret_cast<std::uintptr_t>(green.data()), 4U,
        2U, 2U, 2U, 0U, 1U, 1U));
    CHECK_FALSE(effindom::v2::native::CommitBitmapDirty(
        host, 3U, reinterpret_cast<std::uintptr_t>(green.data()), 4U,
        3U, 2U, 0U, 0U, 1U, 1U));

    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(2, 2));
    REQUIRE(surface);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    const std::vector<std::uint32_t> words = {
        31U, 3U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(2.0f), FloatWord(2.0f),
        ED_IMAGE_SAMPLING_NEAREST, 0U,
    };
    REQUIRE(effindom::v2::native::DrawCanvasBatch(
        host,
        reinterpret_cast<std::uintptr_t>(surface->getCanvas()),
        reinterpret_cast<std::uintptr_t>(words.data()),
        static_cast<std::uint32_t>(words.size())));
    const auto pixels = SnapshotRgba(surface, 2, 2);
    CHECK(Pixel(pixels, 2, 0, 0) == green);
    CHECK(Pixel(pixels, 2, 1, 1)[3U] == 0U);
}

TEST_CASE("native offscreen drawing reads back and composes with top-left premultiplied pixels", "[v2][native][custom-draw][offscreen]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    const std::uint32_t offscreen = effindom::v2::native::CreateOffscreenSurface(host, 4U, 4U);
    REQUIRE(offscreen != 0U);
    CHECK(host.GetEngine().OffscreenSurfaceCountForTesting() == 1U);
    const std::uintptr_t canvas = effindom::v2::native::GetOffscreenCanvas(host, offscreen);
    REQUIRE(canvas != 0U);
    constexpr std::uint32_t half_red = 0xFF000080U;
    const std::vector<std::uint32_t> words = {
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(2.0f), FloatWord(2.0f),
        half_red, 0U, FloatWord(0.0f),
    };
    REQUIRE(effindom::v2::native::DrawCanvasBatch(
        host, canvas, reinterpret_cast<std::uintptr_t>(words.data()),
        static_cast<std::uint32_t>(words.size())));
    std::array<std::uint8_t, 4U * 4U * 4U> pixels{};
    REQUIRE(effindom::v2::native::ReadOffscreenPixels(
        host, offscreen, reinterpret_cast<std::uintptr_t>(pixels.data()), 4U, 4U));
    CHECK(Pixel(std::vector<std::uint8_t>(pixels.begin(), pixels.end()), 4, 0, 0) ==
        std::array<std::uint8_t, 4>{128U, 0U, 0U, 128U});

    REQUIRE(effindom::v2::native::CommitBitmap(
        host, 91U, reinterpret_cast<std::uintptr_t>(pixels.data()),
        static_cast<std::uint32_t>(pixels.size()), 4U, 4U));
    const sk_sp<SkSurface> composed = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(4, 4));
    REQUIRE(composed);
    composed->getCanvas()->clear(SK_ColorTRANSPARENT);
    const std::vector<std::uint32_t> image_words = {
        31U, 91U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(4.0f), FloatWord(4.0f),
        ED_IMAGE_SAMPLING_NEAREST, 0U,
    };
    REQUIRE(effindom::v2::native::DrawCanvasBatch(
        host, reinterpret_cast<std::uintptr_t>(composed->getCanvas()),
        reinterpret_cast<std::uintptr_t>(image_words.data()),
        static_cast<std::uint32_t>(image_words.size())));
    CHECK(Pixel(SnapshotRgba(composed, 4, 4), 4, 0, 0)[3U] == 128U);
    CHECK(effindom::v2::native::ReleaseBitmap(host, 91U));
    CHECK(effindom::v2::native::DestroyOffscreenSurface(host, offscreen));
    CHECK_FALSE(effindom::v2::native::DestroyOffscreenSurface(host, offscreen));
    CHECK(host.GetEngine().OffscreenSurfaceCountForTesting() == 0U);
}

TEST_CASE("native offscreen and retained raster validation is atomic", "[v2][native][custom-draw][offscreen]") {
    NativeHostCore host(NativeInputRouterOptions{}, NativeHostCoreCallbacks{});
    CHECK(effindom::v2::native::CreateOffscreenSurface(host, 0U, 2U) == 0U);
    CHECK(effindom::v2::native::GetOffscreenCanvas(host, 999U) == 0U);
    const std::uint32_t offscreen = effindom::v2::native::CreateOffscreenSurface(host, 2U, 2U);
    REQUIRE(offscreen != 0U);
    std::array<std::uint8_t, 16> pixels{};
    pixels.fill(0xA5U);
    CHECK_FALSE(effindom::v2::native::ReadOffscreenPixels(host, 999U,
        reinterpret_cast<std::uintptr_t>(pixels.data()), 2U, 2U));
    CHECK_FALSE(effindom::v2::native::ReadOffscreenPixels(host, offscreen,
        reinterpret_cast<std::uintptr_t>(pixels.data()), 1U, 2U));
    CHECK(std::all_of(pixels.begin(), pixels.end(), [](std::uint8_t value) { return value == 0xA5U; }));

    constexpr std::uint64_t handle = (1ULL << 32U) | 1ULL;
    CommandBuilder builder;
    builder.CreateNode(handle);
    builder.SetBounds(handle, 3.0f, 2.0f, 4.0f, 4.0f, false);
    builder.SetBoxStyle(handle, 0x00FF00FFU, 0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(host.GetEngine().ExecuteCommandBuffer(
        builder.words().data(), static_cast<std::uint32_t>(builder.words().size())).parsed_commands == 3U);
    std::array<std::uint8_t, 8U * 8U * 4U> rendered{};
    REQUIRE(effindom::v2::native::RenderNodeToRgba(
        host, handle, 8U, 8U, reinterpret_cast<std::uintptr_t>(rendered.data()),
        static_cast<std::uint32_t>(rendered.size()), 1.0f, 0.0f, 0.0f) == rendered.size());
    CHECK(Pixel(std::vector<std::uint8_t>(rendered.begin(), rendered.end()), 8, 1, 1) ==
        std::array<std::uint8_t, 4>{0U, 255U, 0U, 255U});
    CHECK(effindom::v2::native::RenderNodeToRgba(
        host, handle, 8U, 8U, reinterpret_cast<std::uintptr_t>(rendered.data()),
        4U, 1.0f, 0.0f, 0.0f) == 0U);
    CHECK(effindom::v2::native::DestroyOffscreenSurface(host, offscreen));
}
