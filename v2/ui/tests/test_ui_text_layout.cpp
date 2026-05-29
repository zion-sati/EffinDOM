#include "TestUiSupport.h"

TEST_CASE("v2 ui private paragraph helpers cover multiline utility branches", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size())));

    CHECK(GetRuntime().ComputeBreakCandidates("").size() == 1U);
    const auto candidates = GetRuntime().ComputeBreakCandidates("The quick brown fox");
    CHECK(candidates.size() > 2U);

    const auto breaks = GetRuntime().ComputeLineBreaks("The quick brown fox", 40.0f, 1U, 20.0f, false);
    CHECK(breaks.size() > 2U);
    CHECK(GetRuntime().ComputeLineBreaks("", 40.0f, 1U, 20.0f, false) == std::vector<std::int32_t>{0});
    CHECK(GetRuntime().ComputeLineBreaks("supercalifragilistic", 8.0f, 1U, 20.0f, false).size() > 2U);
    CHECK(GetRuntime().ComputeLineBreaks("alpha supercalifragilistic", 40.0f, 1U, 20.0f, false).size() > 3U);

    effindom::v2::ui::UiRuntime::ShapedTextRun shaped{};
    CHECK(GetRuntime().ShapeText("", 1U, 20.0f, shaped));
    CHECK(shaped.height > 0.0f);
    CHECK(shaped.baseline > 0.0f);
    CHECK(shaped.baseline <= shaped.height);
    CHECK(GetRuntime().ShapeText("Ag", 1U, 20.0f, shaped));
    REQUIRE(shaped.glyphs.size() >= 2U);
    CHECK(GetRuntime().ShapeText("MMMM", 1U, 20.0f, shaped));
    CHECK(shaped.width > 50.0f);
    CHECK(std::abs(shaped.glyphs.front().y) < 0.01f);
    CHECK(std::abs(shaped.glyphs.back().y) < 0.01f);
    CHECK_FALSE(GetRuntime().ShapeText("A", 999U, 20.0f, shaped));

    effindom::v2::ui::UINode plain_node{};
    CHECK(GetRuntime().LayoutParagraph(plain_node, 40.0f).height == Approx(0.0f));
    CHECK(GetRuntime().MeasureTextNode(plain_node, 40.0f, YGMeasureModeExactly).height == Approx(0.0f));
    plain_node.is_text_node = true;
    plain_node.text_content = "Missing";
    plain_node.font_id = 999U;
    plain_node.font_size = 20.0f;
    CHECK(GetRuntime().LayoutParagraph(plain_node, 40.0f).height == Approx(0.0f));

    const auto text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_width(text, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("abcdef ghijkl"), 13U);

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const YGSize measured = GetRuntime().MeasureTextNode(*node, 60.0f, YGMeasureModeExactly);
    CHECK(measured.width == Approx(60.0f));
    CHECK(measured.height > 24.0f);
}

TEST_CASE("v2 ui non-wrapping fragment metadata caches and resolves overscanned windows", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size())));

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    for (int index = 0; index < 600; index += 1) {
        node.text_content += "W";
    }

    const auto paragraph = GetRuntime().LayoutParagraph(node, 80.0f);
    REQUIRE(paragraph.total_line_count == 1U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragment_line_offsets.size() == 2U);
    REQUIRE(node.nonwrap_fragment_line_offsets.front() == 0U);
    REQUIRE(node.nonwrap_fragment_line_offsets.back() == node.nonwrap_fragments.size());
    REQUIRE(node.nonwrap_fragments.size() >= 3U);
    CHECK(node.nonwrap_fragment_cache_generation == 1U);
    CHECK(node.nonwrap_fragments.front().local_byte_start == 0U);
    CHECK(node.nonwrap_fragments.back().local_byte_end == static_cast<std::uint32_t>(node.text_content.size()));
    CHECK(node.nonwrap_fragments.front().x == Approx(0.0f));
    CHECK(node.nonwrap_fragments.back().x + node.nonwrap_fragments.back().width == Approx(paragraph.max_line_width).margin(0.01f));

    for (std::size_t index = 0; index < node.nonwrap_fragments.size(); index += 1U) {
        const auto& fragment = node.nonwrap_fragments[index];
        CHECK(fragment.line_index == 0U);
        CHECK(fragment.local_byte_start < fragment.local_byte_end);
        CHECK(fragment.width > 0.0f);
        if (index > 0U) {
            const auto& previous = node.nonwrap_fragments[index - 1U];
            CHECK(fragment.local_byte_start == previous.local_byte_end);
            CHECK(fragment.x == Approx(previous.x + previous.width).margin(0.01f));
        }
    }

    const std::uint32_t cached_generation = node.nonwrap_fragment_cache_generation;
    const auto cached_layout = GetRuntime().LayoutParagraph(node, 240.0f);
    CHECK(cached_layout.max_line_width == Approx(paragraph.max_line_width));
    CHECK(node.nonwrap_fragment_cache_generation == cached_generation);

    const std::size_t middle_index = node.nonwrap_fragments.size() / 2U;
    REQUIRE(middle_index > 0U);
    REQUIRE(middle_index + 1U < node.nonwrap_fragments.size());
    const auto& middle_fragment = node.nonwrap_fragments[middle_index];
    const auto window = GetRuntime().ResolveNonWrappingFragmentWindow(
        node,
        0U,
        middle_fragment.x + 1.0f,
        middle_fragment.x + middle_fragment.width - 1.0f);
    CHECK(window.start == middle_index - 1U);
    CHECK(window.end == middle_index + 2U);

    CHECK(GetRuntime().ResolveNonWrappingFragmentWindow(node, 0U, 10.0f, 10.0f).start == 0U);
    CHECK(GetRuntime().ResolveNonWrappingFragmentWindow(node, 0U, 10.0f, 10.0f).end == 0U);
    CHECK(GetRuntime().ResolveNonWrappingFragmentWindow(node, 9U, 0.0f, 50.0f).start == 0U);
    CHECK(GetRuntime().ResolveNonWrappingFragmentWindow(node, 9U, 0.0f, 50.0f).end == 0U);

    GetRuntime().InvalidateTextLayoutCache(node);
    CHECK_FALSE(node.nonwrap_fragment_cache_valid);
    CHECK(node.nonwrap_fragments.empty());
    CHECK(node.nonwrap_fragment_line_offsets.empty());
    (void)GetRuntime().LayoutParagraph(node, 120.0f);
    CHECK(node.nonwrap_fragment_cache_generation == cached_generation + 1U);
}

TEST_CASE("v2 ui non-wrapping fragment metadata handles wrapped and empty logical lines", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size())));

    effindom::v2::ui::UINode wrapped{};
    wrapped.is_text_node = true;
    wrapped.text_wrap = true;
    wrapped.font_id = 1U;
    wrapped.font_size = 20.0f;
    wrapped.text_content = "wrapped text should not build non-wrap fragments";
    (void)GetRuntime().LayoutParagraph(wrapped, 50.0f);
    CHECK_FALSE(wrapped.nonwrap_fragment_cache_valid);
    CHECK(wrapped.nonwrap_fragments.empty());
    CHECK(wrapped.nonwrap_fragment_line_offsets.empty());

    effindom::v2::ui::UINode empty_nonwrap{};
    empty_nonwrap.is_text_node = true;
    empty_nonwrap.text_wrap = false;
    empty_nonwrap.font_id = 1U;
    empty_nonwrap.font_size = 20.0f;
    const auto empty_layout = GetRuntime().LayoutParagraph(empty_nonwrap, 80.0f);
    CHECK(empty_layout.total_line_count == 1U);
    CHECK(empty_nonwrap.nonwrap_fragment_cache_valid);
    CHECK(empty_nonwrap.nonwrap_fragments.empty());
    REQUIRE(empty_nonwrap.nonwrap_fragment_line_offsets.size() == 2U);
    CHECK(empty_nonwrap.nonwrap_fragment_line_offsets[0] == 0U);
    CHECK(empty_nonwrap.nonwrap_fragment_line_offsets[1] == 0U);
    CHECK(empty_nonwrap.nonwrap_fragment_cache_generation == 1U);

    effindom::v2::ui::UINode multiline{};
    multiline.is_text_node = true;
    multiline.text_wrap = false;
    multiline.font_id = 1U;
    multiline.font_size = 20.0f;
    multiline.text_content = "alpha\n\nbeta";
    const auto paragraph = GetRuntime().LayoutParagraph(multiline, 80.0f);
    REQUIRE(paragraph.total_line_count == 3U);
    REQUIRE(multiline.nonwrap_fragment_cache_valid);
    REQUIRE(multiline.nonwrap_fragment_line_offsets.size() == 4U);
    CHECK(multiline.nonwrap_fragment_line_offsets[1] > multiline.nonwrap_fragment_line_offsets[0]);
    CHECK(multiline.nonwrap_fragment_line_offsets[1] == multiline.nonwrap_fragment_line_offsets[2]);
    CHECK(multiline.nonwrap_fragment_line_offsets[3] > multiline.nonwrap_fragment_line_offsets[2]);

    const auto empty_line_window = GetRuntime().ResolveNonWrappingFragmentWindow(multiline, 1U, 0.0f, 100.0f);
    CHECK(empty_line_window.start == 0U);
    CHECK(empty_line_window.end == 0U);
}

