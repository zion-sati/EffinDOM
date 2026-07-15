#include "TestUiSupport.h"

TEST_CASE("v2 ui multiline non-wrap textbox keeps painting after caret scroll exceeds layout width", "[v2][ui][text-edit][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(360.0f, 120.0f);
    ui_set_width(root, 360.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_text_wrapping(text, false);
    ui_set_text_limits(text, -1, 0);
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();
    GetRuntime().SetFocus(text);

    static constexpr std::string_view kPhrase = "Longer content so scrollbar policy is easy to spot.";
    std::uint32_t length = 0U;
    for (std::size_t paste = 0; paste < 10U; paste += 1U) {
        ui_replace_text_range(
            text,
            length,
            length,
            reinterpret_cast<const std::uint8_t*>(kPhrase.data()),
            static_cast<std::uint32_t>(kPhrase.size()),
            length + static_cast<std::uint32_t>(kPhrase.size()));
        length += static_cast<std::uint32_t>(kPhrase.size());
        ui_commit_frame();
    }

    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(scroll_node->scroll_offset_x > 300.0f);

    const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());
    CHECK_FALSE(glyph_runs.at(text).glyphs.empty());
}

TEST_CASE("v2 ui home and end move caret to line boundaries", "[v2][ui][text-edit]") {
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
    ui_resize_window(260.0f, 80.0f);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 3U;
    node->selection_end = 3U;
    GetRuntime().SetFocus(text);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Home"), 4U, 0U);
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 0U);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("End"), 3U, 0U);
    CHECK(node->selection_start == 11U);
    CHECK(node->selection_end == 11U);
}

TEST_CASE("v2 ui ctrl right jumps to next word boundary", "[v2][ui][text-edit]") {
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
    ui_resize_window(260.0f, 80.0f);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello brave world"), 17U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 1U;
    node->selection_end = 1U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_CTRL);

    CHECK(node->selection_start == 5U);
    CHECK(node->selection_end == 5U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 5U);
    CHECK(g_selection_changes[0].end == 5U);
}

TEST_CASE("v2 ui arrow down moves caret to wrapped next line", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "The quick brown fox jumps over the lazy dog while editors track wrapped selections";
    ui_set_root(root);
    ui_resize_window(180.0f, 260.0f);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visible_line_count >= 3U);
    const std::uint32_t start_index = static_cast<std::uint32_t>(node->break_offsets[0] + 2);
    const auto [x, line] = GetRuntime().GetLocalPositionFromIndex(*node, start_index);
    REQUIRE(line == 0);
    const std::uint32_t expected = GetRuntime().GetStringIndexFromPoint(*node, x, (node->line_height * 1.5f));

    node->selection_start = start_index;
    node->selection_end = start_index;
    GetRuntime().SetFocus(text);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowDown"), 9U, 0U);

    CHECK(node->selection_start == expected);
    CHECK(node->selection_end == expected);
}

TEST_CASE("v2 ui shift arrow down is ignored for now", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "The quick brown fox jumps over the lazy dog while editors track wrapped selections";
    ui_set_root(root);
    ui_resize_window(180.0f, 260.0f);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visible_line_count >= 3U);
    const std::uint32_t start_index = static_cast<std::uint32_t>(node->break_offsets[0] + 2);
    const auto [_, line] = GetRuntime().GetLocalPositionFromIndex(*node, start_index);
    REQUIRE(line == 0);
    node->selection_start = start_index;
    node->selection_end = start_index;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowDown"), 9U, UI_KEY_MOD_SHIFT);

    CHECK(node->selection_start == start_index);
    CHECK(node->selection_end == start_index);
    CHECK(g_selection_changes.empty());
}

TEST_CASE("v2 ui apple line-boundary keys stay on the current wrapped textbox line", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PlatformFamily;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample =
        "The quick brown fox jumps over the lazy dog while editors track wrapped selections";
    ui_set_root(root);
    ui_resize_window(220.0f, 180.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 90.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visible_line_count >= 2U);
    const std::uint32_t first_line_start = static_cast<std::uint32_t>(node->break_offsets[0]);
    const std::uint32_t first_line_end = static_cast<std::uint32_t>(node->break_offsets[1]);
    REQUIRE(first_line_end > first_line_start);
    REQUIRE(first_line_end < static_cast<std::uint32_t>(node->text_content.size()));
    REQUIRE(node->text_content[first_line_end - 1U] != '\n');

    GetRuntime().SetPlatformFamily(static_cast<std::uint32_t>(PlatformFamily::Apple));
    GetRuntime().SetFocus(text);

    node->selection_start = first_line_end;
    node->selection_end = first_line_end;
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowLeft"), 9U, UI_KEY_MOD_META);

    CHECK(node->selection_start == first_line_start);
    CHECK(node->selection_end == first_line_start);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == first_line_start);
    CHECK(g_selection_changes[0].end == first_line_start);

    node->selection_start = first_line_end;
    node->selection_end = first_line_end;
    ResetInteractionLogs();
    ui_on_key_event(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("ArrowLeft"),
        9U,
        UI_KEY_MOD_META | UI_KEY_MOD_SHIFT);

    CHECK(node->selection_start == first_line_end);
    CHECK(node->selection_end == first_line_start);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == first_line_end);
    CHECK(g_selection_changes[0].end == first_line_start);
}

TEST_CASE("v2 ui apple shift line-boundary selection stays on the current non-wrapped hard line", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PlatformFamily;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "first hard line\nsecond hard line";
    ui_set_root(root);
    ui_resize_window(320.0f, 180.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->break_offsets.size() == 3U);
    const std::uint32_t first_line_end = static_cast<std::uint32_t>(node->break_offsets[1]);
    REQUIRE(first_line_end > 0U);
    REQUIRE(first_line_end < node->text_content.size());
    REQUIRE(node->text_content[first_line_end] == '\n');

    GetRuntime().SetPlatformFamily(static_cast<std::uint32_t>(PlatformFamily::Apple));
    GetRuntime().SetFocus(text);
    node->selection_start = first_line_end;
    node->selection_end = first_line_end;
    ResetInteractionLogs();

    ui_on_key_event(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("ArrowLeft"),
        9U,
        UI_KEY_MOD_META | UI_KEY_MOD_SHIFT);

    CHECK(node->selection_start == first_line_end);
    CHECK(node->selection_end == 0U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == first_line_end);
    CHECK(g_selection_changes[0].end == 0U);
}

