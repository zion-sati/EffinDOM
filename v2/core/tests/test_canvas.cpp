#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Engine.h"
#include "CommandBuilder.h"
#include "EngineInternal.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>

using Catch::Approx;

namespace {

using effindom::v2::EdCanvasDrawCircle;
using effindom::v2::EdCanvasDrawLine;
using effindom::v2::EdCanvasDrawRect;
using effindom::v2::EdCanvasDrawRoundRect;
using effindom::v2::EdCanvasClipRect;
using effindom::v2::EdCanvasRestore;
using effindom::v2::EdCanvasRotate;
using effindom::v2::EdCanvasSave;
using effindom::v2::EdCanvasScale;
using effindom::v2::EdCanvasTranslate;
using effindom::v2::Engine;

sk_sp<SkSurface> MakeRasterSurface(int width, int height) {
    const SkImageInfo info = SkImageInfo::Make(
        width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    return SkSurfaces::Raster(info);
}

std::vector<std::uint8_t> SnapshotRgba(sk_sp<SkSurface> surface, int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const bool ok = surface->readPixels(info, pixels.data(), static_cast<size_t>(width) * 4U, 0, 0);
    REQUIRE(ok);
    return pixels;
}

// Helper: check that a pixel at (x, y) has a non-zero alpha (i.e. something was drawn).
bool PixelHasAlpha(const std::vector<std::uint8_t>& pixels, int width, int x, int y) {
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
    return pixels[idx + 3] > 0;
}

// Helper: compute approximate alpha at pixel.
std::uint8_t PixelAlpha(const std::vector<std::uint8_t>& pixels, int width, int x, int y) {
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
    return pixels[idx + 3];
}

std::uint32_t FloatWord(float value) {
    std::uint32_t word = 0U;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

constexpr std::uint32_t kRed = 0xFF0000FFU;     // 0xRRGGBBAA: solid red
constexpr std::uint32_t kGreen = 0x00FF00FFU;   // solid green
constexpr std::uint32_t kBlue = 0x0000FFFFU;     // solid blue
constexpr std::uint32_t kTransparent = 0x00000000U;

} // namespace

TEST_CASE("Canvas null safety", "[canvas]") {
    SECTION("save / restore pass through null canvas") {
        EdCanvasSave(nullptr);
        EdCanvasRestore(nullptr);
        SUCCEED("No crash on null canvas");
    }

    SECTION("drawing primitives pass through null canvas") {
        EdCanvasDrawRect(nullptr, 0, 0, 10, 10, kRed, 0, 0);
        EdCanvasDrawCircle(nullptr, 5, 5, 5, kRed, 0, 0);
        EdCanvasDrawLine(nullptr, 0, 0, 10, 10, kRed, 2);
        EdCanvasDrawRoundRect(nullptr, 0, 0, 10, 10, 3, 3, kRed, 0, 0);
        SUCCEED("No crash on null canvas");
    }
}

TEST_CASE("Canvas draw rect", "[canvas]") {
    auto surface = MakeRasterSurface(64, 64);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    SECTION("filled rect") {
        EdCanvasDrawRect(canvas, 10, 10, 20, 20, kRed, 0, 0);
        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 15, 15));
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 5, 5));
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 35, 35));
    }

    SECTION("stroked rect") {
        EdCanvasDrawRect(canvas, 5, 5, 20, 20, 0, kBlue, 4);
        auto pixels = SnapshotRgba(surface, 64, 64);
        // Interior (no fill) should be transparent
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 15, 15));
        // Border edge — within the 4px stroke region
        CHECK(PixelHasAlpha(pixels, 64, 6, 15));
    }

    SECTION("filled and stroked") {
        EdCanvasDrawRect(canvas, 5, 5, 20, 20, kGreen, kRed, 2);
        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 10, 10));  // interior filled
        CHECK(PixelHasAlpha(pixels, 64, 6, 10));   // border
    }

    SECTION("zero alpha fill draws nothing") {
        EdCanvasDrawRect(canvas, 0, 0, 64, 64, kTransparent, 0, 0);
        auto pixels = SnapshotRgba(surface, 64, 64);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                CHECK_FALSE(PixelHasAlpha(pixels, 64, x, y));
            }
        }
    }
}