TEST_CASE("v2 ui incremental non-wrap cache updates fragments and shifts downstream spans", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size())));

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz "
        "abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz";
    const auto initial_layout = GetRuntime().LayoutParagraph(node, 120.0f);
    REQUIRE(initial_layout.total_line_count == 1U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragments.size() > 2U);

    const auto original_fragments = node.nonwrap_fragments;
    const float original_width = node.line_widths.front();
    const std::uint32_t original_generation = node.nonwrap_fragment_cache_generation;

    const std::string original_text = node.text_content;
    node.text_content.insert(0U, "Z");
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, original_text));
    CHECK(node.text_layout_cache_valid);
    CHECK(node.nonwrap_fragment_cache_valid);
    CHECK(node.nonwrap_fragment_cache_generation == original_generation + 1U);
    REQUIRE(node.break_offsets.size() == 2U);
    CHECK(node.break_offsets[0] == 0);
    CHECK(node.break_offsets[1] == static_cast<std::int32_t>(node.text_content.size()));
    REQUIRE(node.line_widths.size() == 1U);
    CHECK(node.line_widths.front() > original_width);
    REQUIRE(node.nonwrap_fragment_line_offsets.size() == 2U);
    CHECK(node.nonwrap_fragment_line_offsets[1] == node.nonwrap_fragments.size());
    CHECK_FALSE(node.nonwrap_render_fragment_window_valid);
    CHECK_FALSE(node.text_render_window_valid);

    const float insert_width_delta = node.line_widths.front() - original_width;
    REQUIRE_FALSE(node.nonwrap_fragments.empty());
    CHECK(node.nonwrap_fragments.back().local_byte_end == original_fragments.back().local_byte_end + 1U);
    CHECK(node.nonwrap_fragments.back().x + node.nonwrap_fragments.back().width ==
        Approx(original_fragments.back().x + original_fragments.back().width + insert_width_delta).margin(0.01f));

    const std::string inserted_text = node.text_content;
    node.text_content.erase(0U, 1U);
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, inserted_text));
    CHECK(node.line_widths.front() == Approx(original_width).margin(0.05f));
    CHECK(node.nonwrap_fragment_cache_generation == original_generation + 2U);
    CHECK(node.nonwrap_fragments.back().local_byte_end == original_fragments.back().local_byte_end);
    CHECK(node.nonwrap_fragments.back().x + node.nonwrap_fragments.back().width ==
        Approx(original_fragments.back().x + original_fragments.back().width).margin(0.05f));

    effindom::v2::ui::UINode empty_node{};
    empty_node.is_text_node = true;
    empty_node.text_wrap = false;
    empty_node.font_id = 1U;
    empty_node.font_size = 20.0f;
    (void)GetRuntime().LayoutParagraph(empty_node, 80.0f);
    REQUIRE(empty_node.nonwrap_fragment_cache_valid);
    REQUIRE(empty_node.nonwrap_fragments.empty());
    empty_node.text_content = "seed";
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(empty_node, std::string{}));
    CHECK(empty_node.text_layout_cache_valid);
    CHECK(empty_node.nonwrap_fragment_cache_valid);
    CHECK_FALSE(empty_node.nonwrap_fragments.empty());

    effindom::v2::ui::UINode wrapped = node;
    wrapped.text_wrap = true;
    CHECK_FALSE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(wrapped, std::string("x") + wrapped.text_content));

    effindom::v2::ui::UINode multiline{};
    multiline.is_text_node = true;
    multiline.text_wrap = false;
    multiline.font_id = 1U;
    multiline.font_size = 20.0f;
    multiline.text_content = "alpha\nbeta";
    (void)GetRuntime().LayoutParagraph(multiline, 120.0f);
    REQUIRE(multiline.logical_line_shape_cache_valid);
    const std::string multiline_before = multiline.text_content;
    multiline.text_content.insert(5U, "\n");
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(multiline, multiline_before));
    CHECK(multiline.break_offsets == std::vector<std::int32_t>{0, 5, 6, 11});
    REQUIRE(multiline.line_widths.size() == 3U);
    CHECK(multiline.line_widths[1] == Approx(0.0f).margin(0.01f));
    REQUIRE(multiline.nonwrap_fragment_line_offsets.size() == 4U);
    CHECK(multiline.nonwrap_fragment_line_offsets[2] == multiline.nonwrap_fragment_line_offsets[1]);
    REQUIRE(multiline.logical_line_shape_cache_valid);
    REQUIRE(multiline.logical_line_shapes.size() == 3U);

    effindom::v2::ui::UINode multiline_expected{};
    multiline_expected.is_text_node = true;
    multiline_expected.text_wrap = false;
    multiline_expected.font_id = multiline.font_id;
    multiline_expected.font_size = multiline.font_size;
    multiline_expected.text_content = multiline.text_content;
    const auto multiline_expected_layout = GetRuntime().LayoutParagraph(multiline_expected, 120.0f);

    REQUIRE(multiline.break_offsets == multiline_expected.break_offsets);
    REQUIRE(multiline.nonwrap_fragment_line_offsets == multiline_expected.nonwrap_fragment_line_offsets);
    REQUIRE(multiline.nonwrap_fragments.size() == multiline_expected.nonwrap_fragments.size());
    REQUIRE(multiline.line_widths.size() == multiline_expected.line_widths.size());
    CHECK(multiline.text_layout_cache_max_line_width == Approx(multiline_expected_layout.max_line_width).margin(0.05f));
    for (std::size_t index = 0; index < multiline.line_widths.size(); index += 1U) {
        CHECK(multiline.line_widths[index] == Approx(multiline_expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < multiline.nonwrap_fragments.size(); index += 1U) {
        const auto& patched = multiline.nonwrap_fragments[index];
        const auto& fresh = multiline_expected.nonwrap_fragments[index];
        CHECK(patched.line_index == fresh.line_index);
        CHECK(patched.local_byte_start == fresh.local_byte_start);
        CHECK(patched.local_byte_end == fresh.local_byte_end);
        CHECK(patched.x == Approx(fresh.x).margin(0.05f));
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui multiline non-wrap editing keeps visible line starts and patches only the touched line", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string long_line(5000U, 'W');

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.is_selectable = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.layout_width = 160.0f;
    node.text_content = std::string("short\n") + long_line;

    const auto paragraph = GetRuntime().LayoutParagraph(node, 120.0f);
    REQUIRE(paragraph.total_line_count == 2U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragment_line_offsets.size() == 3U);
    REQUIRE(node.nonwrap_fragment_line_offsets[2] > node.nonwrap_fragment_line_offsets[1]);

    const std::uint32_t second_line_start = static_cast<std::uint32_t>(std::string("short\n").size());
    CHECK(GetRuntime().IndexForLineBegin(node, static_cast<std::uint32_t>(node.text_content.size())) == second_line_start);
    CHECK(GetRuntime().IndexForLineEnd(node, second_line_start) == static_cast<std::uint32_t>(node.text_content.size()));

    node.selection_start = static_cast<std::uint32_t>(node.text_content.size());
    node.selection_end = static_cast<std::uint32_t>(node.text_content.size());
    REQUIRE(GetRuntime().HandleTextEditingKey(node, "Home", 0U));
    CHECK(node.selection_start == second_line_start);
    CHECK(node.selection_end == second_line_start);

    const std::uint32_t target_index = second_line_start + 160U;
    const auto [local_x, line_index] = GetRuntime().GetLocalPositionFromIndex(node, target_index);
    CHECK(line_index == 1);
    CHECK(GetRuntime().GetStringIndexFromPoint(node, local_x + 0.25f, node.line_height * 1.5f) == target_index);

    const std::uint32_t original_generation = node.nonwrap_fragment_cache_generation;
    const std::string previous_text = node.text_content;
    node.text_content.insert(1U, "!");
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, previous_text));
    CHECK(node.nonwrap_fragment_cache_generation == original_generation + 1U);
    CHECK(node.break_offsets.size() == 3U);
    CHECK(node.break_offsets[1] == 6);
    CHECK(node.break_offsets[2] == static_cast<std::int32_t>(node.text_content.size()));

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.is_selectable = true;
    expected.text_wrap = false;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.layout_width = node.layout_width;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 120.0f);

    REQUIRE(expected.break_offsets == node.break_offsets);
    REQUIRE(expected.nonwrap_fragment_line_offsets == node.nonwrap_fragment_line_offsets);
    REQUIRE(expected.nonwrap_fragments.size() == node.nonwrap_fragments.size());
    REQUIRE(expected.line_widths.size() == node.line_widths.size());
    CHECK(expected_layout.max_line_width == Approx(node.text_layout_cache_max_line_width).margin(0.05f));
    for (std::size_t index = 0; index < expected.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
}

TEST_CASE("v2 ui multiline non-wrap edits inside a long middle line keep fragment spans aligned with fresh layout", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string long_line(5000U, 'W');

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.text_wrap = false;
    node.max_lines = 0;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.layout_width = 220.0f;
    node.text_content = long_line + "\n" + long_line + "\n" + long_line;

    const auto paragraph = GetRuntime().LayoutParagraph(node, 160.0f);
    REQUIRE(paragraph.total_line_count == 3U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragment_line_offsets.size() == 4U);

    const std::uint32_t second_line_start = static_cast<std::uint32_t>(long_line.size() + 1U);
    const std::uint32_t caret_index = second_line_start + 2500U;
    const std::string previous_text = node.text_content;
    node.text_content.erase(static_cast<std::size_t>(caret_index - 1U), 1U);

    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, previous_text));

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.text_wrap = false;
    expected.max_lines = 0;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.layout_width = node.layout_width;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 160.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.nonwrap_fragment_line_offsets == expected.nonwrap_fragment_line_offsets);
    REQUIRE(node.nonwrap_fragments.size() == expected.nonwrap_fragments.size());
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    CHECK(expected_layout.max_line_width == Approx(node.text_layout_cache_max_line_width).margin(0.05f));
    for (std::size_t index = 0; index < expected.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < expected.nonwrap_fragments.size(); index += 1U) {
        const auto& patched = node.nonwrap_fragments[index];
        const auto& fresh = expected.nonwrap_fragments[index];
        CHECK(patched.line_index == fresh.line_index);
        CHECK(patched.local_byte_start == fresh.local_byte_start);
        CHECK(patched.local_byte_end == fresh.local_byte_end);
        CHECK(patched.x == Approx(fresh.x).margin(0.05f));
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui multiline non-wrap newline inserts patch only the touched document shard", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string long_line(5000U, 'W');
    std::string text{};
    for (std::size_t index = 0; index < 10U; index += 1U) {
        if (!text.empty()) {
            text.push_back('\n');
        }
        text += long_line;
    }

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content = text;
    const auto initial_layout = GetRuntime().LayoutParagraph(node, 120.0f);
    REQUIRE(initial_layout.total_line_count == 10U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 10U);

    const std::size_t touched_line_index = 4U;
    const std::size_t prefix_boundary = node.nonwrap_fragment_line_offsets[touched_line_index];
    const std::size_t suffix_boundary = node.nonwrap_fragment_line_offsets[touched_line_index + 1U];
    const float untouched_width = node.line_widths.front();
    const std::uint32_t touched_line_start = static_cast<std::uint32_t>(node.break_offsets[touched_line_index] + 1);
    const std::uint32_t insert_at = touched_line_start + 2500U;
    const std::string inserted_line(5000U, 'Z');
    const std::string insertion = std::string("\n") + inserted_line + "\n";

    const std::string previous_text = node.text_content;
    node.text_content.insert(static_cast<std::size_t>(insert_at), insertion);
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, previous_text));

    CHECK(node.total_line_count == 12U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 12U);
    REQUIRE(node.nonwrap_fragment_line_offsets.size() == 13U);
    CHECK(node.nonwrap_fragment_line_offsets[touched_line_index] == prefix_boundary);
    CHECK(node.nonwrap_fragment_line_offsets[touched_line_index + 3U] >
        node.nonwrap_fragment_line_offsets[touched_line_index]);
    CHECK(node.line_widths.front() == Approx(untouched_width).margin(0.05f));
    CHECK(node.line_widths.back() == Approx(untouched_width).margin(0.05f));
    CHECK(node.nonwrap_fragment_line_offsets[touched_line_index + 3U] >= suffix_boundary);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = false;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 120.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.nonwrap_fragment_line_offsets == expected.nonwrap_fragment_line_offsets);
    REQUIRE(node.nonwrap_fragments.size() == expected.nonwrap_fragments.size());
    CHECK(node.text_layout_cache_max_line_width == Approx(expected_layout.max_line_width).margin(0.05f));
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.nonwrap_fragments.size(); index += 1U) {
        const auto& patched = node.nonwrap_fragments[index];
        const auto& fresh = expected.nonwrap_fragments[index];
        CHECK(patched.line_index == fresh.line_index);
        CHECK(patched.local_byte_start == fresh.local_byte_start);
        CHECK(patched.local_byte_end == fresh.local_byte_end);
        CHECK(patched.x == Approx(fresh.x).margin(0.05f));
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
    for (std::size_t index = 0; index < expected.nonwrap_fragments.size(); index += 1U) {
        const auto& fresh = expected.nonwrap_fragments[index];
        const auto& patched = node.nonwrap_fragments[index];
        CHECK(patched.line_index == fresh.line_index);
        CHECK(patched.local_byte_start == fresh.local_byte_start);
        CHECK(patched.local_byte_end == fresh.local_byte_end);
        CHECK(patched.x == Approx(fresh.x).margin(0.05f));
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui incremental non-wrap relayout falls back for multiline paste at an earlier hard-line end", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.text_wrap = false;
    node.max_lines = 0;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "Line one\n"
        "Line two\n"
        "Line three\n"
        "Longer content so scrollbar policy is easy to spot.";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 260.0f);
    REQUIRE(initial_layout.total_line_count == 4U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 4U);

    const std::uint32_t insert_at = static_cast<std::uint32_t>(std::string("Line one\nLine two").size());
    const std::string previous_text = node.text_content;
    node.text_content.insert(static_cast<std::size_t>(insert_at), "Line\n");
    CHECK_FALSE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, previous_text));
}

