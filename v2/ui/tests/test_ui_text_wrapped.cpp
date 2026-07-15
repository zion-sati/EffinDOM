#include "TestUiSupport.h"

TEST_CASE("v2 ui focused editable text emits caret", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Focusable"), 9U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_caret_color(text, 0xFF224466U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    ui_commit_frame();

    const auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    CHECK(carets.at(text).height > 0.0f);
    CHECK(carets.at(text).color == 0xFF224466U);
    CHECK(GetRuntime().Resolve(text)->selection_start == 0U);
    CHECK(GetRuntime().Resolve(text)->selection_end == 0U);
}

TEST_CASE("v2 ui wrapped typing does not scroll an already-visible caret to the viewport top", "[v2][ui][text-edit][caret-reveal]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    std::string content{};
    for (std::uint32_t line = 0U; line < 30U; line += 1U) {
        if (line != 0U) {
            content += '\n';
        }
        content += "Stable wrapped line " + std::to_string(line) +
            " carries enough repeated content to wrap across the editor viewport.";
    }

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 140.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text_limits(text, -1, 0);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();
    GetRuntime().SetFocus(text);

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->break_offsets.size() > 20U);
    const std::size_t target_line = 16U;
    const std::uint32_t caret = static_cast<std::uint32_t>(text_node->break_offsets[target_line]);
    const float line_top = GetRuntime().GetLineTopForIndex(*text_node, target_line);
    // Keep the full line visible by one pixel. The former editable overscan
    // incorrectly scrolled it toward the top even though no reveal was needed.
    const float target_offset = std::max(0.0f, line_top - 1.0f);
    ui_set_scroll_offset(scroll, 0.0f, target_offset);
    ui_set_text_selection_range(text, caret, caret);
    ui_commit_frame();

    const auto* before = GetRuntime().Resolve(scroll);
    REQUIRE(before != nullptr);
    const float stable_offset = before->scroll_offset_y;
    REQUIRE(stable_offset > 0.0f);

    static constexpr std::string_view kInserted = "x";
    ui_replace_text_range(
        text,
        caret,
        caret,
        reinterpret_cast<const std::uint8_t*>(kInserted.data()),
        static_cast<std::uint32_t>(kInserted.size()),
        caret + 1U);
    ui_commit_frame();

    const auto* after = GetRuntime().Resolve(scroll);
    REQUIRE(after != nullptr);
    CHECK(after->scroll_offset_y == Approx(stable_offset).margin(0.01f));
}

TEST_CASE("v2 ui focused empty editable text emits a full-height caret", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_caret_color(text, 0xFF224466U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    ui_commit_frame();

    const auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    CHECK(carets.at(text).height == Approx(node->line_height).margin(1.0f));
    CHECK(carets.at(text).height > 10.0f);
}

    TEST_CASE("v2 ui focused empty read-only textbox emits a full-height caret", "[v2][ui][text-edit]") {
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
        ui_resize_window(220.0f, 80.0f);
        ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(text, 1U, 20.0f);
        ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
        ui_set_selectable(text, true, 0x40007AFFU);
        ui_set_editable(text, true);
        ui_set_editable(text, false);
        ui_set_caret_color(text, 0xFF335577U);
        ui_node_add_child(root, text);
        ui_commit_frame();

        GetRuntime().SetFocus(text);
        ui_commit_frame();

        const auto carets = ReadCarets(ReadCommandBuffer());
        REQUIRE(carets.find(text) != carets.end());
        const auto* node = GetRuntime().Resolve(text);
        REQUIRE(node != nullptr);
        CHECK(carets.at(text).height == Approx(node->line_height).margin(1.0f));
        CHECK(carets.at(text).height > 10.0f);
        CHECK(carets.at(text).color == 0xFF335577U);
    }

    TEST_CASE("v2 ui clicking the visual end of a multiline line keeps the caret on that line", "[v2][ui][text-edit]") {
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

    static constexpr char kMultilineText[] = "Line one\nLine two\nLine three";
    ui_set_root(root);
    ui_resize_window(240.0f, 140.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kMultilineText), sizeof(kMultilineText) - 1U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->line_widths.size() >= 2U);

    const float click_x = node->abs_x + node->line_widths[0] - 1.0f;
    const float click_y = node->abs_y + (node->line_height * 0.5f);
    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, click_x, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, click_x, click_y);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("!"), 1U, 0U);

    CHECK(GetRuntime().Resolve(text)->text_content == "Line one!\nLine two\nLine three");
}

TEST_CASE("v2 ui wrapped multiline edits at a hard-line end keep later line starts aligned", "[v2][ui][text-edit]") {
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

    static constexpr char kMultilineText[] =
        "Line one\nLine two\nLine three\nLonger content so scrollbar policy is easy to spot.";
    ui_set_root(root);
    ui_resize_window(320.0f, 220.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kMultilineText), sizeof(kMultilineText) - 1U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->line_widths.size() >= 3U);

    const float click_x = node->abs_x + node->line_widths[0] - 1.0f;
    const float click_y = node->abs_y + (node->line_height * 0.5f);
    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, click_x, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, click_x, click_y);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("t"), 1U, 0U);
    ui_commit_frame();

    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_content == "Line onet\nLine two\nLine three\nLonger content so scrollbar policy is easy to spot.");

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.font_id = node->font_id;
    expected.font_size = node->font_size;
    expected.layout_width = node->layout_width;
    expected.layout_height = node->layout_height;
    expected.text_wrap = true;
    expected.text_content = node->text_content;
    GetRuntime().RebuildTextLineStarts(expected);
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 260.0f);

    REQUIRE(node->text_line_starts == expected.text_line_starts);
    REQUIRE(node->break_offsets == expected.break_offsets);
    REQUIRE(node->line_widths.size() == expected.line_widths.size());
    CHECK(expected_layout.total_line_count == node->total_line_count);
    for (std::size_t index = 0; index < node->line_widths.size(); index += 1U) {
        CHECK(node->line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
}

TEST_CASE("v2 ui clicking the end of a wrapped visual line then pressing Enter matches fresh layout", "[v2][ui][text-edit]") {
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

    const std::string wrapped_text =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon";

    ui_set_root(root);
    ui_resize_window(220.0f, 180.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 96.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(wrapped_text.data()), static_cast<std::uint32_t>(wrapped_text.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_layout_cache_valid);
    REQUIRE(node->visual_line_shape_cache_valid);
    REQUIRE(node->visual_line_shapes.size() == node->total_line_count);
    REQUIRE(node->total_line_count >= 5U);

    const std::uint32_t expected_insert_at = node->visual_line_shapes[3].end;
    const float click_x = node->abs_x + node->layout_width - 1.0f;
    const float click_y = node->abs_y + (node->line_height * 3.5f);
    ui_set_interaction_time(200U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, click_x, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, click_x, click_y);

    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    CHECK(node->selection_start == expected_insert_at);
    CHECK(node->selection_end == expected_insert_at);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Enter"), 5U, 0U);
    ui_commit_frame();

    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_content.size() == wrapped_text.size() + 1U);
    CHECK(node->text_content[expected_insert_at] == '\n');
    CHECK(node->selection_start == expected_insert_at + 1U);
    CHECK(node->selection_end == expected_insert_at + 1U);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.font_id = node->font_id;
    expected.font_size = node->font_size;
    expected.layout_width = node->layout_width;
    expected.layout_height = node->layout_height;
    expected.text_wrap = true;
    expected.max_lines = 0;
    expected.text_content = node->text_content;
    GetRuntime().RebuildTextLineStarts(expected);
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, node->layout_width);

    REQUIRE(node->text_line_starts == expected.text_line_starts);
    REQUIRE(node->break_offsets == expected.break_offsets);
    REQUIRE(node->line_widths.size() == expected.line_widths.size());
    REQUIRE(node->logical_line_shapes.size() == expected.logical_line_shapes.size());
    REQUIRE(node->visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node->total_line_count == expected_layout.total_line_count);
}