TEST_CASE("Canvas draw circle", "[canvas]") {
    auto surface = MakeRasterSurface(64, 64);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    SECTION("filled circle") {
        EdCanvasDrawCircle(canvas, 32, 32, 20, kRed, 0, 0);
        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 32, 32));  // center
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 32, 60));  // outside
    }
}

TEST_CASE("Canvas draw line", "[canvas]") {
    auto surface = MakeRasterSurface(64, 64);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    SECTION("draws a visible line") {
        EdCanvasDrawLine(canvas, 0, 0, 60, 60, kRed, 3);
        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 30, 30));
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 60, 0));
    }

    SECTION("zero stroke width draws nothing") {
        EdCanvasDrawLine(canvas, 0, 0, 64, 64, kRed, 0);
        auto pixels = SnapshotRgba(surface, 64, 64);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                CHECK_FALSE(PixelHasAlpha(pixels, 64, x, y));
            }
        }
    }
}

TEST_CASE("Canvas draw round rect", "[canvas]") {
    auto surface = MakeRasterSurface(64, 64);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    SECTION("filled round rect") {
        EdCanvasDrawRoundRect(canvas, 10, 10, 40, 40, 8, 8, kGreen, 0, 0);
        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 30, 30));  // interior
        // Corner at (12, 12) — rounded corner, should have less alpha than interior
        CHECK(PixelAlpha(pixels, 64, 12, 12) < PixelAlpha(pixels, 64, 30, 30));
    }
}

TEST_CASE("Canvas transforms", "[canvas]") {
    auto surface = MakeRasterSurface(64, 64);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    SECTION("save / restore isolates transforms") {
        canvas->save();
        EdCanvasTranslate(canvas, 10, 10);
        EdCanvasDrawRect(canvas, 0, 0, 20, 20, kRed, 0, 0);
        EdCanvasRestore(canvas);

        // After restore, drawing at (0,0) should not be offset
        canvas->save();
        EdCanvasDrawRect(canvas, 0, 0, 10, 10, kBlue, 0, 0);
        EdCanvasRestore(canvas);

        auto pixels = SnapshotRgba(surface, 64, 64);
        // Red rect should be at (10, 10)
        CHECK(PixelHasAlpha(pixels, 64, 15, 15));
        // Blue rect should be at (0, 0)
        CHECK(PixelHasAlpha(pixels, 64, 5, 5));
    }

    SECTION("clip rect restricts drawing") {
        EdCanvasClipRect(canvas, 10, 10, 20, 20);
        EdCanvasDrawRect(canvas, 0, 0, 64, 64, kRed, 0, 0);
        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 15, 15));    // inside clip
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 5, 5)); // outside clip
    }

    SECTION("scale doubles draw size") {
        canvas->save();
        EdCanvasScale(canvas, 2, 2);
        EdCanvasDrawRect(canvas, 5, 5, 10, 10, kRed, 0, 0);
        EdCanvasRestore(canvas);

        auto pixels = SnapshotRgba(surface, 64, 64);
        // After 2x scale, the rect should span (10, 10) to (30, 30) in canvas coords
        CHECK(PixelHasAlpha(pixels, 64, 20, 20));
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 8, 8));
    }
}

TEST_CASE("Canvas draw batch replays ordered state and draw commands", "[canvas]") {
    Engine engine;
    auto surface = MakeRasterSurface(64, 64);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const std::vector<std::uint32_t> words = {
        1U, // save
        3U, FloatWord(10.0f), FloatWord(10.0f), // translate
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(16.0f), FloatWord(16.0f),
        kRed, 0U, FloatWord(0.0f),
        2U, // restore
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(8.0f), FloatWord(8.0f),
        kBlue, 0U, FloatWord(0.0f),
        12U, FloatWord(20.0f), FloatWord(20.0f), FloatWord(50.0f), FloatWord(20.0f),
        kGreen, FloatWord(2.0f),
        1U, // save
        7U, FloatWord(40.0f), FloatWord(40.0f), FloatWord(12.0f), FloatWord(12.0f),
        FloatWord(2.0f), FloatWord(4.0f), FloatWord(6.0f), FloatWord(8.0f),
        10U, FloatWord(40.0f), FloatWord(40.0f), FloatWord(12.0f), FloatWord(12.0f),
        kRed, 0U, FloatWord(0.0f),
        2U,
    };

    engine.CanvasDrawBatch(canvas, words.data(), static_cast<std::uint32_t>(words.size()));

    const auto pixels = SnapshotRgba(surface, 64, 64);
    CHECK(PixelHasAlpha(pixels, 64, 12, 12)); // translated red rect
    CHECK(PixelHasAlpha(pixels, 64, 4, 4));   // blue rect after restore
    CHECK(PixelHasAlpha(pixels, 64, 30, 20)); // green line
    CHECK(PixelHasAlpha(pixels, 64, 46, 46)); // rounded-clip rect
    CHECK_FALSE(PixelHasAlpha(pixels, 64, 20, 4));
}

