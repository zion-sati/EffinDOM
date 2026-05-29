#include "Engine.h"
#include "CommandBuilder.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string_view>
#include <vector>

#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>

namespace {

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
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U, 255U);
    for (std::uint32_t y = 0; y < height; y += 1) {
        for (std::uint32_t x = 0; x < width; x += 1) {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            pixels[offset + 0] = static_cast<std::uint8_t>(x < 2 || x >= 4 ? 255 : 32);
            pixels[offset + 1] = static_cast<std::uint8_t>(y < 2 || y >= 4 ? 196 : 64);
            pixels[offset + 2] = static_cast<std::uint8_t>((x >= 2 && x < 4 && y >= 2 && y < 4) ? 255 : 96);
        }
    }
    return pixels;
}

void RunStructuredCoverage() {
    using effindom::v2::SceneInstructionDebugView;
    using effindom::v2::test::CommandBuilder;
    using effindom::v2::test::Handle;

    effindom::v2::Engine engine;
    engine.Init(128, 128, 1.0f);

    const std::vector<std::uint8_t> svg = MakeSvgBytes();
    engine.RegisterSvg(7U, svg.data(), static_cast<std::uint32_t>(svg.size()));
    const std::vector<std::uint8_t> pixels = MakeNinePatchPixels();
    engine.RegisterTextureRgba(9U, pixels.data(), 6, 6, pixels.size());

    CommandBuilder builder;
    const std::uint64_t svg_handle = Handle(1);
    const std::uint64_t nine_handle = Handle(2);
    builder.CreateNode(svg_handle);
    builder.CreateNode(nine_handle);
    builder.SetBounds(svg_handle, 8.0f, 8.0f, 48.0f, 48.0f, false);
    builder.SetSvg(svg_handle, 7U, 0xff00ffffU);
    builder.SetBounds(nine_handle, 64.0f, 12.0f, 48.0f, 56.0f, 64.0f, 12.0f, 48.0f, 56.0f, true);
    builder.SetImageNine(nine_handle, 9U, 2.0f, 2.0f, 2.0f, 2.0f);
    builder.CommitPaintOrder({nine_handle});
    builder.CommitScene({
        SceneInstructionDebugView{OP_DRAW_NODE, svg_handle},
        SceneInstructionDebugView{OP_DRAW_NODE, nine_handle},
    });
    engine.ExecuteCommandBuffer(builder.words().data(), static_cast<std::uint32_t>(builder.words().size()));

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(128, 128));
    if (surface) {
        engine.RenderToCanvas(surface->getCanvas());
    }
    engine.HitTest(80.0f, 24.0f);
}

void RunOne(const std::uint8_t* data, std::size_t size) {
    effindom::v2::Engine engine;
    engine.Init(128, 128, 1.0f);

    const std::size_t word_count = size / sizeof(std::uint32_t);
    std::vector<std::uint32_t> words(word_count, 0U);
    if (word_count > 0) {
        std::memcpy(words.data(), data, word_count * sizeof(std::uint32_t));
        engine.ExecuteCommandBuffer(words.data(), static_cast<std::uint32_t>(words.size()));
    }

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(128, 128));
    if (surface) {
        engine.RenderToCanvas(surface->getCanvas());
    }
    engine.HitTest(16.0f, 16.0f);
}

} // namespace

int main(int argc, char** argv) {
    std::size_t iterations = 5000;
    std::size_t max_words = 512;
    for (int index = 1; index < argc; index += 1) {
        const std::string_view arg(argv[index]);
        if (arg == "--iterations" && index + 1 < argc) {
            iterations = static_cast<std::size_t>(std::strtoull(argv[++index], nullptr, 10));
        } else if (arg == "--max-words" && index + 1 < argc) {
            max_words = static_cast<std::size_t>(std::strtoull(argv[++index], nullptr, 10));
        }
    }

    std::mt19937_64 rng(0xE771D04DULL);
    std::uniform_int_distribution<std::uint32_t> word_dist(0U, 0xffffffffU);
    std::uniform_int_distribution<std::size_t> length_dist(0U, max_words);
    std::vector<std::uint32_t> words(max_words, 0U);

    RunStructuredCoverage();

    for (std::size_t iteration = 0; iteration < iterations; iteration += 1) {
        const std::size_t word_count = length_dist(rng);
        for (std::size_t word_index = 0; word_index < word_count; word_index += 1) {
            words[word_index] = word_dist(rng);
        }
        RunOne(reinterpret_cast<const std::uint8_t*>(words.data()), word_count * sizeof(std::uint32_t));
    }
    return 0;
}