TEST_CASE("v2 ui non-wrapped keyboard caret navigation reveals the line end", "[v2][ui][text-edit][scroll]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PlatformFamily;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    const std::string long_line(160U, 'W');
    ui_set_root(root);
    ui_resize_window(240.0f, 100.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(long_line.data()), static_cast<std::uint32_t>(long_line.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(scroll_node->scroll_content_width > scroll_node->layout_width);
    text_node->selection_start = 0U;
    text_node->selection_end = 0U;
    GetRuntime().SetPlatformFamily(static_cast<std::uint32_t>(PlatformFamily::Apple));
    GetRuntime().SetFocus(text);

    ui_on_key_event(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("ArrowRight"),
        10U,
        UI_KEY_MOD_META);

    CHECK(text_node->selection_start == long_line.size());
    CHECK(text_node->selection_end == long_line.size());
    CHECK(scroll_node->scroll_offset_x > 0.0f);
}

TEST_CASE("v2 ui multiline textbox vertical arrows move immediately from an active selection", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample =
        "The quick brown fox jumps over the lazy dog while editors track wrapped selections";
    ui_set_root(root);
    ui_resize_window(200.0f, 220.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visible_line_count >= 3U);

    const std::uint32_t first_line_anchor = static_cast<std::uint32_t>(node->break_offsets[0] + 1);
    const std::uint32_t first_line_focus = static_cast<std::uint32_t>(node->break_offsets[0] + 3);
    const auto [down_x, down_line] = GetRuntime().GetLocalPositionFromIndex(*node, first_line_focus);
    REQUIRE(down_line == 0);
    const std::uint32_t expected_down = GetRuntime().GetStringIndexFromPoint(*node, down_x, node->line_height * 1.5f);
    REQUIRE(expected_down != first_line_focus);

    GetRuntime().SetFocus(text);
    node->selection_start = first_line_anchor;
    node->selection_end = first_line_focus;
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowDown"), 9U, 0U);

    CHECK(node->selection_start == expected_down);
    CHECK(node->selection_end == expected_down);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == expected_down);
    CHECK(g_selection_changes[0].end == expected_down);

    const std::uint32_t second_line_anchor = static_cast<std::uint32_t>(node->break_offsets[1] + 3);
    const std::uint32_t second_line_focus = static_cast<std::uint32_t>(node->break_offsets[1] + 1);
    const auto [up_x, up_line] = GetRuntime().GetLocalPositionFromIndex(*node, second_line_focus);
    REQUIRE(up_line == 1);
    const std::uint32_t expected_up = GetRuntime().GetStringIndexFromPoint(*node, up_x, node->line_height * 0.5f);
    REQUIRE(expected_up != second_line_focus);

    node->selection_start = second_line_anchor;
    node->selection_end = second_line_focus;
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowUp"), 7U, 0U);

    CHECK(node->selection_start == expected_up);
    CHECK(node->selection_end == expected_up);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == expected_up);
    CHECK(g_selection_changes[0].end == expected_up);
}

TEST_CASE("v2 ui multiline textbox vertical arrows follow visual wrapped lines", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample =
        "The quick brown fox jumps over the lazy dog while editors track wrapped caret movement";
    ui_set_root(root);
    ui_resize_window(200.0f, 220.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visible_line_count >= 3U);

    const std::uint32_t initial = static_cast<std::uint32_t>(node->break_offsets[0] + 2);
    const auto [initial_x, initial_line] = GetRuntime().GetLocalPositionFromIndex(*node, initial);
    REQUIRE(initial_line == 0);
    const std::uint32_t expected = GetRuntime().GetStringIndexFromPoint(*node, initial_x, node->line_height * 1.5f);

    GetRuntime().SetFocus(text);
    node->selection_start = initial;
    node->selection_end = initial;
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowDown"), 9U, 0U);

    CHECK(node->selection_start == expected);
    CHECK(node->selection_end == expected);
    CHECK(GetRuntime().GetLocalPositionFromIndex(*node, node->selection_end).second == 1);
}

TEST_CASE("v2 ui multiline textbox shift vertical arrows fully include the edge line", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample =
        "The quick brown fox jumps over the lazy dog while editors track wrapped selections";
    const std::uint32_t text_length = static_cast<std::uint32_t>(std::strlen(kSample));
    ui_set_root(root);
    ui_resize_window(200.0f, 220.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), text_length);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visible_line_count >= 3U);
    GetRuntime().SetFocus(text);

    node->selection_start = text_length;
    node->selection_end = text_length;
    ResetInteractionLogs();
    for (std::size_t step = 0; step < node->visible_line_count; step += 1U) {
        ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowUp"), 7U, UI_KEY_MOD_SHIFT);
    }

    CHECK(node->selection_start == text_length);
    CHECK(node->selection_end == 0U);
    REQUIRE(g_selection_changes.size() == node->visible_line_count);
    CHECK(g_selection_changes.back().start == text_length);
    CHECK(g_selection_changes.back().end == 0U);

    node->selection_start = 0U;
    node->selection_end = 0U;
    GetRuntime().Selection().state().anchor_handle = text;
    GetRuntime().Selection().state().anchor_index = 0U;
    ResetInteractionLogs();
    for (std::size_t step = 0; step < node->visible_line_count; step += 1U) {
        ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowDown"), 9U, UI_KEY_MOD_SHIFT);
    }

    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == text_length);
    REQUIRE(g_selection_changes.size() == node->visible_line_count);
    CHECK(g_selection_changes.back().start == 0U);
    CHECK(g_selection_changes.back().end == text_length);
}