TEST_CASE("v2 ui incremental wrapped relayout matches fresh layout for same-line edits", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 92.0f);
    REQUIRE(initial_layout.total_line_count >= 5U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    REQUIRE(node.visual_line_shapes.size() == initial_layout.total_line_count);

    const auto original_visual_shapes = node.visual_line_shapes;
    const std::string previous_text = node.text_content;
    const std::uint32_t edit_index = static_cast<std::uint32_t>(node.break_offsets[2]) + 1U;
    node.text_content.insert(static_cast<std::size_t>(edit_index), "!");

    REQUIRE(GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text));
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);
    CHECK_FALSE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 92.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }

    REQUIRE(original_visual_shapes.size() >= 2U);
    CHECK(node.visual_line_shapes[0].start == original_visual_shapes[0].start);
    CHECK(node.visual_line_shapes[0].end == original_visual_shapes[0].end);
    CHECK(node.visual_line_shapes[0].width == Approx(original_visual_shapes[0].width).margin(0.05f));
    CHECK(node.visual_line_shapes[1].start == original_visual_shapes[1].start);
    CHECK(node.visual_line_shapes[1].end == original_visual_shapes[1].end);
    CHECK(node.visual_line_shapes[1].width == Approx(original_visual_shapes[1].width).margin(0.05f));
}

TEST_CASE("v2 ui incremental wrapped relayout keeps appending ascii text on one logical line incremental", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content = "This route is the new home for experimental retained controls. ";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 180.0f);
    REQUIRE(initial_layout.total_line_count >= 2U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    const auto original_visual_shapes = node.visual_line_shapes;

    static constexpr char kPaste[] =
        "The first slice exposes TextArea so you can toggle wrapping, read-only mode, scrollbar visibility, and line-height mode live. ";
    for (std::size_t iteration = 0; iteration < 12U; iteration += 1U) {
        const std::string previous_text = node.text_content;
        node.text_content += kPaste;
        REQUIRE(GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text));
        REQUIRE(node.text_layout_cache_valid);
        REQUIRE(node.logical_line_shape_cache_valid);
        REQUIRE(node.visual_line_shape_cache_valid);
        REQUIRE(node.logical_line_shapes.size() == 1U);
        REQUIRE(node.logical_line_shapes.front().break_candidate_cache_valid);
    }

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 180.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(node.visual_line_shapes[0].start == original_visual_shapes[0].start);
    CHECK(node.visual_line_shapes[0].end == original_visual_shapes[0].end);
    CHECK(node.visual_line_shapes[0].width == Approx(original_visual_shapes[0].width).margin(0.05f));
}

TEST_CASE("v2 ui incremental wrapped relayout matches fresh layout for newline edits", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "alpha beta gamma delta epsilon\nsecond paragraph keeps wrapping for a while\nthird paragraph stays intact";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 96.0f);
    REQUIRE(initial_layout.total_line_count >= 4U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 3U);

    const std::string previous_text = node.text_content;
    node.text_content.insert(5U, "\n");
    GetRuntime().RebuildTextLineStarts(node);

    const bool kept_incremental = GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    if (!kept_incremental) {
        GetRuntime().InvalidateTextLayoutCache(node);
    }
    const auto patched_layout = GetRuntime().LayoutParagraph(node, 96.0f);
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);
    CHECK(node.logical_line_shapes.size() == 4U);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 96.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(patched_layout.total_line_count == expected_layout.total_line_count);
    REQUIRE(node.logical_line_shapes.size() == expected.logical_line_shapes.size());
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui incremental wrapped relayout matches fresh layout after Backspace at the start of a later hard line", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "Line one keeps wrapping for a while so wrapped visual rows stay active.\n"
        "Line two\n"
        "Line three\n"
        "Trailing content keeps the wrapped cache warm too.";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 180.0f);
    REQUIRE(initial_layout.total_line_count >= 4U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 4U);

    const std::size_t third_line_start = node.text_content.find("Line three");
    REQUIRE(third_line_start != std::string::npos);
    REQUIRE(third_line_start > 0U);
    REQUIRE(node.text_content[third_line_start - 1U] == '\n');

    const std::string previous_text = node.text_content;
    node.text_content.erase(third_line_start - 1U, 1U);
    GetRuntime().RebuildTextLineStarts(node);

    const bool kept_incremental = GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    if (!kept_incremental) {
        GetRuntime().InvalidateTextLayoutCache(node);
    }
    const auto patched_layout = GetRuntime().LayoutParagraph(node, 180.0f);
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 180.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    REQUIRE(node.logical_line_shapes.size() == expected.logical_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(patched_layout.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.logical_line_index == fresh.logical_line_index);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui incremental wrapped relayout matches fresh layout after deleting a single wide wrapped comment line", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 18.0f;
    node.text_content =
        "#include <hb.h>\n"
        "\n"
        "bool isMonospaced(hb_face_t* face) {\n"
        "    // 1. Fetch the raw 'post' table from HarfBuzz\n"
        "    hb_blob_t* postBlob = hb_face_reference_table(face, HB_OT_TAG_post);\n"
        "    unsigned int length = 0;\n"
        "    const char* data = hb_blob_get_data(postBlob, &length);\n"
        "\n"
        "    bool fixedPitch = false;\n"
        "\n"
        "    // 2. The isFixedPitch field resides safely inside the first 16 header bytes.\n"
        "    // Length verification prevents out-of-bounds reads on broken font files.\n"
        "    if (data && length >= 16) {\n"
        "        \n"
        "        // Reassemble the 4 bytes from Big-Endian (Font File) to host CPU endianness\n"
        "        uint32_t isFixedPitchVal = \n"
        "            ((uint8_t)data[12] << 24) | \n"
        "            ((uint8_t)data[13] << 16) | \n"
        "            ((uint8_t)data[14] <<  8) | \n"
        "             (uint8_t)data[15];\n"
        "\n"
        "        // 0 = proportional font, non-zero (usually 1) = monospaced font\n"
        "        fixedPitch = (isFixedPitchVal != 0);\n"
        "    }\n"
        "\n"
        "    // 3. Clean up the HarfBuzz memory reference\n"
        "    hb_blob_destroy(postBlob);\n"
        "    \n"
        "    return fixedPitch;\n"
        "}\n";

    const std::size_t comment_start_pos =
        node.text_content.find("        // 0 = proportional font, non-zero (usually 1) = monospaced font\n");
    REQUIRE(comment_start_pos != std::string::npos);
    const std::size_t fixed_pitch_start_pos =
        node.text_content.find("        fixedPitch = (isFixedPitchVal != 0);", comment_start_pos);
    REQUIRE(fixed_pitch_start_pos != std::string::npos);
    REQUIRE(fixed_pitch_start_pos > comment_start_pos);

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 660.0f);
    REQUIRE(initial_layout.total_line_count >= 6U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, 0U) != nullptr);
    for (std::size_t line_index = 0; line_index < node.visual_line_shapes.size(); line_index += 1U) {
        REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, line_index) != nullptr);
    }

    const auto [comment_x, comment_line] = GetRuntime().GetLocalPositionFromIndex(
        node,
        static_cast<std::uint32_t>(comment_start_pos));
    const auto [fixed_x, fixed_line] = GetRuntime().GetLocalPositionFromIndex(
        node,
        static_cast<std::uint32_t>(fixed_pitch_start_pos));
    CHECK(comment_x == Approx(0.0f).margin(0.05f));
    CHECK(fixed_x == Approx(0.0f).margin(0.05f));
    CHECK(fixed_line == (comment_line + 1));

    const std::string previous_text = node.text_content;
    node.text_content.erase(comment_start_pos, fixed_pitch_start_pos - comment_start_pos);
    GetRuntime().RebuildTextLineStarts(node);

    const bool kept_incremental = GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    if (!kept_incremental) {
        GetRuntime().InvalidateTextLayoutCache(node);
    }
    const auto patched_layout = GetRuntime().LayoutParagraph(node, 660.0f);
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 660.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(patched_layout.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));

        const auto* cached_line = GetRuntime().EnsureWrappedVisualLineShape(node, index);
        REQUIRE(cached_line != nullptr);
        const auto* fresh_line = GetRuntime().EnsureWrappedVisualLineShape(expected, index);
        REQUIRE(fresh_line != nullptr);
        REQUIRE(cached_line->glyphs.size() == fresh_line->glyphs.size());
        REQUIRE(cached_line->cluster_stops.size() == fresh_line->cluster_stops.size());
        for (std::size_t glyph_index = 0; glyph_index < cached_line->glyphs.size(); glyph_index += 1U) {
            CHECK(cached_line->glyphs[glyph_index].glyph_id == fresh_line->glyphs[glyph_index].glyph_id);
            CHECK(cached_line->glyphs[glyph_index].font_id == fresh_line->glyphs[glyph_index].font_id);
            CHECK(cached_line->glyphs[glyph_index].cluster == fresh_line->glyphs[glyph_index].cluster);
            CHECK(cached_line->glyphs[glyph_index].x == Approx(fresh_line->glyphs[glyph_index].x).margin(0.05f));
            CHECK(cached_line->glyphs[glyph_index].y == Approx(fresh_line->glyphs[glyph_index].y).margin(0.05f));
        }
    }
}

