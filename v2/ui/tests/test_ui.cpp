#include "TestUiSupport.h"

namespace {

std::uint64_t g_reentrant_scroll_test_handle = UI_INVALID_HANDLE;
std::size_t g_reentrant_scroll_callback_depth = 0U;
std::size_t g_reentrant_scroll_max_depth = 0U;

void ReentrantScrollApplyCallback(
    std::uint64_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height) {
    test_ui_support::RecordScrollChange(
        handle,
        offset_x,
        offset_y,
        content_width,
        content_height,
        viewport_width,
        viewport_height);
    g_reentrant_scroll_callback_depth += 1U;
    g_reentrant_scroll_max_depth = std::max(g_reentrant_scroll_max_depth, g_reentrant_scroll_callback_depth);
    if (handle == g_reentrant_scroll_test_handle && test_ui_support::g_scroll_changes.size() == 1U) {
        auto& runtime = effindom::v2::ui::GetRuntime();
        auto* node = runtime.ResolveMutable(handle);
        REQUIRE(node != nullptr);
        REQUIRE(runtime.ApplyScrollOffset(handle, *node, offset_x, offset_y + 10.0f, true));
    }
    g_reentrant_scroll_callback_depth -= 1U;
}

} // namespace

TEST_CASE("v2 ui create_node exhausts the pool without duplicate indices", "[v2][ui][unit]") {
    ui_reset();

    std::set<std::uint32_t> indices{};
    for (std::size_t count = 1; count < effindom::v2::ui::kMaxNodes; count += 1) {
        const std::uint64_t handle = ui_create_node(UI_NODE_FLEX_BOX);
        REQUIRE(handle != UI_INVALID_HANDLE);
        indices.insert(HandleIndex(handle));
    }

    CHECK(indices.size() == effindom::v2::ui::kMaxNodes - 1U);
    CHECK(indices.count(0U) == 0U);
    CHECK(ui_create_node(UI_NODE_FLEX_BOX) == UI_INVALID_HANDLE);
}

TEST_CASE("v2 ui reuses indices with a newer generation after delete", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t first = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(first != UI_INVALID_HANDLE);

    ui_delete_node(first);

    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(second != UI_INVALID_HANDLE);

    CHECK(HandleIndex(second) == HandleIndex(first));
    CHECK(HandleGeneration(second) == HandleGeneration(first) + 1U);
    CHECK(second != first);
}

TEST_CASE("v2 ui command builder bit-casts floats, deletes, and glyph runs into Core words", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetBounds(42ULL, 10.5f, 20.0f, 100.0f, 50.0f);
    builder.DeleteNode(0x0000000200000001ULL);
    builder.SetTextFade(42ULL, ED_FADE_BOTTOM);
    builder.SetGlyphRun(42ULL, 7U, 24.0f, 0xff112233U, {
        effindom::v2::ui::GlyphPlacement{10U, 0.0f, 20.0f},
        effindom::v2::ui::GlyphPlacement{11U, 12.5f, 20.0f},
    });
    builder.SetBoxStyle(42ULL, 0xff223344U, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0xff556677U, ED_BORDER_DASHED, 6.0f, 7.0f);
    builder.SetLayerEffect(42ULL, 0.5f, 2.0f, ED_BLEND_MULTIPLY);
    builder.SetLinearGradient(42ULL, 8.0f, 9.0f, 10.0f, 11.0f, {
        effindom::v2::ui::GradientStop{0.0f, 0xff0000ffU},
        effindom::v2::ui::GradientStop{1.0f, 0x00ff00ffU},
    });

    REQUIRE(words.size() == 69U);
    CHECK(words[0] == CMD_SET_BOUNDS);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(words[3] == effindom::v2::ui::CommandBuilder::FloatToWord(10.5f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[3]) == Approx(10.5f));
    CHECK(words[16] == CMD_DELETE_NODE);
    CHECK(words[17] == 1U);
    CHECK(words[18] == 2U);
    CHECK(words[19] == CMD_SET_TEXT_FADE);
    CHECK(words[22] == ED_FADE_BOTTOM);
    CHECK(words[23] == CMD_SET_GLYPH_RUN);
    CHECK(words[26] == 7U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[27]) == Approx(24.0f));
    CHECK(words[29] == 2U);
    CHECK(words[38] == CMD_SET_BOX_STYLE);
    CHECK(words[41] == 0xff223344U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[42]) == Approx(1.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[46]) == Approx(5.0f));
    CHECK(words[48] == ED_BORDER_DASHED);
    CHECK(words[51] == CMD_SET_LAYER_EFFECT);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[54]) == Approx(0.5f));
    CHECK(words[56] == ED_BLEND_MULTIPLY);
    CHECK(words[57] == CMD_SET_LINEAR_GRADIENT);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[60]) == Approx(8.0f));
    CHECK(words[64] == 2U);
    CHECK(words[66] == 0xff0000ffU);
    CHECK(words[68] == 0x00ff00ffU);
}

TEST_CASE("v2 ui command builder encodes background blur into Core words", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetBackgroundBlur(42ULL, 6.5f);

    REQUIRE(words.size() == 4U);
    CHECK(words[0] == CMD_SET_BACKGROUND_BLUR);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[3]) == Approx(6.5f));
}

TEST_CASE("v2 ui command builder encodes colored glyph runs into Core words", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetGlyphRunColored(42ULL, 7U, 24.0f, {
        effindom::v2::ui::GlyphPlacement{10U, 0.0f, 20.0f, 0U, 7U, 0xff0000ffU},
        effindom::v2::ui::GlyphPlacement{11U, 12.5f, 20.0f, 1U, 7U, 0x00ff00ffU},
    });

    REQUIRE(words.size() == 16U);
    CHECK(words[0] == CMD_SET_GLYPH_RUN_COLORED);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(words[3] == 7U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[4]) == Approx(24.0f));
    CHECK(words[5] == 2U);
    CHECK(words[10] == 0xff0000ffU);
    CHECK(words[15] == 0x00ff00ffU);
}

TEST_CASE("v2 ui command builder encodes retained image and svg commands", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetImage(42ULL, 17U, ED_OBJECT_FIT_CONTAIN);
    builder.SetImageNine(42ULL, 18U, 1.0f, 2.0f, 3.0f, 4.0f);
    builder.SetSvg(42ULL, 19U, 0xff3366ffU);

    REQUIRE(words.size() == 18U);
    CHECK(words[0] == CMD_SET_IMAGE);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(words[3] == 17U);
    CHECK(words[4] == ED_OBJECT_FIT_CONTAIN);
    CHECK(words[5] == CMD_SET_IMAGE_NINE);
    CHECK(words[8] == 18U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[9]) == Approx(1.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[12]) == Approx(4.0f));
    CHECK(words[13] == CMD_SET_SVG);
    CHECK(words[16] == 19U);
    CHECK(words[17] == 0xff3366ffU);
}

TEST_CASE("v2 ui runtime emits retained box style layer effect and gradient commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_box_style(root, 0xff112233U, 4.0f, 5.0f, 6.0f, 7.0f, 2.0f, 0xff445566U, ED_BORDER_DASHED, 8.0f, 3.0f);
    ui_set_layer_effect(root, 0.5f, 1.5f, ED_BLEND_MULTIPLY);
    const float offsets[] = {0.0f, 1.0f};
    const ui_color_t colors[] = {0xff0000ffU, 0x00ff00ffU};
    ui_set_linear_gradient(root, 10.0f, 20.0f, 30.0f, 40.0f, 2U, offsets, colors);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_BOX_STYLE) == 1U);
    CHECK(CountCommand(words, CMD_SET_LAYER_EFFECT) == 1U);
    CHECK(CountCommand(words, CMD_SET_LINEAR_GRADIENT) == 1U);

    const auto box_it = std::find(words.begin(), words.end(), CMD_SET_BOX_STYLE);
    REQUIRE(box_it != words.end());
    const std::size_t box_index = static_cast<std::size_t>(std::distance(words.begin(), box_it));
    CHECK(words[box_index + 3U] == 0xff112233U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[box_index + 4U]) == Approx(4.0f));
    CHECK(words[box_index + 10U] == ED_BORDER_DASHED);

    const auto layer_it = std::find(words.begin(), words.end(), CMD_SET_LAYER_EFFECT);
    REQUIRE(layer_it != words.end());
    const std::size_t layer_index = static_cast<std::size_t>(std::distance(words.begin(), layer_it));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[layer_index + 3U]) == Approx(0.5f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[layer_index + 4U]) == Approx(1.5f));
    CHECK(words[layer_index + 5U] == ED_BLEND_MULTIPLY);

    const auto gradient_it = std::find(words.begin(), words.end(), CMD_SET_LINEAR_GRADIENT);
    REQUIRE(gradient_it != words.end());
    const std::size_t gradient_index = static_cast<std::size_t>(std::distance(words.begin(), gradient_it));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[gradient_index + 3U]) == Approx(10.0f));
    CHECK(words[gradient_index + 7U] == 2U);
    CHECK(words[gradient_index + 9U] == 0xff0000ffU);
    CHECK(words[gradient_index + 11U] == 0x00ff00ffU);
}

TEST_CASE("v2 ui style runs emit a colored glyph command with per-glyph colors", "[v2][ui][layout][text]") {
    ui_reset();
    RegisterTestFont(1U);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 120.0f);
    ui_set_width(text, 280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("HelloWorld"), 10U);

    const std::uint32_t runs_words[] = {
        0U, 5U, 1U, effindom::v2::ui::CommandBuilder::FloatToWord(20.0f), 0xff0000ffU, 0U, 0U,
        5U, 10U, 1U, effindom::v2::ui::CommandBuilder::FloatToWord(20.0f), 0x00ff00ffU, 0U, 0U,
    };
    ui_set_text_style_runs(text, 2U, runs_words);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto words = ReadCommandBuffer();
    bool saw_colored_run = false;
    bool saw_red = false;
    bool saw_green = false;

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 5U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 8U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_HIGHLIGHTS_COLORED:
            i += 4U + (static_cast<std::size_t>(words[i + 3U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            const std::uint32_t glyph_count = words[i + 5U];
            if (handle == text) {
                saw_colored_run = true;
                for (std::uint32_t glyph_index = 0; glyph_index < glyph_count; glyph_index += 1U) {
                    const std::size_t base = i + 6U + (static_cast<std::size_t>(glyph_index) * 5U);
                    const std::uint32_t color = words[base + 4U];
                    saw_red = saw_red || color == 0xff0000ffU;
                    saw_green = saw_green || color == 0x00ff00ffU;
                }
            }
            i += 6U + (static_cast<std::size_t>(glyph_count) * 5U);
            break;
        }
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    CHECK(saw_colored_run);
    CHECK(saw_red);
    CHECK(saw_green);
}

TEST_CASE("v2 ui style-run backgrounds and decorations emit colored highlight rects", "[v2][ui][layout][text]") {
    ui_reset();
    RegisterTestFont(1U);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 120.0f);
    ui_set_width(text, 280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    const std::uint32_t runs_words[] = {
        0U, 5U, 1U, effindom::v2::ui::CommandBuilder::FloatToWord(20.0f), 0xffffffffU, 0xff223344U, 3U,
    };
    ui_set_text_style_runs(text, 1U, runs_words);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto words = ReadCommandBuffer();
    bool saw_colored_highlights = false;
    for (std::size_t i = 0; i < words.size();) {
        const std::uint32_t cmd = words[i];
        if (cmd == CMD_SET_HIGHLIGHTS_COLORED) {
            REQUIRE(i + 4U <= words.size());
            const std::size_t rect_count = static_cast<std::size_t>(words[i + 3U]);
            REQUIRE(i + 4U + (rect_count * 5U) <= words.size());
            REQUIRE(rect_count >= 3U);
            CHECK(words[i + 8U] == 0xff223344U);
            CHECK(words[i + 13U] == 0xffffffffU);
            CHECK(words[i + 18U] == 0xffffffffU);
            saw_colored_highlights = true;
            break;
        }
        switch (cmd) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 5U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 8U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_HIGHLIGHTS_COLORED:
            i += 4U + (static_cast<std::size_t>(words[i + 3U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            FAIL("Unexpected command while parsing command buffer");
            return;
        }
    }
    CHECK(saw_colored_highlights);
}

TEST_CASE("v2 ui runtime emits retained background blur commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_background_blur(root, 9.0f);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_BACKGROUND_BLUR) == 1U);
    const auto backdrop_it = std::find(words.begin(), words.end(), CMD_SET_BACKGROUND_BLUR);
    REQUIRE(backdrop_it != words.end());
    const std::size_t backdrop_index = static_cast<std::size_t>(std::distance(words.begin(), backdrop_it));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[backdrop_index + 3U]) == Approx(9.0f));
}

TEST_CASE("v2 ui runtime emits retained drop shadow commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_drop_shadow(root, 0x00000044U, 2.0f, 6.0f, 18.0f, 4.0f);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_DROP_SHADOW) == 1U);
    const auto shadow_it = std::find(words.begin(), words.end(), CMD_SET_DROP_SHADOW);
    REQUIRE(shadow_it != words.end());
    const std::size_t shadow_index = static_cast<std::size_t>(std::distance(words.begin(), shadow_it));
    CHECK(words[shadow_index + 3U] == 0x00000044U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 4U]) == Approx(2.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 5U]) == Approx(6.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 6U]) == Approx(18.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 7U]) == Approx(4.0f));
}

TEST_CASE("v2 ui runtime emits retained image and svg commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t image = ui_create_node(UI_NODE_IMAGE);
    const std::uint64_t svg = ui_create_node(UI_NODE_SVG);
    REQUIRE(image != UI_INVALID_HANDLE);
    REQUIRE(svg != UI_INVALID_HANDLE);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(image, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(image, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(svg, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(svg, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, image);
    ui_node_add_child(root, svg);

    ui_set_image(image, 21U, ED_OBJECT_FIT_COVER);
    ui_set_svg(svg, 22U, 0xff1122ffU);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_IMAGE) == 1U);
    CHECK(CountCommand(words, CMD_SET_SVG) == 1U);

    const auto image_it = std::find(words.begin(), words.end(), CMD_SET_IMAGE);
    REQUIRE(image_it != words.end());
    const std::size_t image_index = static_cast<std::size_t>(std::distance(words.begin(), image_it));
    CHECK(words[image_index + 3U] == 21U);
    CHECK(words[image_index + 4U] == ED_OBJECT_FIT_COVER);

    const auto svg_it = std::find(words.begin(), words.end(), CMD_SET_SVG);
    REQUIRE(svg_it != words.end());
    const std::size_t svg_index = static_cast<std::size_t>(std::distance(words.begin(), svg_it));
    CHECK(words[svg_index + 3U] == 22U);
    CHECK(words[svg_index + 4U] == 0xff1122ffU);

    ui_set_svg(svg, 0U, 0xff1122ffU);
    ui_commit_frame();
    const std::vector<std::uint32_t> cleared_words = ReadCommandBuffer();
    CHECK(CountCommand(cleared_words, CMD_SET_SVG) == 1U);
    const auto cleared_svg_it = std::find(cleared_words.begin(), cleared_words.end(), CMD_SET_SVG);
    REQUIRE(cleared_svg_it != cleared_words.end());
    const std::size_t cleared_svg_index = static_cast<std::size_t>(std::distance(cleared_words.begin(), cleared_svg_it));
    CHECK(cleared_words[cleared_svg_index + 3U] == 0U);
    CHECK(cleared_words[cleared_svg_index + 4U] == 0xff1122ffU);
}

TEST_CASE("v2 ui runtime emits retained nine-patch image commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t image = ui_create_node(UI_NODE_IMAGE);
    REQUIRE(image != UI_INVALID_HANDLE);
    ui_set_root(image);
    ui_set_width(image, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(image, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_image_nine(image, 33U, 4.0f, 5.0f, 6.0f, 7.0f);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_IMAGE_NINE) == 1U);
    const auto image_nine_it = std::find(words.begin(), words.end(), CMD_SET_IMAGE_NINE);
    REQUIRE(image_nine_it != words.end());
    const std::size_t image_nine_index = static_cast<std::size_t>(std::distance(words.begin(), image_nine_it));
    CHECK(words[image_nine_index + 3U] == 33U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[image_nine_index + 4U]) == Approx(4.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[image_nine_index + 7U]) == Approx(7.0f));
}

