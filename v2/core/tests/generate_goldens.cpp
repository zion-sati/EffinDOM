#include "CommandBuilder.h"
#include "Engine.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <include/core/SkData.h>
#include <include/core/SkImage.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>
#include <include/encode/SkPngEncoder.h>

namespace {

using effindom::v2::Engine;
using effindom::v2::GradientStop;
using effindom::v2::GlyphPlacement;
using effindom::v2::PathVerbRecord;
using effindom::v2::Rect;
using effindom::v2::SceneInstructionDebugView;
using effindom::v2::test::CommandBuilder;
using effindom::v2::test::Handle;

std::vector<std::uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path StyledSceneGoldenPath(const std::filesystem::path& output_dir) {
#if defined(__linux__)
    return output_dir / "styled-scene-linux.png";
#elif defined(__APPLE__)
    return output_dir / "styled-scene-macos.png";
#else
    return output_dir / "styled-scene.png";
#endif
}

bool WritePng(const sk_sp<SkSurface>& surface, const std::filesystem::path& output_path) {
    const sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        return false;
    }
    SkPngEncoder::Options options;
    const sk_sp<SkData> png = SkPngEncoder::Encode(nullptr, image.get(), options);
    if (!png) {
        return false;
    }
    std::ofstream output(output_path, std::ios::binary);
    output.write(static_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
    return output.good();
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

void RenderStyledScene(const std::filesystem::path& output_path) {
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
    builder.SetHighlights(text, 0xffffffffU, { Rect{56.0f, 72.0f, 92.0f, 24.0f} });
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
    engine.ExecuteCommandBuffer(builder.words().data(), static_cast<std::uint32_t>(builder.words().size()));

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, 220));
    engine.RenderToCanvas(surface->getCanvas());
    WritePng(surface, output_path);
}

void RenderImageScene(const std::filesystem::path& output_path) {
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
    engine.ExecuteCommandBuffer(builder.words().data(), static_cast<std::uint32_t>(builder.words().size()));

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(220, 180));
    engine.RenderToCanvas(surface->getCanvas());
    WritePng(surface, output_path);
}

void RenderBackgroundBlurScene(const std::filesystem::path& output_path) {
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
    engine.ExecuteCommandBuffer(builder.words().data(), static_cast<std::uint32_t>(builder.words().size()));

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(220, 180));
    engine.RenderToCanvas(surface->getCanvas());
    WritePng(surface, output_path);
}

void RenderSvgNineScene(const std::filesystem::path& output_path) {
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
    engine.ExecuteCommandBuffer(builder.words().data(), static_cast<std::uint32_t>(builder.words().size()));

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(260, 180));
    engine.RenderToCanvas(surface->getCanvas());
    WritePng(surface, output_path);
}

} // namespace

int main() {
    const std::filesystem::path output_dir = std::filesystem::path(EFFINDOM_SOURCE_DIR) / "v2/core/tests/goldens";
    std::filesystem::create_directories(output_dir);
    RenderStyledScene(StyledSceneGoldenPath(output_dir));
    RenderImageScene(output_dir / "image-scene.png");
    RenderBackgroundBlurScene(output_dir / "background-blur-scene.png");
    RenderSvgNineScene(output_dir / "svg-nine-scene.png");
    return 0;
}
