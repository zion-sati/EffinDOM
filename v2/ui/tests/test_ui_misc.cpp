#include "TestUiSupport.h"

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


TEST_CASE("v2 ui fill sizing respects padding and margins", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fixed = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fill = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(fixed != UI_INVALID_HANDLE);
    REQUIRE(fill != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(200.0f, 100.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);

    ui_set_width(fixed, 50.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(fixed, 20.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_fill_width(fill, true);
    ui_set_fill_height(fill, true);
    ui_set_margin(fill, 5.0f, 5.0f, 5.0f, 5.0f);

    ui_node_add_child(root, fixed);
    ui_node_add_child(root, fill);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(fixed) != bounds.end());
    REQUIRE(bounds.find(fill) != bounds.end());

    CHECK(bounds.at(fixed).width == Approx(50.0f).margin(0.01f));
    CHECK(bounds.at(fixed).height == Approx(20.0f).margin(0.01f));
    CHECK(bounds.at(fill).width == Approx(120.0f).margin(0.01f));
    CHECK(bounds.at(fill).height == Approx(70.0f).margin(0.01f));
}


TEST_CASE("v2 ui percent width sizes the box while fill width uses offered space", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t percent = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fill = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(percent != UI_INVALID_HANDLE);
    REQUIRE(fill != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(200.0f, 120.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);

    ui_set_width(percent, 100.0f, UI_SIZE_UNIT_PERCENT);
    ui_set_height(percent, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(percent, 5.0f, 0.0f, 5.0f, 0.0f);

    ui_set_fill_width(fill, true);
    ui_set_height(fill, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(fill, 5.0f, 0.0f, 5.0f, 0.0f);

    ui_node_add_child(root, percent);
    ui_node_add_child(root, fill);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(percent) != bounds.end());
    REQUIRE(bounds.find(fill) != bounds.end());

    CHECK(bounds.at(percent).width == Approx(180.0f).margin(0.01f));
    CHECK(bounds.at(fill).width == Approx(170.0f).margin(0.01f));
}


TEST_CASE("v2 ui percent height sizes the box while fill height uses offered space", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fixed = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t percent = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fill = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(fixed != UI_INVALID_HANDLE);
    REQUIRE(percent != UI_INVALID_HANDLE);
    REQUIRE(fill != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(200.0f, 220.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 0U);
    ui_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);

    ui_set_height(fixed, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(fixed, 20.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_height(percent, 100.0f, UI_SIZE_UNIT_PERCENT);
    ui_set_width(percent, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(percent, 0.0f, 5.0f, 0.0f, 5.0f);

    ui_set_fill_height(fill, true);
    ui_set_width(fill, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(fill, 0.0f, 5.0f, 0.0f, 5.0f);

    ui_node_add_child(root, fixed);
    ui_node_add_child(root, percent);
    ui_node_add_child(root, fill);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(percent) != bounds.end());
    REQUIRE(bounds.find(fill) != bounds.end());

    CHECK(bounds.at(percent).height == Approx(200.0f).margin(0.01f));
    CHECK(bounds.at(fill).height == Approx(150.0f).margin(0.01f));
}


TEST_CASE("v2 ui root available-space percent fill resolves from window size", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 200.0f);
    ui_set_fill_width_percent(root, 50.0f);
    ui_set_fill_height_percent(root, 25.0f);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(root) != bounds.end());

    CHECK(bounds.at(root).width == Approx(160.0f).margin(0.01f));
    CHECK(bounds.at(root).height == Approx(50.0f).margin(0.01f));
}


TEST_CASE("v2 ui available-space percent fill resolves against offered space and min max clamps", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fixed = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t percent = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(fixed != UI_INVALID_HANDLE);
    REQUIRE(percent != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 140.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);

    ui_set_width(fixed, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(fixed, 20.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_fill_width_percent(percent, 50.0f);
    ui_set_fill_height_percent(percent, 50.0f);
    ui_set_min_width(percent, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_max_height(percent, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(percent, 5.0f, 4.0f, 5.0f, 4.0f);

    ui_node_add_child(root, fixed);
    ui_node_add_child(root, percent);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(percent) != bounds.end());

    CHECK(bounds.at(percent).width == Approx(85.0f).margin(0.05f));
    CHECK(bounds.at(percent).height == Approx(40.0f).margin(0.05f));
}


TEST_CASE("v2 ui percent min and max clamps apply during layout", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fixed = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t percent = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(fixed != UI_INVALID_HANDLE);
    REQUIRE(percent != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(220.0f, 140.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);

    ui_set_width(fixed, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(fixed, 20.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_fill_width_percent(percent, 80.0f);
    ui_set_height(percent, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(percent, 5.0f, 0.0f, 5.0f, 0.0f);
    ui_set_max_width(percent, 50.0f, UI_SIZE_UNIT_PERCENT);
    ui_set_min_width(percent, 40.0f, UI_SIZE_UNIT_PERCENT);

    ui_node_add_child(root, fixed);
    ui_node_add_child(root, percent);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(percent) != bounds.end());

    CHECK(bounds.at(percent).width == Approx(100.0f).margin(0.05f));
}


TEST_CASE("v2 ui switching sizing modes clears stale axis state", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fixed = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(fixed != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(200.0f, 120.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);
    ui_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);

    ui_set_width(fixed, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(fixed, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(child, 5.0f, 0.0f, 5.0f, 0.0f);
    ui_node_add_child(root, fixed);
    ui_node_add_child(root, child);

    ui_set_width(child, 100.0f, UI_SIZE_UNIT_PERCENT);
    ui_commit_frame();
    auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(child) != bounds.end());
    CHECK(bounds.at(child).width == Approx(180.0f).margin(0.01f));

    ui_set_fill_width_percent(child, 50.0f);
    ui_commit_frame();
    bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(child) != bounds.end());
    CHECK(bounds.at(child).width == Approx(65.0f).margin(0.05f));

    ui_set_fill_width(child, true);
    ui_commit_frame();
    bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(child) != bounds.end());
    CHECK(bounds.at(child).width == Approx(130.0f).margin(0.05f));
}


TEST_CASE("v2 ui switching height sizing modes clears stale axis state", "[v2][ui][layout]") {
    ui_reset();

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t fixed = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(fixed != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(160.0f, 220.0f);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 0U);
    ui_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);

    ui_set_height(fixed, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(fixed, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(child, 0.0f, 5.0f, 0.0f, 5.0f);
    ui_node_add_child(root, fixed);
    ui_node_add_child(root, child);

    ui_set_height(child, 100.0f, UI_SIZE_UNIT_PERCENT);
    ui_commit_frame();
    auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(child) != bounds.end());
    CHECK(bounds.at(child).height == Approx(200.0f).margin(0.01f));

    ui_set_fill_height_percent(child, 50.0f);
    ui_commit_frame();
    bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(child) != bounds.end());
    CHECK(bounds.at(child).height == Approx(75.0f).margin(0.05f));

    ui_set_fill_height(child, true);
    ui_commit_frame();
    bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(child) != bounds.end());
    CHECK(bounds.at(child).height == Approx(150.0f).margin(0.05f));
}


TEST_CASE("v2 ui align-self can opt a child out of implicit cross-axis stretch", "[v2][ui][layout]") {
    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t stretched = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t child = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(stretched != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 0U);

    ui_set_height(stretched, 12.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_align_self(child, UI_ALIGN_SELF_START);
    ui_set_padding(child, 6.0f, 4.0f, 6.0f, 4.0f);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hi"), 2U);

    ui_node_add_child(root, stretched);
    ui_node_add_child(child, text);
    ui_node_add_child(root, child);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(stretched) != bounds.end());
    REQUIRE(bounds.find(child) != bounds.end());
    REQUIRE(bounds.find(text) != bounds.end());

    CHECK(bounds.at(stretched).width == Approx(240.0f).margin(0.01f));
    CHECK(bounds.at(child).width < (240.0f + 0.01f));
    CHECK(bounds.at(child).width == Approx(bounds.at(text).width + 12.0f).margin(1.5f));
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