TEST_CASE("v2 ui soft wrap boundary caret has a virtual arrow stop", "[v2][ui][text-edit]") {
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

    const std::string wrapped_text =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon";

    ui_set_root(root);
    ui_resize_window(220.0f, 180.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 96.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(wrapped_text.data()), static_cast<std::uint32_t>(wrapped_text.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visual_line_shape_cache_valid);
    REQUIRE(node->visual_line_shapes.size() == node->total_line_count);
    REQUIRE(node->total_line_count >= 5U);

    const std::size_t wrapped_line = 3U;
    const std::uint32_t boundary_index = node->visual_line_shapes[wrapped_line].end;
    const auto [trailing_x, trailing_line] = GetRuntime().GetLocalPositionFromIndex(*node, boundary_index, true);
    const auto [leading_x, leading_line] = GetRuntime().GetLocalPositionFromIndex(*node, boundary_index, false);
    REQUIRE(trailing_line == static_cast<int>(wrapped_line));
    REQUIRE(leading_line == static_cast<int>(wrapped_line + 1U));

    const float click_x = node->abs_x + node->layout_width - 1.0f;
    const float click_y =
        node->abs_y +
        GetRuntime().GetLineTopForIndex(*node, wrapped_line) +
        (GetRuntime().GetLineHeightForIndex(*node, wrapped_line) * 0.5f);
    ui_set_interaction_time(200U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, click_x, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, click_x, click_y);
    ui_commit_frame();

    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    CHECK(node->selection_start == boundary_index);
    CHECK(node->selection_end == boundary_index);
    CHECK(node->caret_trailing_edge);
    auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    CHECK(carets.at(text).x == Approx(node->abs_x + trailing_x).margin(0.75f));

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, 0U);
    ui_commit_frame();

    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    CHECK(node->selection_start == boundary_index);
    CHECK(node->selection_end == boundary_index);
    CHECK_FALSE(node->caret_trailing_edge);
    carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    CHECK(carets.at(text).x == Approx(node->abs_x + leading_x).margin(0.75f));

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, 0U);
    ui_commit_frame();
    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    CHECK(node->selection_start > boundary_index);
    CHECK(node->selection_end > boundary_index);
}