TEST_CASE("v2 ui incremental wrapped relayout keeps following line after deleting a block before its trailing newline", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 18.0f;
    node.text_content =
        "std::vector<std::int32_t> UiRuntime::ComputeLineBreaks(\n"
        "    std::string_view utf8,\n"
        "    float max_width,\n"
        "    std::uint32_t font_id,\n"
        "    float font_size,\n"
        "    bool obscured) const {\n"
        "    const auto compute_with_candidates = [&](std::string_view segment, const std::vector<std::int32_t>& candidates) {\n"
        "        std::vector<std::int32_t> breaks{0};\n"
        "        std::unordered_map<std::uint64_t, float> width_cache{};\n"
        "        ShapedTextRun full_segment_shape{};\n"
        "        std::vector<TextClusterStop> full_segment_stops{};\n"
        "        const bool use_ascii_prefix_widths =\n"
        "            IsAsciiOnly(segment) &&\n"
        "            ShapeText(segment, font_id, font_size, full_segment_shape, obscured);\n"
        "\n"
        "        if (use_ascii_prefix_widths) {\n"
        "            full_segment_stops = BuildClusterStops(\n"
        "                full_segment_shape.glyphs,\n"
        "                full_segment_shape.width,\n"
        "                segment.size());\n"
        "        }\n"
        "\n"
        "    };\n"
        "\n"
        "    for (std::size_t index = 0; index < utf8.size(); index += 1U) {\n"
        "        const unsigned char ch = static_cast<unsigned char>(utf8[index]);\n"
        "        if (ch != '\\n' && ch != '\\r') {\n"
        "            continue;\n"
        "        }\n"
        "        const std::int32_t break_offset = static_cast<std::int32_t>(index);\n"
        "        append_segment_breaks(\n"
        "            std::string_view(\n"
        "                utf8.data() + segment_start,\n"
        "                static_cast<std::size_t>(break_offset - segment_start)),\n"
        "            segment_start);\n"
        "        if (ch == '\\r' && index + 1U < utf8.size() && utf8[index + 1U] == '\\n') {\n"
        "            index += 1U;\n"
        "        }\n"
        "        segment_start = static_cast<std::int32_t>(index + 1U);\n"
        "    }\n"
        "\n"
        "    append_segment_breaks(\n"
        "        std::string_view(\n"
        "            utf8.data() + segment_start,\n"
        "            static_cast<std::size_t>(static_cast<std::int32_t>(utf8.size()) - segment_start)),\n"
        "        segment_start);\n"
        "    return breaks;\n"
        "}\n";

    const std::string block_start_text =
        "        if (ch == '\\r' && index + 1U < utf8.size() && utf8[index + 1U] == '\\n') {\n";
    const std::size_t block_start_pos = node.text_content.find(block_start_text);
    REQUIRE(block_start_pos != std::string::npos);
    const std::size_t block_end_pos = node.text_content.find("        }\n        segment_start = ", block_start_pos);
    REQUIRE(block_end_pos != std::string::npos);
    const std::size_t selection_end = block_end_pos + std::string("        }").size();
    REQUIRE(selection_end < node.text_content.size());
    REQUIRE(node.text_content[selection_end] == '\n');

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 820.0f);
    REQUIRE(initial_layout.total_line_count >= 8U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    for (std::size_t line_index = 0; line_index < node.visual_line_shapes.size(); line_index += 1U) {
        REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, line_index) != nullptr);
    }

    const std::string previous_text = node.text_content;
    node.text_content.erase(block_start_pos, selection_end - block_start_pos);
    GetRuntime().RebuildTextLineStarts(node);

    const bool kept_incremental = GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    if (!kept_incremental) {
        GetRuntime().InvalidateTextLayoutCache(node);
    }
    const auto patched_layout = GetRuntime().LayoutParagraph(node, 820.0f);
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 820.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(patched_layout.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui wrapped incremental relayout preserves clean prefix lines and lazily dirties rebuilt lines", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        u8"éclair beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 92.0f);
    REQUIRE(initial_layout.total_line_count >= 5U);
    REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, 0U) != nullptr);
    REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, 1U) != nullptr);
    CHECK(node.visual_line_shapes[0].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[0].cache_dirty);
    CHECK(node.visual_line_shapes[1].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[1].cache_dirty);

    const std::string previous_text = node.text_content;
    const std::uint32_t edit_index = static_cast<std::uint32_t>(node.break_offsets[3]) + 1U;
    REQUIRE(edit_index < node.text_content.size());
    node.text_content.insert(static_cast<std::size_t>(edit_index), "!");

    REQUIRE(GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text));
    REQUIRE(node.visual_line_shapes.size() >= 3U);
    CHECK(node.visual_line_shapes[0].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[0].cache_dirty);
    CHECK(node.visual_line_shapes[1].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[1].cache_dirty);
    CHECK_FALSE(node.visual_line_shapes[2].cache_materialized);
    CHECK(node.visual_line_shapes[2].cache_dirty);
    CHECK(node.visual_line_shapes[2].resume_candidate_index > 0U);

    REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, 2U) != nullptr);
    CHECK(node.visual_line_shapes[2].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[2].cache_dirty);
}

