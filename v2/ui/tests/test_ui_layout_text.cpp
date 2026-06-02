#include "TestUiSupport.h"

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
    CHECK(GetRuntime().SetFillWidthPercent(box, 60.0f));
    CHECK_FALSE(GetRuntime().SetFillWidthPercent(box, -1.0f));
    CHECK(GetRuntime().SetFillHeightPercent(box, 40.0f));
    CHECK_FALSE(GetRuntime().SetFillHeightPercent(box, -1.0f));
    CHECK(GetRuntime().SetMinWidth(box, 12.0f, UI_SIZE_UNIT_PIXEL));
    CHECK(GetRuntime().SetMaxWidth(box, 80.0f, UI_SIZE_UNIT_PERCENT));
    CHECK(GetRuntime().SetMinHeight(box, 14.0f, UI_SIZE_UNIT_PIXEL));
    CHECK(GetRuntime().SetMaxHeight(box, 90.0f, UI_SIZE_UNIT_PERCENT));
    CHECK(GetRuntime().SetMinWidth(box, 0.0f, UI_SIZE_UNIT_AUTO));
    CHECK(GetRuntime().SetMaxWidth(box, 0.0f, UI_SIZE_UNIT_AUTO));
    CHECK(GetRuntime().SetMinHeight(box, 0.0f, UI_SIZE_UNIT_AUTO));
    CHECK(GetRuntime().SetMaxHeight(box, 0.0f, UI_SIZE_UNIT_AUTO));
    CHECK_FALSE(GetRuntime().SetMinWidth(box, 1.0f, 99U));
    CHECK_FALSE(GetRuntime().SetMaxHeight(box, 1.0f, 99U));

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
    CHECK(GetRuntime().SetAlignSelf(box, UI_ALIGN_SELF_AUTO));
    CHECK(GetRuntime().SetAlignSelf(box, UI_ALIGN_SELF_START));
    CHECK(GetRuntime().SetAlignSelf(box, UI_ALIGN_SELF_CENTER));
    CHECK(GetRuntime().SetAlignSelf(box, UI_ALIGN_SELF_END));
    CHECK(GetRuntime().SetAlignSelf(box, UI_ALIGN_SELF_STRETCH));
    CHECK_FALSE(GetRuntime().SetAlignSelf(box, 99U));

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
    CHECK(GetRuntime().SetFillWidth(box, true));
    CHECK(GetRuntime().SetFillHeight(box, true));
    CHECK(GetRuntime().Resolve(box)->fill_width);
    CHECK(GetRuntime().Resolve(box)->fill_height);

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