TEST_CASE("v2 ui multiline textbox page keys move by the scroll viewport", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kLines = "L00\nL01\nL02\nL03\nL04\nL05\nL06\nL07\nL08\nL09\nL10\nL11";
    ui_set_root(root);
    ui_resize_window(220.0f, 96.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 96.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 72.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kLines), static_cast<std::uint32_t>(std::strlen(kLines)));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    const auto* scroll_node = GetRuntime().Resolve(scroll);
    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->visible_line_count >= 10U);
    REQUIRE(scroll_node->scroll_content_height > scroll_node->layout_height);

    const auto compute_page_delta = [&](std::size_t start_line, bool moving_down) {
        float consumed_height = 0.0f;
        int visible_lines = 0;
        int line = static_cast<int>(start_line);
        while (line >= 0 &&
               line < static_cast<int>(text_node->visible_line_count) &&
               consumed_height < scroll_node->layout_height) {
            consumed_height += text_node->line_heights[static_cast<std::size_t>(line)];
            visible_lines += 1;
            line += moving_down ? 1 : -1;
        }
        return std::max(1, visible_lines - 1);
    };
    const int page_delta = compute_page_delta(0U, true);
    const std::uint32_t start_index = 2U;
    const std::uint32_t expected_down = static_cast<std::uint32_t>((page_delta * 4) + 2);

    GetRuntime().SetFocus(text);
    text_node->selection_start = start_index;
    text_node->selection_end = start_index;
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("PageDown"), 8U, 0U);

    CHECK(text_node->selection_start == expected_down);
    CHECK(text_node->selection_end == expected_down);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == expected_down);
    CHECK(g_selection_changes[0].end == expected_down);

    scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y > 0.0f);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("PageUp"), 6U, 0U);

    CHECK(text_node->selection_start == start_index);
    CHECK(text_node->selection_end == start_index);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == start_index);
    CHECK(g_selection_changes[0].end == start_index);

    const std::uint32_t near_top_index = static_cast<std::uint32_t>(text_node->break_offsets[1]) + 1U;
    const std::uint32_t near_bottom_index = static_cast<std::uint32_t>(text_node->break_offsets[text_node->visible_line_count - 1U]) + 1U;

    text_node->selection_start = near_top_index;
    text_node->selection_end = near_top_index;
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("PageUp"), 6U, 0U);

    CHECK(text_node->selection_start == 0U);
    CHECK(text_node->selection_end == 0U);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 0U);
    CHECK(g_selection_changes[0].end == 0U);

    text_node->selection_start = near_bottom_index;
    text_node->selection_end = near_bottom_index;
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("PageDown"), 8U, 0U);

    CHECK(text_node->selection_start == static_cast<std::uint32_t>(std::strlen(kLines)));
    CHECK(text_node->selection_end == static_cast<std::uint32_t>(std::strlen(kLines)));
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == static_cast<std::uint32_t>(std::strlen(kLines)));
    CHECK(g_selection_changes[0].end == static_cast<std::uint32_t>(std::strlen(kLines)));
}

TEST_CASE("v2 ui multiline textbox shift page keys fully include the edge line", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kLines = "L00\nL01\nL02\nL03\nL04\nL05\nL06\nL07\nL08\nL09\nL10\nL11";
    const std::uint32_t text_length = static_cast<std::uint32_t>(std::strlen(kLines));
    ui_set_root(root);
    ui_resize_window(220.0f, 96.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 96.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 72.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kLines), text_length);
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    const auto* scroll_node = GetRuntime().Resolve(scroll);
    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->visible_line_count >= 10U);
    REQUIRE(scroll_node->scroll_content_height > scroll_node->layout_height);

    const auto compute_page_delta = [&](std::size_t start_line, bool moving_down) {
        float consumed_height = 0.0f;
        int visible_lines = 0;
        int line = static_cast<int>(start_line);
        while (line >= 0 &&
               line < static_cast<int>(text_node->visible_line_count) &&
               consumed_height < scroll_node->layout_height) {
            consumed_height += text_node->line_heights[static_cast<std::size_t>(line)];
            visible_lines += 1;
            line += moving_down ? 1 : -1;
        }
        return std::max(1, visible_lines - 1);
    };
    const int page_delta = compute_page_delta(0U, true);
    const std::size_t steps_to_edge_line =
        ((text_node->visible_line_count - 1U) + static_cast<std::size_t>(page_delta) - 1U) /
        static_cast<std::size_t>(page_delta);

    GetRuntime().SetFocus(text);

    text_node->selection_start = text_length;
    text_node->selection_end = text_length;
    ResetInteractionLogs();
    for (std::size_t step = 0; step < steps_to_edge_line; step += 1U) {
        ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("PageUp"), 6U, UI_KEY_MOD_SHIFT);
    }

    CHECK(text_node->selection_start == text_length);
    CHECK(text_node->selection_end == 0U);
    REQUIRE(g_selection_changes.size() == steps_to_edge_line);
    CHECK(g_selection_changes.back().start == text_length);
    CHECK(g_selection_changes.back().end == 0U);

    text_node->selection_start = 0U;
    text_node->selection_end = 0U;
    GetRuntime().Selection().state().anchor_handle = text;
    GetRuntime().Selection().state().anchor_index = 0U;
    ResetInteractionLogs();
    for (std::size_t step = 0; step < steps_to_edge_line; step += 1U) {
        ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("PageDown"), 8U, UI_KEY_MOD_SHIFT);
    }

    CHECK(text_node->selection_start == 0U);
    CHECK(text_node->selection_end == text_length);
    REQUIRE(g_selection_changes.size() == steps_to_edge_line);
    CHECK(g_selection_changes.back().start == 0U);
    CHECK(g_selection_changes.back().end == text_length);
}