TEST_CASE("v2 ui commit without a root flushes pending creates and empty scene commits", "[v2][ui][unit]") {
    using effindom::v2::ui::CommandBuilder;

    ui_reset();

    std::uint32_t empty_word_count = 77U;
    CHECK(ui_get_command_buffer(&empty_word_count) == nullptr);
    CHECK(empty_word_count == 0U);
    CHECK(ui_get_command_buffer(nullptr) == nullptr);
    std::uint32_t empty_semantic_word_count = 55U;
    CHECK(ui_get_semantic_buffer(&empty_semantic_word_count) == nullptr);
    CHECK(empty_semantic_word_count == 0U);
    CHECK(ui_get_semantic_buffer(nullptr) == nullptr);

    const std::uint64_t handle = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(handle != UI_INVALID_HANDLE);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    std::vector<std::uint32_t> expected{};
    CommandBuilder builder(expected);
    builder.CreateNode(handle);
    builder.CommitPaintOrder({});
    builder.CommitScene({});

    CHECK(words == expected);
    CHECK(ReadSemanticBuffer() == std::vector<std::uint32_t>{0U});
}

TEST_CASE("v2 ui yoga layout centres two children in a row", "[v2][ui][layout]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child1 = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child2 = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child1 != UI_INVALID_HANDLE);
    REQUIRE(child2 != UI_INVALID_HANDLE);

    ui_resize_window(800.0f, 600.0f);
    ui_set_root(root);
    ui_set_width(root, 800.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 600.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_justify_content(root, 2U);

    ui_set_width(child1, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child1, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_bg_color(child1, 0xFF0000FFU);

    ui_set_width(child2, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child2, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_bg_color(child2, 0x0000FFFFU);

    ui_node_add_child(root, child1);
    ui_node_add_child(root, child2);
    ui_commit_frame();

    const std::vector<std::uint32_t> words = ReadCommandBuffer();
    const auto bounds = ReadBounds(words);
    REQUIRE(bounds.find(child1) != bounds.end());
    REQUIRE(bounds.find(child2) != bounds.end());

    CHECK(CountCommand(words, CMD_CREATE_NODE) == 3U);
    CHECK(bounds.at(child1).x == Approx(300.0f));
    CHECK(bounds.at(child2).x == Approx(400.0f));
    CHECK(bounds.at(child1).width == Approx(100.0f));
    CHECK(bounds.at(child1).height == Approx(100.0f));
    CHECK(bounds.at(child2).width == Approx(100.0f));
    CHECK(bounds.at(child2).height == Approx(100.0f));

    CHECK(GetRuntime().root_handle() == root);
    CHECK(GetRuntime().window_width() == Approx(800.0f));
    CHECK(GetRuntime().window_height() == Approx(600.0f));
}

TEST_CASE("v2 ui yoga centres a single-line text node and outputs glyph run", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(500.0f, 500.0f);
    ui_set_width(root, 500.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 500.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 0U);
    ui_set_justify_content(root, 2U);
    ui_set_align_items(root, 2U);

    ui_set_font(text, 1U, 24.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("AVA"), 3U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const std::vector<std::uint32_t> words = ReadCommandBuffer();
    const auto bounds = ReadBounds(words);
    const auto glyph_runs = ReadGlyphRuns(words);

    REQUIRE(bounds.find(text) != bounds.end());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());

    const Bounds& tb = bounds.at(text);
    const float left_margin = tb.x;
    const float right_margin = 500.0f - (tb.x + tb.width);
    CHECK(left_margin == Approx(right_margin).margin(1.0f));

    CHECK(glyph_runs.at(text).font_id == 1U);
    CHECK(glyph_runs.at(text).font_size == Approx(24.0f));
    CHECK(glyph_runs.at(text).glyphs.size() == 3U);
}

TEST_CASE("v2 ui positions flex children using margins", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t first = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t second = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_width(first, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(first, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(second, 30.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(second, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(second, 12.0f, 4.0f, 0.0f, 0.0f);
    ui_node_add_child(root, first);
    ui_node_add_child(root, second);

    ui_commit_frame();
    auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(first) != bounds.end());
    REQUIRE(bounds.find(second) != bounds.end());
    CHECK(bounds.at(first).x == Approx(0.0f));
    CHECK(bounds.at(second).x == Approx(32.0f));
    CHECK(bounds.at(second).y == Approx(4.0f));

    ui_set_margin(second, 24.0f, 8.0f, 0.0f, 0.0f);
    ui_commit_frame();
    bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(second) != bounds.end());
    CHECK(bounds.at(second).x == Approx(44.0f));
    CHECK(bounds.at(second).y == Approx(8.0f));
}

TEST_CASE("v2 ui wraps multi-line text into one glyph run", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "The quick brown fox jumps over the lazy dog";
    ui_set_root(root);
    ui_resize_window(160.0f, 320.0f);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);
    ui_commit_frame();

    const std::vector<std::uint32_t> words = ReadCommandBuffer();
    const auto bounds = ReadBounds(words);
    const auto glyph_runs = ReadGlyphRuns(words);
    REQUIRE(bounds.find(text) != bounds.end());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());

    std::set<int> baselines{};
    for (const auto& glyph : glyph_runs.at(text).glyphs) {
        baselines.insert(static_cast<int>(std::lround(glyph.y * 10.0f)));
    }

    CHECK(CountCommand(words, CMD_SET_GLYPH_RUN) == 1U);
    CHECK(baselines.size() > 1U);
    CHECK(bounds.at(text).height > 20.0f * 1.5f);
}

TEST_CASE("v2 ui centres each wrapped line within the text node bounds", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Pack my box with five dozen liquor jugs";
    ui_set_root(root);
    ui_resize_window(140.0f, 320.0f);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_align(text, effindom::v2::ui::ALIGN_CENTER);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());

    std::unordered_map<int, std::pair<float, float>> extents_by_line{};
    for (const auto& glyph : glyph_runs.at(text).glyphs) {
        const int baseline = static_cast<int>(std::lround(glyph.y * 10.0f));
        auto [it, inserted] = extents_by_line.emplace(baseline, std::make_pair(glyph.x, glyph.x));
        if (!inserted) {
            it->second.first = std::min(it->second.first, glyph.x);
            it->second.second = std::max(it->second.second, glyph.x);
        }
    }

    REQUIRE(extents_by_line.size() > 1U);
    bool saw_center_offset = false;
    for (const auto& [baseline, extents] : extents_by_line) {
        (void)baseline;
        saw_center_offset = saw_center_offset || extents.first > 0.5f;
        CHECK(extents.second < 80.0f);
    }
    CHECK(saw_center_offset);
}

TEST_CASE("v2 ui caps wrapped text height and emits bottom fade when max lines clip", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Sphinx of black quartz judge my vow while waves crash against the paragraph edge";
    ui_set_root(root);
    ui_resize_window(140.0f, 320.0f);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_limits(text, 0, 2);
    ui_set_text_overflow(text, effindom::v2::ui::OVERFLOW_FADE);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);
    ui_commit_frame();

    const std::vector<std::uint32_t> words = ReadCommandBuffer();
    const auto bounds = ReadBounds(words);
    const auto glyph_runs = ReadGlyphRuns(words);
    const auto fades = ReadTextFades(words);
    REQUIRE(bounds.find(text) != bounds.end());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());
    REQUIRE(fades.find(text) != fades.end());

    std::set<int> baselines{};
    for (const auto& glyph : glyph_runs.at(text).glyphs) {
        baselines.insert(static_cast<int>(std::lround(glyph.y * 10.0f)));
    }

    CHECK(baselines.size() == 2U);
    CHECK(bounds.at(text).height == Approx(48.0f).margin(1.0f));
    CHECK(fades.at(text) == ED_FADE_BOTTOM);
}

TEST_CASE("v2 ui right-aligns wrapped text within the node bounds", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Wrap me on the right edge please";
    ui_set_root(root);
    ui_resize_window(180.0f, 200.0f);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_align(text, effindom::v2::ui::ALIGN_RIGHT);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());

    float max_x = 0.0f;
    for (const auto& glyph : glyph_runs.at(text).glyphs) {
        max_x = std::max(max_x, glyph.x);
    }
    CHECK(max_x > 40.0f);
    CHECK(max_x < 80.0f);
}

TEST_CASE("v2 ui repositions wrapped text after dynamic alignment changes", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Wrap me on the right edge please";
    ui_set_root(root);
    ui_resize_window(180.0f, 200.0f);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);

    const auto max_line_start = [&](void) {
        const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
        REQUIRE(glyph_runs.find(text) != glyph_runs.end());
        std::unordered_map<int, float> line_starts{};
        for (const auto& glyph : glyph_runs.at(text).glyphs) {
            const int baseline = static_cast<int>(std::lround(glyph.y * 10.0f));
            auto [it, inserted] = line_starts.emplace(baseline, glyph.x);
            if (!inserted) {
                it->second = std::min(it->second, glyph.x);
            }
        }
        REQUIRE(line_starts.size() > 1U);
        float max_start = 0.0f;
        for (const auto& [baseline, start] : line_starts) {
            (void)baseline;
            max_start = std::max(max_start, start);
        }
        return max_start;
    };

    ui_commit_frame();
    CHECK(max_line_start() < 0.5f);

    ui_set_text_align(text, effindom::v2::ui::ALIGN_RIGHT);
    ui_commit_frame();
    CHECK(max_line_start() > 8.0f);
}

TEST_CASE("v2 ui vertically aligns text within a taller text node", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Center me";
    ui_set_root(root);
    ui_resize_window(220.0f, 200.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);

    const auto min_glyph_y = [&](std::uint32_t align) {
        ui_set_text_vertical_align(text, align);
        ui_commit_frame();
        const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
        REQUIRE(glyph_runs.find(text) != glyph_runs.end());
        float min_y = std::numeric_limits<float>::max();
        for (const auto& glyph : glyph_runs.at(text).glyphs) {
            min_y = std::min(min_y, glyph.y);
        }
        return min_y;
    };

    const float top_min_y = min_glyph_y(effindom::v2::ui::VERTICAL_ALIGN_TOP);
    const float center_min_y = min_glyph_y(effindom::v2::ui::VERTICAL_ALIGN_CENTER);
    const float bottom_min_y = min_glyph_y(effindom::v2::ui::VERTICAL_ALIGN_BOTTOM);

    CHECK(center_min_y > top_min_y + 20.0f);
    CHECK(bottom_min_y > center_min_y + 20.0f);
}

TEST_CASE("v2 ui updates bottom fade after dynamic overflow changes", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Sphinx of black quartz judge my vow while waves crash against the paragraph edge";
    ui_set_root(root);
    ui_resize_window(140.0f, 320.0f);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_limits(text, 0, 2);
    ui_set_text_overflow(text, effindom::v2::ui::OVERFLOW_CLIP);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);

    ui_commit_frame();
    auto fades = ReadTextFades(ReadCommandBuffer());
    REQUIRE(fades.find(text) != fades.end());
    CHECK(fades.at(text) == ED_FADE_NONE);

    ui_set_text_overflow(text, effindom::v2::ui::OVERFLOW_FADE);
    ui_commit_frame();
    fades = ReadTextFades(ReadCommandBuffer());
    REQUIRE(fades.find(text) != fades.end());
    CHECK(fades.at(text) == ED_FADE_BOTTOM);
}

TEST_CASE("v2 ui updates axis-based text fades without changing glyph runs", "[v2][ui][layout][text]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample =
        "Sphinx of black quartz judge my vow\nwhile waves crash against the paragraph edge";
    ui_set_root(root);
    ui_resize_window(180.0f, 140.0f);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text_overflow(text, effindom::v2::ui::OVERFLOW_CLIP);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);

    const auto glyphs_for = [&]() {
        const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
        REQUIRE(glyph_runs.find(text) != glyph_runs.end());
        return glyph_runs.at(text).glyphs;
    };
    const auto expect_same_glyphs = [&](const std::vector<effindom::v2::ui::GlyphPlacement>& expected) {
        const auto glyphs = glyphs_for();
        REQUIRE(glyphs.size() == expected.size());
        for (std::size_t index = 0; index < glyphs.size(); index += 1U) {
            CHECK(glyphs[index].glyph_id == expected[index].glyph_id);
            CHECK(glyphs[index].x == Approx(expected[index].x));
            CHECK(glyphs[index].y == Approx(expected[index].y));
            CHECK(glyphs[index].font_id == expected[index].font_id);
            CHECK(glyphs[index].color == expected[index].color);
        }
    };

    ui_commit_frame();
    auto fades = ReadTextFades(ReadCommandBuffer());
    REQUIRE(fades.find(text) != fades.end());
    CHECK(fades.at(text) == ED_FADE_NONE);
    const auto baseline_glyphs = glyphs_for();

    ui_set_text_overflow_fade(text, true, false);
    ui_commit_frame();
    fades = ReadTextFades(ReadCommandBuffer());
    REQUIRE(fades.find(text) != fades.end());
    CHECK(fades.at(text) == ED_FADE_RIGHT);
    expect_same_glyphs(baseline_glyphs);

    ui_set_text_overflow_fade(text, false, true);
    ui_commit_frame();
    fades = ReadTextFades(ReadCommandBuffer());
    REQUIRE(fades.find(text) != fades.end());
    CHECK(fades.at(text) == ED_FADE_BOTTOM);
    expect_same_glyphs(baseline_glyphs);

    ui_set_text_overflow_fade(text, true, true);
    ui_commit_frame();
    fades = ReadTextFades(ReadCommandBuffer());
    REQUIRE(fades.find(text) != fades.end());
    CHECK(fades.at(text) == (ED_FADE_RIGHT | ED_FADE_BOTTOM));
    expect_same_glyphs(baseline_glyphs);
}

TEST_CASE("v2 ui obscured text swaps rendered glyph ids to bullets without changing layout", "[v2][ui][layout][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Password123";
    ui_set_root(root);
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 24.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto plain_bounds = ReadBounds(ReadCommandBuffer());
    const auto plain_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(plain_runs.find(text) != plain_runs.end());
    REQUIRE(plain_bounds.find(text) != plain_bounds.end());
    const auto plain_glyphs = plain_runs.at(text).glyphs;
    const float plain_width = plain_bounds.at(text).width;
    const float plain_height = plain_bounds.at(text).height;

    ui_set_text_obscured(text, true);
    ui_commit_frame();
    const auto obscured_bounds = ReadBounds(ReadCommandBuffer());
    const auto obscured_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(obscured_runs.find(text) != obscured_runs.end());
    REQUIRE(obscured_bounds.find(text) != obscured_bounds.end());
    const auto* font = GetRuntime().LookupFont(1U);
    REQUIRE(font != nullptr);
    REQUIRE(font->bullet_glyph_id.has_value());
    REQUIRE(obscured_runs.at(text).glyphs.size() == plain_glyphs.size());
    for (const auto& glyph : obscured_runs.at(text).glyphs) {
        CHECK(glyph.glyph_id == *font->bullet_glyph_id);
    }
    CHECK(obscured_bounds.at(text).width == Approx(plain_width));
    CHECK(obscured_bounds.at(text).height == Approx(plain_height));

    ui_set_text_obscured(text, false);
    ui_commit_frame();
    const auto restored_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(restored_runs.find(text) != restored_runs.end());
    REQUIRE(restored_runs.at(text).glyphs.size() == plain_glyphs.size());
    for (std::size_t index = 0; index < plain_glyphs.size(); index += 1U) {
        CHECK(restored_runs.at(text).glyphs[index].glyph_id == plain_glyphs[index].glyph_id);
    }
}

TEST_CASE("v2 ui missing coverage uses placeholder glyphs with stable clusters", "[v2][ui][layout][text][tofu-phase2]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    test_ui_support::ResetInteractionLogs();
    RegisterBridgeBodyTestFont();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = u8"กขค";
    ui_set_root(root);
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 24.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);
    ui_commit_frame();

    REQUIRE(test_ui_support::g_missing_font_coverage_requests.size() == 1U);
    CHECK(test_ui_support::g_missing_font_coverage_requests[0].font_id == 1U);
    CHECK(test_ui_support::g_missing_font_coverage_requests[0].coverage_kind == UI_MISSING_FONT_COVERAGE_THAI);
    CHECK(test_ui_support::g_missing_font_coverage_requests[0].sample_text == kSample);

    const auto runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(runs.find(text) != runs.end());
    CHECK(runs.at(text).glyphs.size() == 3U);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    const auto* line = GetRuntime().EnsureWrappedVisualLineShape(*node, 0U);
    REQUIRE(line != nullptr);
    REQUIRE(line->glyphs.size() == 3U);
    CHECK(line->width > 0.0f);
    CHECK(line->glyphs[0].cluster == 0U);
    CHECK(line->glyphs[1].cluster == 3U);
    CHECK(line->glyphs[2].cluster == 6U);
    for (const auto& glyph : line->glyphs) {
        CHECK(glyph.font_id == 1U);
    }
}