TEST_CASE("Canvas draw batch ignores invalid input", "[canvas]") {
    Engine engine;
    auto surface = MakeRasterSurface(16, 16);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    const std::array<std::uint32_t, 10> truncated_after_valid_command = {
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(8.0f), FloatWord(8.0f),
        kRed, 0U, FloatWord(0.0f),
        10U, FloatWord(0.0f),
    };
    const std::array<std::uint32_t, 9> unknown_after_valid_command = {
        10U, FloatWord(0.0f), FloatWord(0.0f), FloatWord(8.0f), FloatWord(8.0f),
        kRed, 0U, FloatWord(0.0f),
        999U,
    };

    CHECK(engine.CanvasDrawBatch(nullptr, nullptr, 0U));
    CHECK_FALSE(engine.CanvasDrawBatch(
        nullptr,
        truncated_after_valid_command.data(),
        static_cast<std::uint32_t>(truncated_after_valid_command.size())));
    CHECK_FALSE(engine.CanvasDrawBatch(canvas, nullptr, 1U));
    CHECK_FALSE(engine.CanvasDrawBatch(
        canvas,
        truncated_after_valid_command.data(),
        static_cast<std::uint32_t>(truncated_after_valid_command.size())));
    CHECK_FALSE(engine.CanvasDrawBatch(
        canvas,
        unknown_after_valid_command.data(),
        static_cast<std::uint32_t>(unknown_after_valid_command.size())));

    const auto pixels = SnapshotRgba(surface, 16, 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            CHECK_FALSE(PixelHasAlpha(pixels, 16, x, y));
        }
    }
}