TEST_CASE("v2 ui moving focus away from editable text transfers the caret to focused read-only textbox text", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t editable = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t readonly = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(editable != UI_INVALID_HANDLE);
    REQUIRE(readonly != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(editable, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(editable, 1U, 20.0f);
    ui_set_text(editable, reinterpret_cast<const std::uint8_t*>("Editable"), 8U);
    ui_set_selectable(editable, true, 0x40007AFFU);
    ui_set_editable(editable, true);
    ui_set_caret_color(editable, 0xFF224466U);
    ui_set_width(readonly, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(readonly, 1U, 20.0f);
    ui_set_text(readonly, reinterpret_cast<const std::uint8_t*>("Read only"), 9U);
    ui_set_semantic_role(readonly, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(readonly, true, 0x40007AFFU);
    ui_set_editable(readonly, true);
    ui_set_editable(readonly, false);
    ui_set_caret_color(readonly, 0xFF335577U);
    ui_node_add_child(root, editable);
    ui_node_add_child(root, readonly);
    ui_commit_frame();

    ui_set_interaction_time(111U);
    GetRuntime().SetFocus(editable);
    ui_commit_frame();
    auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(editable) != carets.end());
    CHECK(carets.at(editable).height > 0.0f);
    CHECK(carets.at(editable).last_interaction_ms == 111U);

    ui_set_interaction_time(222U);
    GetRuntime().SetFocus(readonly);
    ui_commit_frame();
    carets = ReadCarets(ReadCommandBuffer());
    CHECK(carets.find(editable) == carets.end());
    REQUIRE(carets.find(readonly) != carets.end());
    CHECK(carets.at(readonly).height > 0.0f);
    CHECK(carets.at(readonly).color == 0xFF335577U);
    CHECK(carets.at(readonly).last_interaction_ms == 222U);
}

TEST_CASE("v2 ui focused selectable text without textbox semantics does not render a caret", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Plain selectable"), 16U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_caret_color(text, 0xFF224466U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    ui_set_interaction_time(333U);
    GetRuntime().SetFocus(text);
    ui_commit_frame();

    const auto carets = ReadCarets(ReadCommandBuffer());
    CHECK(carets.find(text) == carets.end());
    CHECK_FALSE(GetRuntime().NeedsAnimationFrame());
}

TEST_CASE("v2 ui textbox semantics without editor behavior does not render a caret", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Semantic only"), 13U);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_caret_color(text, 0xFF224466U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    ui_set_interaction_time(334U);
    GetRuntime().SetFocus(text);
    ui_commit_frame();

    const auto carets = ReadCarets(ReadCommandBuffer());
    CHECK(carets.find(text) == carets.end());
}

TEST_CASE("v2 ui editor behavior without textbox semantics renders a caret", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Editor only"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_editable(text, false);
    ui_set_caret_color(text, 0xFF224466U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 3U;
    node->selection_end = 3U;
    ui_set_interaction_time(335U);
    GetRuntime().SetFocus(text);
    ui_commit_frame();

    const auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, 0U);
    CHECK(node->selection_start == 4U);
    CHECK(node->selection_end == 4U);
}

TEST_CASE("v2 ui read-only textbox keeps keyboard caret movement and selection extension", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 1);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_editable(text, false);
    ui_set_interactive(text, true);
    ui_set_caret_color(text, 0xFF224466U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 3U;
    node->selection_end = 3U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_set_interaction_time(444U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, 0U);

    CHECK(node->selection_start == 4U);
    CHECK(node->selection_end == 4U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 4U);
    CHECK(g_selection_changes[0].end == 4U);

    ResetInteractionLogs();
    ui_set_interaction_time(445U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);

    CHECK(node->selection_start == 4U);
    CHECK(node->selection_end == 5U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 4U);
    CHECK(g_selection_changes[0].end == 5U);
}

TEST_CASE("v2 ui keyboard arrow right moves caret by character", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 3U;
    node->selection_end = 3U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_set_interaction_time(123U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, 0U);

    CHECK(node->selection_start == 4U);
    CHECK(node->selection_end == 4U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 4U);
    CHECK(g_selection_changes[0].end == 4U);
}

TEST_CASE("v2 ui shift right only adjusts an existing selection", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 80.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 3U;
    node->selection_end = 3U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_set_interaction_time(124U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);

    CHECK(node->selection_start == 3U);
    CHECK(node->selection_end == 3U);
    CHECK(g_selection_changes.empty());

    node->selection_start = 2U;
    node->selection_end = 3U;
    GetRuntime().Selection().state().anchor_handle = text;
    GetRuntime().Selection().state().anchor_index = 2U;
    ResetInteractionLogs();
    ui_set_interaction_time(125U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);

    CHECK(node->selection_start == 2U);
    CHECK(node->selection_end == 4U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 2U);
    CHECK(g_selection_changes[0].end == 4U);

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK(highlights.at(text).rects.size() >= 1U);
}

TEST_CASE("v2 ui mouse drag selection keeps its anchor for shift horizontal keys", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 100.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [anchor_x, anchor_line] = GetRuntime().GetLocalPositionFromIndex(*node, 2U);
    const auto [focus_x, focus_line] = GetRuntime().GetLocalPositionFromIndex(*node, 5U);
    REQUIRE(anchor_line == 0);
    REQUIRE(focus_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + anchor_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, node->abs_x + focus_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + focus_x + 0.5f, y);

    auto* mutable_node = GetRuntime().ResolveMutable(text);
    REQUIRE(mutable_node != nullptr);
    CHECK(mutable_node->selection_start == 2U);
    CHECK(mutable_node->selection_end == 5U);
    CHECK(GetRuntime().Selection().state().anchor_handle == text);
    CHECK(GetRuntime().Selection().state().anchor_index == 2U);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);

    CHECK(mutable_node->selection_start == 2U);
    CHECK(mutable_node->selection_end == 6U);
    CHECK(GetRuntime().Selection().state().anchor_index == 2U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 2U);
    CHECK(g_selection_changes[0].end == 6U);
}

TEST_CASE("v2 ui backward mouse drag selection keeps its anchor for shift horizontal keys", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 100.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [anchor_x, anchor_line] = GetRuntime().GetLocalPositionFromIndex(*node, 5U);
    const auto [focus_x, focus_line] = GetRuntime().GetLocalPositionFromIndex(*node, 2U);
    REQUIRE(anchor_line == 0);
    REQUIRE(focus_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + anchor_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, node->abs_x + focus_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + focus_x + 0.5f, y);

    auto* mutable_node = GetRuntime().ResolveMutable(text);
    REQUIRE(mutable_node != nullptr);
    CHECK(mutable_node->selection_start == 5U);
    CHECK(mutable_node->selection_end == 2U);
    CHECK(GetRuntime().Selection().state().anchor_handle == text);
    CHECK(GetRuntime().Selection().state().anchor_index == 5U);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowLeft"), 9U, UI_KEY_MOD_SHIFT);

    CHECK(mutable_node->selection_start == 5U);
    CHECK(mutable_node->selection_end == 1U);
    CHECK(GetRuntime().Selection().state().anchor_index == 5U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 5U);
    CHECK(g_selection_changes[0].end == 1U);
}

TEST_CASE("v2 ui wrapped mouse drag selection on a large document deletes the full selected range", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string source_text = ReadWrappedTextFixture();
    const WrappedTextFixtureTargets fixture = FindWrappedTextFixtureTargets(source_text);
    const std::uint32_t selection_start = fixture.reverse_selection_start;
    const std::uint32_t selection_end =
        static_cast<std::uint32_t>(std::min(source_text.size(), static_cast<std::size_t>(fixture.reverse_selection_start) + 256U));
    REQUIRE(selection_end > selection_start);

    const std::string expected_text =
        source_text.substr(0, selection_start) + source_text.substr(selection_end);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(1280.0f, 640.0f);
    ui_set_width(root, 1280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 640.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 1180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 540.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 1100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(
        text,
        reinterpret_cast<const std::uint8_t*>(source_text.data()),
        static_cast<std::uint32_t>(source_text.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->layout_height > scroll_node->layout_height);

    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, selection_start);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, selection_end);
    REQUIRE(end_line >= start_line);

    const float start_local_y = (static_cast<float>(start_line) * text_node->line_height) + (text_node->line_height * 0.5f);
    const float target_scroll_y = std::max(0.0f, start_local_y - text_node->line_height);
    ui_set_scroll_offset(scroll, 0.0f, target_scroll_y);
    ui_commit_frame();

    text_node = GetRuntime().ResolveMutable(text);
    scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);

    const float start_y = text_node->abs_y +
        (static_cast<float>(start_line) * text_node->line_height) +
        (text_node->line_height * 0.5f);
    const float end_y = text_node->abs_y +
        (static_cast<float>(end_line) * text_node->line_height) +
        (text_node->line_height * 0.5f);
    REQUIRE(start_y >= scroll_node->abs_y);
    REQUIRE(end_y <= scroll_node->abs_y + scroll_node->layout_height);

    GetRuntime().SetFocus(text);
    ResetInteractionLogs();
    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, text_node->abs_x + start_x + 0.5f, start_y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, text_node->abs_x + end_x + 0.5f, end_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, text_node->abs_x + end_x + 0.5f, end_y);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->selection_start == selection_start);
    CHECK(text_node->selection_end == selection_end);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Backspace"), 9U, 0U);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_content == expected_text);
}