TEST_CASE("v2 ui incremental wrapped relayout matches fresh layout for newline inserts at wrapped line boundaries", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.max_lines = 0;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 96.0f);
    REQUIRE(initial_layout.total_line_count >= 5U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.visual_line_shapes.size() == initial_layout.total_line_count);

    const std::uint32_t insert_at = static_cast<std::uint32_t>(std::max(node.break_offsets[4], 0));
    REQUIRE(insert_at > 0U);
    REQUIRE(insert_at < node.text_content.size());

    const std::string previous_text = node.text_content;
    node.text_content.insert(insert_at, "\n");
    GetRuntime().RebuildTextLineStarts(node);

    const bool kept_incremental = GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    if (!kept_incremental) {
        GetRuntime().InvalidateTextLayoutCache(node);
    }
    const auto patched_layout = GetRuntime().LayoutParagraph(node, 96.0f);
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.max_lines = 0;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 96.0f);

    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    REQUIRE(node.logical_line_shapes.size() == expected.logical_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(patched_layout.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui incremental wrapped relayout matches fresh layout for newline inserts at wrapped hard-line ends", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.max_lines = 0;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "Line one\nLine two\nLine three\nLonger content so scrollbar policy is easy to spot.";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 260.0f);
    REQUIRE(initial_layout.total_line_count >= 5U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 4U);

    const std::uint32_t insert_at = static_cast<std::uint32_t>(node.text_content.size());
    const std::string previous_text = node.text_content;
    node.text_content.insert(insert_at, "\n");
    GetRuntime().RebuildTextLineStarts(node);

    const bool kept_incremental = GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    if (!kept_incremental) {
        GetRuntime().InvalidateTextLayoutCache(node);
    }
    const auto patched_layout = GetRuntime().LayoutParagraph(node, 260.0f);
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.max_lines = 0;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 260.0f);

    REQUIRE(node.text_line_starts == expected.text_line_starts);
    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    REQUIRE(node.logical_line_shapes.size() == expected.logical_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(patched_layout.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui incremental wrapped relayout matches fresh layout for newline inserts at earlier hard-line ends", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.max_lines = 0;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "Line one\nLine two\nLine three\nLonger content so scrollbar policy is easy to spot.";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 260.0f);
    REQUIRE(initial_layout.total_line_count >= 5U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 4U);

    const std::uint32_t insert_at = static_cast<std::uint32_t>(std::string("Line one\nLine two\nLine three").size());
    const std::string previous_text = node.text_content;
    node.text_content.insert(insert_at, "\n");
    GetRuntime().RebuildTextLineStarts(node);

    const bool kept_incremental = GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    if (!kept_incremental) {
        GetRuntime().InvalidateTextLayoutCache(node);
    }
    const auto patched_layout = GetRuntime().LayoutParagraph(node, 260.0f);
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.max_lines = 0;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 260.0f);

    REQUIRE(node.text_line_starts == expected.text_line_starts);
    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    REQUIRE(node.logical_line_shapes.size() == expected.logical_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    CHECK(patched_layout.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui incremental wrapped relayout keeps multiline inserts on the splice path", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.max_lines = 0;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa\n"
        "lambda mu nu xi omicron pi rho sigma tau upsilon phi chi\n"
        "psi omega aardvark badger cougar dolphin eagle falcon";

    const auto initial_layout = GetRuntime().LayoutParagraph(node, 96.0f);
    REQUIRE(initial_layout.total_line_count >= 8U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 3U);
    REQUIRE(node.visual_line_shapes.size() == initial_layout.total_line_count);
    REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, 0U) != nullptr);
    REQUIRE(GetRuntime().EnsureWrappedVisualLineShape(node, 1U) != nullptr);
    CHECK(node.visual_line_shapes[0].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[0].cache_dirty);
    CHECK(node.visual_line_shapes[1].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[1].cache_dirty);

    const std::size_t insert_at_pos = node.text_content.find("omicron");
    REQUIRE(insert_at_pos != std::string::npos);
    const std::string previous_text = node.text_content;
    node.text_content.insert(
        insert_at_pos,
        "INSERTED wrapped alpha beta gamma delta\nINSERTED epsilon zeta eta theta\n");
    GetRuntime().RebuildTextLineStarts(node);

    REQUIRE(GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text));
    CHECK(node.text_layout_cache_valid);
    CHECK(node.visual_line_shape_cache_valid);
    CHECK(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shapes.size() >= 2U);
    CHECK(node.visual_line_shapes[0].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[0].cache_dirty);
    CHECK(node.visual_line_shapes[1].cache_materialized);
    CHECK_FALSE(node.visual_line_shapes[1].cache_dirty);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.max_lines = 0;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 96.0f);

    REQUIRE(node.text_line_starts == expected.text_line_starts);
    REQUIRE(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    REQUIRE(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    REQUIRE(node.logical_line_shapes.size() == expected.logical_line_shapes.size());
    CHECK(node.total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node.visual_line_shapes.size(); index += 1U) {
        const auto& patched = node.visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui long ascii wrapped layout stays stable across wrap toggles", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string long_ascii =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon "
        "phi chi psi omega alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho "
        "sigma tau upsilon phi chi psi omega alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu";

    effindom::v2::ui::UINode fresh_wrapped{};
    fresh_wrapped.is_text_node = true;
    fresh_wrapped.text_wrap = true;
    fresh_wrapped.font_id = 1U;
    fresh_wrapped.font_size = 20.0f;
    fresh_wrapped.text_content = long_ascii;
    const auto fresh_layout = GetRuntime().LayoutParagraph(fresh_wrapped, 120.0f);
    REQUIRE(fresh_layout.total_line_count > 4U);
    REQUIRE(fresh_wrapped.logical_line_shape_cache_valid);
    REQUIRE(fresh_wrapped.logical_line_shapes.size() == 1U);
    REQUIRE(fresh_wrapped.logical_line_shapes.front().break_candidate_cache_valid);
    REQUIRE_FALSE(fresh_wrapped.logical_line_shapes.front().break_candidates.empty());
    const auto cached_line_width = fresh_wrapped.logical_line_shapes.front().width;
    const auto cached_break_candidates = fresh_wrapped.logical_line_shapes.front().break_candidates;
    CHECK(cached_line_width > fresh_layout.line_widths.front());

    effindom::v2::ui::UINode nonwrap_first{};
    nonwrap_first.is_text_node = true;
    nonwrap_first.text_wrap = false;
    nonwrap_first.font_id = 1U;
    nonwrap_first.font_size = 20.0f;
    nonwrap_first.text_content = long_ascii;
    const auto nonwrap_first_layout = GetRuntime().LayoutParagraph(nonwrap_first, 120.0f);
    REQUIRE(nonwrap_first_layout.total_line_count == 1U);
    REQUIRE(nonwrap_first.logical_line_shape_cache_valid);
    REQUIRE(nonwrap_first.logical_line_shapes.size() == 1U);
    CHECK_FALSE(nonwrap_first.logical_line_shapes.front().break_candidate_cache_valid);

    nonwrap_first.text_wrap = true;
    const auto nonwrap_flip_layout = GetRuntime().LayoutParagraph(nonwrap_first, 120.0f);
    CHECK(nonwrap_flip_layout.total_line_count == fresh_layout.total_line_count);
    REQUIRE(nonwrap_first.logical_line_shapes.front().break_candidate_cache_valid);
    CHECK(nonwrap_first.logical_line_shapes.front().break_candidates == cached_break_candidates);
    CHECK(nonwrap_first.break_offsets == fresh_wrapped.break_offsets);

    effindom::v2::ui::UINode toggled = fresh_wrapped;
    toggled.text_wrap = false;
    const auto nonwrap_layout = GetRuntime().LayoutParagraph(toggled, 120.0f);
    REQUIRE(nonwrap_layout.total_line_count == 1U);
    REQUIRE(toggled.logical_line_shape_cache_valid);
    REQUIRE(toggled.logical_line_shapes.size() == 1U);
    CHECK(toggled.logical_line_shapes.front().width == Approx(cached_line_width).margin(0.05f));
    CHECK(nonwrap_layout.line_widths.front() == Approx(cached_line_width).margin(0.05f));

    toggled.text_wrap = true;
    const auto toggled_layout = GetRuntime().LayoutParagraph(toggled, 120.0f);
    CHECK(toggled_layout.total_line_count == fresh_layout.total_line_count);
    REQUIRE(toggled.break_offsets == fresh_wrapped.break_offsets);
    REQUIRE(toggled.line_widths.size() == fresh_wrapped.line_widths.size());
    REQUIRE(toggled.logical_line_shape_cache_valid);
    REQUIRE(toggled.logical_line_shapes.size() == 1U);
    CHECK(toggled.logical_line_shapes.front().break_candidate_cache_valid);
    CHECK(toggled.logical_line_shapes.front().break_candidates == cached_break_candidates);
    for (std::size_t index = 0; index < toggled.line_widths.size(); index += 1U) {
        CHECK(toggled.line_widths[index] == Approx(fresh_wrapped.line_widths[index]).margin(0.05f));
    }
}

TEST_CASE("v2 ui non-wrap mono ascii lines mark the monospace fragment fast path", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterMonoTestFont();

    std::string code_line{};
    code_line.reserve(1024U);
    for (std::size_t index = 0U; index < 16U; index += 1U) {
        code_line += "const answer_";
        code_line += std::to_string(index);
        code_line += " = value_";
        code_line += std::to_string(index);
        code_line += "; ";
    }

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 5U;
    node.font_size = 16.0f;
    node.text_content = code_line;

    const auto paragraph = GetRuntime().LayoutParagraph(node, 240.0f);
    REQUIRE(paragraph.total_line_count == 1U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    const auto& logical_line = node.logical_line_shapes.front();
    REQUIRE(logical_line.monospace_fast_path_eligible);
    REQUIRE(logical_line.monospace_cell_width > 0.0f);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragments.size() > 1U);

    const std::uint32_t first_fragment_columns =
        node.nonwrap_fragments.front().local_byte_end - node.nonwrap_fragments.front().local_byte_start;
    REQUIRE(first_fragment_columns > 0U);
    CHECK(node.nonwrap_fragments.front().width == Approx(
        static_cast<float>(first_fragment_columns) * logical_line.monospace_cell_width).margin(0.05f));
    for (std::size_t index = 0U; index + 1U < node.nonwrap_fragments.size(); index += 1U) {
        const auto& fragment = node.nonwrap_fragments[index];
        const std::uint32_t fragment_columns = fragment.local_byte_end - fragment.local_byte_start;
        CHECK(fragment_columns == first_fragment_columns);
        CHECK(fragment.width == Approx(
            static_cast<float>(fragment_columns) * logical_line.monospace_cell_width).margin(0.05f));
    }
}

TEST_CASE("v2 ui non-wrap variable ascii lines stay on the proportional fragment path", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string line =
        "const wide = wwwwww + narrowiii + mixedSpacing + punctuation_12345;";

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 16.0f;
    node.text_content = line;

    const auto paragraph = GetRuntime().LayoutParagraph(node, 240.0f);
    REQUIRE(paragraph.total_line_count == 1U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    CHECK_FALSE(node.logical_line_shapes.front().monospace_fast_path_eligible);
    CHECK(node.logical_line_shapes.front().monospace_cell_width == Approx(0.0f));
}

TEST_CASE("v2 ui non-wrap mono lines bail out of the monospace fast path once fallback fonts are used", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterMonoTestFont();
    RegisterEmojiTestFont();
    ui_register_font_fallback(5U, 4U);

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 5U;
    node.font_size = 16.0f;
    node.text_content = "const icon = \"🌍\";";

    const auto paragraph = GetRuntime().LayoutParagraph(node, 240.0f);
    REQUIRE(paragraph.total_line_count == 1U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    CHECK_FALSE(node.logical_line_shapes.front().monospace_fast_path_eligible);
    CHECK(node.logical_line_shapes.front().monospace_cell_width == Approx(0.0f));
}

TEST_CASE("v2 ui commit profile attributes large wrap toggles to cached line reuse and wrap-specific rebuild stages", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    std::string huge_line{};
    huge_line.reserve(64U * 1024U);
    for (std::size_t index = 0U; index < 2200U; index += 1U) {
        huge_line += "alpha beta gamma delta epsilon zeta eta theta ";
    }

    const auto root = ui_create_node(UI_NODE_FLEX_BOX);
    const auto text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_node_add_child(root, text);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 96.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(huge_line.data()), static_cast<std::uint32_t>(huge_line.size()));
    ui_set_text_wrapping(text, true);

    ui_commit_frame();
    const auto* initial_node = GetRuntime().Resolve(text);
    REQUIRE(initial_node != nullptr);
    REQUIRE(initial_node->logical_line_shape_cache_valid);
    REQUIRE(initial_node->logical_line_shapes.size() == 1U);
    REQUIRE(initial_node->logical_line_shapes.front().break_candidate_cache_valid);

    GetRuntime().ClearTextCommitProfile();
    REQUIRE(GetRuntime().SetTextWrapping(text, false));
    ui_commit_frame();

    const auto nonwrap_profile = GetRuntime().last_text_commit_profile();
    const auto* nonwrap_node = GetRuntime().Resolve(text);
    REQUIRE(nonwrap_node != nullptr);
    REQUIRE(nonwrap_node->nonwrap_fragment_cache_valid);
    CHECK(nonwrap_profile.layout_paragraph_calls >= 1U);
    CHECK(nonwrap_profile.nonwrap_layout_calls >= 1U);
    CHECK(nonwrap_profile.logical_line_shape_cache_hits >= 1U);
    CHECK(nonwrap_profile.nonwrap_fragment_line_builds >= 1U);
    CHECK(nonwrap_profile.nonwrap_fragment_build_ms > 0.0);

    GetRuntime().ClearTextCommitProfile();
    REQUIRE(GetRuntime().SetTextWrapping(text, true));
    ui_commit_frame();

    const auto wrapped_profile = GetRuntime().last_text_commit_profile();
    const auto* wrapped_node = GetRuntime().Resolve(text);
    REQUIRE(wrapped_node != nullptr);
    REQUIRE(wrapped_node->visual_line_shape_cache_valid);
    CHECK(wrapped_profile.layout_paragraph_calls >= 1U);
    CHECK(wrapped_profile.wrapped_layout_calls >= 1U);
    CHECK(wrapped_profile.logical_line_shape_cache_hits >= 1U);
    CHECK(wrapped_profile.break_candidate_cache_hits >= 1U);
    CHECK(wrapped_profile.wrapped_segment_break_calls >= 1U);
    CHECK(wrapped_profile.wrapped_segment_break_ms > 0.0);
}

TEST_CASE("v2 ui huge wrapped lines build shard metrics and advance wrapped resume tokens", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    std::string huge_line{};
    huge_line.reserve(64U * 1024U);
    for (std::size_t index = 0; index < 2200U; index += 1U) {
        huge_line += "alpha beta gamma delta epsilon zeta eta theta ";
    }

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content = huge_line;

    const auto paragraph = GetRuntime().LayoutParagraph(node, 96.0f);
    REQUIRE(paragraph.total_line_count > 64U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    const auto& logical_line = node.logical_line_shapes.front();
    REQUIRE(logical_line.break_candidate_cache_valid);
    REQUIRE(logical_line.break_candidate_x_offsets.size() == logical_line.break_candidates.size());
    REQUIRE(logical_line.break_shards.size() > 1U);
    CHECK(logical_line.break_shards.front().start_candidate_index == 0U);
    CHECK(logical_line.break_shards.back().end_candidate_index + 1U == logical_line.break_candidates.size());
    for (const auto& shard : logical_line.break_shards) {
        CHECK(shard.end_candidate_index > shard.start_candidate_index);
        CHECK(shard.end_x >= shard.start_x);
    }

    REQUIRE(node.visual_line_shapes.size() == paragraph.total_line_count);
    bool saw_resume_token_advance = false;
    std::size_t previous_resume_candidate = 0U;
    for (std::size_t line_index = 0U; line_index < node.visual_line_shapes.size(); line_index += 1U) {
        const auto& visual_line = node.visual_line_shapes[line_index];
        CHECK(visual_line.resume_candidate_index >= previous_resume_candidate);
        if (visual_line.resume_candidate_index > previous_resume_candidate) {
            saw_resume_token_advance = true;
        }
        previous_resume_candidate = visual_line.resume_candidate_index;
    }
    CHECK(saw_resume_token_advance);

    const std::size_t middle_line_index = paragraph.total_line_count / 2U;
    const auto* middle_cached_line = GetRuntime().EnsureWrappedVisualLineShape(node, middle_line_index);
    REQUIRE(middle_cached_line != nullptr);

    effindom::v2::ui::UINode visual_slice{};
    visual_slice.is_text_node = true;
    visual_slice.text_wrap = true;
    visual_slice.font_id = node.font_id;
    visual_slice.font_size = node.font_size;
    visual_slice.text_content = node.text_content.substr(
        static_cast<std::size_t>(middle_cached_line->start),
        static_cast<std::size_t>(middle_cached_line->end - middle_cached_line->start));
    const auto visual_slice_layout = GetRuntime().LayoutParagraph(visual_slice, std::nullopt);
    REQUIRE(visual_slice_layout.total_line_count == 1U);
    const auto* fresh_visual_slice = GetRuntime().EnsureWrappedVisualLineShape(visual_slice, 0U);
    REQUIRE(fresh_visual_slice != nullptr);
    CHECK(middle_cached_line->width == Approx(fresh_visual_slice->width).margin(0.05f));
    CHECK(middle_cached_line->cluster_stops.size() == fresh_visual_slice->cluster_stops.size());
}

TEST_CASE("v2 ui narrow wrapped geometry round-trips the browser smoke target indices", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string source_text = ReadWrappedTextFixture();
    const WrappedTextFixtureTargets fixture = FindWrappedTextFixtureTargets(source_text);
    const std::uint32_t reverse_selection_start = fixture.reverse_selection_start;
    const std::uint32_t reverse_selection_end = static_cast<std::uint32_t>(
        std::min(source_text.size(), static_cast<std::size_t>(fixture.reverse_selection_start) + 128U));
    const std::uint32_t block_start = fixture.block_start;
    const std::uint32_t block_end = fixture.block_end;

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.max_lines = 0;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.layout_width = 120.0f;
    node.layout_height = 800.0f;
    node.text_content = source_text;

    const auto paragraph = GetRuntime().LayoutParagraph(node, 120.0f);
    REQUIRE(paragraph.total_line_count > 0U);
    REQUIRE(node.visual_line_shape_cache_valid);

    const auto check_roundtrip = [&](std::uint32_t index) {
        const auto [local_x, line_index] = GetRuntime().GetLocalPositionFromIndex(node, index);
        CAPTURE(index, local_x, line_index);
        REQUIRE(line_index >= 0);
        CHECK(GetRuntime().GetStringIndexFromPoint(
            node,
            local_x,
            (static_cast<float>(line_index) * node.line_height) + (node.line_height * 0.5f)) == index);
    };

    check_roundtrip(reverse_selection_start);
    check_roundtrip(reverse_selection_end);
    check_roundtrip(block_start);
    check_roundtrip(block_end);
}

TEST_CASE("v2 ui fragment geometry helpers round-trip indices and selection rects on long non-wrap lines", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "This is a long non wrapping line that should exercise fragment aware caret geometry and hit testing without "
        "reshaping the entire line from the beginning every time.";

    const auto paragraph = GetRuntime().LayoutParagraph(node, 120.0f);
    REQUIRE(paragraph.total_line_count == 1U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragments.size() > 2U);

    const std::size_t middle_fragment_index = node.nonwrap_fragments.size() / 2U;
    const auto& middle_fragment = node.nonwrap_fragments[middle_fragment_index];
    const std::uint32_t target_index = middle_fragment.local_byte_start + ((middle_fragment.local_byte_end - middle_fragment.local_byte_start) / 2U);
    REQUIRE(target_index > 6U);
    REQUIRE(target_index + 8U < static_cast<std::uint32_t>(node.text_content.size()));

    effindom::v2::ui::UiRuntime::FragmentGeometrySlice index_slice{};
    REQUIRE(GetRuntime().TryShapeFragmentGeometrySliceForIndex(node, 0U, target_index, index_slice));
    CHECK(index_slice.slice_start <= target_index);
    CHECK(index_slice.slice_end >= target_index);
    CHECK(index_slice.slice_start > 0U);
    CHECK((index_slice.slice_start > 0U || index_slice.slice_end < static_cast<std::uint32_t>(node.text_content.size())));
    CHECK(index_slice.full_line_width == Approx(paragraph.max_line_width).margin(0.01f));
    CHECK(index_slice.slice_x <= middle_fragment.x);
    CHECK_FALSE(index_slice.cluster_stops.empty());
    REQUIRE(node.cached_nonwrap_geometry_slices.size() == 1U);
    CHECK(node.cached_nonwrap_geometry_slices.front().line_index == 0U);
    CHECK(node.cached_nonwrap_geometry_slices.front().slice_start == index_slice.slice_start);
    CHECK(node.cached_nonwrap_geometry_slices.front().slice_end == index_slice.slice_end);
    CHECK_FALSE(node.cached_nonwrap_geometry_slices.front().cluster_stops.empty());

    const auto [local_x, line_index] = GetRuntime().GetLocalPositionFromIndex(node, target_index);
    CHECK(line_index == 0);
    CHECK(local_x > index_slice.slice_x);

    effindom::v2::ui::UiRuntime::FragmentGeometrySlice point_slice{};
    REQUIRE(GetRuntime().TryShapeFragmentGeometrySliceForX(node, 0U, local_x, point_slice));
    CHECK(point_slice.slice_start <= target_index);
    CHECK(point_slice.slice_end >= target_index);
    CHECK(point_slice.slice_start > 0U);
    CHECK((point_slice.slice_start > 0U || point_slice.slice_end < static_cast<std::uint32_t>(node.text_content.size())));
    CHECK(point_slice.slice_start == index_slice.slice_start);
    CHECK(point_slice.slice_end == index_slice.slice_end);
    CHECK_FALSE(point_slice.cluster_stops.empty());
    CHECK(GetRuntime().GetStringIndexFromPoint(node, local_x + 0.25f, paragraph.line_height * 0.5f) == target_index);

    const auto selection_start = target_index - 6U;
    const auto selection_end = target_index + 8U;
    const auto rects = GetRuntime().BuildSelectionRects(node, selection_start, selection_end);
    REQUIRE(rects.size() == 1U);
    const auto [selection_left, selection_left_line] = GetRuntime().GetLocalPositionFromIndex(node, selection_start);
    const auto [selection_right, selection_right_line] = GetRuntime().GetLocalPositionFromIndex(node, selection_end);
    CHECK(selection_left_line == 0);
    CHECK(selection_right_line == 0);
    CHECK(rects.front().x == Approx(selection_left).margin(0.5f));
    CHECK(rects.front().width == Approx(selection_right - selection_left).margin(0.5f));
    CHECK(rects.front().height == Approx(paragraph.line_height).margin(0.5f));
}

TEST_CASE("v2 ui non-wrap fragment geometry slices reuse cached logical line shapes", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "This is a long ASCII non wrapping line that keeps enough cached fragments alive to reuse the logical shape "
        "cache for viewport geometry without reshaping fragment substrings again.";

    const auto paragraph = GetRuntime().LayoutParagraph(node, 120.0f);
    REQUIRE(paragraph.total_line_count == 1U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragments.size() > 2U);

    const std::size_t middle_fragment_index = node.nonwrap_fragments.size() / 2U;
    const auto& middle_fragment = node.nonwrap_fragments[middle_fragment_index];
    const std::uint32_t visible_start = node.logical_line_shapes.front().visible_start;
    const std::uint32_t slice_start = visible_start + middle_fragment.local_byte_start;
    const std::uint32_t slice_end = visible_start + middle_fragment.local_byte_end;
    const std::string_view expected_text(node.text_content.data() + slice_start, static_cast<std::size_t>(slice_end - slice_start));

    effindom::v2::ui::UiRuntime::ShapedTextRun expected_shape{};
    REQUIRE(GetRuntime().ShapeText(expected_text, 1U, node.font_size, expected_shape, node.is_obscured));

    node.font_id = 999U;

    effindom::v2::ui::UiRuntime::FragmentGeometrySlice direct_slice{};
    REQUIRE(GetRuntime().TryBuildFragmentGeometrySliceFromLogicalLineShape(node, 0U, slice_start, slice_end, direct_slice));
    CHECK(direct_slice.slice_start == slice_start);
    CHECK(direct_slice.slice_end == slice_end);
    CHECK(direct_slice.shaped.width == Approx(expected_shape.width).margin(0.01f));
    CHECK(direct_slice.shaped.glyphs.size() == expected_shape.glyphs.size());
    CHECK_FALSE(direct_slice.cluster_stops.empty());

    node.cached_nonwrap_geometry_slices.clear();
    const std::uint32_t target_index = slice_start + ((slice_end - slice_start) / 2U);
    effindom::v2::ui::UiRuntime::FragmentGeometrySlice cached_slice{};
    REQUIRE(GetRuntime().TryShapeFragmentGeometrySliceForIndex(node, 0U, target_index, cached_slice));
    CHECK(cached_slice.slice_start <= target_index);
    CHECK(cached_slice.slice_end >= target_index);
    CHECK(cached_slice.shaped.width > 0.0f);
    REQUIRE(node.cached_nonwrap_geometry_slices.size() == 1U);
}

TEST_CASE("v2 ui non-wrap monospace geometry stays arithmetic and skips fragment slice caching", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterMonoTestFont();

    std::string code_line{};
    code_line.reserve(2048U);
    for (std::size_t index = 0U; index < 48U; index += 1U) {
        code_line += "value_";
        code_line += std::to_string(index);
        code_line += " = value_";
        code_line += std::to_string(index);
        code_line += "; ";
    }

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.semantic_role = UI_SEMANTIC_TEXTBOX;
    node.max_lines = 0;
    node.text_wrap = false;
    node.font_id = 5U;
    node.font_size = 16.0f;
    node.text_content = code_line;

    const auto paragraph = GetRuntime().LayoutParagraph(node, 240.0f);
    REQUIRE(paragraph.total_line_count == 1U);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    REQUIRE(node.logical_line_shapes.front().monospace_fast_path_eligible);
    REQUIRE(node.logical_line_shapes.front().monospace_cell_width > 0.0f);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragments.size() > 2U);
    REQUIRE(node.cached_nonwrap_geometry_slices.empty());

    const std::size_t middle_fragment_index = node.nonwrap_fragments.size() / 2U;
    const auto& middle_fragment = node.nonwrap_fragments[middle_fragment_index];
    const std::uint32_t target_index =
        middle_fragment.local_byte_start + ((middle_fragment.local_byte_end - middle_fragment.local_byte_start) / 2U);
    REQUIRE(target_index > 8U);
    REQUIRE(target_index + 8U < static_cast<std::uint32_t>(node.text_content.size()));

    const float cell_width = node.logical_line_shapes.front().monospace_cell_width;
    const auto [local_x, line_index] = GetRuntime().GetLocalPositionFromIndex(node, target_index);
    CHECK(line_index == 0);
    CHECK(local_x == Approx(static_cast<float>(target_index) * cell_width).margin(0.05f));
    CHECK(node.cached_nonwrap_geometry_slices.empty());

    CHECK(GetRuntime().GetStringIndexFromPoint(node, local_x + (cell_width * 0.25f), paragraph.line_height * 0.5f) == target_index);
    CHECK(GetRuntime().GetStringIndexFromPoint(node, local_x + (cell_width * 0.75f), paragraph.line_height * 0.5f) == (target_index + 1U));
    CHECK(node.cached_nonwrap_geometry_slices.empty());

    const std::uint32_t selection_start = target_index - 6U;
    const std::uint32_t selection_end = target_index + 8U;
    const auto rects = GetRuntime().BuildSelectionRects(node, selection_start, selection_end);
    REQUIRE(rects.size() == 1U);
    CHECK(rects.front().x == Approx(static_cast<float>(selection_start) * cell_width).margin(0.05f));
    CHECK(rects.front().width == Approx(static_cast<float>(selection_end - selection_start) * cell_width).margin(0.05f));
    CHECK(rects.front().height == Approx(paragraph.line_height).margin(0.05f));
    CHECK(node.cached_nonwrap_geometry_slices.empty());
}

TEST_CASE("v2 ui non-wrap caches invalidate across font fallback obscured and wrap changes", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont(1U);
    RegisterTestFont(2U);

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    static constexpr char kLongLine[] =
        "This is a long non wrapping line that keeps enough cached fragments alive to validate full invalidation paths "
        "when font, fallback, obscured, and wrapping settings all change in sequence.";
    ui_set_root(root);
    ui_resize_window(260.0f, 80.0f);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kLongLine), sizeof(kLongLine) - 1U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    REQUIRE(text_node->nonwrap_fragments.size() > 2U);

    auto rebuild = [&]() -> effindom::v2::ui::UINode* {
        ui_commit_frame();
        auto* updated = GetRuntime().ResolveMutable(text);
        REQUIRE(updated != nullptr);
        return updated;
    };

    std::uint32_t generation = text_node->nonwrap_fragment_cache_generation;

    CHECK(GetRuntime().SetTextObscured(text, true));
    CHECK_FALSE(text_node->nonwrap_fragment_cache_valid);
    CHECK_FALSE(text_node->nonwrap_render_fragment_window_valid);
    CHECK_FALSE(text_node->text_render_window_valid);
    text_node = rebuild();
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->is_obscured);
    CHECK(text_node->nonwrap_fragment_cache_generation == generation + 1U);
    generation = text_node->nonwrap_fragment_cache_generation;

    CHECK(GetRuntime().SetFont(text, 2U, 18.0f));
    CHECK_FALSE(text_node->nonwrap_fragment_cache_valid);
    text_node = rebuild();
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->font_id == 2U);
    CHECK(text_node->font_size == Approx(18.0f));
    CHECK(text_node->nonwrap_fragment_cache_generation == generation + 1U);
    generation = text_node->nonwrap_fragment_cache_generation;

    CHECK(GetRuntime().SetTextWrapping(text, true));
    CHECK_FALSE(text_node->nonwrap_fragment_cache_valid);
    text_node = rebuild();
    CHECK(text_node->text_wrap);
    CHECK_FALSE(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragments.empty());
    CHECK(text_node->nonwrap_fragment_line_offsets.empty());

    CHECK(GetRuntime().SetTextWrapping(text, false));
    text_node = rebuild();
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    CHECK_FALSE(text_node->text_wrap);
    CHECK(text_node->nonwrap_fragment_cache_generation == generation + 1U);
    generation = text_node->nonwrap_fragment_cache_generation;

    CHECK(GetRuntime().SetFont(text, 1U, 20.0f));
    text_node = rebuild();
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->font_id == 1U);
    CHECK_FALSE(text_node->is_obscured == false);
    CHECK(text_node->nonwrap_fragment_cache_generation == generation + 1U);
    generation = text_node->nonwrap_fragment_cache_generation;

    REQUIRE(GetRuntime().RegisterFontFallback(1U, 2U));
    CHECK_FALSE(text_node->nonwrap_fragment_cache_valid);
    text_node = rebuild();
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == generation + 1U);
    generation = text_node->nonwrap_fragment_cache_generation;

    REQUIRE(GetRuntime().RegisterFontFallback(1U, 2U));
    CHECK(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == generation);

    GetRuntime().FontLoaded(2U);
    CHECK_FALSE(text_node->nonwrap_fragment_cache_valid);
    text_node = rebuild();
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == generation + 1U);
}