TEST_CASE("Path management", "[path]") {
    Engine engine;

    SECTION("create and destroy paths") {
        CHECK(engine.PathCountForTesting() == 0U);
        const std::uint32_t id1 = engine.CreatePath();
        REQUIRE(id1 > 0);
        const std::uint32_t id2 = engine.CreatePath();
        REQUIRE(id2 > id1);

        CHECK(engine.PathCountForTesting() == 2U);
        CHECK(engine.DestroyPath(id1));
        CHECK_FALSE(engine.DestroyPath(id1));
        CHECK_FALSE(engine.DestroyPath(0U));
        CHECK(engine.DestroyPath(id2));
        CHECK(engine.PathCountForTesting() == 0U);
    }

    SECTION("drawing an empty path does nothing") {
        // Create a raster surface to draw on
        auto surface = MakeRasterSurface(32, 32);
        surface->getCanvas()->clear(SK_ColorTRANSPARENT);

        const std::uint32_t path_id = engine.CreatePath();
        engine.CanvasDrawPath(surface->getCanvas(), path_id, kRed, 0, 0);

        auto pixels = SnapshotRgba(surface, 32, 32);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                CHECK_FALSE(PixelHasAlpha(pixels, 32, x, y));
            }
        }
        engine.DestroyPath(path_id);
    }

    SECTION("drawing a rect path") {
        const std::uint32_t path_id = engine.CreatePath();
        engine.PathAddRect(path_id, 10, 10, 20, 20);

        auto surface = MakeRasterSurface(64, 64);
        surface->getCanvas()->clear(SK_ColorTRANSPARENT);
        engine.CanvasDrawPath(surface->getCanvas(), path_id, kRed, 0, 0);

        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 15, 15));
        CHECK_FALSE(PixelHasAlpha(pixels, 64, 5, 5));

        engine.DestroyPath(path_id);
    }

    SECTION("path with manual verbs") {
        const std::uint32_t path_id = engine.CreatePath();
        engine.PathMoveTo(path_id, 10, 10);
        engine.PathLineTo(path_id, 30, 10);
        engine.PathLineTo(path_id, 30, 30);
        engine.PathLineTo(path_id, 10, 30);
        engine.PathClose(path_id);

        auto surface = MakeRasterSurface(64, 64);
        surface->getCanvas()->clear(SK_ColorTRANSPARENT);
        engine.CanvasDrawPath(surface->getCanvas(), path_id, kGreen, kRed, 2);

        auto pixels = SnapshotRgba(surface, 64, 64);
        CHECK(PixelHasAlpha(pixels, 64, 20, 20));  // interior
        CHECK(PixelHasAlpha(pixels, 64, 11, 20));  // stroke

        engine.DestroyPath(path_id);
    }

    SECTION("rejects unknown IDs and invalid geometry without mutating a live path") {
        const std::uint32_t path_id = engine.CreatePath();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();

        CHECK_FALSE(engine.PathMoveTo(999U, 1.0f, 1.0f));
        CHECK_FALSE(engine.PathMoveTo(path_id, nan, 1.0f));
        CHECK_FALSE(engine.PathLineTo(path_id, 1.0f, infinity));
        CHECK_FALSE(engine.PathQuadTo(path_id, 1.0f, 1.0f, nan, 2.0f));
        CHECK_FALSE(engine.PathCubicTo(path_id, 1.0f, 1.0f, 2.0f, infinity, 3.0f, 3.0f));
        CHECK_FALSE(engine.PathAddRect(path_id, 0.0f, 0.0f, 0.0f, 8.0f));
        CHECK_FALSE(engine.PathAddRect(path_id, 0.0f, 0.0f, 8.0f, -1.0f));
        CHECK_FALSE(engine.PathAddCircle(path_id, 4.0f, 4.0f, 0.0f));
        CHECK_FALSE(engine.PathClose(999U));

        auto surface = MakeRasterSurface(16, 16);
        surface->getCanvas()->clear(SK_ColorTRANSPARENT);
        engine.CanvasDrawPath(surface->getCanvas(), path_id, kRed, kRed, 2.0f);
        const auto pixels = SnapshotRgba(surface, 16, 16);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                CHECK_FALSE(PixelHasAlpha(pixels, 16, x, y));
            }
        }
        CHECK(engine.DestroyPath(path_id));
    }
}