TEST_CASE("v2 ui ctrl c copies the selected substring", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 0U;
    node->selection_end = 5U;
    GetRuntime().SetFocus(text);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("c"), 1U, UI_KEY_MOD_CTRL);

    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == "Hello");
    CHECK(node->text_content == "Hello world");
}

TEST_CASE("v2 ui ctrl x cuts the selected substring", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 0U;
    node->selection_end = 5U;
    GetRuntime().SetFocus(text);

    ui_set_interaction_time(222U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("x"), 1U, UI_KEY_MOD_CTRL);

    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == "Hello");
    CHECK(node->text_content == " world");
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 0U);
    REQUIRE(g_text_changes.size() == 1U);
    CHECK(g_text_changes[0].text == " world");
}

TEST_CASE("v2 ui ctrl v requests clipboard and paste text inserts at caret", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 5U;
    node->selection_end = 5U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("v"), 1U, UI_KEY_MOD_CTRL);
    REQUIRE(g_clipboard_read_requests.size() == 1U);
    CHECK(g_clipboard_read_requests[0].handle == text);

    ui_set_interaction_time(333U);
    ui_on_paste_text(text, reinterpret_cast<const std::uint8_t*>(", brave new"), 11U);

    CHECK(node->text_content == "Hello, brave new world");
    CHECK(node->selection_start == 16U);
    CHECK(node->selection_end == 16U);
    REQUIRE(g_text_changes.size() == 1U);
    CHECK(g_text_changes[0].text == "Hello, brave new world");
}

TEST_CASE("v2 ui copy and cut ignore empty selections", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 5U;
    node->selection_end = 5U;
    GetRuntime().SetFocus(text);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("c"), 1U, UI_KEY_MOD_CTRL);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("x"), 1U, UI_KEY_MOD_CTRL);

    CHECK(g_clipboard_writes.empty());
    CHECK(g_text_changes.empty());
    CHECK(node->text_content == "Hello world");
}

TEST_CASE("v2 ui paste replaces an active selection", "[v2][ui][text-edit]") {
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
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 6U;
    node->selection_end = 11U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("v"), 1U, UI_KEY_MOD_CTRL);
    REQUIRE(g_clipboard_read_requests.size() == 1U);

    ui_on_paste_text(text, reinterpret_cast<const std::uint8_t*>("team"), 4U);

    CHECK(node->text_content == "Hello team");
    CHECK(node->selection_start == 10U);
    CHECK(node->selection_end == 10U);
    REQUIRE(g_text_changes.size() == 1U);
    CHECK(g_text_changes[0].text == "Hello team");
}

TEST_CASE("v2 ui ime update emits caret, timestamp, and text callback", "[v2][ui][text-edit]") {
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
    ui_resize_window(180.0f, 100.0f);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_caret_color(text, 0xFF00AAFFU);
    ui_node_add_child(root, text);

    ui_set_interaction_time(345U);
    ui_on_ime_update(text, reinterpret_cast<const std::uint8_t*>("Hello"), 5U, 2U);
    REQUIRE(g_text_changes.size() == 1U);
    CHECK(g_text_changes[0].handle == text);
    CHECK(g_text_changes[0].text == "Hello");

    ui_commit_frame();
    const auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    CHECK(carets.at(text).height > 0.0f);
    CHECK(carets.at(text).color == 0xFF00AAFFU);
    CHECK(carets.at(text).last_interaction_ms == 345U);
    CHECK(GetRuntime().Resolve(text)->text_content == "Hello");
    CHECK(GetRuntime().Resolve(text)->selection_start == 2U);
    CHECK(GetRuntime().Resolve(text)->selection_end == 2U);
}

TEST_CASE("v2 ui editable insert undo restores prior empty state", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    ui_set_interaction_time(100U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("A"), 1U, 0U);

    CHECK(node->text_content == "A");
    CHECK(node->selection_start == 1U);
    CHECK(node->selection_end == 1U);
    CHECK(node->undo_stack.size() == 1U);

    ui_set_interaction_time(150U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);

    CHECK(node->text_content.empty());
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 0U);
    REQUIRE(g_text_changes.size() == 2U);
    CHECK(g_text_changes.back().text.empty());
}

TEST_CASE("v2 ui editable typing within debounce coalesces into one undo step", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    ui_set_interaction_time(0U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("H"), 1U, 0U);
    ui_set_interaction_time(100U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("e"), 1U, 0U);
    ui_set_interaction_time(200U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("llo"), 3U, 0U);

    CHECK(node->text_content == "Hello");
    CHECK(node->undo_stack.size() == 1U);

    ui_set_interaction_time(250U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);

    CHECK(node->text_content.empty());
    CHECK(node->redo_stack.size() == 1U);
}

TEST_CASE("v2 ui editable edits beyond debounce create separate undo groups", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    ui_set_interaction_time(10U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("A"), 1U, 0U);
    ui_set_interaction_time(410U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("B"), 1U, 0U);

    CHECK(node->text_content == "AB");
    CHECK(node->undo_stack.size() == 2U);

    ui_set_interaction_time(450U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);
    CHECK(node->text_content == "A");

    ui_set_interaction_time(500U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);
    CHECK(node->text_content.empty());
}