TEST_CASE("v2 ui large non-wrap replacements and deletes do not leave stale fragments behind", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    auto verify_fragment_cache = [&](const effindom::v2::ui::UINode& candidate) {
        REQUIRE(candidate.nonwrap_fragment_cache_valid);
        REQUIRE(candidate.break_offsets.size() == 2U);
        REQUIRE(candidate.line_widths.size() == 1U);
        REQUIRE(candidate.nonwrap_fragment_line_offsets.size() == 2U);
        CHECK(candidate.nonwrap_fragment_line_offsets.front() == 0U);
        CHECK(candidate.nonwrap_fragment_line_offsets.back() == candidate.nonwrap_fragments.size());
        if (!candidate.nonwrap_fragments.empty()) {
            CHECK(candidate.nonwrap_fragments.front().local_byte_start == 0U);
            CHECK(candidate.nonwrap_fragments.back().local_byte_end == static_cast<std::uint32_t>(candidate.text_content.size()));
        }
        for (std::size_t index = 1; index < candidate.nonwrap_fragments.size(); index += 1U) {
            const auto& previous = candidate.nonwrap_fragments[index - 1U];
            const auto& fragment = candidate.nonwrap_fragments[index];
            CHECK(fragment.local_byte_start == previous.local_byte_end);
            CHECK(fragment.x == Approx(previous.x + previous.width).margin(0.05f));
        }
    };

    auto compare_to_fresh_layout = [&](const effindom::v2::ui::UINode& candidate) {
        effindom::v2::ui::UINode expected{};
        expected.is_text_node = true;
        expected.text_wrap = false;
        expected.font_id = candidate.font_id;
        expected.font_size = candidate.font_size;
        expected.is_obscured = candidate.is_obscured;
        expected.text_content = candidate.text_content;
        const auto expected_layout = GetRuntime().LayoutParagraph(expected, 120.0f);
        const auto candidate_layout = GetRuntime().LayoutParagraph(const_cast<effindom::v2::ui::UINode&>(candidate), 120.0f);
        REQUIRE(expected.nonwrap_fragment_cache_valid);
        REQUIRE(expected.break_offsets == candidate.break_offsets);
        REQUIRE(expected.nonwrap_fragment_line_offsets == candidate.nonwrap_fragment_line_offsets);
        REQUIRE(expected.nonwrap_fragments.size() == candidate.nonwrap_fragments.size());
        CHECK(expected_layout.max_line_width == Approx(candidate_layout.max_line_width).margin(0.05f));
        CHECK(expected.line_widths.front() == Approx(candidate.line_widths.front()).margin(0.05f));
        for (std::size_t index = 0; index < expected.nonwrap_fragments.size(); index += 1U) {
            const auto& fresh = expected.nonwrap_fragments[index];
            const auto& patched = candidate.nonwrap_fragments[index];
            CHECK(patched.line_index == fresh.line_index);
            CHECK(patched.local_byte_start == fresh.local_byte_start);
            CHECK(patched.local_byte_end == fresh.local_byte_end);
            CHECK(patched.x == Approx(fresh.x).margin(0.05f));
            CHECK(patched.width == Approx(fresh.width).margin(0.05f));
        }
    };

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz "
        "abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz";
    (void)GetRuntime().LayoutParagraph(node, 120.0f);
    verify_fragment_cache(node);

    const std::string before_large_replace = node.text_content;
    node.text_content.replace(48U, 86U, "START-MIDDLE-END");
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, before_large_replace));
    verify_fragment_cache(node);
    compare_to_fresh_layout(node);

    const std::string before_large_delete = node.text_content;
    node.text_content.erase(12U, 96U);
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, before_large_delete));
    verify_fragment_cache(node);
    compare_to_fresh_layout(node);

    const std::string before_almost_all_delete = node.text_content;
    node.text_content.erase(1U, node.text_content.size() - 2U);
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, before_almost_all_delete));
    verify_fragment_cache(node);
    compare_to_fresh_layout(node);
}