TEST_CASE("Texture uploads validate atomically and preserve pixels outside dirty rectangles", "[canvas][texture]") {
    Engine engine;
    const std::array<std::uint8_t, 4> one_red_pixel = {255U, 0U, 0U, 255U};
    std::vector<std::uint8_t> red_pixels(4U * 4U * 4U, 0U);
    for (std::size_t index = 0U; index < red_pixels.size(); index += 4U) {
        red_pixels[index] = 255U;
        red_pixels[index + 3U] = 255U;
    }

    CHECK_FALSE(engine.RegisterTextureRgba(0U, red_pixels.data(), 4U, 4U, red_pixels.size()));
    CHECK_FALSE(engine.RegisterTextureRgba(1U, nullptr, 4U, 4U, red_pixels.size()));
    CHECK_FALSE(engine.RegisterTextureRgba(1U, red_pixels.data(), 0U, 4U, red_pixels.size()));
    CHECK_FALSE(engine.RegisterTextureRgba(1U, red_pixels.data(), 4U, 4U, red_pixels.size() - 1U));
    CHECK_FALSE(engine.RegisterTextureRgba(1U, red_pixels.data(), 4U, 4U, red_pixels.size() + 1U));
    CHECK_FALSE(engine.RegisterTextureRgba(
        1U, one_red_pixel.data(), UINT32_MAX, UINT32_MAX, one_red_pixel.size()));
    CHECK(engine.TextureCountForTesting() == 0U);

    REQUIRE(engine.RegisterTextureRgba(1U, red_pixels.data(), 4U, 4U, red_pixels.size()));
    CHECK(engine.TextureCountForTesting() == 1U);
    CHECK(engine.GetTextureSizeForTesting(1U) == std::pair<std::uint32_t, std::uint32_t>{4U, 4U});

    const std::array<std::uint8_t, 4> blue_pixel = {0U, 0U, 255U, 255U};
    CHECK_FALSE(engine.RegisterTextureSubRgba(1U, nullptr, 1U, 1U, 1U, 1U, 4U, 4U, 4U));
    CHECK_FALSE(engine.RegisterTextureSubRgba(1U, blue_pixel.data(), 4U, 0U, 1U, 1U, 4U, 4U, 4U));
    CHECK_FALSE(engine.RegisterTextureSubRgba(1U, blue_pixel.data(), 1U, 1U, 1U, 1U, 5U, 4U, 4U));
    CHECK_FALSE(engine.RegisterTextureSubRgba(1U, blue_pixel.data(), 1U, 1U, 1U, 1U, 4U, 4U, 3U));
    REQUIRE(engine.RegisterTextureSubRgba(1U, blue_pixel.data(), 2U, 1U, 1U, 1U, 4U, 4U, 4U));

    auto surface = MakeRasterSurface(4, 4);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    engine.CanvasDrawImage(surface->getCanvas(), 1U, 0.0f, 0.0f, 4.0f, 4.0f, ED_IMAGE_SAMPLING_NEAREST, 0U);
    const auto pixels = SnapshotRgba(surface, 4, 4);
    const std::size_t unchanged = (1U * 4U + 1U) * 4U;
    const std::size_t changed = (1U * 4U + 2U) * 4U;
    CHECK(pixels[unchanged] == 255U);
    CHECK(pixels[unchanged + 2U] == 0U);
    CHECK(pixels[changed] == 0U);
    CHECK(pixels[changed + 2U] == 255U);

    CHECK(engine.UnregisterTexture(1U));
    CHECK_FALSE(engine.UnregisterTexture(1U));
    CHECK_FALSE(engine.UnregisterTexture(0U));
    CHECK(engine.TextureCountForTesting() == 0U);
}

TEST_CASE("A first dirty texture upload creates a transparent full image", "[canvas][texture]") {
    Engine engine;
    const std::array<std::uint8_t, 4> green_pixel = {0U, 255U, 0U, 255U};
    REQUIRE(engine.RegisterTextureSubRgba(
        7U, green_pixel.data(), 0U, 0U, 1U, 1U, 2U, 2U, green_pixel.size()));

    auto surface = MakeRasterSurface(2, 2);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    engine.CanvasDrawImage(surface->getCanvas(), 7U, 0.0f, 0.0f, 2.0f, 2.0f, ED_IMAGE_SAMPLING_NEAREST, 0U);
    const auto pixels = SnapshotRgba(surface, 2, 2);
    CHECK(pixels[1U] == 255U);
    CHECK(pixels[3U] == 255U);
    CHECK(pixels[(1U * 2U + 1U) * 4U + 3U] == 0U);
}