TEST_CASE("v2 ui wrapped mouse drag selection stays exact after pasting a large document", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string source_text = ReadWrappedTextFixture();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(1280.0f, 640.0f);
    ui_set_width(root, 1280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 640.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 1180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 540.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 1100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    GetRuntime().HandlePasteText(
        text,
        reinterpret_cast<const std::uint8_t*>(source_text.data()),
        static_cast<std::uint32_t>(source_text.size()));
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->text_content == source_text);
    REQUIRE(text_node->layout_height > scroll_node->layout_height);

    REQUIRE(text_node->break_offsets.size() >= 8U);
    const std::size_t start_visual_line = 3U;
    const std::size_t end_visual_line = 6U;
    const std::uint32_t selection_start = static_cast<std::uint32_t>(std::max(text_node->break_offsets[start_visual_line], 0));
    const std::uint32_t selection_end = static_cast<std::uint32_t>(std::max(text_node->break_offsets[end_visual_line], 0));
    REQUIRE(selection_end > selection_start);
    const std::string expected_text =
        source_text.substr(0, selection_start) + source_text.substr(selection_end);

    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, selection_start);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, selection_end);
    REQUIRE(end_line >= start_line);

    const float start_local_y = (static_cast<float>(start_line) * text_node->line_height) + (text_node->line_height * 0.5f);
    ui_set_scroll_offset(scroll, 0.0f, std::max(0.0f, start_local_y - text_node->line_height));
    ui_commit_frame();

    text_node = GetRuntime().ResolveMutable(text);
    scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);

    const float start_y = text_node->abs_y +
        (static_cast<float>(start_line) * text_node->line_height) +
        (text_node->line_height * 0.5f);
    const float end_y = text_node->abs_y +
        (static_cast<float>(end_line) * text_node->line_height) +
        (text_node->line_height * 0.5f);
    REQUIRE(start_y >= scroll_node->abs_y);
    REQUIRE(end_y <= scroll_node->abs_y + scroll_node->layout_height);

    ResetInteractionLogs();
    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, text_node->abs_x + start_x + 0.5f, start_y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, text_node->abs_x + end_x + 0.5f, end_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, text_node->abs_x + end_x + 0.5f, end_y);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->selection_start == selection_start);
    CHECK(text_node->selection_end == selection_end);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Backspace"), 9U, 0U);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_content == expected_text);
}

TEST_CASE("v2 ui wrapped reverse mouse drag deletes the full selected multiline range", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string source_text =
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

    const std::size_t selection_start_pos =
        source_text.find("        uint32_t isFixedPitchVal = ");
    REQUIRE(selection_start_pos != std::string::npos);
    const std::size_t continuation_end_pos =
        source_text.find("             (uint8_t)data[15];\n", selection_start_pos);
    REQUIRE(continuation_end_pos != std::string::npos);
    const std::uint32_t selection_start = static_cast<std::uint32_t>(selection_start_pos);
    const std::uint32_t selection_end = static_cast<std::uint32_t>(
        continuation_end_pos + std::string("             (uint8_t)data[15];\n\n").size());
    REQUIRE(selection_end > selection_start);
    const std::string selected_text = source_text.substr(selection_start, selection_end - selection_start);
    CHECK(
        selected_text ==
        "        uint32_t isFixedPitchVal = \n"
        "            ((uint8_t)data[12] << 24) | \n"
        "            ((uint8_t)data[13] << 16) | \n"
        "            ((uint8_t)data[14] <<  8) | \n"
        "             (uint8_t)data[15];\n"
        "\n");

    const std::string expected_text =
        source_text.substr(0, selection_start) + source_text.substr(selection_end);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(960.0f, 640.0f);
    ui_set_width(root, 960.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 640.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 900.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 540.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 820.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(
        text,
        reinterpret_cast<const std::uint8_t*>(source_text.data()),
        static_cast<std::uint32_t>(source_text.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);

    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, selection_start);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, selection_end);
    REQUIRE(end_line >= start_line);

    const float start_y = text_node->abs_y +
        (static_cast<float>(start_line) * text_node->line_height) +
        (text_node->line_height * 0.5f);
    const float end_y = text_node->abs_y +
        (static_cast<float>(end_line) * text_node->line_height) +
        (text_node->line_height * 0.5f);
    REQUIRE(start_y >= scroll_node->abs_y);
    REQUIRE(end_y <= scroll_node->abs_y + scroll_node->layout_height);

    GetRuntime().SetFocus(text);
    ResetInteractionLogs();
    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, text_node->abs_x + end_x + 0.5f, end_y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, text_node->abs_x + start_x + 0.5f, start_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, text_node->abs_x + start_x + 0.5f, start_y);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(std::min(text_node->selection_start, text_node->selection_end) == selection_start);
    CHECK(std::max(text_node->selection_start, text_node->selection_end) == selection_end);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Backspace"), 9U, 0U);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_content == expected_text);
}