TEST_CASE("v2 ui reset preserves registered fonts for scene rebuilds", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size())));

    effindom::v2::ui::UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText("Persisted", 1U, 20.0f, shaped));
    REQUIRE_FALSE(shaped.glyphs.empty());

    ui_reset();

    CHECK(GetRuntime().ShapeText("Persisted after reset", 1U, 20.0f, shaped));
    CHECK_FALSE(shaped.glyphs.empty());
}

TEST_CASE("v2 ui non-wrap incremental edits shrink stale tall cached line height", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content = "alpha\nbeta\ngamma";
    (void)GetRuntime().LayoutParagraph(node, 160.0f);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 3U);

    const std::string previous_text = node.text_content;
    node.logical_line_shapes[1].height += 7.0f;
    node.line_height = node.logical_line_shapes[1].height;
    node.text_content.replace(node.text_content.find("beta"), 4U, "bet");
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, previous_text));

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = false;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 160.0f);
    CHECK(node.line_height == Approx(expected.line_height).margin(0.01f));
    CHECK(node.total_line_count == expected.total_line_count);
    CHECK(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    CHECK(expected_layout.height == Approx(static_cast<float>(expected.total_line_count) * expected.line_height).margin(0.05f));
}

TEST_CASE("v2 ui wrapped incremental edits shrink stale tall cached line height", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.text_content =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau";
    (void)GetRuntime().LayoutParagraph(node, 92.0f);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 1U);
    REQUIRE(node.visual_line_shapes.size() >= 3U);

    const std::uint32_t edit_index = 1U;
    REQUIRE(edit_index < node.text_content.size());
    const std::string previous_text = node.text_content;
    node.logical_line_shapes[0].height += 7.0f;
    node.line_height = node.logical_line_shapes[0].height;
    for (auto& visual_line : node.visual_line_shapes) {
        visual_line.height += 7.0f;
    }
    node.text_content.insert(static_cast<std::size_t>(edit_index), "!");
    REQUIRE(GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text));

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.text_content = node.text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 92.0f);
    CHECK(node.line_height == Approx(expected.line_height).margin(0.01f));
    CHECK(node.total_line_count == expected.total_line_count);
    CHECK(node.break_offsets == expected.break_offsets);
    REQUIRE(node.line_widths.size() == expected.line_widths.size());
    for (std::size_t index = 0; index < node.line_widths.size(); index += 1U) {
        CHECK(node.line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    CHECK(node.visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(expected_layout.height == Approx(static_cast<float>(expected.total_line_count) * expected.line_height).margin(0.05f));
}

TEST_CASE("v2 ui non-wrap incremental edits only shrink the touched tall line", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 16.0f;
    node.text_content = "alpha\nbeta\ngamma";
    (void)GetRuntime().LayoutParagraph(node, 160.0f);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 3U);
    REQUIRE(node.line_heights.size() == 3U);

    const float base_line_height = node.line_heights[0];
    const float tall_line_height = base_line_height + 7.0f;
    node.logical_line_shapes[1].height = tall_line_height;
    node.logical_line_shapes[2].height = tall_line_height;
    node.line_heights = {base_line_height, tall_line_height, tall_line_height};
    node.line_y_offsets = {0.0f, base_line_height, base_line_height + tall_line_height, base_line_height + tall_line_height * 2.0f};
    node.line_height = tall_line_height;

    const std::string previous_text = node.text_content;
    node.text_content.replace(node.text_content.find("beta"), 4U, "bet");
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, previous_text));

    REQUIRE(node.line_heights.size() == 3U);
    CHECK(node.line_heights[0] == Approx(base_line_height).margin(0.01f));
    CHECK(node.line_heights[1] == Approx(base_line_height).margin(0.01f));
    CHECK(node.line_heights[2] == Approx(tall_line_height).margin(0.01f));
    REQUIRE(node.line_y_offsets.size() == 4U);
    CHECK(node.line_y_offsets[1] == Approx(base_line_height).margin(0.01f));
    CHECK(node.line_y_offsets[2] == Approx(base_line_height * 2.0f).margin(0.01f));
    CHECK(node.line_y_offsets[3] == Approx((base_line_height * 2.0f) + tall_line_height).margin(0.01f));
}