TEST_CASE("v2 ui supplementary missing coverage reports the sample text once per segment", "[v2][ui][layout][text][tofu-phase3]") {
    ui_reset();
    test_ui_support::ResetInteractionLogs();
    RegisterBridgeBodyTestFont();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = u8"काला";
    ui_set_root(root);
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 24.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_node_add_child(root, text);
    ui_commit_frame();

    REQUIRE(test_ui_support::g_missing_font_coverage_requests.size() == 1U);
    CHECK(test_ui_support::g_missing_font_coverage_requests[0].font_id == 1U);
    CHECK(test_ui_support::g_missing_font_coverage_requests[0].coverage_kind == UI_MISSING_FONT_COVERAGE_SUPPLEMENTAL);
    CHECK(test_ui_support::g_missing_font_coverage_requests[0].sample_text == kSample);
}

TEST_CASE("v2 ui decodes WOFF2 fallback shards before shaping", "[v2][ui][layout][text][tofu-phase3]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    test_ui_support::ResetInteractionLogs();
    RegisterBridgeBodyTestFont();

    const auto fallback_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/ui/tests/fixtures/noto-sans-sc-cjk-subset.woff2");
    REQUIRE(GetRuntime().RegisterFont(42U, fallback_bytes.data(), static_cast<std::uint32_t>(fallback_bytes.size())));
    REQUIRE(GetRuntime().RegisterFontFallback(1U, 42U));

    effindom::v2::ui::UiRuntime::ShapedTextRun shaped{};
    constexpr const char* kSample = u8"你好，你好吗？";
    REQUIRE(GetRuntime().ShapeText(kSample, 1U, 24.0f, shaped));
    REQUIRE(shaped.glyphs.size() == 7U);
    CHECK(test_ui_support::g_missing_font_coverage_requests.empty());
    for (const auto& glyph : shaped.glyphs) {
        CHECK(glyph.font_id == 42U);
    }
}

TEST_CASE("v2 ui can unregister fallback shards and restore missing coverage reporting", "[v2][ui][layout][text][tofu-phase4]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    test_ui_support::ResetInteractionLogs();
    RegisterBridgeBodyTestFont();

    const auto fallback_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/ui/tests/fixtures/noto-sans-sc-cjk-subset.woff2");
    REQUIRE(GetRuntime().RegisterFont(42U, fallback_bytes.data(), static_cast<std::uint32_t>(fallback_bytes.size())));
    REQUIRE(GetRuntime().RegisterFontFallback(1U, 42U));

    effindom::v2::ui::UiRuntime::ShapedTextRun shaped{};
    constexpr const char* kSample = u8"你好";
    REQUIRE(GetRuntime().ShapeText(kSample, 1U, 24.0f, shaped));
    CHECK(test_ui_support::g_missing_font_coverage_requests.empty());
    for (const auto& glyph : shaped.glyphs) {
        CHECK(glyph.font_id == 42U);
    }

    REQUIRE(GetRuntime().UnregisterFontFallback(1U, 42U));
    test_ui_support::ResetInteractionLogs();
    shaped = {};
    REQUIRE(GetRuntime().ShapeText(kSample, 1U, 24.0f, shaped));
    REQUIRE(test_ui_support::g_missing_font_coverage_requests.size() == 1U);
    CHECK(test_ui_support::g_missing_font_coverage_requests[0].coverage_kind == UI_MISSING_FONT_COVERAGE_CJK);
    for (const auto& glyph : shaped.glyphs) {
        CHECK(glyph.font_id == 1U);
    }

    REQUIRE(GetRuntime().UnregisterFont(42U));
    CHECK_FALSE(GetRuntime().UnregisterFontFallback(1U, 42U));
    CHECK_FALSE(GetRuntime().UnregisterFont(42U));
}

TEST_CASE("v2 ui second unchanged rooted frame only emits retained scene commits", "[v2][ui][unit]") {
    using effindom::v2::ui::CommandBuilder;
    using effindom::v2::ui::SceneInstruction;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(child, 25.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child, 25.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, child);

    ui_commit_frame();
    const std::vector<std::uint32_t> first_frame = ReadCommandBuffer();
    REQUIRE_FALSE(first_frame.empty());

    ui_commit_frame();
    const std::vector<std::uint32_t> second_frame = ReadCommandBuffer();

    std::vector<std::uint32_t> expected{};
    CommandBuilder builder(expected);
    builder.CommitPaintOrder({root, child});
    builder.CommitScene({
        SceneInstruction{OP_DRAW_NODE, root},
        SceneInstruction{OP_DRAW_NODE, child},
    });

    CHECK(second_frame == expected);
    CHECK(second_frame.size() < first_frame.size());
}

TEST_CASE("v2 ui dirty tracking and deletions stay local to changed nodes", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(child, 25.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child, 25.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, child);

    ui_commit_frame();
    REQUIRE(GetRuntime().SetNodeColor(child, 0x00FF00FFU));

    ui_commit_frame();
    const std::vector<std::uint32_t> dirty_words = ReadCommandBuffer();
    const auto dirty_bounds = ReadBounds(dirty_words);
    CHECK(CountCommand(dirty_words, CMD_SET_BOUNDS) == 1U);
    CHECK(dirty_bounds.at(child).width == Approx(25.0f));
    CHECK(CountCommand(dirty_words, CMD_SET_BOX_STYLE) == 1U);

    ui_delete_node(child);
    ui_commit_frame();
    const std::vector<std::uint32_t> deleted_frame = ReadCommandBuffer();
    CHECK(CountCommand(deleted_frame, CMD_DELETE_NODE) == 1U);
    CHECK(CountCommand(deleted_frame, CMD_SET_BOUNDS) == 1U);

    ui_commit_frame();
    const std::vector<std::uint32_t> steady_frame = ReadCommandBuffer();
    CHECK(CountCommand(steady_frame, CMD_DELETE_NODE) == 0U);
    CHECK(CountCommand(steady_frame, CMD_SET_BOUNDS) == 0U);
}

TEST_CASE("v2 ui hierarchy operations support reparenting and invalid input", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PackHandle;

    ui_reset();

    const std::uint64_t root_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t root_b = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root_a != UI_INVALID_HANDLE);
    REQUIRE(root_b != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    CHECK_FALSE(GetRuntime().AddChild(root_a, root_a));
    CHECK_FALSE(GetRuntime().AddChild(root_a, UI_INVALID_HANDLE));
    CHECK(GetRuntime().AddChild(root_a, child));
    CHECK(GetRuntime().AddChild(root_a, child));
    CHECK(GetRuntime().AddChild(root_b, child));

    const effindom::v2::ui::UINode* root_a_node = GetRuntime().Resolve(root_a);
    const effindom::v2::ui::UINode* root_b_node = GetRuntime().Resolve(root_b);
    const effindom::v2::ui::UINode* child_node = GetRuntime().Resolve(child);
    REQUIRE(root_a_node != nullptr);
    REQUIRE(root_b_node != nullptr);
    REQUIRE(child_node != nullptr);
    CHECK(root_a_node->children.empty());
    REQUIRE(root_b_node->children.size() == 1U);
    CHECK(root_b_node->children.front() == child);
    CHECK(child_node->parent_handle == root_b);

    CHECK_FALSE(GetRuntime().RemoveChild(PackHandle(0U, 1U), child));
    CHECK_FALSE(GetRuntime().RemoveChild(root_a, child));
    ui_node_remove_child(root_b, child);
    CHECK_FALSE(GetRuntime().RemoveChild(root_b, child));
    CHECK(GetRuntime().Resolve(child)->parent_handle == UI_INVALID_HANDLE);
}

TEST_CASE("v2 ui node ids stay unique and reusable across active nodes", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t first = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t second = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    CHECK(GetRuntime().SetNodeId(first, reinterpret_cast<const std::uint8_t*>("alpha"), 5U));
    REQUIRE(GetRuntime().Resolve(first) != nullptr);
    CHECK(GetRuntime().Resolve(first)->node_id == "alpha");
    REQUIRE(GetRuntime().node_id_map_.find("alpha") != GetRuntime().node_id_map_.end());
    CHECK(GetRuntime().node_id_map_.at("alpha") == first);

    CHECK_FALSE(GetRuntime().SetNodeId(second, reinterpret_cast<const std::uint8_t*>("alpha"), 5U));
    CHECK(GetRuntime().SetNodeId(first, reinterpret_cast<const std::uint8_t*>("alpha"), 5U));

    CHECK(GetRuntime().SetNodeId(first, nullptr, 0U));
    CHECK(GetRuntime().Resolve(first)->node_id.empty());
    CHECK(GetRuntime().node_id_map_.find("alpha") == GetRuntime().node_id_map_.end());

    CHECK(GetRuntime().SetNodeId(second, reinterpret_cast<const std::uint8_t*>("alpha"), 5U));
    CHECK(GetRuntime().node_id_map_.at("alpha") == second);

    CHECK(GetRuntime().DeleteNode(second));
    CHECK(GetRuntime().node_id_map_.find("alpha") == GetRuntime().node_id_map_.end());

    const std::uint64_t third = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(third != UI_INVALID_HANDLE);
    GetRuntime().node_id_map_["ghost"] = second;
    CHECK(GetRuntime().SetNodeId(third, reinterpret_cast<const std::uint8_t*>("ghost"), 5U));
    CHECK(GetRuntime().Resolve(third)->node_id == "ghost");
    CHECK(GetRuntime().node_id_map_.at("ghost") == third);

    CHECK_FALSE(GetRuntime().SetNodeId(first, nullptr, 1U));
}

TEST_CASE("v2 ui restores focus across root replacement for matching node ids", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t first_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first_target = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(first_root != UI_INVALID_HANDLE);
    REQUIRE(first_target != UI_INVALID_HANDLE);
    REQUIRE(GetRuntime().AddChild(first_root, first_target));
    REQUIRE(GetRuntime().SetNodeId(first_target, reinterpret_cast<const std::uint8_t*>("focus-target"), 12U));
    REQUIRE(GetRuntime().SetInteractive(first_target, true));
    REQUIRE(GetRuntime().SetFocusable(first_target, true, 0));
    REQUIRE(GetRuntime().SetRoot(first_root));

    GetRuntime().SetFocus(first_target);
    REQUIRE(GetRuntime().focused_handle_ == first_target);

    ResetInteractionLogs();
    REQUIRE(GetRuntime().DeleteNode(first_target));
    REQUIRE(GetRuntime().DeleteNode(first_root));

    const std::uint64_t second_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t second_target = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(second_root != UI_INVALID_HANDLE);
    REQUIRE(second_target != UI_INVALID_HANDLE);
    REQUIRE(GetRuntime().AddChild(second_root, second_target));
    REQUIRE(GetRuntime().SetNodeId(second_target, reinterpret_cast<const std::uint8_t*>("focus-target"), 12U));
    REQUIRE(GetRuntime().SetInteractive(second_target, true));
    REQUIRE(GetRuntime().SetFocusable(second_target, true, 0));

    REQUIRE(GetRuntime().SetRoot(second_root));

    CHECK(GetRuntime().focused_handle_ == second_target);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[0].handle == first_target);
    CHECK_FALSE(g_focus_events[0].is_focused);
    CHECK(g_focus_events[1].handle == second_target);
    CHECK(g_focus_events[1].is_focused);
}