TEST_CASE("v2 ui wrapped deleting a single comment line emits the same glyph run as fresh layout", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    struct BuiltScene {
        std::uint64_t scroll = UI_INVALID_HANDLE;
        std::uint64_t text = UI_INVALID_HANDLE;
    };

    const auto build_scene = [](const std::string& text_content) -> BuiltScene {
        const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
        const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
        const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
        REQUIRE(root != UI_INVALID_HANDLE);
        REQUIRE(scroll != UI_INVALID_HANDLE);
        REQUIRE(text != UI_INVALID_HANDLE);

        ui_set_root(root);
        ui_resize_window(960.0f, 640.0f);
        ui_set_width(root, 960.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(root, 640.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(scroll, 900.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(scroll, 540.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_scroll_enabled(scroll, false, true);
        ui_set_width(text, 660.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(text, 800.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(text, 1U, 18.0f);
        ui_set_text(
            text,
            reinterpret_cast<const std::uint8_t*>(text_content.data()),
            static_cast<std::uint32_t>(text_content.size()));
        ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
        ui_set_text_wrapping(text, true);
        ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
        ui_set_selectable(text, true, 0x40007AFFU);
        ui_set_editable(text, true);
        ui_set_interactive(text, true);
        ui_node_add_child(root, scroll);
        ui_node_add_child(scroll, text);
        ui_commit_frame();
        return BuiltScene{scroll, text};
    };

    const std::string source_text =
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
        source_text.find("        // 0 = proportional font, non-zero (usually 1) = monospaced font\n");
    REQUIRE(comment_start_pos != std::string::npos);
    const std::size_t fixed_pitch_start_pos =
        source_text.find("        fixedPitch = (isFixedPitchVal != 0);", comment_start_pos);
    REQUIRE(fixed_pitch_start_pos != std::string::npos);
    REQUIRE(fixed_pitch_start_pos > comment_start_pos);
    const std::string expected_text =
        source_text.substr(0, comment_start_pos) + source_text.substr(fixed_pitch_start_pos);

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();
    const BuiltScene edited_scene = build_scene(source_text);
    GetRuntime().SetFocus(edited_scene.text);
    ui_set_text_selection_range(
        edited_scene.text,
        static_cast<std::uint32_t>(comment_start_pos),
        static_cast<std::uint32_t>(fixed_pitch_start_pos));
    ui_commit_frame();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Backspace"), 9U, 0U);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(edited_scene.text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->text_content == expected_text);
    const auto* edited_scroll_node = GetRuntime().Resolve(edited_scene.scroll);
    REQUIRE(edited_scroll_node != nullptr);
    const float edited_scroll_offset_x = edited_scroll_node->scroll_offset_x;
    const float edited_scroll_offset_y = edited_scroll_node->scroll_offset_y;
    const auto edited_words = ReadCommandBuffer();
    const auto edited_runs = ReadGlyphRuns(edited_words);
    REQUIRE(edited_runs.size() == 1U);
    const auto edited_run = edited_runs.begin()->second;

    ui_reset();
    RegisterTestFont();
    const BuiltScene fresh_scene = build_scene(expected_text);
    GetRuntime().SetFocus(fresh_scene.text);
    ui_set_text_selection_range(
        fresh_scene.text,
        static_cast<std::uint32_t>(comment_start_pos),
        static_cast<std::uint32_t>(comment_start_pos));
    ui_set_scroll_offset(fresh_scene.scroll, edited_scroll_offset_x, edited_scroll_offset_y);
    ui_set_text_color(fresh_scene.text, 0x010101FFU);
    ui_commit_frame();
    const auto fresh_words = ReadCommandBuffer();
    const auto fresh_runs = ReadGlyphRuns(fresh_words);
    REQUIRE(fresh_runs.size() == 1U);
    const auto fresh_run = fresh_runs.begin()->second;

    REQUIRE(edited_run.font_id == fresh_run.font_id);
    REQUIRE(edited_run.glyphs.size() == fresh_run.glyphs.size());
    for (std::size_t glyph_index = 0; glyph_index < edited_run.glyphs.size(); glyph_index += 1U) {
        CHECK(edited_run.glyphs[glyph_index].glyph_id == fresh_run.glyphs[glyph_index].glyph_id);
        CHECK(edited_run.glyphs[glyph_index].font_id == fresh_run.glyphs[glyph_index].font_id);
        CHECK(edited_run.glyphs[glyph_index].cluster == fresh_run.glyphs[glyph_index].cluster);
        CHECK(edited_run.glyphs[glyph_index].x == Approx(fresh_run.glyphs[glyph_index].x).margin(0.05f));
        CHECK(edited_run.glyphs[glyph_index].y == Approx(fresh_run.glyphs[glyph_index].y).margin(0.05f));
    }
}

TEST_CASE("v2 ui wrapped deleting a block before its trailing newline emits the same glyph run as fresh layout", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    struct BuiltScene {
        std::uint64_t scroll = UI_INVALID_HANDLE;
        std::uint64_t text = UI_INVALID_HANDLE;
    };

    const auto build_scene = [](const std::string& text_content) -> BuiltScene {
        const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
        const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
        const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
        REQUIRE(root != UI_INVALID_HANDLE);
        REQUIRE(scroll != UI_INVALID_HANDLE);
        REQUIRE(text != UI_INVALID_HANDLE);

        ui_set_root(root);
        ui_resize_window(960.0f, 640.0f);
        ui_set_width(root, 960.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(root, 640.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(scroll, 900.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(scroll, 540.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_scroll_enabled(scroll, false, true);
        ui_set_width(text, 820.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(text, 1U, 18.0f);
        ui_set_text(
            text,
            reinterpret_cast<const std::uint8_t*>(text_content.data()),
            static_cast<std::uint32_t>(text_content.size()));
        ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
        ui_set_text_wrapping(text, true);
        ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
        ui_set_selectable(text, true, 0x40007AFFU);
        ui_set_editable(text, true);
        ui_set_interactive(text, true);
        ui_node_add_child(root, scroll);
        ui_node_add_child(scroll, text);
        ui_commit_frame();
        return BuiltScene{scroll, text};
    };

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string source_text = ReadWrappedTextFixture();
    const WrappedTextFixtureTargets fixture = FindWrappedTextFixtureTargets(source_text);
    const std::size_t block_start_pos = fixture.block_start;
    const std::size_t block_end_pos = source_text.find("        }\n        segment_start = ", block_start_pos);
    REQUIRE(block_end_pos != std::string::npos);
    const std::size_t selection_end = block_end_pos + std::string("        }").size();
    REQUIRE(selection_end < source_text.size());
    REQUIRE(source_text[selection_end] == '\n');
    const std::string expected_text =
        source_text.substr(0, block_start_pos) + source_text.substr(selection_end);

    const BuiltScene edited_scene = build_scene(source_text);
    auto* edited_text_node = GetRuntime().ResolveMutable(edited_scene.text);
    auto* edited_scroll_node = GetRuntime().ResolveMutable(edited_scene.scroll);
    REQUIRE(edited_text_node != nullptr);
    REQUIRE(edited_scroll_node != nullptr);

    const auto [selection_x, selection_line] = GetRuntime().GetLocalPositionFromIndex(
        *edited_text_node,
        static_cast<std::uint32_t>(selection_end));
    (void)selection_x;
    const float selection_local_y =
        (static_cast<float>(selection_line) * edited_text_node->line_height) +
        (edited_text_node->line_height * 0.5f);
    ui_set_scroll_offset(
        edited_scene.scroll,
        0.0f,
        std::max(0.0f, selection_local_y - (edited_text_node->line_height * 4.0f)));
    ui_commit_frame();

    GetRuntime().SetFocus(edited_scene.text);
    ui_set_text_selection_range(
        edited_scene.text,
        static_cast<std::uint32_t>(block_start_pos),
        static_cast<std::uint32_t>(selection_end));
    ui_commit_frame();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Backspace"), 9U, 0U);
    ui_commit_frame();

    edited_text_node = GetRuntime().ResolveMutable(edited_scene.text);
    edited_scroll_node = GetRuntime().ResolveMutable(edited_scene.scroll);
    REQUIRE(edited_text_node != nullptr);
    REQUIRE(edited_scroll_node != nullptr);
    REQUIRE(edited_text_node->text_content == expected_text);
    const float edited_scroll_offset_x = edited_scroll_node->scroll_offset_x;
    const float edited_scroll_offset_y = edited_scroll_node->scroll_offset_y;
    const auto edited_words = ReadCommandBuffer();
    const auto edited_runs = ReadGlyphRuns(edited_words);
    REQUIRE(edited_runs.size() == 1U);
    const auto edited_run = edited_runs.begin()->second;

    ui_reset();
    RegisterTestFont();
    const BuiltScene fresh_scene = build_scene(expected_text);
    GetRuntime().SetFocus(fresh_scene.text);
    ui_set_text_selection_range(
        fresh_scene.text,
        static_cast<std::uint32_t>(block_start_pos),
        static_cast<std::uint32_t>(block_start_pos));
    ui_set_scroll_offset(fresh_scene.scroll, edited_scroll_offset_x, edited_scroll_offset_y);
    ui_commit_frame();
    const auto fresh_words = ReadCommandBuffer();
    const auto fresh_runs = ReadGlyphRuns(fresh_words);
    REQUIRE(fresh_runs.size() == 1U);
    const auto fresh_run = fresh_runs.begin()->second;

    REQUIRE(edited_run.font_id == fresh_run.font_id);
    REQUIRE(edited_run.glyphs.size() == fresh_run.glyphs.size());
    for (std::size_t glyph_index = 0; glyph_index < edited_run.glyphs.size(); glyph_index += 1U) {
        CHECK(edited_run.glyphs[glyph_index].glyph_id == fresh_run.glyphs[glyph_index].glyph_id);
        CHECK(edited_run.glyphs[glyph_index].font_id == fresh_run.glyphs[glyph_index].font_id);
        CHECK(edited_run.glyphs[glyph_index].cluster == fresh_run.glyphs[glyph_index].cluster);
        CHECK(edited_run.glyphs[glyph_index].x == Approx(fresh_run.glyphs[glyph_index].x).margin(0.05f));
        CHECK(edited_run.glyphs[glyph_index].y == Approx(fresh_run.glyphs[glyph_index].y).margin(0.05f));
    }
}

TEST_CASE("v2 ui wrapped Backspace at the start of line three emits the same glyph run as fresh layout", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    struct BuiltScene {
        std::uint64_t text = UI_INVALID_HANDLE;
    };

    const auto build_scene = [](const std::string& text_content) -> BuiltScene {
        const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
        const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
        REQUIRE(root != UI_INVALID_HANDLE);
        REQUIRE(text != UI_INVALID_HANDLE);

        ui_set_root(root);
        ui_resize_window(320.0f, 220.0f);
        ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(text, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(text, 180.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(text, 1U, 20.0f);
        ui_set_text(
            text,
            reinterpret_cast<const std::uint8_t*>(text_content.data()),
            static_cast<std::uint32_t>(text_content.size()));
        ui_set_text_wrapping(text, true);
        ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
        ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
        ui_set_selectable(text, true, 0x40007AFFU);
        ui_set_editable(text, true);
        ui_set_interactive(text, true);
        ui_node_add_child(root, text);
        ui_commit_frame();
        return BuiltScene{text};
    };

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string source_text =
        "Line one keeps wrapping for a while so wrapped visual rows stay active.\n"
        "Line two\n"
        "Line three\n"
        "Trailing content keeps the wrapped cache warm too.";
    const std::size_t third_line_start = source_text.find("Line three");
    REQUIRE(third_line_start != std::string::npos);
    REQUIRE(third_line_start > 0U);
    REQUIRE(source_text[third_line_start - 1U] == '\n');
    const std::string expected_text =
        source_text.substr(0, third_line_start - 1U) + source_text.substr(third_line_start);

    const BuiltScene edited_scene = build_scene(source_text);
    GetRuntime().SetFocus(edited_scene.text);
    ui_set_text_selection_range(
        edited_scene.text,
        static_cast<std::uint32_t>(third_line_start),
        static_cast<std::uint32_t>(third_line_start));
    ui_commit_frame();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Backspace"), 9U, 0U);
    ui_commit_frame();

    auto* edited_text_node = GetRuntime().ResolveMutable(edited_scene.text);
    REQUIRE(edited_text_node != nullptr);
    REQUIRE(edited_text_node->text_content == expected_text);
    const auto edited_words = ReadCommandBuffer();
    const auto edited_runs = ReadGlyphRuns(edited_words);
    REQUIRE(edited_runs.size() == 1U);
    const auto edited_run = edited_runs.begin()->second;

    ui_reset();
    RegisterTestFont();
    (void)build_scene(expected_text);
    const auto fresh_words = ReadCommandBuffer();
    const auto fresh_runs = ReadGlyphRuns(fresh_words);
    REQUIRE(fresh_runs.size() == 1U);
    const auto fresh_run = fresh_runs.begin()->second;

    REQUIRE(edited_run.font_id == fresh_run.font_id);
    REQUIRE(edited_run.glyphs.size() == fresh_run.glyphs.size());
    for (std::size_t glyph_index = 0; glyph_index < edited_run.glyphs.size(); glyph_index += 1U) {
        CHECK(edited_run.glyphs[glyph_index].glyph_id == fresh_run.glyphs[glyph_index].glyph_id);
        CHECK(edited_run.glyphs[glyph_index].font_id == fresh_run.glyphs[glyph_index].font_id);
        CHECK(edited_run.glyphs[glyph_index].cluster == fresh_run.glyphs[glyph_index].cluster);
        CHECK(edited_run.glyphs[glyph_index].x == Approx(fresh_run.glyphs[glyph_index].x).margin(0.05f));
        CHECK(edited_run.glyphs[glyph_index].y == Approx(fresh_run.glyphs[glyph_index].y).margin(0.05f));
    }
}

TEST_CASE("v2 ui wrapped shift-up selection after pasting a large document deletes the intended block", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string source_text = ReadWrappedTextFixture();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(960.0f, 640.0f);
    ui_set_width(root, 960.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 640.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 900.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 540.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 820.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    GetRuntime().HandlePasteText(
        text,
        reinterpret_cast<const std::uint8_t*>(source_text.data()),
        static_cast<std::uint32_t>(source_text.size()));
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->text_content == source_text);

    REQUIRE(text_node->break_offsets.size() >= 8U);
    const std::size_t end_visual_line = 6U;
    const std::uint32_t selection_end = static_cast<std::uint32_t>(std::max(text_node->break_offsets[end_visual_line], 0));

    const auto [caret_x, caret_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, selection_end);
    const float caret_local_y = (static_cast<float>(caret_line) * text_node->line_height) + (text_node->line_height * 0.5f);
    ui_set_scroll_offset(scroll, 0.0f, std::max(0.0f, caret_local_y - (text_node->line_height * 4.0f)));
    ui_commit_frame();

    text_node = GetRuntime().ResolveMutable(text);
    scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);

    const float caret_y = text_node->abs_y +
        (static_cast<float>(caret_line) * text_node->line_height) +
        (text_node->line_height * 0.5f);
    REQUIRE(caret_y >= scroll_node->abs_y);
    REQUIRE(caret_y <= scroll_node->abs_y + scroll_node->layout_height);

    ResetInteractionLogs();
    ui_set_interaction_time(100U);
    const float caret_click_x = text_node->abs_x + caret_x + 0.5f;
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, caret_click_x, caret_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, caret_click_x, caret_y);
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->selection_start == selection_end);
    CHECK(text_node->selection_end == selection_end);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowUp"), 7U, UI_KEY_MOD_SHIFT);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowUp"), 7U, UI_KEY_MOD_SHIFT);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowUp"), 7U, UI_KEY_MOD_SHIFT);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    const std::uint32_t range_start = std::min(text_node->selection_start, text_node->selection_end);
    const std::uint32_t range_end = std::max(text_node->selection_start, text_node->selection_end);
    CHECK(range_end == selection_end);
    REQUIRE(range_end > range_start);
    const std::string expected_text =
        source_text.substr(0, range_start) + source_text.substr(range_end);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Backspace"), 9U, 0U);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_content == expected_text);
}

TEST_CASE("v2 ui wrapped repeated paste force-breaks long unbroken runs", "[v2][ui][text-edit]") {
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
    ui_resize_window(160.0f, 180.0f);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_text_wrapping(text, true);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    static constexpr char kPaste[] = "AAAAA";
    std::string expected_text{};
    for (std::size_t index = 0U; index < 16U; index += 1U) {
        GetRuntime().HandlePasteText(
            text,
            reinterpret_cast<const std::uint8_t*>(kPaste),
            static_cast<std::uint32_t>(sizeof(kPaste) - 1U));
        expected_text += kPaste;
    }
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_content == expected_text);
    REQUIRE(node->total_line_count > 1U);
    for (const float line_width : node->line_widths) {
        CHECK(line_width <= Approx(node->layout_width).margin(0.05f));
    }

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.font_id = node->font_id;
    expected.font_size = node->font_size;
    expected.layout_width = node->layout_width;
    expected.layout_height = node->layout_height;
    expected.text_wrap = true;
    expected.max_lines = 0;
    expected.text_content = node->text_content;
    GetRuntime().RebuildTextLineStarts(expected);
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, node->layout_width);

    REQUIRE(node->break_offsets == expected.break_offsets);
    REQUIRE(node->line_widths.size() == expected.line_widths.size());
    CHECK(node->total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node->line_widths.size(); index += 1U) {
        CHECK(node->line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
}

TEST_CASE("v2 ui pointer up without drag keeps a collapsed selection at the pointer-down index", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 100.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [down_x, down_line] = GetRuntime().GetLocalPositionFromIndex(*node, 2U);
    const auto [up_x, up_line] = GetRuntime().GetLocalPositionFromIndex(*node, 5U);
    REQUIRE(down_line == 0);
    REQUIRE(up_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + down_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + up_x + 0.5f, y);

    auto* mutable_node = GetRuntime().ResolveMutable(text);
    REQUIRE(mutable_node != nullptr);
    CHECK(mutable_node->selection_start == 2U);
    CHECK(mutable_node->selection_end == 2U);
    CHECK_FALSE(GetRuntime().Selection().state().active_dragged);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 2U);
    CHECK(g_selection_changes[0].end == 2U);
}

TEST_CASE("v2 ui single-line textbox keeps its horizontal viewport when clicking a visible caret position", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "abcdefghijklmnopqrstuvwxyz0123456789";

    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 1);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = static_cast<std::uint32_t>(std::strlen(kSample));
    node->selection_end = static_cast<std::uint32_t>(std::strlen(kSample));
    const auto [end_local_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*node, node->selection_end);
    REQUIRE(end_line == 0);
    REQUIRE(node->textbox_viewport_offset_x > 0.0f);
    const float initial_offset = node->textbox_viewport_offset_x;
    const std::uint32_t visible_index = GetRuntime().GetStringIndexFromPoint(
        *node,
        node->layout_width * 0.75f,
        node->line_height * 0.5f);
    REQUIRE(visible_index > 0U);
    REQUIRE(visible_index < static_cast<std::uint32_t>(std::strlen(kSample)));

    const auto [visible_local_x, visible_line] = GetRuntime().GetLocalPositionFromIndex(*node, visible_index);
    REQUIRE(visible_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + visible_local_x, y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + visible_local_x, y);

    CHECK(node->selection_start == visible_index);
    CHECK(node->selection_end == visible_index);
    CHECK(node->textbox_viewport_offset_x == Approx(initial_offset));
    CHECK(end_local_x > visible_local_x);
}

TEST_CASE("v2 ui single-line textbox paints the caret at or after the rightmost visible glyph while typing at the end", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "abcdefghijklmnopqrstuvwxyz0123456789";

    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 1);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_caret_color(text, 0xFF224466U);
    ui_node_add_child(root, text);
    ui_commit_frame();

    GetRuntime().SetFocus(text);
    for (std::size_t length = 1; length <= std::strlen(kSample); length += 1U) {
        ui_set_interaction_time(static_cast<std::uint64_t>(100U + length));
        ui_on_ime_update(
            text,
            reinterpret_cast<const std::uint8_t*>(kSample),
            static_cast<std::uint32_t>(length),
            static_cast<std::uint32_t>(length));
        ui_commit_frame();

        const auto words = ReadCommandBuffer();
        const auto bounds = ReadBounds(words);
        const auto glyph_runs = ReadGlyphRuns(words);
        const auto carets = ReadCarets(words);
        REQUIRE(bounds.find(text) != bounds.end());
        REQUIRE(carets.find(text) != carets.end());
        REQUIRE(glyph_runs.find(text) != glyph_runs.end());
        const auto& run = glyph_runs.at(text);
        if (run.glyphs.empty()) {
            continue;
        }

        const float max_glyph_x = std::max_element(
            run.glyphs.begin(),
            run.glyphs.end(),
            [](const effindom::v2::ui::GlyphPlacement& lhs, const effindom::v2::ui::GlyphPlacement& rhs) {
                return lhs.x < rhs.x;
            })->x;
        const float local_caret_x = carets.at(text).x - bounds.at(text).x;
        CHECK(local_caret_x + 0.5f >= max_glyph_x);
    }
}

TEST_CASE("v2 ui multiline textbox inserts a newline on Enter", "[v2][ui][text-edit]") {
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
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hi"), 2U);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 2U;
    node->selection_end = 2U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_set_interaction_time(777U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Enter"), 5U, 0U);

    CHECK(node->text_content == "Hi\n");
    CHECK(node->selection_start == 3U);
    CHECK(node->selection_end == 3U);
    REQUIRE(g_text_changes.size() == 1U);
    CHECK(g_text_changes[0].text == "Hi\n");
}

TEST_CASE("v2 ui wrapped multiline textbox Enter at a wrapped line end matches fresh layout", "[v2][ui][text-edit]") {
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

    const std::string wrapped_text =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon";

    ui_set_root(root);
    ui_resize_window(220.0f, 160.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 96.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(wrapped_text.data()), static_cast<std::uint32_t>(wrapped_text.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_layout_cache_valid);
    REQUIRE(node->visual_line_shape_cache_valid);
    REQUIRE(node->visual_line_shapes.size() == node->total_line_count);
    REQUIRE(node->total_line_count >= 5U);

    const std::uint32_t insert_at = node->visual_line_shapes[3].end;
    REQUIRE(insert_at > 0U);
    REQUIRE(insert_at < node->text_content.size());

    GetRuntime().SetFocus(text);
    node->selection_start = insert_at;
    node->selection_end = insert_at;
    ResetInteractionLogs();

    ui_set_interaction_time(1337U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Enter"), 5U, 0U);
    ui_commit_frame();

    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_content.size() == wrapped_text.size() + 1U);
    CHECK(node->text_content[insert_at] == '\n');
    CHECK(node->selection_start == insert_at + 1U);
    CHECK(node->selection_end == insert_at + 1U);

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.font_id = node->font_id;
    expected.font_size = node->font_size;
    expected.layout_width = node->layout_width;
    expected.layout_height = node->layout_height;
    expected.text_wrap = true;
    expected.max_lines = 0;
    expected.text_content = node->text_content;
    GetRuntime().RebuildTextLineStarts(expected);
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, node->layout_width);

    REQUIRE(node->text_line_starts == expected.text_line_starts);
    REQUIRE(node->break_offsets == expected.break_offsets);
    REQUIRE(node->line_widths.size() == expected.line_widths.size());
    REQUIRE(node->logical_line_shapes.size() == expected.logical_line_shapes.size());
    REQUIRE(node->visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node->total_line_count == expected_layout.total_line_count);
    for (std::size_t index = 0; index < node->line_widths.size(); index += 1U) {
        CHECK(node->line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < node->visual_line_shapes.size(); index += 1U) {
        const auto& patched = node->visual_line_shapes[index];
        const auto& fresh = expected.visual_line_shapes[index];
        CHECK(patched.start == fresh.start);
        CHECK(patched.end == fresh.end);
        CHECK(patched.width == Approx(fresh.width).margin(0.05f));
    }
}

TEST_CASE("v2 ui wrapped monospace logical lines enable arithmetic break metrics", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterMonoTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    const std::string source_text(64U, 'A');

    ui_set_root(root);
    ui_resize_window(220.0f, 180.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 88.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 5U, 18.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(source_text.data()), static_cast<std::uint32_t>(source_text.size()));
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->logical_line_shape_cache_valid);
    REQUIRE(node->logical_line_shapes.size() == 1U);
    REQUIRE(node->total_line_count > 1U);

    auto& logical_shape = node->logical_line_shapes.front();
    GetRuntime().EnsureCachedLogicalLineBreakCandidates(node->text_content, logical_shape);
    REQUIRE(logical_shape.break_candidate_cache_valid);
    CHECK(logical_shape.monospace_fast_path_eligible);
    CHECK(logical_shape.monospace_wrapped_metrics_eligible);
    REQUIRE(logical_shape.break_candidates.size() == logical_shape.break_candidate_x_offsets.size());
    for (std::size_t index = 0; index < logical_shape.break_candidates.size(); index += 1U) {
        const std::uint32_t local_offset =
            static_cast<std::uint32_t>(std::max(logical_shape.break_candidates[index], 0));
        const float expected_x = std::min(
            static_cast<float>(local_offset) * logical_shape.monospace_cell_width,
            logical_shape.width);
        CHECK(logical_shape.break_candidate_x_offsets[index] == Approx(expected_x).margin(0.05f));
    }
}

TEST_CASE("v2 ui wrapped monospace append edits preserve arithmetic break metrics", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterMonoTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    const std::string source_text(40U, 'A');
    static constexpr std::string_view kAppend = "BBBBBBBB";

    ui_set_root(root);
    ui_resize_window(220.0f, 180.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 88.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 5U, 18.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text_limits(text, -1, 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(source_text.data()), static_cast<std::uint32_t>(source_text.size()));
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* initial_node = GetRuntime().ResolveMutable(text);
    REQUIRE(initial_node != nullptr);
    REQUIRE(initial_node->logical_line_shape_cache_valid);
    REQUIRE(initial_node->logical_line_shapes.size() == 1U);
    GetRuntime().EnsureCachedLogicalLineBreakCandidates(initial_node->text_content, initial_node->logical_line_shapes.front());
    REQUIRE(initial_node->logical_line_shapes.front().monospace_wrapped_metrics_eligible);

    ui_replace_text_range(
        text,
        static_cast<std::uint32_t>(source_text.size()),
        static_cast<std::uint32_t>(source_text.size()),
        reinterpret_cast<const std::uint8_t*>(kAppend.data()),
        static_cast<std::uint32_t>(kAppend.size()),
        static_cast<std::uint32_t>(source_text.size() + kAppend.size()));
    ui_commit_frame();

    auto* updated_node = GetRuntime().ResolveMutable(text);
    REQUIRE(updated_node != nullptr);
    REQUIRE(updated_node->logical_line_shape_cache_valid);
    REQUIRE(updated_node->logical_line_shapes.size() == 1U);
    auto& patched_shape = updated_node->logical_line_shapes.front();
    GetRuntime().EnsureCachedLogicalLineBreakCandidates(updated_node->text_content, patched_shape);
    CHECK(patched_shape.monospace_fast_path_eligible);
    CHECK(patched_shape.monospace_wrapped_metrics_eligible);

    effindom::v2::ui::UINode recomputed = *updated_node;
    recomputed.text_layout_cache_valid = false;
    recomputed.logical_line_shape_cache_valid = false;
    recomputed.visual_line_shape_cache_valid = false;
    recomputed.logical_line_shapes.clear();
    recomputed.visual_line_shapes.clear();
    recomputed.break_offsets.clear();
    recomputed.line_widths.clear();
    recomputed.line_heights.clear();
    recomputed.line_ascents.clear();
    recomputed.line_y_offsets.clear();
    (void)GetRuntime().LayoutParagraph(recomputed, updated_node->layout_width);
    REQUIRE(recomputed.logical_line_shape_cache_valid);
    REQUIRE(recomputed.logical_line_shapes.size() == 1U);
    auto& fresh_shape = recomputed.logical_line_shapes.front();
    GetRuntime().EnsureCachedLogicalLineBreakCandidates(recomputed.text_content, fresh_shape);

    CHECK(fresh_shape.monospace_wrapped_metrics_eligible);
    REQUIRE(updated_node->break_offsets == recomputed.break_offsets);
    REQUIRE(updated_node->line_widths.size() == recomputed.line_widths.size());
    for (std::size_t index = 0; index < updated_node->line_widths.size(); index += 1U) {
        CHECK(updated_node->line_widths[index] == Approx(recomputed.line_widths[index]).margin(0.05f));
    }
    for (std::size_t index = 0; index < patched_shape.break_candidates.size(); index += 1U) {
        const std::uint32_t local_offset =
            static_cast<std::uint32_t>(std::max(patched_shape.break_candidates[index], 0));
        const float expected_x = std::min(
            static_cast<float>(local_offset) * patched_shape.monospace_cell_width,
            patched_shape.width);
        CHECK(patched_shape.break_candidate_x_offsets[index] == Approx(expected_x).margin(0.05f));
    }
}