TEST_CASE("v2 ui wrapped incremental edits only shrink the touched tall line", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 16.0f;
    node.text_content = "alpha\nbeta\ngamma";
    (void)GetRuntime().LayoutParagraph(node, 400.0f);
    REQUIRE(node.logical_line_shape_cache_valid);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.logical_line_shapes.size() == 3U);
    REQUIRE(node.visual_line_shapes.size() == 3U);
    REQUIRE(node.line_heights.size() == 3U);

    const float base_line_height = node.line_heights[0];
    const float tall_line_height = base_line_height + 7.0f;
    node.visual_line_shapes[1].height = tall_line_height;
    node.visual_line_shapes[2].height = tall_line_height;
    node.line_heights = {base_line_height, tall_line_height, tall_line_height};
    node.line_y_offsets = {0.0f, base_line_height, base_line_height + tall_line_height, base_line_height + tall_line_height * 2.0f};
    node.line_height = tall_line_height;

    const std::size_t beta_pos = node.text_content.find("beta");
    REQUIRE(beta_pos != std::string::npos);
    const std::string previous_text = node.text_content;
    node.text_content.insert(beta_pos + 1U, "!");
    REQUIRE(GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text));

    REQUIRE(node.line_heights.size() == 3U);
    CHECK(node.line_heights[0] == Approx(base_line_height).margin(0.01f));
    CHECK(node.line_heights[1] == Approx(base_line_height).margin(0.01f));
    CHECK(node.line_heights[2] == Approx(tall_line_height).margin(0.01f));
    REQUIRE(node.line_y_offsets.size() == 4U);
    CHECK(node.line_y_offsets[1] == Approx(base_line_height).margin(0.01f));
    CHECK(node.line_y_offsets[2] == Approx(base_line_height * 2.0f).margin(0.01f));
    CHECK(node.line_y_offsets[3] == Approx((base_line_height * 2.0f) + tall_line_height).margin(0.01f));
}

TEST_CASE("v2 ui explicit line height fixes non-wrap line metrics across fallback glyph lines", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto thai_font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/NotoSansThai-Regular.ttf");
    const auto arabic_font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/NotoNaskhArabic-Variable.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, thai_font_bytes.data(), static_cast<std::uint32_t>(thai_font_bytes.size())));
    REQUIRE(GetRuntime().RegisterFont(2U, arabic_font_bytes.data(), static_cast<std::uint32_t>(arabic_font_bytes.size())));
    REQUIRE(GetRuntime().RegisterFontFallback(1U, 2U));

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 20.0f;
    node.authored_line_height = 12.0f;
    node.text_content = std::string(u8"plain line\nกم");
    const auto paragraph = GetRuntime().LayoutParagraph(node, 320.0f);

    REQUIRE(node.line_heights.size() == 2U);
    REQUIRE(node.line_ascents.size() == 2U);
    CHECK(node.line_heights[0] == Approx(12.0f).margin(0.01f));
    CHECK(node.line_heights[1] == Approx(12.0f).margin(0.01f));
    CHECK(node.line_ascents[0] == Approx(node.line_ascents[1]).margin(0.01f));
    CHECK(node.line_height == Approx(12.0f).margin(0.01f));
    CHECK(paragraph.height == Approx(24.0f).margin(0.01f));
}

TEST_CASE("v2 ui explicit line height survives incremental non-wrap edits", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 16.0f;
    node.authored_line_height = 12.0f;
    node.text_content = "alpha\nbeta\ngamma";
    (void)GetRuntime().LayoutParagraph(node, 160.0f);
    REQUIRE(node.line_heights.size() == 3U);

    const std::string previous_text = node.text_content;
    node.text_content.replace(node.text_content.find("beta"), 4U, "bet!");
    REQUIRE(GetRuntime().TryApplyIncrementalNonWrapLayoutCache(node, previous_text));

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = false;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.authored_line_height = node.authored_line_height;
    expected.text_content = node.text_content;
    (void)GetRuntime().LayoutParagraph(expected, 160.0f);

    CHECK(node.line_height == Approx(12.0f).margin(0.01f));
    REQUIRE(node.line_heights.size() == expected.line_heights.size());
    REQUIRE(node.line_ascents.size() == expected.line_ascents.size());
    REQUIRE(node.line_y_offsets.size() == expected.line_y_offsets.size());
    for (std::size_t index = 0; index < node.line_heights.size(); index += 1U) {
        CHECK(node.line_heights[index] == Approx(expected.line_heights[index]).margin(0.01f));
        CHECK(node.line_ascents[index] == Approx(expected.line_ascents[index]).margin(0.01f));
    }
    for (std::size_t index = 0; index < node.line_y_offsets.size(); index += 1U) {
        CHECK(node.line_y_offsets[index] == Approx(expected.line_y_offsets[index]).margin(0.01f));
    }
}

TEST_CASE("v2 ui explicit line height survives incremental wrapped edits", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 16.0f;
    node.authored_line_height = 12.0f;
    node.text_content =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau";
    (void)GetRuntime().LayoutParagraph(node, 92.0f);
    REQUIRE_FALSE(node.line_heights.empty());

    const std::string previous_text = node.text_content;
    node.text_content.insert(1U, "!");
    REQUIRE(GetRuntime().TryApplyIncrementalWrappedLayoutCache(node, previous_text));

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = true;
    expected.font_id = node.font_id;
    expected.font_size = node.font_size;
    expected.authored_line_height = node.authored_line_height;
    expected.text_content = node.text_content;
    (void)GetRuntime().LayoutParagraph(expected, 92.0f);

    CHECK(node.line_height == Approx(12.0f).margin(0.01f));
    REQUIRE(node.line_heights.size() == expected.line_heights.size());
    REQUIRE(node.line_ascents.size() == expected.line_ascents.size());
    REQUIRE(node.line_y_offsets.size() == expected.line_y_offsets.size());
    for (std::size_t index = 0; index < node.line_heights.size(); index += 1U) {
        CHECK(node.line_heights[index] == Approx(expected.line_heights[index]).margin(0.01f));
        CHECK(node.line_ascents[index] == Approx(expected.line_ascents[index]).margin(0.01f));
    }
    for (std::size_t index = 0; index < node.line_y_offsets.size(); index += 1U) {
        CHECK(node.line_y_offsets[index] == Approx(expected.line_y_offsets[index]).margin(0.01f));
    }
}

TEST_CASE("v2 ui font fallback shapes missing glyphs with registered fallback fonts", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto thai_font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/NotoSansThai-Regular.ttf");
    const auto arabic_font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/NotoNaskhArabic-Variable.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, thai_font_bytes.data(), static_cast<std::uint32_t>(thai_font_bytes.size())));
    REQUIRE(GetRuntime().RegisterFont(2U, arabic_font_bytes.data(), static_cast<std::uint32_t>(arabic_font_bytes.size())));
    REQUIRE(GetRuntime().RegisterFontFallback(1U, 2U));

    effindom::v2::ui::UiRuntime::ShapedTextRun thai_only{};
    effindom::v2::ui::UiRuntime::ShapedTextRun arabic_only{};
    effindom::v2::ui::UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText(u8"ก", 1U, 20.0f, thai_only));
    REQUIRE(GetRuntime().ShapeText(u8"م", 2U, 20.0f, arabic_only));
    REQUIRE(GetRuntime().ShapeText(u8"กم", 1U, 20.0f, shaped));
    REQUIRE_FALSE(shaped.glyphs.empty());

    CHECK(shaped.baseline == Approx(shaped.ascent).margin(0.01f));
    CHECK(shaped.height == Approx(shaped.ascent + shaped.descent).margin(0.01f));
    CHECK(shaped.height >= thai_only.height);
    CHECK(shaped.height >= arabic_only.height);

    bool saw_primary_font = false;
    bool saw_fallback_font = false;
    for (const auto& glyph : shaped.glyphs) {
        if (glyph.font_id == 1U) {
            saw_primary_font = true;
        } else if (glyph.font_id == 2U) {
            saw_fallback_font = true;
        }
    }
    CHECK(saw_primary_font);
    CHECK(saw_fallback_font);

    const ui_handle_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_resize_window(320.0f, 120.0f);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(u8"กم"), 5U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());
    bool emitted_fallback_font = false;
    for (const auto& glyph : glyph_runs.at(text).glyphs) {
        if (glyph.font_id == 2U) {
            emitted_fallback_font = true;
            break;
        }
    }
    CHECK(emitted_fallback_font);
}

TEST_CASE("v2 ui text defaults use documented RGBA color packing", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    CHECK(node->text_color == kDefaultTextColor);
    CHECK(node->caret_color == kDefaultTextColor);
    CHECK(node->selection_color == kDefaultSelectionColor);
}

TEST_CASE("v2 ui text color changes invalidate cached glyph run colors", "[v2][ui][unit][text]") {
    ui_reset();
    RegisterTestFont(1U);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 120.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Theme recolor"), 13U);
    ui_set_text_color(text, 0xffffffffU);
    ui_node_add_child(root, text);

    ui_commit_frame();
    auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());
    CHECK(glyph_runs.at(text).color == 0xffffffffU);

    ui_set_text_color(text, 0x0f172affU);
    ui_commit_frame();
    glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());
    CHECK(glyph_runs.at(text).color == 0x0f172affU);
}