TEST_CASE("v2 ui text and layout setters cover enums, fonts, measurement, and stale handles", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PackHandle;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
    ui_register_font(0U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
    ui_register_font(2U, nullptr, static_cast<std::uint32_t>(font_bytes.size()));
    ui_register_font(3U, font_bytes.data(), 0U);

    const std::uint64_t box = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    REQUIRE(box != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);

    CHECK(GetRuntime().SetRoot(UI_INVALID_HANDLE));
    CHECK(GetRuntime().root_handle() == UI_INVALID_HANDLE);
    CHECK_FALSE(GetRuntime().SetRoot(PackHandle(0U, 1U)));
    CHECK(GetRuntime().SetRoot(box));

    CHECK(GetRuntime().SetWidth(box, 50.0f, UI_SIZE_UNIT_PIXEL));
    CHECK(GetRuntime().SetWidth(box, 20.0f, UI_SIZE_UNIT_AUTO));
    CHECK(GetRuntime().SetWidth(box, 75.0f, UI_SIZE_UNIT_PERCENT));
    CHECK_FALSE(GetRuntime().SetWidth(box, 1.0f, 99U));

    CHECK(GetRuntime().SetHeight(box, 50.0f, UI_SIZE_UNIT_PIXEL));
    CHECK(GetRuntime().SetHeight(box, 20.0f, UI_SIZE_UNIT_AUTO));
    CHECK(GetRuntime().SetHeight(box, 75.0f, UI_SIZE_UNIT_PERCENT));
    CHECK_FALSE(GetRuntime().SetHeight(box, 1.0f, 99U));

    CHECK(GetRuntime().SetFlexDirection(box, 0U));
    CHECK(GetRuntime().SetFlexDirection(box, 1U));
    CHECK_FALSE(GetRuntime().SetFlexDirection(box, 99U));

    CHECK(GetRuntime().SetJustifyContent(box, 0U));
    CHECK(GetRuntime().SetJustifyContent(box, 1U));
    CHECK(GetRuntime().SetJustifyContent(box, 2U));
    CHECK(GetRuntime().SetJustifyContent(box, 3U));
    CHECK_FALSE(GetRuntime().SetJustifyContent(box, 99U));

    ui_set_align_items(box, 2U);
    CHECK(GetRuntime().SetAlignItems(box, 0U));
    CHECK(GetRuntime().SetAlignItems(box, 1U));
    CHECK(GetRuntime().SetAlignItems(box, 2U));
    CHECK(GetRuntime().SetAlignItems(box, 3U));
    CHECK(GetRuntime().SetAlignItems(box, 4U));
    CHECK_FALSE(GetRuntime().SetAlignItems(box, 99U));

    ui_set_padding(box, 1.0f, 2.0f, 3.0f, 4.0f);
    CHECK(GetRuntime().SetPadding(box, 1.0f, 2.0f, 3.0f, 4.0f));
    ui_set_margin(box, 5.0f, 6.0f, 7.0f, 8.0f);
    CHECK(GetRuntime().SetMargin(box, 5.0f, 6.0f, 7.0f, 8.0f));
    ui_set_clip_to_bounds(box, true);
    CHECK(GetRuntime().SetClipToBounds(box, true));
    ui_set_node_id(box, reinterpret_cast<const std::uint8_t*>("box"), 3U);
    ui_set_semantic_role(box, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(box, reinterpret_cast<const std::uint8_t*>("Button"), 6U);
    CHECK(GetRuntime().SetNodeId(box, reinterpret_cast<const std::uint8_t*>("box"), 3U));
    CHECK(GetRuntime().SetSemanticRole(box, UI_SEMANTIC_LINK));
    CHECK(GetRuntime().Resolve(box)->semantic_role == UI_SEMANTIC_LINK);
    CHECK(GetRuntime().SetSemanticLabel(box, reinterpret_cast<const std::uint8_t*>("Label"), 5U));
    CHECK(GetRuntime().Resolve(box)->semantic_label == "Label");
    CHECK(GetRuntime().SetSemanticLabel(box, nullptr, 0U));
    CHECK(GetRuntime().Resolve(box)->semantic_label.empty());
    ui_set_width(box, 7.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(box, 8.0f, UI_SIZE_UNIT_PIXEL);
    CHECK(GetRuntime().SetNodeColor(box, 0x12345678U));
    CHECK(GetRuntime().SetFlexGrow(box, 1.0f));
    CHECK(YGNodeStyleGetFlexGrow(GetRuntime().Resolve(box)->yg_node) == Approx(1.0f));

    ui_set_font(text, 1U, 24.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("AVA"), 3U);
    ui_set_text(text, nullptr, 0U);
    ui_set_text_color(text, 0xFF00FFFFU);
    ui_set_text_align(text, effindom::v2::ui::ALIGN_RIGHT);
    ui_set_text_limits(text, 3, 2);
    ui_set_text_overflow(text, effindom::v2::ui::OVERFLOW_FADE);
    ui_set_text_overflow_fade(text, true, false);
    ui_set_text_obscured(text, true);
    ui_set_interactive(box, true);
    ui_set_focusable(box, true, 0);
    ui_set_is_portal(box, true);
    const float grid_cols[] = {100.0f, 1.0f};
    const std::uint8_t grid_col_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_STAR};
    const float grid_rows[] = {1.0f, 40.0f};
    const std::uint8_t grid_row_types[] = {UI_GRID_UNIT_AUTO, UI_GRID_UNIT_PIXEL};
    ui_grid_set_columns(grid, 2U, grid_cols, grid_col_types);
    ui_grid_set_rows(grid, 2U, grid_rows, grid_row_types);
    ui_node_set_grid_placement(text, 1U, 0U, 2U, 3U);
    ui_set_scroll_offset(scroll, 3.0f, 4.0f);
    ui_set_selectable(text, true, 0x40112233U);
    ui_set_editable(text, true);
    ui_set_caret_color(text, 0xFF445566U);
    CHECK(GetRuntime().SetFont(text, 1U, 24.0f));
    CHECK(GetRuntime().SetText(text, reinterpret_cast<const std::uint8_t*>("AVA"), 3U));
    CHECK(GetRuntime().SetTextColor(text, 0x111111FFU));
    CHECK(GetRuntime().SetTextAlign(text, effindom::v2::ui::ALIGN_LEFT));
    CHECK(GetRuntime().SetTextAlign(text, effindom::v2::ui::ALIGN_CENTER));
    CHECK(GetRuntime().SetTextAlign(text, effindom::v2::ui::ALIGN_RIGHT));
    CHECK_FALSE(GetRuntime().SetTextAlign(text, 99U));
    CHECK(GetRuntime().SetTextVerticalAlign(text, effindom::v2::ui::VERTICAL_ALIGN_TOP));
    CHECK(GetRuntime().SetTextVerticalAlign(text, effindom::v2::ui::VERTICAL_ALIGN_CENTER));
    CHECK(GetRuntime().SetTextVerticalAlign(text, effindom::v2::ui::VERTICAL_ALIGN_BOTTOM));
    CHECK_FALSE(GetRuntime().SetTextVerticalAlign(text, 99U));
    CHECK(GetRuntime().SetTextLimits(text, 0, 0));
    CHECK(GetRuntime().SetTextLimits(text, 4, 2));
    CHECK(GetRuntime().SetTextWrapping(text, false));
    CHECK(GetRuntime().Resolve(text)->text_wrap == false);
    CHECK(GetRuntime().SetTextWrapping(text, true));
    CHECK_FALSE(GetRuntime().SetTextLimits(text, -1, 1));
    CHECK_FALSE(GetRuntime().SetTextLimits(text, 1, -1));
    CHECK(GetRuntime().SetTextOverflow(text, effindom::v2::ui::OVERFLOW_CLIP));
    CHECK(GetRuntime().SetTextOverflow(text, effindom::v2::ui::OVERFLOW_ELLIPSIS));
    CHECK(GetRuntime().SetTextOverflow(text, effindom::v2::ui::OVERFLOW_FADE));
    CHECK(GetRuntime().SetTextOverflowFade(text, true, false));
    CHECK(GetRuntime().Resolve(text)->text_overflow_fade_horizontal);
    CHECK_FALSE(GetRuntime().Resolve(text)->text_overflow_fade_vertical);
    CHECK(GetRuntime().SetTextObscured(text, true));
    CHECK(GetRuntime().Resolve(text)->is_obscured);
    CHECK(GetRuntime().SetTextObscured(text, false));
    CHECK(GetRuntime().SetInteractive(box, true));
    CHECK(GetRuntime().SetInteractive(box, false));
    CHECK(GetRuntime().SetFocusable(box, true, 0));
    CHECK(GetRuntime().SetFocusable(box, true, -1));
    CHECK(GetRuntime().SetFocusable(box, false, 3));
    CHECK(GetRuntime().SetPortal(box, true));
    CHECK(GetRuntime().SetPortal(box, false));
    CHECK(GetRuntime().SetGridColumns(grid, 2U, grid_cols, grid_col_types));
    CHECK(GetRuntime().SetGridRows(grid, 2U, grid_rows, grid_row_types));
    CHECK(GetRuntime().SetGridPlacement(text, 1U, 2U, 3U, 4U));
    CHECK(GetRuntime().Resolve(text)->grid_row == 1U);
    CHECK(GetRuntime().Resolve(text)->grid_col == 2U);
    CHECK(GetRuntime().Resolve(text)->grid_row_span == 3U);
    CHECK(GetRuntime().Resolve(text)->grid_col_span == 4U);
    CHECK(GetRuntime().SetScrollOffset(scroll, 5.0f, 6.0f));
    CHECK(GetRuntime().Resolve(scroll)->is_scroll_view);
    CHECK(GetRuntime().Resolve(grid)->is_grid);
    CHECK(GetRuntime().SetSelectable(text, true, 0x40112233U));
    CHECK(GetRuntime().SetEditable(text, true));
    CHECK(GetRuntime().Resolve(text)->is_selectable);
    CHECK(GetRuntime().SetEditable(text, false));
    CHECK(GetRuntime().SetSelectable(text, false, 0x40112233U));
    CHECK(GetRuntime().SetCaretColor(text, 0xFF445566U));
    GetRuntime().SetInteractionTime(4321U);
    CHECK(GetRuntime().interaction_time_ms_ == 4321U);
    CHECK_FALSE(GetRuntime().SetTextOverflow(text, 99U));
    CHECK_FALSE(GetRuntime().SetTextOverflowFade(box, true, false));
    CHECK_FALSE(GetRuntime().SetText(text, nullptr, 1U));
    CHECK_FALSE(GetRuntime().SetFont(box, 1U, 24.0f));
    CHECK_FALSE(GetRuntime().SetText(box, reinterpret_cast<const std::uint8_t*>("AVA"), 3U));
    CHECK_FALSE(GetRuntime().SetTextColor(box, 0xFFFFFFFFU));
    CHECK_FALSE(GetRuntime().SetTextAlign(box, effindom::v2::ui::ALIGN_LEFT));
    CHECK_FALSE(GetRuntime().SetTextVerticalAlign(box, effindom::v2::ui::VERTICAL_ALIGN_TOP));
    CHECK_FALSE(GetRuntime().SetTextLimits(box, 1, 1));
    CHECK_FALSE(GetRuntime().SetTextWrapping(box, true));
    CHECK_FALSE(GetRuntime().SetTextOverflow(box, effindom::v2::ui::OVERFLOW_CLIP));
    CHECK_FALSE(GetRuntime().SetTextObscured(box, true));
    CHECK_FALSE(GetRuntime().SetInteractive(PackHandle(0U, 1U), true));
    CHECK_FALSE(GetRuntime().SetFocusable(PackHandle(0U, 1U), true, 0));
    CHECK_FALSE(GetRuntime().SetClipToBounds(PackHandle(0U, 1U), true));
    CHECK_FALSE(GetRuntime().SetNodeId(PackHandle(0U, 1U), reinterpret_cast<const std::uint8_t*>("id"), 2U));
    CHECK_FALSE(GetRuntime().SetSemanticRole(PackHandle(0U, 1U), UI_SEMANTIC_BUTTON));
    CHECK_FALSE(GetRuntime().SetSemanticLabel(PackHandle(0U, 1U), reinterpret_cast<const std::uint8_t*>("id"), 2U));
    CHECK_FALSE(GetRuntime().SetPortal(PackHandle(0U, 1U), true));
    CHECK_FALSE(GetRuntime().SetGridColumns(PackHandle(0U, 1U), 2U, grid_cols, grid_col_types));
    CHECK_FALSE(GetRuntime().SetGridRows(PackHandle(0U, 1U), 2U, grid_rows, grid_row_types));
    CHECK_FALSE(GetRuntime().SetGridPlacement(PackHandle(0U, 1U), 0U, 0U, 1U, 1U));
    CHECK_FALSE(GetRuntime().SetScrollOffset(PackHandle(0U, 1U), 1.0f, 2.0f));
    CHECK_FALSE(GetRuntime().SetSelectable(box, true, 0x40FFFFFFU));
    CHECK_FALSE(GetRuntime().SetEditable(box, true));
    CHECK_FALSE(GetRuntime().SetCaretColor(box, 0xFFFFFFFFU));
    const std::uint8_t invalid_grid_types[] = {99U};
    CHECK_FALSE(GetRuntime().SetGridColumns(grid, 1U, grid_cols, invalid_grid_types));
    CHECK_FALSE(GetRuntime().SetGridRows(grid, 1U, grid_rows, invalid_grid_types));
    CHECK_FALSE(GetRuntime().SetGridColumns(box, 1U, grid_cols, grid_col_types));
    CHECK_FALSE(GetRuntime().SetGridRows(box, 1U, grid_rows, grid_row_types));
    CHECK_FALSE(GetRuntime().SetScrollOffset(box, 1.0f, 2.0f));
    CHECK_FALSE(GetRuntime().SetSemanticRole(box, 99U));
    CHECK_FALSE(GetRuntime().SetSemanticLabel(box, nullptr, 1U));
    CHECK_FALSE(GetRuntime().SetPadding(box, -1.0f, 0.0f, 0.0f, 0.0f));
    CHECK_FALSE(GetRuntime().SetPadding(box, 0.0f, NAN, 0.0f, 0.0f));
    CHECK_FALSE(GetRuntime().SetMargin(box, -1.0f, 0.0f, 0.0f, 0.0f));
    CHECK_FALSE(GetRuntime().SetMargin(box, 0.0f, NAN, 0.0f, 0.0f));

    float measured_width = -1.0f;
    float measured_height = -1.0f;
    ui_measure_text(
        reinterpret_cast<const std::uint8_t*>("AVA"),
        3U,
        1U,
        24.0f,
        200.0f,
        &measured_width,
        &measured_height);
    CHECK(measured_width > 0.0f);
    CHECK(measured_height == Approx(27.9375f).margin(0.01f));

    ui_measure_text(
        reinterpret_cast<const std::uint8_t*>("The quick brown fox"),
        19U,
        1U,
        24.0f,
        60.0f,
        &measured_width,
        &measured_height);
    CHECK(measured_width > 0.0f);
    CHECK(measured_height > 24.0f);

    ui_measure_text(nullptr, 3U, 1U, 24.0f, 200.0f, &measured_width, &measured_height);
    CHECK(measured_width == Approx(0.0f));
    CHECK(measured_height == Approx(0.0f));

    CHECK(GetRuntime().Resolve(PackHandle(0U, 1U)) == nullptr);
    CHECK_FALSE(GetRuntime().SetPadding(PackHandle(static_cast<std::uint32_t>(effindom::v2::ui::kMaxNodes), 1U), 0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(GetRuntime().Resolve(PackHandle(static_cast<std::uint32_t>(effindom::v2::ui::kMaxNodes), 1U)) == nullptr);
    CHECK_FALSE(GetRuntime().DeleteNode(PackHandle(0U, 1U)));

    ui_delete_node(text);
    CHECK_FALSE(GetRuntime().SetNodeId(text, reinterpret_cast<const std::uint8_t*>("dead"), 4U));
    CHECK_FALSE(GetRuntime().SetSemanticRole(text, UI_SEMANTIC_TEXTBOX));
    CHECK_FALSE(GetRuntime().SetSemanticLabel(text, reinterpret_cast<const std::uint8_t*>("dead"), 4U));
    CHECK_FALSE(GetRuntime().SetNodeColor(text, 0xFFFFFFFFU));
    CHECK_FALSE(GetRuntime().SetWidth(text, 1.0f, UI_SIZE_UNIT_PIXEL));
    CHECK_FALSE(GetRuntime().SetHeight(text, 1.0f, UI_SIZE_UNIT_PIXEL));
    CHECK_FALSE(GetRuntime().SetFlexDirection(text, 0U));
    CHECK_FALSE(GetRuntime().SetJustifyContent(text, 0U));
    CHECK_FALSE(GetRuntime().SetAlignItems(text, 0U));
    CHECK_FALSE(GetRuntime().SetFont(text, 1U, 12.0f));
    CHECK_FALSE(GetRuntime().SetText(text, reinterpret_cast<const std::uint8_t*>("A"), 1U));
    CHECK_FALSE(GetRuntime().SetTextColor(text, 0xFFFFFFFFU));
    CHECK_FALSE(GetRuntime().SetTextAlign(text, effindom::v2::ui::ALIGN_LEFT));
    CHECK_FALSE(GetRuntime().SetTextLimits(text, 0, 1));
    CHECK_FALSE(GetRuntime().SetTextOverflow(text, effindom::v2::ui::OVERFLOW_CLIP));
    CHECK_FALSE(GetRuntime().SetTextOverflowFade(text, true, false));
    CHECK_FALSE(GetRuntime().SetInteractive(text, true));
    CHECK_FALSE(GetRuntime().SetFocusable(text, true, 0));
    CHECK_FALSE(GetRuntime().SetClipToBounds(text, true));
    CHECK_FALSE(GetRuntime().SetPortal(text, true));
    CHECK_FALSE(GetRuntime().SetSelectable(text, true, 0x40000000U));
    CHECK_FALSE(GetRuntime().SetEditable(text, true));
    CHECK_FALSE(GetRuntime().SetCaretColor(text, 0xFFFFFFFFU));
    CHECK_FALSE(GetRuntime().SetTextObscured(text, true));
    CHECK(GetRuntime().Resolve(text) == nullptr);
}

TEST_CASE("v2 ui arena api resets and deleting root clears the tree", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    CHECK(ui_arena_alloc(0U) == 0U);

    const std::uintptr_t first = ui_arena_alloc(16U);
    const std::uintptr_t second = ui_arena_alloc(32U);
    REQUIRE(first != 0U);
    REQUIRE(second != 0U);
    CHECK(second > first);
    CHECK(ui_arena_alloc(static_cast<std::uint32_t>(effindom::v2::ui::kFrameArenaCapacity)) == 0U);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(child, 10.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child, 10.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, child);
    ui_commit_frame();
    CHECK(ui_arena_alloc(16U) == first);

    auto* mutable_root = const_cast<effindom::v2::ui::UINode*>(GetRuntime().Resolve(root));
    REQUIRE(mutable_root != nullptr);
    mutable_root->children.push_back(UI_INVALID_HANDLE);
    ui_commit_frame();

    ui_delete_node(root);
    CHECK(GetRuntime().root_handle() == UI_INVALID_HANDLE);
    CHECK(GetRuntime().Resolve(child)->parent_handle == UI_INVALID_HANDLE);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();
    CHECK(CountCommand(words, CMD_DELETE_NODE) == 1U);
    CHECK(CountCommand(words, CMD_COMMIT_PAINT_ORDER) == 1U);
    CHECK(CountCommand(words, CMD_COMMIT_SCENE) == 1U);

    ui_reset();
    CHECK(GetRuntime().root_handle() == UI_INVALID_HANDLE);
    CHECK(GetRuntime().window_width() == Approx(800.0f));
    CHECK(GetRuntime().window_height() == Approx(600.0f));
}

TEST_CASE("v2 ui portal children are deferred after later main-tree siblings", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_b = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_c = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_d = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child_a != UI_INVALID_HANDLE);
    REQUIRE(child_b != UI_INVALID_HANDLE);
    REQUIRE(child_c != UI_INVALID_HANDLE);
    REQUIRE(child_d != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_node_add_child(root, child_a);
    ui_node_add_child(root, child_b);
    ui_node_add_child(child_b, child_c);
    ui_node_add_child(root, child_d);
    ui_set_is_portal(child_b, true);

    ui_commit_frame();
    const auto paint_order = ReadPaintOrder(ReadCommandBuffer());

    REQUIRE(paint_order == std::vector<std::uint64_t>{root, child_a, child_b, child_d, child_c});
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_b) == 1);
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_c) == 1);
}