TEST_CASE("v2 ui editable redo restores an undone edit", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    ui_set_interaction_time(0U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Hello"), 5U, 0U);
    ui_set_interaction_time(50U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);
    CHECK(node->text_content.empty());

    ui_set_interaction_time(75U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL | UI_KEY_MOD_SHIFT);
    CHECK(node->text_content == "Hello");

    ui_set_interaction_time(100U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("y"), 1U, UI_KEY_MOD_CTRL);
    CHECK(node->text_content == "Hello");
}

TEST_CASE("v2 ui editable redo honors Apple shortcut mapping", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PlatformFamily;

    ui_reset();
    UseRecordingInteractionCallbacks();
    GetRuntime().SetPlatformFamily(static_cast<std::uint32_t>(PlatformFamily::Apple));

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    ui_set_interaction_time(0U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Hello"), 5U, 0U);
    ui_set_interaction_time(50U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_META);
    CHECK(node->text_content.empty());

    ui_set_interaction_time(75U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("y"), 1U, UI_KEY_MOD_META);
    CHECK(node->text_content.empty());

    ui_set_interaction_time(100U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_META | UI_KEY_MOD_SHIFT);
    CHECK(node->text_content == "Hello");
}

TEST_CASE("v2 ui editable cut and paste integrate with undo history", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    node->selection_start = 0U;
    node->selection_end = 5U;
    ui_set_interaction_time(100U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("x"), 1U, UI_KEY_MOD_CTRL);

    CHECK(node->text_content == " world");
    CHECK(node->undo_stack.size() == 1U);

    ui_set_interaction_time(150U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);
    CHECK(node->text_content == "Hello world");
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 5U);

    node->selection_start = 5U;
    node->selection_end = 5U;
    ResetInteractionLogs();
    ui_set_interaction_time(600U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("v"), 1U, UI_KEY_MOD_CTRL);
    REQUIRE(g_clipboard_read_requests.size() == 1U);
    ui_on_paste_text(text, reinterpret_cast<const std::uint8_t*>(", brave new"), 11U);

    CHECK(node->text_content == "Hello, brave new world");

    ui_set_interaction_time(650U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);
    CHECK(node->text_content == "Hello world");
}

TEST_CASE("v2 ui non-wrap line-boundary edits keep layout and selection geometry consistent", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    enum class BoundaryEditKind {
        Paste,
        PasteThenUndo,
        Key,
    };

    struct Scenario {
        const char* name = "";
        BoundaryEditKind kind = BoundaryEditKind::Paste;
        std::uint32_t selection_start = 0U;
        std::uint32_t selection_end = 0U;
        const char* payload = nullptr;
        std::uint32_t payload_len = 0U;
        std::string expected_text{};
    };

    const std::string base_text =
        "Line one\n"
        "Line two\n"
        "Line three\n"
        "Longer content so scrollbar policy is easy to spot.";
    const std::uint32_t line_two_end = static_cast<std::uint32_t>(std::string("Line one\nLine two").size());
    const std::uint32_t line_three_start = line_two_end + 1U;
    const std::string double_break_text =
        "Line one\n"
        "Line two\n"
        "\n"
        "Line three\n"
        "Longer content so scrollbar policy is easy to spot.";
    const std::string inserted_line_before_break_text =
        "Line one\n"
        "Line twoLine\n"
        "\n"
        "Line three\n"
        "Longer content so scrollbar policy is easy to spot.";
    const std::string inserted_line_after_break_text =
        "Line one\n"
        "Line two\n"
        "Line\n"
        "Line three\n"
        "Longer content so scrollbar policy is easy to spot.";
    const std::string replaced_newline_with_text =
        "Line one\n"
        "Line twoLine\n"
        "Line three\n"
        "Longer content so scrollbar policy is easy to spot.";
    const std::string merged_line_text =
        "Line one\n"
        "Line twoLine three\n"
        "Longer content so scrollbar policy is easy to spot.";

    const std::vector<Scenario> scenarios = {
        Scenario{
            "paste newline adjacent to existing newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_two_end,
            "\n",
            1U,
            double_break_text,
        },
        Scenario{
            "paste text newline adjacent to existing newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_two_end,
            "Line\n",
            5U,
            inserted_line_before_break_text,
        },
        Scenario{
            "paste newline immediately after existing newline",
            BoundaryEditKind::Paste,
            line_three_start,
            line_three_start,
            "\n",
            1U,
            double_break_text,
        },
        Scenario{
            "paste text newline immediately after existing newline",
            BoundaryEditKind::Paste,
            line_three_start,
            line_three_start,
            "Line\n",
            5U,
            inserted_line_after_break_text,
        },
        Scenario{
            "replace selected newline with text newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_three_start,
            "Line\n",
            5U,
            replaced_newline_with_text,
        },
        Scenario{
            "replace selected newline with double newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_three_start,
            "\n\n",
            2U,
            double_break_text,
        },
        Scenario{
            "undo after text newline paste at line end",
            BoundaryEditKind::PasteThenUndo,
            line_two_end,
            line_two_end,
            "Line\n",
            5U,
            base_text,
        },
        Scenario{
            "undo after text newline paste at next line start",
            BoundaryEditKind::PasteThenUndo,
            line_three_start,
            line_three_start,
            "Line\n",
            5U,
            base_text,
        },
        Scenario{
            "backspace deletes the preceding newline",
            BoundaryEditKind::Key,
            line_three_start,
            line_three_start,
            "Backspace",
            9U,
            merged_line_text,
        },
        Scenario{
            "delete removes the following newline",
            BoundaryEditKind::Key,
            line_two_end,
            line_two_end,
            "Delete",
            6U,
            merged_line_text,
        },
    };

    for (const Scenario& scenario : scenarios) {
        INFO(scenario.name);

        ui_reset();
        RegisterTestFont();

        const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
        const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
        REQUIRE(root != UI_INVALID_HANDLE);
        REQUIRE(text != UI_INVALID_HANDLE);

        ui_set_root(root);
        ui_resize_window(320.0f, 180.0f);
        ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(text, 260.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(text, 1U, 20.0f);
        ui_set_text(text, reinterpret_cast<const std::uint8_t*>(base_text.data()), static_cast<std::uint32_t>(base_text.size()));
        ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
        ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
        ui_set_selectable(text, true, 0x40007AFFU);
        ui_set_editable(text, true);
        ui_set_interactive(text, true);
        ui_set_text_wrapping(text, false);
        ui_node_add_child(root, text);
        ui_commit_frame();

        GetRuntime().SetFocus(text);
        auto* node = GetRuntime().ResolveMutable(text);
        REQUIRE(node != nullptr);
        node->selection_start = scenario.selection_start;
        node->selection_end = scenario.selection_end;

        switch (scenario.kind) {
            case BoundaryEditKind::Paste:
                ui_set_interaction_time(100U);
                GetRuntime().HandlePasteText(
                    text,
                    reinterpret_cast<const std::uint8_t*>(scenario.payload),
                    scenario.payload_len);
                break;
            case BoundaryEditKind::PasteThenUndo:
                ui_set_interaction_time(100U);
                GetRuntime().HandlePasteText(
                    text,
                    reinterpret_cast<const std::uint8_t*>(scenario.payload),
                    scenario.payload_len);
                ui_set_interaction_time(200U);
                REQUIRE(GetRuntime().UndoTextEditAtHandle(text));
                break;
            case BoundaryEditKind::Key:
                ui_set_interaction_time(100U);
                ui_on_key_event(
                    UI_KEY_EVENT_DOWN,
                    reinterpret_cast<const std::uint8_t*>(scenario.payload),
                    scenario.payload_len,
                    0U);
                break;
        }

        ui_commit_frame();

        node = GetRuntime().ResolveMutable(text);
        REQUIRE(node != nullptr);
        REQUIRE(node->text_content == scenario.expected_text);

        effindom::v2::ui::UINode expected{};
        expected.is_text_node = true;
        expected.is_selectable = true;
        expected.is_editable = true;
        expected.semantic_role = UI_SEMANTIC_TEXTBOX;
        expected.text_wrap = false;
        expected.max_lines = 0;
        expected.font_id = 1U;
        expected.font_size = 20.0f;
        expected.text_content = scenario.expected_text;
        const auto expected_layout = GetRuntime().LayoutParagraph(expected, 260.0f);

        REQUIRE(node->text_line_starts == expected.text_line_starts);
        REQUIRE(node->break_offsets == expected.break_offsets);
        REQUIRE(node->line_widths.size() == expected.line_widths.size());
        REQUIRE(node->line_heights.size() == expected.line_heights.size());
        REQUIRE(node->line_ascents.size() == expected.line_ascents.size());
        REQUIRE(node->nonwrap_fragment_line_offsets == expected.nonwrap_fragment_line_offsets);
        REQUIRE(node->nonwrap_fragments.size() == expected.nonwrap_fragments.size());
        CHECK(node->total_line_count == expected_layout.total_line_count);
        CHECK(node->text_layout_cache_max_line_width == Approx(expected_layout.max_line_width).margin(0.05f));
        for (std::size_t index = 0; index < node->line_widths.size(); index += 1U) {
            CHECK(node->line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
            CHECK(node->line_heights[index] == Approx(expected.line_heights[index]).margin(0.05f));
            CHECK(node->line_ascents[index] == Approx(expected.line_ascents[index]).margin(0.05f));
        }
        for (std::size_t index = 0; index < node->nonwrap_fragments.size(); index += 1U) {
            const auto& patched = node->nonwrap_fragments[index];
            const auto& fresh = expected.nonwrap_fragments[index];
            CHECK(patched.line_index == fresh.line_index);
            CHECK(patched.local_byte_start == fresh.local_byte_start);
            CHECK(patched.local_byte_end == fresh.local_byte_end);
            CHECK(patched.x == Approx(fresh.x).margin(0.05f));
            CHECK(patched.width == Approx(fresh.width).margin(0.05f));
        }

        const std::size_t marker_pos = scenario.expected_text.find("Line three");
        if (marker_pos != std::string::npos) {
            const auto [_, candidate_line] = GetRuntime().GetLocalPositionFromIndex(
                *node,
                static_cast<std::uint32_t>(marker_pos));
            const auto [__, expected_line] = GetRuntime().GetLocalPositionFromIndex(
                expected,
                static_cast<std::uint32_t>(marker_pos));
            CHECK(candidate_line == expected_line);
        }
    }
}

TEST_CASE("v2 ui mono non-wrap edits keep the fragment cache on the monospace path", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterMonoTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    std::string code_line{};
    code_line.reserve(2048U);
    for (std::size_t index = 0U; index < 48U; index += 1U) {
        code_line += "value_";
        code_line += std::to_string(index);
        code_line += " = value_";
        code_line += std::to_string(index);
        code_line += "; ";
    }

    ui_set_root(root);
    ui_resize_window(320.0f, 120.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 5U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_text_limits(text, -1, 1);
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(code_line.data()), static_cast<std::uint32_t>(code_line.size()));
    ui_commit_frame();

    const auto* initial_node = GetRuntime().Resolve(text);
    REQUIRE(initial_node != nullptr);
    REQUIRE(initial_node->logical_line_shape_cache_valid);
    REQUIRE(initial_node->logical_line_shapes.size() == 1U);
    REQUIRE(initial_node->logical_line_shapes.front().monospace_fast_path_eligible);
    const std::size_t previous_generation = initial_node->nonwrap_fragment_cache_generation;

    static constexpr std::string_view kInsert = "let ";
    ui_replace_text_range(
        text,
        0U,
        0U,
        reinterpret_cast<const std::uint8_t*>(kInsert.data()),
        static_cast<std::uint32_t>(kInsert.size()),
        static_cast<std::uint32_t>(kInsert.size()));
    ui_commit_frame();

    const auto* updated_node = GetRuntime().Resolve(text);
    REQUIRE(updated_node != nullptr);
    CHECK(updated_node->nonwrap_fragment_cache_valid);
    CHECK(updated_node->nonwrap_fragment_cache_generation > previous_generation);

    effindom::v2::ui::UINode recomputed = *updated_node;
    recomputed.text_layout_cache_valid = false;
    recomputed.logical_line_shape_cache_valid = false;
    recomputed.nonwrap_fragment_cache_valid = false;
    recomputed.logical_line_shapes.clear();
    recomputed.nonwrap_fragments.clear();
    recomputed.nonwrap_fragment_line_offsets.clear();
    (void)GetRuntime().LayoutParagraph(recomputed, 260.0f);
    REQUIRE(recomputed.logical_line_shape_cache_valid);
    REQUIRE(recomputed.logical_line_shapes.size() == 1U);
    CHECK(recomputed.logical_line_shapes.front().monospace_fast_path_eligible);
}

TEST_CASE("v2 ui wrapped line-boundary edits keep layout and selection geometry consistent", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    enum class BoundaryEditKind {
        Paste,
        PasteThenUndo,
        Key,
    };

    struct Scenario {
        const char* name = "";
        BoundaryEditKind kind = BoundaryEditKind::Paste;
        std::uint32_t selection_start = 0U;
        std::uint32_t selection_end = 0U;
        const char* payload = nullptr;
        std::uint32_t payload_len = 0U;
        std::string expected_text{};
    };

    const std::string base_text =
        "Line one\n"
        "Line two\n"
        "Line three\n"
        "Longer content so wrapped layout has to keep flowing after the edit.";
    const std::uint32_t line_two_end = static_cast<std::uint32_t>(std::string("Line one\nLine two").size());
    const std::uint32_t line_three_start = line_two_end + 1U;
    const std::string double_break_text =
        "Line one\n"
        "Line two\n"
        "\n"
        "Line three\n"
        "Longer content so wrapped layout has to keep flowing after the edit.";
    const std::string inserted_line_before_break_text =
        "Line one\n"
        "Line twoLine\n"
        "\n"
        "Line three\n"
        "Longer content so wrapped layout has to keep flowing after the edit.";
    const std::string inserted_line_after_break_text =
        "Line one\n"
        "Line two\n"
        "Line\n"
        "Line three\n"
        "Longer content so wrapped layout has to keep flowing after the edit.";
    const std::string replaced_newline_with_text =
        "Line one\n"
        "Line twoLine\n"
        "Line three\n"
        "Longer content so wrapped layout has to keep flowing after the edit.";
    const std::string merged_line_text =
        "Line one\n"
        "Line twoLine three\n"
        "Longer content so wrapped layout has to keep flowing after the edit.";

    const std::vector<Scenario> scenarios = {
        Scenario{
            "paste newline adjacent to existing newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_two_end,
            "\n",
            1U,
            double_break_text,
        },
        Scenario{
            "paste text newline adjacent to existing newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_two_end,
            "Line\n",
            5U,
            inserted_line_before_break_text,
        },
        Scenario{
            "paste newline immediately after existing newline",
            BoundaryEditKind::Paste,
            line_three_start,
            line_three_start,
            "\n",
            1U,
            double_break_text,
        },
        Scenario{
            "paste text newline immediately after existing newline",
            BoundaryEditKind::Paste,
            line_three_start,
            line_three_start,
            "Line\n",
            5U,
            inserted_line_after_break_text,
        },
        Scenario{
            "replace selected newline with text newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_three_start,
            "Line\n",
            5U,
            replaced_newline_with_text,
        },
        Scenario{
            "replace selected newline with double newline",
            BoundaryEditKind::Paste,
            line_two_end,
            line_three_start,
            "\n\n",
            2U,
            double_break_text,
        },
        Scenario{
            "undo after text newline paste at line end",
            BoundaryEditKind::PasteThenUndo,
            line_two_end,
            line_two_end,
            "Line\n",
            5U,
            base_text,
        },
        Scenario{
            "undo after text newline paste at next line start",
            BoundaryEditKind::PasteThenUndo,
            line_three_start,
            line_three_start,
            "Line\n",
            5U,
            base_text,
        },
        Scenario{
            "backspace deletes the preceding newline",
            BoundaryEditKind::Key,
            line_three_start,
            line_three_start,
            "Backspace",
            9U,
            merged_line_text,
        },
        Scenario{
            "delete removes the following newline",
            BoundaryEditKind::Key,
            line_two_end,
            line_two_end,
            "Delete",
            6U,
            merged_line_text,
        },
    };

    for (const Scenario& scenario : scenarios) {
        INFO(scenario.name);

        ui_reset();
        RegisterTestFont();

        const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
        const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
        REQUIRE(root != UI_INVALID_HANDLE);
        REQUIRE(text != UI_INVALID_HANDLE);

        ui_set_root(root);
        ui_resize_window(220.0f, 180.0f);
        ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_width(text, 150.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(text, 1U, 20.0f);
        ui_set_text(text, reinterpret_cast<const std::uint8_t*>(base_text.data()), static_cast<std::uint32_t>(base_text.size()));
        ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
        ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
        ui_set_selectable(text, true, 0x40007AFFU);
        ui_set_editable(text, true);
        ui_set_interactive(text, true);
        ui_set_text_wrapping(text, true);
        ui_node_add_child(root, text);
        ui_commit_frame();

        GetRuntime().SetFocus(text);
        auto* node = GetRuntime().ResolveMutable(text);
        REQUIRE(node != nullptr);
        node->selection_start = scenario.selection_start;
        node->selection_end = scenario.selection_end;

        switch (scenario.kind) {
            case BoundaryEditKind::Paste:
                ui_set_interaction_time(100U);
                GetRuntime().HandlePasteText(
                    text,
                    reinterpret_cast<const std::uint8_t*>(scenario.payload),
                    scenario.payload_len);
                break;
            case BoundaryEditKind::PasteThenUndo:
                ui_set_interaction_time(100U);
                GetRuntime().HandlePasteText(
                    text,
                    reinterpret_cast<const std::uint8_t*>(scenario.payload),
                    scenario.payload_len);
                ui_set_interaction_time(200U);
                REQUIRE(GetRuntime().UndoTextEditAtHandle(text));
                break;
            case BoundaryEditKind::Key:
                ui_set_interaction_time(100U);
                ui_on_key_event(
                    UI_KEY_EVENT_DOWN,
                    reinterpret_cast<const std::uint8_t*>(scenario.payload),
                    scenario.payload_len,
                    0U);
                break;
        }

        ui_commit_frame();

        node = GetRuntime().ResolveMutable(text);
        REQUIRE(node != nullptr);
        REQUIRE(node->text_content == scenario.expected_text);

        effindom::v2::ui::UINode expected{};
        expected.is_text_node = true;
        expected.is_selectable = true;
        expected.is_editable = true;
        expected.semantic_role = UI_SEMANTIC_TEXTBOX;
        expected.text_wrap = true;
        expected.max_lines = 0;
        expected.font_id = 1U;
        expected.font_size = 20.0f;
        expected.text_content = scenario.expected_text;
        const auto expected_layout = GetRuntime().LayoutParagraph(expected, 150.0f);

        REQUIRE(node->text_line_starts == expected.text_line_starts);
        REQUIRE(node->break_offsets == expected.break_offsets);
        REQUIRE(node->line_widths.size() == expected.line_widths.size());
        REQUIRE(node->line_heights.size() == expected.line_heights.size());
        REQUIRE(node->line_ascents.size() == expected.line_ascents.size());
        REQUIRE(node->visual_line_shapes.size() == expected.visual_line_shapes.size());
        CHECK(node->total_line_count == expected_layout.total_line_count);
        CHECK(node->text_layout_cache_max_line_width == Approx(expected_layout.max_line_width).margin(0.05f));
        for (std::size_t index = 0; index < node->line_widths.size(); index += 1U) {
            CHECK(node->line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
            CHECK(node->line_heights[index] == Approx(expected.line_heights[index]).margin(0.05f));
            CHECK(node->line_ascents[index] == Approx(expected.line_ascents[index]).margin(0.05f));
        }
        for (std::size_t index = 0; index < node->visual_line_shapes.size(); index += 1U) {
            const auto& patched = node->visual_line_shapes[index];
            const auto& fresh = expected.visual_line_shapes[index];
            CHECK(patched.start == fresh.start);
            CHECK(patched.end == fresh.end);
            CHECK(patched.width == Approx(fresh.width).margin(0.05f));
        }

        const std::size_t marker_pos = scenario.expected_text.find("Longer content");
        REQUIRE(marker_pos != std::string::npos);
        const auto [_, candidate_line] = GetRuntime().GetLocalPositionFromIndex(
            *node,
            static_cast<std::uint32_t>(marker_pos));
        const auto [__, expected_line] = GetRuntime().GetLocalPositionFromIndex(
            expected,
            static_cast<std::uint32_t>(marker_pos));
        CHECK(candidate_line == expected_line);
    }
}

TEST_CASE("v2 ui explicit text menu actions target the requested editable node", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    CHECK_FALSE(GetRuntime().CanUndoTextEdit(text));
    CHECK_FALSE(GetRuntime().CanRedoTextEdit(text));

    CHECK(GetRuntime().SelectAllText(text));
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 5U);

    node->selection_start = 2U;
    node->selection_end = 2U;
    ui_set_interaction_time(50U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("a"), 1U, UI_KEY_MOD_CTRL);
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 5U);

    ResetInteractionLogs();
    CHECK(GetRuntime().CopyTextSelection(text));
    REQUIRE(g_clipboard_writes.size() == 1U);
    CHECK(g_clipboard_writes[0].text == "Hello");

    CHECK(GetRuntime().CutTextSelection(text));
    CHECK(node->text_content.empty());
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 0U);

    CHECK(GetRuntime().CanUndoTextEdit(text));
    CHECK(GetRuntime().UndoTextEditAtHandle(text));
    CHECK(node->text_content == "Hello");
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 5U);
    CHECK(GetRuntime().CanRedoTextEdit(text));

    ResetInteractionLogs();
    CHECK(GetRuntime().PasteText(text));
    REQUIRE(g_clipboard_read_requests.size() == 1U);
    CHECK(g_clipboard_read_requests[0].handle == text);

    CHECK(GetRuntime().RedoTextEditAtHandle(text));
    CHECK(node->text_content.empty());
}

TEST_CASE("v2 ui editable undo stack limit discards the oldest history", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::UINode;

    ui_reset();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    for (std::size_t index = 0; index < UINode::kMaxUndo + 5U; index += 1U) {
        ui_set_interaction_time(static_cast<std::uint64_t>(1000U + (index * 400U)));
        ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("a"), 1U, 0U);
    }

    CHECK(node->undo_stack.size() == UINode::kMaxUndo);
    CHECK(node->text_content.size() == UINode::kMaxUndo + 5U);

    for (std::size_t index = 0; index < UINode::kMaxUndo; index += 1U) {
        ui_set_interaction_time(static_cast<std::uint64_t>(50000U + index));
        ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("z"), 1U, UI_KEY_MOD_CTRL);
    }

    CHECK(node->text_content.size() == 5U);
}

TEST_CASE("v2 ui programmatic text updates clear editable undo history", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    GetRuntime().SetFocus(text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    ui_set_interaction_time(100U);
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("A"), 1U, 0U);
    REQUIRE_FALSE(node->undo_stack.empty());

    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Reset"), 5U);
    CHECK(node->text_content == "Reset");
    CHECK(node->undo_stack.empty());
    CHECK(node->redo_stack.empty());
    CHECK_FALSE(node->undo_group_open);

    node->undo_stack.push_back(effindom::v2::ui::UINode::UndoEntry{});
    node->undo_group_open = true;
    ui_set_editable(text, false);
    CHECK(node->undo_stack.empty());
    CHECK(node->redo_stack.empty());
    CHECK_FALSE(node->undo_group_open);
}