TEST_CASE("Offscreen surfaces", "[offscreen]") {
    Engine engine;

    SECTION("create and destroy") {
        const std::uint32_t id = engine.CreateOffscreenSurface(64, 64);
        REQUIRE(id > 0);
        CHECK(engine.OffscreenSurfaceCountForTesting() == 1U);

        void* canvas = engine.GetOffscreenCanvas(id);
        REQUIRE(canvas != nullptr);

        CHECK(engine.DestroyOffscreenSurface(id));
        CHECK_FALSE(engine.DestroyOffscreenSurface(id));
        CHECK(engine.OffscreenSurfaceCountForTesting() == 0U);
        REQUIRE(engine.GetOffscreenCanvas(id) == nullptr);
    }

    SECTION("draw on offscreen and read back") {
        const std::uint32_t id = engine.CreateOffscreenSurface(32, 32);
        void* canvas = engine.GetOffscreenCanvas(id);
        REQUIRE(canvas != nullptr);

        auto* skCanvas = static_cast<SkCanvas*>(canvas);
        skCanvas->clear(SK_ColorTRANSPARENT);
        EdCanvasDrawRect(skCanvas, 0, 0, 16, 16, kRed, 0, 0);

        std::vector<std::uint8_t> pixels(32 * 32 * 4);
        REQUIRE(engine.ReadOffscreenPixels(id, pixels.data(), 32U, 32U));

        // Check a pixel inside the red rect
        const std::size_t idx = (8U * 32U + 8U) * 4U;
        CHECK(pixels[idx + 3] > 0);  // alpha present

        // Check outside
        const std::size_t out_idx = (24U * 32U + 24U) * 4U;
        CHECK(pixels[out_idx + 3] == 0);

        CHECK(engine.DestroyOffscreenSurface(id));
    }

    SECTION("zero-size offscreen returns 0") {
        REQUIRE(engine.CreateOffscreenSurface(0, 10) == 0);
        REQUIRE(engine.CreateOffscreenSurface(10, 0) == 0);
        REQUIRE(engine.CreateOffscreenSurface(std::numeric_limits<std::uint32_t>::max(), 1U) == 0U);
    }

    SECTION("invalid readback is atomic") {
        const std::uint32_t id = engine.CreateOffscreenSurface(2U, 2U);
        REQUIRE(id != 0U);
        std::array<std::uint8_t, 16> pixels{};
        pixels.fill(0xA5U);
        CHECK_FALSE(engine.ReadOffscreenPixels(0U, pixels.data(), 2U, 2U));
        CHECK_FALSE(engine.ReadOffscreenPixels(id, nullptr, 2U, 2U));
        CHECK_FALSE(engine.ReadOffscreenPixels(id, pixels.data(), 1U, 2U));
        CHECK(std::all_of(pixels.begin(), pixels.end(), [](std::uint8_t value) { return value == 0xA5U; }));
        CHECK(engine.DestroyOffscreenSurface(id));
    }
}

TEST_CASE("RenderNodeToRgba validation", "[canvas]") {
    Engine engine;
    std::array<uint8_t, 64> buffer{};
    constexpr std::uint32_t buffer_capacity = static_cast<std::uint32_t>(buffer.size());
    engine.Init(100, 100, 2.0f);

    SECTION("null handle returns 0") {
        REQUIRE(engine.RenderNodeToRgba(0, 10, 10, buffer.data(), buffer_capacity, 1.0f, 0.0f, 0.0f) == 0);
    }

    SECTION("zero dimensions return 0") {
        REQUIRE(engine.RenderNodeToRgba(1, 0, 10, buffer.data(), buffer_capacity, 1.0f, 0.0f, 0.0f) == 0);
        REQUIRE(engine.RenderNodeToRgba(1, 10, 0, buffer.data(), buffer_capacity, 1.0f, 0.0f, 0.0f) == 0);
    }

    SECTION("null output buffer returns 0") {
        REQUIRE(engine.RenderNodeToRgba(1, 10, 10, nullptr, 100, 1.0f, 0.0f, 0.0f) == 0);
    }

    SECTION("insufficient capacity returns 0") {
        REQUIRE(engine.RenderNodeToRgba(1, 10, 10, buffer.data(), 10, 1.0f, 0.0f, 0.0f) == 0);
    }

    SECTION("unknown handle returns 0") {
        REQUIRE(engine.RenderNodeToRgba(9999, 10, 10, buffer.data(), buffer_capacity, 1.0f, 0.0f, 0.0f) == 0);
    }

    SECTION("generic retained nodes render top-left premultiplied RGBA") {
        constexpr std::uint64_t handle = (1ULL << 32U) | 1ULL;
        effindom::v2::test::CommandBuilder builder;
        builder.CreateNode(handle);
        builder.SetBounds(handle, 2.0f, 3.0f, 4.0f, 4.0f, false);
        builder.SetBoxStyle(handle, 0xFF000080U, 0.0f, 0.0f, 0.0f, 0.0f);
        REQUIRE(engine.ExecuteCommandBuffer(
            builder.words().data(), static_cast<std::uint32_t>(builder.words().size())).parsed_commands == 3U);
        std::array<std::uint8_t, 8U * 8U * 4U> pixels{};
        REQUIRE(engine.RenderNodeToRgba(
            handle, 8U, 8U, pixels.data(), static_cast<std::uint32_t>(pixels.size()),
            1.0f, 0.0f, 0.0f) == pixels.size());
        CHECK(pixels[0U] == 128U);
        CHECK(pixels[1U] == 0U);
        CHECK(pixels[2U] == 0U);
        CHECK(pixels[3U] == 128U);
    }
}