TEST_CASE("v2 ui portal children added after the initial frame still render in the deferred scene", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(portal, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_node_add_child(root, portal);
    ui_commit_frame();

    const std::uint64_t portal_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(portal_child != UI_INVALID_HANDLE);
    ui_set_width(portal_child, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal_child, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(portal, portal_child);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto portal_child_bounds = bounds.find(portal_child);
    REQUIRE(portal_child_bounds != bounds.end());
    CHECK(portal_child_bounds->second.x == Approx(0.0f));
    CHECK(portal_child_bounds->second.y == Approx(0.0f));

    const auto portal_scene = ReadScene(ReadCommandBuffer());
    REQUIRE(std::count_if(
                portal_scene.begin(),
                portal_scene.end(),
                [portal_child](const effindom::v2::ui::SceneInstruction& instruction) {
                    return instruction.opcode == OP_DRAW_NODE && instruction.handle == portal_child;
                }) == 1);
}

TEST_CASE("v2 ui portal children added after the initial frame keep absolute popup geometry", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t overlay = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t panel = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(overlay != UI_INVALID_HANDLE);
    REQUIRE(panel != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_set_position_type(portal, UI_POSITION_ABSOLUTE);
    ui_set_position(portal, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(portal, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, portal);
    ui_commit_frame();

    ui_set_position_type(overlay, UI_POSITION_ABSOLUTE);
    ui_set_position(overlay, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(overlay, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(overlay, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_position_type(panel, UI_POSITION_ABSOLUTE);
    ui_set_position(panel, 40.0f, 52.0f, NAN, NAN);
    ui_set_width(panel, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(panel, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(overlay, panel);
    ui_node_add_child(portal, overlay);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto overlay_bounds = bounds.find(overlay);
    const auto panel_bounds = bounds.find(panel);
    REQUIRE(overlay_bounds != bounds.end());
    REQUIRE(panel_bounds != bounds.end());
    CHECK(overlay_bounds->second.x == Approx(0.0f));
    CHECK(overlay_bounds->second.y == Approx(0.0f));
    CHECK(overlay_bounds->second.width == Approx(200.0f));
    CHECK(overlay_bounds->second.height == Approx(120.0f));
    CHECK(panel_bounds->second.x == Approx(40.0f));
    CHECK(panel_bounds->second.y == Approx(52.0f));
    CHECK(panel_bounds->second.width == Approx(120.0f));
    CHECK(panel_bounds->second.height == Approx(32.0f));

    const auto scene = ReadScene(ReadCommandBuffer());
    REQUIRE(std::count_if(
                scene.begin(),
                scene.end(),
                [overlay](const effindom::v2::ui::SceneInstruction& instruction) {
                    return instruction.opcode == OP_DRAW_NODE && instruction.handle == overlay;
                }) == 1);
    REQUIRE(std::count_if(
                scene.begin(),
                scene.end(),
                [panel](const effindom::v2::ui::SceneInstruction& instruction) {
                    return instruction.opcode == OP_DRAW_NODE && instruction.handle == panel;
                }) == 1);
}

TEST_CASE("v2 ui portal popup nodes added after the initial frame still emit first-frame paint data", "[v2][ui][unit]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t panel = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t label = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(panel != UI_INVALID_HANDLE);
    REQUIRE(label != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_set_position_type(portal, UI_POSITION_ABSOLUTE);
    ui_set_position(portal, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(portal, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, portal);
    ui_commit_frame();

    ui_set_position_type(panel, UI_POSITION_ABSOLUTE);
    ui_set_position(panel, 24.0f, 36.0f, NAN, NAN);
    ui_set_width(panel, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(panel, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_box_style(panel, 0xff223344U, 6.0f, 6.0f, 6.0f, 6.0f, 0.0f, 0U, ED_BORDER_SOLID, 0.0f, 0.0f);
    ui_set_font(label, 1U, 18.0f);
    ui_set_text(label, reinterpret_cast<const std::uint8_t*>("Popup"), 5U);
    ui_set_text_color(label, 0xffffffffU);
    ui_node_add_child(panel, label);
    ui_node_add_child(portal, panel);
    ui_commit_frame();

    const auto words = ReadCommandBuffer();
    CHECK(CountCommand(words, CMD_SET_BOX_STYLE) == 1U);
    const auto glyph_runs = ReadGlyphRuns(words);
    const auto label_run = glyph_runs.find(label);
    REQUIRE(label_run != glyph_runs.end());
    CHECK(label_run->second.font_id == 1U);
    CHECK(label_run->second.font_size == Approx(18.0f));
    CHECK_FALSE(label_run->second.glyphs.empty());
}

TEST_CASE("v2 ui deferred portal children escape a clipped parent after the clip pop", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t owner = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inline_child = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t popup = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(owner != UI_INVALID_HANDLE);
    REQUIRE(inline_child != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(popup != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(owner, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(owner, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(owner, true);
    ui_set_width(inline_child, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inline_child, 16.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_set_position_type(portal, UI_POSITION_ABSOLUTE);
    ui_set_position(portal, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(portal, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_position_type(popup, UI_POSITION_ABSOLUTE);
    ui_set_position(popup, 0.0f, 40.0f, NAN, NAN);
    ui_set_width(popup, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(popup, 48.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(popup, true);
    ui_node_add_child(root, owner);
    ui_node_add_child(owner, inline_child);
    ui_node_add_child(owner, portal);
    ui_node_add_child(portal, popup);
    ui_commit_frame();

    const auto scene = ReadScene(ReadCommandBuffer());
    const auto owner_clip = std::find_if(
        scene.begin(),
        scene.end(),
        [owner](const effindom::v2::ui::SceneInstruction& instruction) {
            return instruction.opcode == OP_PUSH_CLIP && instruction.handle == owner;
        });
    const auto owner_pop = std::find_if(
        scene.begin(),
        scene.end(),
        [](const effindom::v2::ui::SceneInstruction& instruction) {
            return instruction.opcode == OP_POP && instruction.handle == UI_INVALID_HANDLE;
        });
    const auto popup_draw = std::find_if(
        scene.begin(),
        scene.end(),
        [popup](const effindom::v2::ui::SceneInstruction& instruction) {
            return instruction.opcode == OP_DRAW_NODE && instruction.handle == popup;
        });

    REQUIRE(owner_clip != scene.end());
    REQUIRE(owner_pop != scene.end());
    REQUIRE(popup_draw != scene.end());
    CHECK(owner_clip < owner_pop);
    CHECK(owner_pop < popup_draw);

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto hit_bounds = ReadHitBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(popup) != bounds.end());
    REQUIRE(hit_bounds.find(popup) != hit_bounds.end());
    CHECK(bounds.at(popup).x == Approx(0.0f));
    CHECK(bounds.at(popup).y == Approx(40.0f));
    CHECK(bounds.at(popup).width == Approx(120.0f));
    CHECK(bounds.at(popup).height == Approx(48.0f));
    CHECK(hit_bounds.at(popup).x == Approx(0.0f));
    CHECK(hit_bounds.at(popup).y == Approx(40.0f));
    CHECK(hit_bounds.at(popup).width == Approx(120.0f));
    CHECK(hit_bounds.at(popup).height == Approx(48.0f));
}

TEST_CASE("v2 ui deferred portal children use viewport-space bounds inside a scrolled ancestor", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t owner = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t popup = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(owner != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(popup != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(spacer, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(owner, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(owner, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(owner, true);
    ui_set_is_portal(portal, true);
    ui_set_width(portal, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_position_type(popup, UI_POSITION_ABSOLUTE);
    ui_set_position(popup, 0.0f, 40.0f, NAN, NAN);
    ui_set_width(popup, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(popup, 48.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(popup, true);

    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, owner);
    ui_node_add_child(owner, portal);
    ui_node_add_child(portal, popup);

    ui_set_scroll_offset(scroll, 0.0f, 60.0f);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto hit_bounds = ReadHitBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(popup) != bounds.end());
    REQUIRE(hit_bounds.find(popup) != hit_bounds.end());
    CHECK(bounds.at(popup).x == Approx(0.0f));
    CHECK(bounds.at(popup).y == Approx(60.0f));
    CHECK(bounds.at(popup).width == Approx(120.0f));
    CHECK(bounds.at(popup).height == Approx(48.0f));
    CHECK(hit_bounds.at(popup).x == Approx(0.0f));
    CHECK(hit_bounds.at(popup).y == Approx(60.0f));
    CHECK(hit_bounds.at(popup).width == Approx(120.0f));
    CHECK(hit_bounds.at(popup).height == Approx(48.0f));
}

TEST_CASE("v2 ui nested portals stay deferred in portal DFS order", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_b = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_c = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_d = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_e = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child_a != UI_INVALID_HANDLE);
    REQUIRE(child_b != UI_INVALID_HANDLE);
    REQUIRE(child_c != UI_INVALID_HANDLE);
    REQUIRE(child_d != UI_INVALID_HANDLE);
    REQUIRE(child_e != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_node_add_child(root, child_a);
    ui_node_add_child(root, child_b);
    ui_node_add_child(child_b, child_c);
    ui_node_add_child(child_c, child_d);
    ui_node_add_child(root, child_e);
    ui_set_is_portal(child_b, true);
    ui_set_is_portal(child_c, true);

    ui_commit_frame();
    const auto paint_order = ReadPaintOrder(ReadCommandBuffer());

    REQUIRE(paint_order == std::vector<std::uint64_t>{root, child_a, child_b, child_e, child_c, child_d});
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_b) == 1);
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_c) == 1);
}

TEST_CASE("v2 ui scroll views offset child bounds and clamp programmatic offsets", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);

    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(scroll_node != nullptr);
    scroll_node->scroll_velocity_x = 5.0f;
    scroll_node->scroll_velocity_y = 7.0f;

    ui_set_scroll_offset(scroll, 0.0f, 999.0f);
    ui_commit_frame();

    const auto words = ReadCommandBuffer();
    auto bounds = ReadBounds(words);
    REQUIRE(bounds.find(content) != bounds.end());
    CHECK(bounds.at(content).y == Approx(0.0f));
    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(140.0f));
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_x == Approx(0.0f));
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_y == Approx(0.0f));
    const auto scene = ReadScene(words);
    CHECK(std::any_of(scene.begin(), scene.end(), [scroll](const effindom::v2::ui::SceneInstruction& instruction) {
        return instruction.opcode == OP_PUSH_TRANSLATE &&
            instruction.handle == scroll &&
            std::abs(instruction.arg0) < 0.001f &&
            std::abs(instruction.arg1 + 140.0f) < 0.001f;
    }));

    ui_commit_frame();
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(140.0f));
}

TEST_CASE("v2 ui scroll views accept explicit content size overrides", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);

    ui_set_scroll_content_size(scroll, -1.0f, 200.0f);
    ui_set_scroll_offset(scroll, 0.0f, 999.0f);
    ui_commit_frame();

    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_content_height == Approx(200.0f));
    CHECK(scroll_node->scroll_offset_y == Approx(140.0f));
}

TEST_CASE("v2 ui scroll apply guard defers reentrant notifications", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;
    using namespace test_ui_support;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_commit_frame();
    ResetInteractionLogs();

    g_reentrant_scroll_test_handle = scroll;
    g_reentrant_scroll_callback_depth = 0U;
    g_reentrant_scroll_max_depth = 0U;
    g_scroll_change_callback = &ReentrantScrollApplyCallback;

    auto& runtime = GetRuntime();
    auto* scroll_node = runtime.ResolveMutable(scroll);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(runtime.ApplyScrollOffset(scroll, *scroll_node, 0.0f, 20.0f, true));

    CHECK(g_reentrant_scroll_max_depth == 1U);
    REQUIRE(g_scroll_changes.size() == 2U);
    CHECK(g_scroll_changes[0].offset_y == Approx(20.0f));
    CHECK(g_scroll_changes[1].offset_y == Approx(30.0f));
    CHECK(scroll_node->scroll_offset_y == Approx(30.0f));

    UseRecordingInteractionCallbacks();
    g_reentrant_scroll_test_handle = UI_INVALID_HANDLE;
    g_reentrant_scroll_callback_depth = 0U;
    g_reentrant_scroll_max_depth = 0U;
}

TEST_CASE("v2 ui pointer move without button does not drag stale scroll state", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_set_scroll_offset(scroll, 0.0f, 60.0f);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.active_scroll_handle_ = scroll;
    runtime.active_scroll_dragged_ = false;
    runtime.primary_pointer_down_ = false;
    runtime.last_pointer_logical_x_ = 40.0f;
    runtime.last_pointer_logical_y_ = 40.0f;

    ui_on_pointer_event(UI_EVENT_POINTER_MOVE, content, 48.0f, 54.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(60.0f));
    CHECK(scroll_node->scroll_velocity_x == Approx(0.0f));
    CHECK(scroll_node->scroll_velocity_y == Approx(0.0f));
}

TEST_CASE("v2 ui wheel events scroll the hovered scroll view", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_hovered_handle_ = content;
    runtime.last_pointer_logical_x_ = 40.0f;
    runtime.last_pointer_logical_y_ = 40.0f;

    ui_on_wheel_event(0.0f, 28.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(28.0f));
}

TEST_CASE("v2 ui wheel events do not latch onto a sibling scroll view outside its bounds", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t gap = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t track = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(gap != UI_INVALID_HANDLE);
    REQUIRE(track != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 176.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);

    ui_set_width(scroll, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(gap, 8.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(gap, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(track, 8.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(track, 80.0f, UI_SIZE_UNIT_PIXEL);

    ui_node_add_child(root, scroll);
    ui_node_add_child(root, gap);
    ui_node_add_child(root, track);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_hovered_handle_ = gap;
    runtime.last_pointer_logical_x_ = 164.0f;
    runtime.last_pointer_logical_y_ = 40.0f;
    ui_on_wheel_event(0.0f, 20.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(0.0f));

    runtime.last_hovered_handle_ = track;
    runtime.last_pointer_logical_x_ = 172.0f;
    runtime.last_pointer_logical_y_ = 40.0f;
    ui_on_wheel_event(0.0f, 16.0f);
    CHECK(scroll_node->scroll_offset_y == Approx(0.0f));
}

TEST_CASE("v2 ui visibility hidden keeps layout while collapsed removes layout footprint", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t middle = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t last = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(middle != UI_INVALID_HANDLE);
    REQUIRE(last != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);

    ui_set_width(first, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(first, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(middle, 30.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(middle, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(last, 50.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(last, 20.0f, UI_SIZE_UNIT_PIXEL);

    ui_node_add_child(root, first);
    ui_node_add_child(root, middle);
    ui_node_add_child(root, last);

    ui_set_visibility(middle, UI_VISIBILITY_COLLAPSED);
    ui_commit_frame();
    auto bounds = ReadBounds(ReadCommandBuffer());
    auto paint_order = ReadPaintOrder(ReadCommandBuffer());
    REQUIRE(bounds.find(last) != bounds.end());
    CHECK(bounds.at(last).x == Approx(40.0f));
    CHECK(std::find(paint_order.begin(), paint_order.end(), middle) == paint_order.end());

    ui_set_visibility(middle, UI_VISIBILITY_HIDDEN);
    ui_commit_frame();
    bounds = ReadBounds(ReadCommandBuffer());
    paint_order = ReadPaintOrder(ReadCommandBuffer());
    REQUIRE(bounds.find(last) != bounds.end());
    CHECK(bounds.at(last).x == Approx(70.0f));
    CHECK(std::find(paint_order.begin(), paint_order.end(), middle) == paint_order.end());
}

TEST_CASE("v2 ui wheel and touch scroll share scroll-box proxy routing", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll_box = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t gap = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t track = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll_box != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(gap != UI_INVALID_HANDLE);
    REQUIRE(track != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 176.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_width(scroll_box, 176.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll_box, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(scroll_box, 1U);
    ui_set_width(scroll, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(gap, 8.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(gap, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(track, 8.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(track, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_proxy_target(scroll_box, scroll);

    ui_node_add_child(root, scroll_box);
    ui_node_add_child(scroll_box, scroll);
    ui_node_add_child(scroll_box, gap);
    ui_node_add_child(scroll_box, track);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_hovered_handle_ = gap;
    runtime.last_pointer_logical_x_ = 164.0f;
    runtime.last_pointer_logical_y_ = 40.0f;

    ui_on_wheel_event(0.0f, 20.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(20.0f));

    ui_touch_scroll_begin(gap, 164.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == scroll);

    ui_touch_scroll_update(0.0f, 16.0f);
    CHECK(runtime.Resolve(scroll)->scroll_offset_y == Approx(36.0f));
    CHECK(runtime.Resolve(scroll)->scroll_velocity_y == Approx(16.0f));

    ui_touch_scroll_end();
    CHECK(runtime.active_scroll_handle_ == UI_INVALID_HANDLE);
    CHECK(runtime.Resolve(scroll)->scroll_velocity_y == Approx(16.0f));
}

TEST_CASE("v2 ui nested scroll-box proxy hands wheel and touch scrolling to an ancestor at vertical edges", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t outer_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll_box = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(outer_content != UI_INVALID_HANDLE);
    REQUIRE(scroll_box != UI_INVALID_HANDLE);
    REQUIRE(inner_scroll != UI_INVALID_HANDLE);
    REQUIRE(inner_content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_scroll, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll_box, 92.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll_box, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(spacer, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_proxy_target(scroll_box, inner_scroll);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, scroll_box);
    ui_node_add_child(outer_content, spacer);
    ui_node_add_child(scroll_box, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    ui_set_scroll_offset(outer_scroll, 0.0f, 40.0f);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_hovered_handle_ = scroll_box;
    runtime.last_pointer_logical_x_ = 40.0f;
    runtime.last_pointer_logical_y_ = 40.0f;
    ui_on_wheel_event(0.0f, -20.0f);

    REQUIRE(runtime.Resolve(outer_scroll) != nullptr);
    REQUIRE(runtime.Resolve(inner_scroll) != nullptr);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(20.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(0.0f));

    ui_touch_scroll_begin(scroll_box, 40.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == inner_scroll);
    ui_touch_scroll_update(0.0f, -15.0f);
    CHECK(runtime.active_scroll_handle_ == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(5.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(0.0f));

    ui_set_scroll_offset(outer_scroll, 0.0f, 20.0f);
    ui_set_scroll_offset(inner_scroll, 0.0f, 140.0f);
    ui_commit_frame();

    ui_on_wheel_event(0.0f, 20.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(40.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(140.0f));

    ui_touch_scroll_begin(scroll_box, 40.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == inner_scroll);
    ui_touch_scroll_update(0.0f, 15.0f);
    CHECK(runtime.active_scroll_handle_ == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(55.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(140.0f));
}

TEST_CASE("v2 ui clipped overflow does not retarget wheel scrolling through a scroll proxy owner", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t outer_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll_box = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(outer_content != UI_INVALID_HANDLE);
    REQUIRE(scroll_box != UI_INVALID_HANDLE);
    REQUIRE(inner_scroll != UI_INVALID_HANDLE);
    REQUIRE(inner_content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_scroll, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll_box, 92.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll_box, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(scroll_box, true);
    ui_set_width(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_content, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(spacer, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_proxy_target(scroll_box, inner_scroll);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, scroll_box);
    ui_node_add_child(outer_content, spacer);
    ui_node_add_child(scroll_box, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    CHECK(runtime.ResolveScrollTarget(inner_content, 116.0f, 40.0f) == outer_scroll);

    runtime.last_hovered_handle_ = inner_content;
    runtime.last_pointer_logical_x_ = 116.0f;
    runtime.last_pointer_logical_y_ = 40.0f;
    ui_on_wheel_event(0.0f, 20.0f);

    const auto* outer_node = runtime.Resolve(outer_scroll);
    const auto* inner_node = runtime.Resolve(inner_scroll);
    REQUIRE(outer_node != nullptr);
    REQUIRE(inner_node != nullptr);
    CHECK(outer_node->scroll_offset_y == Approx(20.0f));
    CHECK(inner_node->scroll_offset_y == Approx(0.0f));
}

TEST_CASE("v2 ui scroll views respect configured scroll axes and friction", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 420.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_pointer_logical_x_ = 40.0f;
    runtime.last_pointer_logical_y_ = 40.0f;

    ui_set_scroll_enabled(scroll, true, false);
    ui_on_wheel_event(0.0f, 24.0f);
    REQUIRE(runtime.Resolve(scroll) != nullptr);
    CHECK(runtime.Resolve(scroll)->scroll_offset_y == Approx(0.0f));

    ui_on_wheel_event(28.0f, 0.0f);
    CHECK(runtime.Resolve(scroll)->scroll_offset_x == Approx(28.0f));

    ui_set_scroll_enabled(scroll, false, true);
    ui_on_wheel_event(20.0f, 0.0f);
    CHECK(runtime.Resolve(scroll)->scroll_offset_x == Approx(0.0f));

    ui_on_wheel_event(0.0f, 18.0f);
    CHECK(runtime.Resolve(scroll)->scroll_offset_y == Approx(18.0f));

    ui_set_scroll_enabled(scroll, true, true);
    ui_set_scroll_friction(scroll, 0.5f);
    ui_touch_scroll_begin(content, 40.0f, 40.0f);
    ui_touch_scroll_update(0.0f, 20.0f);
    ui_touch_scroll_end();
    ui_commit_frame();

    REQUIRE(runtime.Resolve(scroll) != nullptr);
    CHECK(runtime.Resolve(scroll)->scroll_velocity_y == Approx(10.0f));
}

TEST_CASE("v2 ui pull-to-refresh reports true for non-scroll starts and top-of-scroll targets", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t header = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t sidebar = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(header != UI_INVALID_HANDLE);
    REQUIRE(sidebar != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_width(sidebar, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(sidebar, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(sidebar, 1U);
    ui_set_width(header, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(header, 48.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 172.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 360.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, sidebar);
    ui_node_add_child(sidebar, header);
    ui_node_add_child(sidebar, scroll);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    ui_touch_scroll_begin(header, 120.0f, 24.0f);
    CHECK(ui_touch_scroll_allows_pull_to_refresh());
    ui_touch_scroll_end();

    auto& runtime = GetRuntime();
    REQUIRE(runtime.ResolveMutable(scroll) != nullptr);
    runtime.active_touch_scroll_handle_y_ = scroll;
    CHECK(ui_touch_scroll_allows_pull_to_refresh());
    runtime.ResolveMutable(scroll)->scroll_offset_y = 18.0f;
    CHECK_FALSE(ui_touch_scroll_allows_pull_to_refresh());
    runtime.active_touch_scroll_handle_y_ = UI_INVALID_HANDLE;
    CHECK(ui_touch_scroll_allows_pull_to_refresh());
}

TEST_CASE("v2 ui horizontal wheel and touch fall back to an ancestor that can scroll that axis", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t outer_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner_content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(outer_content != UI_INVALID_HANDLE);
    REQUIRE(inner_scroll != UI_INVALID_HANDLE);
    REQUIRE(inner_content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(120.0f, 120.0f);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(outer_scroll, true, false);
    ui_set_scroll_enabled(inner_scroll, false, true);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_pointer_logical_x_ = 40.0f;
    runtime.last_pointer_logical_y_ = 40.0f;
    ui_on_wheel_event(24.0f, 0.0f);

    const auto* outer_node = runtime.Resolve(outer_scroll);
    const auto* inner_node = runtime.Resolve(inner_scroll);
    REQUIRE(outer_node != nullptr);
    REQUIRE(inner_node != nullptr);
    CHECK(outer_node->scroll_offset_x == Approx(24.0f));
    CHECK(inner_node->scroll_offset_y == Approx(0.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == inner_scroll);
    ui_touch_scroll_update(30.0f, 0.0f);
    CHECK(runtime.active_scroll_handle_ == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(54.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(0.0f));
}

TEST_CASE("v2 ui vertical wheel and touch fall back to an ancestor once the nested scroll view is at its edge", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t outer_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner_content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(outer_content != UI_INVALID_HANDLE);
    REQUIRE(inner_scroll != UI_INVALID_HANDLE);
    REQUIRE(inner_content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(120.0f, 120.0f);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(outer_scroll, false, true);
    ui_set_scroll_enabled(inner_scroll, false, true);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    ui_set_scroll_offset(outer_scroll, 0.0f, 40.0f);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_pointer_logical_x_ = 40.0f;
    runtime.last_pointer_logical_y_ = 40.0f;
    ui_on_wheel_event(0.0f, -20.0f);

    const auto* outer_node = runtime.Resolve(outer_scroll);
    const auto* inner_node = runtime.Resolve(inner_scroll);
    REQUIRE(outer_node != nullptr);
    REQUIRE(inner_node != nullptr);
    CHECK(outer_node->scroll_offset_y == Approx(20.0f));
    CHECK(inner_node->scroll_offset_y == Approx(0.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == inner_scroll);
    ui_touch_scroll_update(0.0f, -15.0f);
    CHECK(runtime.active_scroll_handle_ == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(5.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(0.0f));

    ui_set_scroll_offset(outer_scroll, 0.0f, 20.0f);
    ui_set_scroll_offset(inner_scroll, 0.0f, 140.0f);
    ui_commit_frame();

    ui_on_wheel_event(0.0f, 20.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(40.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(140.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == inner_scroll);
    ui_touch_scroll_update(0.0f, 15.0f);
    CHECK(runtime.active_scroll_handle_ == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(55.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(140.0f));
}

TEST_CASE("v2 ui horizontal wheel and touch fall back to an ancestor when the nested scroll view hits left and right edges", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t outer_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner_content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(outer_content != UI_INVALID_HANDLE);
    REQUIRE(inner_scroll != UI_INVALID_HANDLE);
    REQUIRE(inner_content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(120.0f, 120.0f);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(outer_scroll, true, false);
    ui_set_scroll_enabled(inner_scroll, true, false);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    ui_set_scroll_offset(outer_scroll, 30.0f, 0.0f);
    ui_set_scroll_offset(inner_scroll, 140.0f, 0.0f);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.last_pointer_logical_x_ = 40.0f;
    runtime.last_pointer_logical_y_ = 40.0f;

    ui_on_wheel_event(20.0f, 0.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(50.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_x == Approx(140.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == inner_scroll);
    ui_touch_scroll_update(15.0f, 0.0f);
    CHECK(runtime.active_scroll_handle_ == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(65.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_x == Approx(140.0f));

    ui_set_scroll_offset(outer_scroll, 65.0f, 0.0f);
    ui_set_scroll_offset(inner_scroll, 0.0f, 0.0f);
    ui_commit_frame();

    runtime.last_pointer_logical_x_ = 10.0f;
    ui_on_wheel_event(-20.0f, 0.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(45.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_x == Approx(0.0f));

    ui_touch_scroll_begin(inner_scroll, 10.0f, 40.0f);
    CHECK(runtime.active_scroll_handle_ == inner_scroll);
    ui_touch_scroll_update(-15.0f, 0.0f);
    CHECK(runtime.active_scroll_handle_ == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(30.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_x == Approx(0.0f));
}

TEST_CASE("v2 ui touch scroll can split diagonal movement across horizontal and vertical ancestors", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t outer_content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner_content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(outer_content != UI_INVALID_HANDLE);
    REQUIRE(inner_scroll != UI_INVALID_HANDLE);
    REQUIRE(inner_content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(120.0f, 120.0f);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(outer_scroll, true, false);
    ui_set_scroll_enabled(inner_scroll, false, true);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    ui_touch_scroll_update(30.0f, 24.0f);

    auto& runtime = GetRuntime();
    REQUIRE(runtime.Resolve(outer_scroll) != nullptr);
    REQUIRE(runtime.Resolve(inner_scroll) != nullptr);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(30.0f));
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_x == Approx(30.0f));
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_y == Approx(0.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(24.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_x == Approx(0.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_y == Approx(24.0f));

    runtime.CommitFrame();
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(30.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(24.0f));
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_x == Approx(30.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_y == Approx(24.0f));

    ui_touch_scroll_end();
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_x == Approx(30.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_y == Approx(24.0f));
}

TEST_CASE("v2 ui touch scroll updates a scroll view and preserves fling velocity after release", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 420.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    ui_touch_scroll_begin(content, 40.0f, 40.0f);
    CHECK(GetRuntime().active_scroll_handle_ == scroll);
    CHECK(ui_touch_scroll_allows_pull_to_refresh());

    ui_touch_scroll_update(0.0f, 28.0f);
    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(28.0f));
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_y == Approx(28.0f));
    CHECK_FALSE(ui_touch_scroll_allows_pull_to_refresh());

    ui_touch_scroll_end();
    CHECK(GetRuntime().active_scroll_handle_ == UI_INVALID_HANDLE);
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_y == Approx(28.0f));

    ui_commit_frame();
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(56.0f));
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_y == Approx(26.6f));
}

TEST_CASE("v2 ui touch fling default friction uses platform-specific coarse-pointer tuning", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PlatformFamily;

    auto run_touch_fling = [](std::uint32_t platform_family) {
        ui_reset();

        const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
        const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
        const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
        REQUIRE(root != UI_INVALID_HANDLE);
        REQUIRE(scroll != UI_INVALID_HANDLE);
        REQUIRE(content != UI_INVALID_HANDLE);

        ui_set_root(root);
        ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(scroll, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(content, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(content, 420.0f, UI_SIZE_UNIT_PIXEL);
        ui_node_add_child(root, scroll);
        ui_node_add_child(scroll, content);
        ui_commit_frame();

        GetRuntime().SetCoarsePointerMode(true);
        GetRuntime().SetPlatformFamily(platform_family);

        ui_touch_scroll_begin(content, 40.0f, 40.0f);
        ui_touch_scroll_update(0.0f, 28.0f);
        ui_touch_scroll_end();
        ui_commit_frame();

        const auto* scroll_node = GetRuntime().Resolve(scroll);
        REQUIRE(scroll_node != nullptr);
        return scroll_node->scroll_velocity_y;
    };

    const float android_velocity = run_touch_fling(static_cast<std::uint32_t>(PlatformFamily::Linux));
    const float apple_velocity = run_touch_fling(static_cast<std::uint32_t>(PlatformFamily::Apple));

    CHECK(android_velocity == Approx(28.0f * 0.982f));
    CHECK(apple_velocity == Approx(28.0f * 0.988f));
    CHECK(apple_velocity > android_velocity);
}

TEST_CASE("v2 ui cross-node drag stitches text and highlights all covered nodes", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, second}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(root, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("World"), 5U);
    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* second_node = GetRuntime().Resolve(second);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*first_node, 0U);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*second_node, 5U);
    REQUIRE(start_line == 0);
    REQUIRE(end_line == 0);

    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, first, first_node->abs_x + start_x + 0.5f, first_node->abs_y + (first_node->line_height * 0.5f));
    ui_on_pointer_event(UI_EVENT_POINTER_MOVE, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));
    ui_on_pointer_event(UI_EVENT_POINTER_UP, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));

    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text == "Hello\nWorld");

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(first) != highlights.end());
    REQUIRE(highlights.find(second) != highlights.end());
    CHECK_FALSE(highlights.at(first).rects.empty());
    CHECK_FALSE(highlights.at(second).rects.empty());
    CHECK(highlights.at(first).rects.front().x >= 0.0f);
    CHECK(highlights.at(first).rects.front().y >= 0.0f);
    CHECK(highlights.at(first).rects.front().x < first_node->layout_width);
    CHECK(highlights.at(second).rects.front().x >= 0.0f);
    CHECK(highlights.at(second).rects.front().y >= 0.0f);
    CHECK(highlights.at(second).rects.front().x < second_node->layout_width);
}

TEST_CASE("v2 ui cross-node selection covers intermediate nodes and clears on empty click", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t middle = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t last = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(middle != UI_INVALID_HANDLE);
    REQUIRE(last != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, middle, last}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(root, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("One"), 3U);
    ui_set_text(middle, reinterpret_cast<const std::uint8_t*>("Two"), 3U);
    ui_set_text(last, reinterpret_cast<const std::uint8_t*>("Three"), 5U);
    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* last_node = GetRuntime().Resolve(last);
    REQUIRE(first_node != nullptr);
    REQUIRE(last_node != nullptr);
    const auto [start_x, _] = GetRuntime().GetLocalPositionFromIndex(*first_node, 0U);
    const auto [end_x, __] = GetRuntime().GetLocalPositionFromIndex(*last_node, 5U);

    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, first, first_node->abs_x + start_x + 0.5f, first_node->abs_y + (first_node->line_height * 0.5f));
    ui_on_pointer_event(UI_EVENT_POINTER_MOVE, last, last_node->abs_x + end_x, last_node->abs_y + (last_node->line_height * 0.5f));
    ui_on_pointer_event(UI_EVENT_POINTER_UP, last, last_node->abs_x + end_x, last_node->abs_y + (last_node->line_height * 0.5f));

    ui_commit_frame();
    auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(middle) != highlights.end());
    CHECK_FALSE(highlights.at(middle).rects.empty());

    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, root, 2.0f, 2.0f);
    CHECK_FALSE(GetRuntime().cross_selection_active_);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text.empty());
    ui_commit_frame();
    highlights = ReadHighlights(ReadCommandBuffer());
    if (highlights.find(first) != highlights.end()) {
        CHECK(highlights.at(first).rects.empty());
    }
    if (highlights.find(middle) != highlights.end()) {
        CHECK(highlights.at(middle).rects.empty());
    }
    if (highlights.find(last) != highlights.end()) {
        CHECK(highlights.at(last).rects.empty());
    }
}

TEST_CASE("v2 ui selection-area select-all uses cross-selection and clears on click", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Scrollable list"), 15U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);

    CHECK(GetRuntime().SelectAllText(text));
    CHECK(GetRuntime().cross_selection_active_);
    CHECK(GetRuntime().active_selection_handle_ == UI_INVALID_HANDLE);
    CHECK(text_node->selection_start == 0U);
    CHECK(text_node->selection_end == 0U);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text == "Scrollable list");

    ui_commit_frame();
    auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK_FALSE(highlights.at(text).rects.empty());

    const auto [click_x, click_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, 4U);
    REQUIRE(click_line == 0U);
    ui_on_pointer_event(
        UI_EVENT_POINTER_DOWN,
        text,
        text_node->abs_x + click_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    ui_on_pointer_event(
        UI_EVENT_POINTER_UP,
        text,
        text_node->abs_x + click_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));

    CHECK_FALSE(GetRuntime().cross_selection_active_);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text.empty());

    ui_commit_frame();
    highlights = ReadHighlights(ReadCommandBuffer());
    if (highlights.find(text) != highlights.end()) {
        CHECK(highlights.at(text).rects.empty());
    }
}

TEST_CASE("v2 ui cross-node copy and shift-click extension use stitched text", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, second}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(root, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Alpha"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("Beta"), 4U);
    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* second_node = GetRuntime().Resolve(second);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    const auto [start_x, _] = GetRuntime().GetLocalPositionFromIndex(*first_node, 0U);
    const auto [end_x, __] = GetRuntime().GetLocalPositionFromIndex(*second_node, 4U);

    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, first, first_node->abs_x + start_x + 0.5f, first_node->abs_y + (first_node->line_height * 0.5f));
    ui_set_key_modifiers(UI_KEY_MOD_SHIFT);
    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));
    ui_on_pointer_event(UI_EVENT_POINTER_UP, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));
    ui_set_key_modifiers(0U);

    REQUIRE(GetRuntime().cross_selection_active_);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().text == "Alpha\nBeta");

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("c"), 1U, UI_KEY_MOD_CTRL);
    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == "Alpha\nBeta");
}

TEST_CASE("v2 ui current-selection helpers hit test, copy, and clear focused text selections", "[v2][ui][selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    auto* text_node = runtime.ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    text_node->selection_start = 1U;
    text_node->selection_end = 5U;
    runtime.SetFocus(text);

    const auto [selection_left, selection_line] = runtime.GetLocalPositionFromIndex(*text_node, 1U);
    const auto [selection_right, selection_right_line] = runtime.GetLocalPositionFromIndex(*text_node, 5U);
    REQUIRE(selection_line == 0);
    REQUIRE(selection_right_line == 0);
    const float probe_x = text_node->abs_x + ((selection_left + selection_right) * 0.5f);
    const float probe_y = text_node->abs_y + (text_node->line_height * 0.5f);

    CHECK(ui_is_point_in_selection(probe_x, probe_y));
    ResetInteractionLogs();
    ui_copy_current_selection();
    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == "ello");

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Escape"), 6U, 0U);
    CHECK(text_node->selection_start == 5U);
    CHECK(text_node->selection_end == 5U);
    CHECK_FALSE(ui_is_point_in_selection(probe_x, probe_y));
    REQUIRE_FALSE(g_selection_changes.empty());
    CHECK(g_selection_changes.back().handle == text);
    CHECK(g_selection_changes.back().start == 5U);
    CHECK(g_selection_changes.back().end == 5U);
}

TEST_CASE("v2 ui styled text copy emits a rich clipboard payload", "[v2][ui][clipboard]") {
    using effindom::v2::ui::CommandBuilder;
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();
    RegisterMonoTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello plain World";

    ui_set_root(root);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    const std::uint32_t runs_words[] = {
        0U, 5U, 1U, CommandBuilder::FloatToWord(20.0f), EF_RGB(0xF8U, 0xFAU, 0xFCU), 0U, 1U,
        12U, 17U, 5U, CommandBuilder::FloatToWord(18.0f), EF_RGB(0x60U, 0xA5U, 0xFAU), EF_RGB(0x1EU, 0x29U, 0x3BU), 2U,
    };
    ui_set_text_style_runs(text, 2U, runs_words);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    text_node->selection_start = 0U;
    text_node->selection_end = 17U;

    ResetInteractionLogs();
    REQUIRE(GetRuntime().CopyTextSelection(text));
    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == kSample);
    CHECK_FALSE(g_clipboard_writes[0].rich_json.empty());
    CHECK(g_clipboard_writes[0].rich_json.find("\"version\":1") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"text\":\"Hello\"") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"text\":\" plain \"") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"text\":\"World\"") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"fontId\":5") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"decorationFlags\":2") != std::string::npos);
}

TEST_CASE("v2 ui current-selection helpers cover cross-selection hit testing, copy, and clear", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, second}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(root, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Alpha"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("Beta"), 4U);
    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* second_node = GetRuntime().Resolve(second);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    const auto [start_x, _] = GetRuntime().GetLocalPositionFromIndex(*first_node, 0U);
    const auto [end_x, __] = GetRuntime().GetLocalPositionFromIndex(*second_node, 4U);

    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, first, first_node->abs_x + start_x + 0.5f, first_node->abs_y + (first_node->line_height * 0.5f));
    ui_set_key_modifiers(UI_KEY_MOD_SHIFT);
    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));
    ui_on_pointer_event(UI_EVENT_POINTER_UP, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));
    ui_set_key_modifiers(0U);

    REQUIRE(GetRuntime().cross_selection_active_);
    const auto [probe_local_x, probe_line] = GetRuntime().GetLocalPositionFromIndex(*second_node, 2U);
    REQUIRE(probe_line == 0);
    const float probe_x = second_node->abs_x + probe_local_x;
    const float probe_y = second_node->abs_y + (second_node->line_height * 0.5f);

    CHECK(ui_is_point_in_selection(probe_x, probe_y));
    ResetInteractionLogs();
    ui_copy_current_selection();
    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == "Alpha\nBeta");

    ui_clear_current_selection();
    CHECK_FALSE(GetRuntime().cross_selection_active_);
    CHECK_FALSE(ui_is_point_in_selection(probe_x, probe_y));
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text.empty());
}

TEST_CASE("v2 ui cross-selection copy emits rich clipboard payloads when styled nodes participate", "[v2][ui][clipboard]") {
    using effindom::v2::ui::CommandBuilder;
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();
    RegisterMonoTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, second}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(root, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Alpha"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("Beta"), 4U);
    const std::uint32_t first_runs_words[] = {
        0U, 5U, 5U, CommandBuilder::FloatToWord(20.0f), EF_RGB(0xF8U, 0xFAU, 0xFCU), 0U, 1U,
    };
    const std::uint32_t second_runs_words[] = {
        0U, 4U, 1U, CommandBuilder::FloatToWord(20.0f), EF_RGB(0xF8U, 0x71U, 0x71U), 0U, 2U,
    };
    ui_set_text_style_runs(first, 1U, first_runs_words);
    ui_set_text_style_runs(second, 1U, second_runs_words);
    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* second_node = GetRuntime().Resolve(second);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    const auto [start_x, _] = GetRuntime().GetLocalPositionFromIndex(*first_node, 0U);
    const auto [end_x, __] = GetRuntime().GetLocalPositionFromIndex(*second_node, 4U);

    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, first, first_node->abs_x + start_x + 0.5f, first_node->abs_y + (first_node->line_height * 0.5f));
    ui_set_key_modifiers(UI_KEY_MOD_SHIFT);
    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));
    ui_on_pointer_event(UI_EVENT_POINTER_UP, second, second_node->abs_x + end_x, second_node->abs_y + (second_node->line_height * 0.5f));
    ui_set_key_modifiers(0U);

    REQUIRE(GetRuntime().cross_selection_active_);
    ResetInteractionLogs();
    ui_copy_current_selection();
    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == "Alpha\nBeta");
    CHECK_FALSE(g_clipboard_writes[0].rich_json.empty());
    CHECK(g_clipboard_writes[0].rich_json.find("\"text\":\"Alpha\"") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"text\":\"\\n\"") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"text\":\"Beta\"") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"fontId\":5") != std::string::npos);
    CHECK(g_clipboard_writes[0].rich_json.find("\"fontId\":1") != std::string::npos);
}

TEST_CASE("v2 ui cross-node keyboard extension traverses adjacent nodes", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, second}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(root, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("Again"), 5U);
    ui_commit_frame();

    auto* first_mut = GetRuntime().ResolveMutable(first);
    auto* second_mut = GetRuntime().ResolveMutable(second);
    REQUIRE(first_mut != nullptr);
    REQUIRE(second_mut != nullptr);
    first_mut->selection_start = 5U;
    first_mut->selection_end = 5U;
    GetRuntime().SetFocus(first);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);
    CHECK_FALSE(GetRuntime().cross_selection_active_);

    GetRuntime().cross_selection_active_ = true;
    GetRuntime().selection_area_handle_ = root;
    GetRuntime().selection_area_nodes_dirty_ = true;
    GetRuntime().start_node_handle_ = first;
    GetRuntime().start_index_ = 4U;
    GetRuntime().end_node_handle_ = first;
    GetRuntime().end_index_ = 5U;
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);
    CHECK(GetRuntime().cross_selection_active_);
    CHECK(GetRuntime().end_node_handle_ == second);
    CHECK(GetRuntime().end_index_ == 0U);

    ui_on_pointer_event(UI_EVENT_POINTER_DOWN, root, 2.0f, 2.0f);
    second_mut->selection_start = 3U;
    second_mut->selection_end = 3U;
    GetRuntime().SetFocus(second);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Home"), 4U, UI_KEY_MOD_SHIFT);
    CHECK(GetRuntime().start_node_handle_ == second);
    CHECK(GetRuntime().start_index_ == 3U);
    CHECK(GetRuntime().end_node_handle_ == first);
    CHECK(GetRuntime().end_index_ == 0U);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().text == "Hello\nAga");
}

TEST_CASE("v2 ui cross-selection keyboard vertical extension traverses wrapped lines and adjacent nodes", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t above = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t wrapped = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t below = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(above != UI_INVALID_HANDLE);
    REQUIRE(wrapped != UI_INVALID_HANDLE);
    REQUIRE(below != UI_INVALID_HANDLE);

    constexpr const char* kWrapped =
        "The quick brown fox jumps over the lazy dog while editors track wrapped selections";
    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {above, wrapped, below}) {
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(root, child);
    }
    ui_set_width(above, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(wrapped, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(below, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_text(above, reinterpret_cast<const std::uint8_t*>("Above"), 5U);
    ui_set_text(wrapped, reinterpret_cast<const std::uint8_t*>(kWrapped), static_cast<std::uint32_t>(std::strlen(kWrapped)));
    ui_set_text(below, reinterpret_cast<const std::uint8_t*>("Below"), 5U);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    const auto* above_node = runtime.Resolve(above);
    const auto* below_node = runtime.Resolve(below);
    auto* wrapped_node = runtime.ResolveMutable(wrapped);
    REQUIRE(above_node != nullptr);
    REQUIRE(below_node != nullptr);
    REQUIRE(wrapped_node != nullptr);
    REQUIRE(wrapped_node->visible_line_count >= 3U);

    const std::uint32_t first_line_start = static_cast<std::uint32_t>(wrapped_node->break_offsets[0]);
    const std::uint32_t first_line_end = static_cast<std::uint32_t>(wrapped_node->break_offsets[1]);
    REQUIRE(first_line_end > first_line_start + 1U);

    const std::uint32_t first_line_anchor = first_line_start;
    const std::uint32_t first_line_selection_end = first_line_start + 1U;
    const auto [first_line_local_x, first_line_index] =
        runtime.GetLocalPositionFromIndex(*wrapped_node, first_line_selection_end);
    REQUIRE(first_line_index == 0);
    const std::uint32_t expected_wrapped_down =
        runtime.GetStringIndexFromPoint(*wrapped_node, first_line_local_x, wrapped_node->line_height * 1.5f);

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_dirty_ = true;
    runtime.start_node_handle_ = wrapped;
    runtime.start_index_ = first_line_anchor;
    runtime.end_node_handle_ = wrapped;
    runtime.end_index_ = first_line_selection_end;
    runtime.SetFocus(wrapped);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowDown"), 9U, UI_KEY_MOD_SHIFT);

    CHECK(runtime.end_node_handle_ == wrapped);
    CHECK(runtime.end_index_ == expected_wrapped_down);
    REQUIRE_FALSE(g_cross_selection_changes.empty());

    const float preferred_abs_x = wrapped_node->abs_x + first_line_local_x;
    const std::uint32_t expected_above_up =
        runtime.GetStringIndexFromPoint(*above_node, preferred_abs_x - above_node->abs_x, above_node->line_height * 0.5f);

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_dirty_ = true;
    runtime.start_node_handle_ = wrapped;
    runtime.start_index_ = first_line_anchor;
    runtime.end_node_handle_ = wrapped;
    runtime.end_index_ = first_line_selection_end;
    runtime.SetFocus(wrapped);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowUp"), 7U, UI_KEY_MOD_SHIFT);

    CHECK(runtime.end_node_handle_ == above);
    CHECK(runtime.end_index_ == expected_above_up);
    CHECK(runtime.focused_handle_ == above);
    REQUIRE_FALSE(g_cross_selection_changes.empty());

    wrapped_node = runtime.ResolveMutable(wrapped);
    REQUIRE(wrapped_node != nullptr);
    const std::size_t last_line = wrapped_node->visible_line_count - 1U;
    const std::uint32_t last_line_start = static_cast<std::uint32_t>(wrapped_node->break_offsets[last_line]);
    const std::uint32_t last_line_end = static_cast<std::uint32_t>(wrapped_node->break_offsets[last_line + 1U]);
    REQUIRE(last_line_end > last_line_start);
    const std::uint32_t last_line_anchor = last_line_start;
    const std::uint32_t last_line_selection_end = std::min(last_line_start + 1U, last_line_end);
    REQUIRE(last_line_selection_end > last_line_anchor);
    const auto [last_line_local_x, last_line_index] =
        runtime.GetLocalPositionFromIndex(*wrapped_node, last_line_selection_end);
    REQUIRE(static_cast<std::size_t>(last_line_index) == last_line);
    const std::uint32_t expected_below_down =
        runtime.GetStringIndexFromPoint(*below_node, (wrapped_node->abs_x + last_line_local_x) - below_node->abs_x, below_node->line_height * 0.5f);

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_dirty_ = true;
    runtime.start_node_handle_ = wrapped;
    runtime.start_index_ = last_line_anchor;
    runtime.end_node_handle_ = wrapped;
    runtime.end_index_ = last_line_selection_end;
    runtime.SetFocus(wrapped);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowDown"), 9U, UI_KEY_MOD_SHIFT);

    CHECK(runtime.end_node_handle_ == below);
    CHECK(runtime.end_index_ == expected_below_down);
    CHECK(runtime.focused_handle_ == below);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
}

TEST_CASE("v2 ui mouse drag cross-selection keeps its anchor for shift horizontal keys", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, second}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_set_interactive(child, true);
        ui_node_add_child(root, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("World"), 5U);
    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* second_node = GetRuntime().Resolve(second);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*first_node, 2U);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*second_node, 3U);
    REQUIRE(start_line == 0);
    REQUIRE(end_line == 0);

    ui_on_pointer_event(
        UI_EVENT_POINTER_DOWN,
        first,
        first_node->abs_x + start_x + 0.5f,
        first_node->abs_y + (first_node->line_height * 0.5f));
    ui_on_pointer_event(
        UI_EVENT_POINTER_MOVE,
        second,
        second_node->abs_x + end_x + 0.5f,
        second_node->abs_y + (second_node->line_height * 0.5f));
    ui_on_pointer_event(
        UI_EVENT_POINTER_UP,
        second,
        second_node->abs_x + end_x + 0.5f,
        second_node->abs_y + (second_node->line_height * 0.5f));

    CHECK(GetRuntime().cross_selection_active_);
    CHECK(GetRuntime().start_node_handle_ == first);
    CHECK(GetRuntime().start_index_ == 2U);
    CHECK(GetRuntime().end_node_handle_ == second);
    CHECK(GetRuntime().end_index_ == 3U);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);

    CHECK(GetRuntime().start_node_handle_ == first);
    CHECK(GetRuntime().start_index_ == 2U);
    CHECK(GetRuntime().end_node_handle_ == second);
    CHECK(GetRuntime().end_index_ == 4U);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
}

TEST_CASE("v2 ui cross-selection helpers cover invalid state and direct endpoint queries", "[v2][ui][coverage][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    auto& runtime = GetRuntime();
    const std::uint64_t area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t empty_area = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(area != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);
    REQUIRE(empty_area != UI_INVALID_HANDLE);

    ui_set_root(area);
    ui_set_selection_area(area, true);
    ui_set_width(area, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(area, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(area, 1U);
    for (const std::uint64_t child : {first, second}) {
        ui_set_width(child, 100.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(area, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Alpha"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("Beta"), 4U);
    ui_commit_frame();

    const std::uint64_t stale = (static_cast<std::uint64_t>(99999U) << 32U) | 7U;
    CHECK(runtime.FindSelectionAreaAncestor(stale) == UI_INVALID_HANDLE);

    std::vector<std::uint64_t> collected{};
    runtime.CollectSelectionAreaNodes(stale, collected);
    CHECK(collected.empty());

    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first};
    runtime.selection_area_nodes_dirty_ = true;
    runtime.EnsureSelectionAreaNodes(UI_INVALID_HANDLE);
    CHECK(runtime.selection_area_handle_ == UI_INVALID_HANDLE);
    CHECK(runtime.selection_area_nodes_.empty());

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 1U;
    runtime.end_node_handle_ = second;
    runtime.end_index_ = 2U;
    CHECK(runtime.FindSelectionAreaNodeIndex(area) == -1);
    CHECK(runtime.BuildCrossSelectionText() == "lpha Be");

    auto* first_node = runtime.ResolveMutable(first);
    REQUIRE(first_node != nullptr);
    first_node->parent_handle = UI_INVALID_HANDLE;
    CHECK(runtime.BuildCrossSelectionText() == "lpha\nBe");
    first_node->parent_handle = area;

    std::uint32_t highlight_start = 0U;
    std::uint32_t highlight_end = 0U;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 1U;
    runtime.end_node_handle_ = first;
    runtime.end_index_ = 4U;
    REQUIRE(runtime.GetCrossSelectionHighlight(first, highlight_start, highlight_end));
    CHECK(highlight_start == 1U);
    CHECK(highlight_end == 4U);
    CHECK_FALSE(runtime.GetCrossSelectionHighlight(second, highlight_start, highlight_end));

    runtime.start_index_ = 2U;
    runtime.end_index_ = 2U;
    CHECK_FALSE(runtime.GetCrossSelectionHighlight(first, highlight_start, highlight_end));

    runtime.start_node_handle_ = stale;
    CHECK(runtime.BuildCrossSelectionText().empty());
    CHECK_FALSE(runtime.GetCrossSelectionHighlight(first, highlight_start, highlight_end));

    runtime.cross_selection_active_ = false;
    CHECK_FALSE(runtime.UpdateCrossSelectionEndpoint(first, 0.0f, 0.0f));

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = empty_area;
    runtime.selection_area_nodes_.clear();
    runtime.selection_area_nodes_dirty_ = true;
    CHECK_FALSE(runtime.UpdateCrossSelectionEndpoint(UI_INVALID_HANDLE, 4.0f, 4.0f));

    const auto* second_node = runtime.Resolve(second);
    REQUIRE(second_node != nullptr);
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 0U;
    runtime.end_node_handle_ = first;
    runtime.end_index_ = 0U;
    REQUIRE(runtime.UpdateCrossSelectionEndpoint(
        UI_INVALID_HANDLE,
        second_node->abs_x + 2.0f,
        second_node->abs_y + (second_node->line_height * 0.5f)));
    CHECK(runtime.end_node_handle_ == second);
    CHECK_FALSE(runtime.UpdateCrossSelectionEndpoint(
        UI_INVALID_HANDLE,
        second_node->abs_x + 2.0f,
        second_node->abs_y + (second_node->line_height * 0.5f)));
    CHECK_FALSE(runtime.UpdateCrossSelectionEndpoint(UI_INVALID_HANDLE, -50.0f, -50.0f));

    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 1U;
    runtime.end_node_handle_ = first;
    runtime.end_index_ = 1U;
    runtime.NotifyCrossSelectionChanged();
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().text.empty());

    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.cross_selection_active_ = true;
    runtime.ClearCrossSelection(true);
    CHECK(g_cross_selection_changes.back().handle == area);
    CHECK(g_cross_selection_changes.back().text.empty());

    runtime.cross_selection_active_ = false;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_.clear();
    CHECK(runtime.BuildCrossSelectionText().empty());

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, stale, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 0U;
    runtime.end_node_handle_ = second;
    runtime.end_index_ = 4U;
    CHECK(runtime.BuildCrossSelectionText() == "Alpha\nBeta");

    CHECK_FALSE(runtime.GetCrossSelectionHighlight(stale, highlight_start, highlight_end));
    const std::uint64_t other_area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t other_text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(other_area != UI_INVALID_HANDLE);
    REQUIRE(other_text != UI_INVALID_HANDLE);
    ui_set_selection_area(other_area, true);
    ui_set_font(other_text, 1U, 20.0f);
    ui_set_selectable(other_text, true, 0x40007AFFU);
    ui_set_text(other_text, reinterpret_cast<const std::uint8_t*>("Other"), 5U);
    ui_node_add_child(other_area, other_text);
    CHECK_FALSE(runtime.GetCrossSelectionHighlight(other_text, highlight_start, highlight_end));

    runtime.selection_area_nodes_ = {stale, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 0U;
    runtime.end_node_handle_ = first;
    runtime.end_index_ = 0U;
    REQUIRE(runtime.UpdateCrossSelectionEndpoint(
        UI_INVALID_HANDLE,
        second_node->abs_x + 2.0f,
        second_node->abs_y + (second_node->line_height * 0.5f)));
    CHECK(runtime.end_node_handle_ == second);
}

TEST_CASE("v2 ui cross-selection navigation helpers cover word, horizontal, vertical, and no-op branches", "[v2][ui][coverage][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    auto& runtime = GetRuntime();
    const std::uint64_t empty_area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t wrapped = ui_create_node(UI_NODE_TEXT);
    REQUIRE(empty_area != UI_INVALID_HANDLE);
    REQUIRE(area != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);
    REQUIRE(wrapped != UI_INVALID_HANDLE);

    ui_set_root(area);
    ui_set_selection_area(area, true);
    ui_set_width(area, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(area, 220.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {first, second, wrapped}) {
        ui_set_width(child, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(child, 1U, 20.0f);
        ui_set_selectable(child, true, 0x40007AFFU);
        ui_node_add_child(area, child);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Alpha"), 5U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("Beta"), 4U);
    ui_set_text(wrapped, reinterpret_cast<const std::uint8_t*>("wrap wrap wrap wrap"), 19U);
    ui_set_width(wrapped, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_commit_frame();

    auto* first_node = runtime.ResolveMutable(first);
    auto* second_node = runtime.ResolveMutable(second);
    auto* wrapped_node = runtime.ResolveMutable(wrapped);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    REQUIRE(wrapped_node != nullptr);

    runtime.focused_handle_ = first;
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(empty_area, *first_node, "Home", UI_KEY_MOD_SHIFT));
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *first_node, "A", UI_KEY_MOD_SHIFT));

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 0U;
    runtime.end_node_handle_ = UI_INVALID_HANDLE;
    runtime.end_index_ = 0U;
    first_node->selection_end = 1U;
    REQUIRE(runtime.HandleCrossSelectionNavigation(area, *first_node, "ArrowRight", UI_KEY_MOD_SHIFT));
    CHECK(runtime.end_node_handle_ == first);
    CHECK(runtime.end_index_ == 2U);

    runtime.cross_selection_active_ = false;
    runtime.selection_area_nodes_.clear();
    runtime.selection_area_nodes_dirty_ = true;
    first_node->selection_end = 0U;
    runtime.focused_handle_ = first;
    REQUIRE(runtime.HandleCrossSelectionNavigation(area, *first_node, "End", UI_KEY_MOD_SHIFT));
    CHECK(runtime.end_node_handle_ == wrapped);
    CHECK(runtime.end_index_ == wrapped_node->text_content.size());

    runtime.cross_selection_active_ = false;
    runtime.focused_handle_ = second;
    second_node->selection_end = 0U;
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *second_node, "ArrowLeft", UI_KEY_MOD_SHIFT));

    runtime.cross_selection_active_ = false;
    runtime.focused_handle_ = first;
    first_node->selection_end = 0U;
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *first_node, "ArrowRight", UI_KEY_MOD_SHIFT | UI_KEY_MOD_CTRL));

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = second;
    runtime.start_index_ = 1U;
    runtime.end_node_handle_ = second;
    runtime.end_index_ = 0U;
    runtime.focused_handle_ = second;
    REQUIRE(runtime.HandleCrossSelectionNavigation(area, *second_node, "ArrowLeft", UI_KEY_MOD_SHIFT));
    CHECK(runtime.end_node_handle_ == first);
    CHECK(runtime.end_index_ == first_node->text_content.size());

    runtime.cross_selection_active_ = false;
    runtime.selection_area_nodes_.clear();
    runtime.selection_area_nodes_dirty_ = true;
    runtime.focused_handle_ = wrapped;
    wrapped_node->selection_start = 0U;
    wrapped_node->selection_end = 1U;
    const auto [wrapped_down_x, wrapped_line] = runtime.GetLocalPositionFromIndex(*wrapped_node, wrapped_node->selection_end);
    REQUIRE(wrapped_line == 0);
    const std::uint32_t expected_wrapped_down =
        runtime.GetStringIndexFromPoint(*wrapped_node, wrapped_down_x, wrapped_node->line_height * 1.5f);
    REQUIRE(runtime.HandleCrossSelectionNavigation(area, *wrapped_node, "ArrowDown", UI_KEY_MOD_SHIFT));
    CHECK(runtime.end_node_handle_ == wrapped);
    CHECK(runtime.end_index_ == expected_wrapped_down);

    runtime.cross_selection_active_ = false;
    runtime.focused_handle_ = first;
    first_node->selection_start = 0U;
    first_node->selection_end = 0U;
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *first_node, "ArrowDown", UI_KEY_MOD_SHIFT));

    runtime.cross_selection_active_ = false;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.focused_handle_ = second;
    runtime.end_node_handle_ = second;
    runtime.end_index_ = 0U;
    second_node->selection_end = 0U;
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *second_node, "ArrowDown", UI_KEY_MOD_SHIFT));
    CHECK(runtime.end_node_handle_ == second);

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = first;
    runtime.start_index_ = 0U;
    runtime.end_node_handle_ = first;
    runtime.end_index_ = 0U;
    runtime.focused_handle_ = first;
    CHECK(runtime.HandleCrossSelectionNavigation(area, *first_node, "Home", UI_KEY_MOD_SHIFT));
    CHECK(runtime.end_node_handle_ == first);
    CHECK(runtime.end_index_ == 0U);

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.end_node_handle_ = UI_INVALID_HANDLE;
    runtime.end_index_ = 0U;
    runtime.focused_handle_ = UI_INVALID_HANDLE;
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *second_node, "Home", UI_KEY_MOD_SHIFT));

    const std::uint64_t stale = first;
    REQUIRE(runtime.DeleteNode(first));
    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {stale, second};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.end_node_handle_ = stale;
    runtime.end_index_ = 0U;
    runtime.focused_handle_ = second;
    second_node = runtime.ResolveMutable(second);
    REQUIRE(second_node != nullptr);
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *second_node, "Home", UI_KEY_MOD_SHIFT));

    const std::uint64_t lower = ui_create_node(UI_NODE_TEXT);
    REQUIRE(lower != UI_INVALID_HANDLE);
    ui_set_width(lower, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(lower, 1U, 20.0f);
    ui_set_selectable(lower, true, 0x40007AFFU);
    ui_set_text(lower, reinterpret_cast<const std::uint8_t*>("Lower"), 5U);
    ui_node_add_child(area, lower);
    ui_commit_frame();

    const auto* lower_node = runtime.Resolve(lower);
    second_node = runtime.ResolveMutable(second);
    REQUIRE(lower_node != nullptr);
    REQUIRE(second_node != nullptr);
    wrapped_node = runtime.ResolveMutable(wrapped);
    REQUIRE(wrapped_node != nullptr);
    const std::size_t wrapped_last_line = wrapped_node->visible_line_count - 1U;
    const std::uint32_t wrapped_last_line_start = static_cast<std::uint32_t>(wrapped_node->break_offsets[wrapped_last_line]);
    const std::uint32_t wrapped_last_line_end = static_cast<std::uint32_t>(wrapped_node->break_offsets[wrapped_last_line + 1U]);
    REQUIRE(wrapped_last_line_end > wrapped_last_line_start);
    const std::uint32_t wrapped_last_selection_end = std::min(wrapped_last_line_start + 1U, wrapped_last_line_end);
    REQUIRE(wrapped_last_selection_end > wrapped_last_line_start);
    const auto [wrapped_last_x, wrapped_last_line_index] =
        runtime.GetLocalPositionFromIndex(*wrapped_node, wrapped_last_selection_end);
    REQUIRE(static_cast<std::size_t>(wrapped_last_line_index) == wrapped_last_line);
    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {first, second, wrapped, lower};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.start_node_handle_ = wrapped;
    runtime.start_index_ = wrapped_last_line_start;
    runtime.end_node_handle_ = wrapped;
    runtime.end_index_ = wrapped_last_selection_end;
    runtime.focused_handle_ = wrapped;
    const std::uint32_t expected_lower_down = runtime.GetStringIndexFromPoint(
        *lower_node,
        (wrapped_node->abs_x + wrapped_last_x) - lower_node->abs_x,
        lower_node->line_height * 0.5f);
    REQUIRE(runtime.HandleCrossSelectionNavigation(area, *wrapped_node, "ArrowDown", UI_KEY_MOD_SHIFT));
    CHECK(runtime.end_node_handle_ == lower);
    CHECK(runtime.end_index_ == expected_lower_down);

    runtime.cross_selection_active_ = false;
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_ = {stale, second, lower};
    runtime.selection_area_nodes_dirty_ = false;
    runtime.focused_handle_ = second;
    second_node->selection_end = 0U;
    CHECK_FALSE(runtime.HandleCrossSelectionNavigation(area, *second_node, "ArrowDown", UI_KEY_MOD_SHIFT));
}

TEST_CASE("v2 ui selection-area invalidation clears cross-selection on tree mutations", "[v2][ui][coverage][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    auto& runtime = GetRuntime();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t extra = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t replacement_root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    REQUIRE(extra != UI_INVALID_HANDLE);
    REQUIRE(replacement_root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(root, true);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Alpha"), 5U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_ = {text};
    runtime.start_node_handle_ = text;
    runtime.end_node_handle_ = text;
    runtime.selection_area_nodes_dirty_ = false;
    REQUIRE(runtime.AddChild(root, extra));
    CHECK(runtime.selection_area_nodes_dirty_);

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_ = {text};
    runtime.start_node_handle_ = text;
    runtime.end_node_handle_ = text;
    runtime.selection_area_nodes_dirty_ = false;
    REQUIRE(runtime.RemoveChild(root, text));
    CHECK_FALSE(runtime.cross_selection_active_);

    REQUIRE(runtime.AddChild(root, text));
    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_ = {text};
    runtime.start_node_handle_ = text;
    runtime.end_node_handle_ = text;
    runtime.selection_area_nodes_dirty_ = false;
    REQUIRE(runtime.DeleteNode(text));
    CHECK_FALSE(runtime.cross_selection_active_);

    const std::uint64_t replacement_text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(replacement_text != UI_INVALID_HANDLE);
    ui_set_font(replacement_text, 1U, 20.0f);
    ui_set_selectable(replacement_text, true, 0x40007AFFU);
    ui_set_text(replacement_text, reinterpret_cast<const std::uint8_t*>("Beta"), 4U);
    ui_node_add_child(root, replacement_text);

    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_ = {replacement_text};
    runtime.start_node_handle_ = replacement_text;
    runtime.end_node_handle_ = replacement_text;
    runtime.selection_area_nodes_dirty_ = false;
    REQUIRE(runtime.SetRoot(replacement_root));
    CHECK_FALSE(runtime.cross_selection_active_);

    ui_set_root(root);
    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = root;
    runtime.selection_area_nodes_ = {replacement_text};
    runtime.start_node_handle_ = replacement_text;
    runtime.end_node_handle_ = replacement_text;
    runtime.selection_area_nodes_dirty_ = false;
    REQUIRE(runtime.SetSelectionArea(root, false));
    CHECK_FALSE(runtime.cross_selection_active_);

    CHECK_FALSE(runtime.SetSelectionArea(UI_INVALID_HANDLE, true));
}
