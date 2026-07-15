#include "TestUiSupport.h"

namespace {

void TouchPointerEvent(std::uint32_t event_enum, ui_handle_t handle, float logical_x, float logical_y) {
    UiTestPointerEvent(
        event_enum,
        handle,
        logical_x,
        logical_y,
        -1,
        UI_POINTER_TYPE_TOUCH,
        0,
        0,
        0.0f,
        0.0f,
        0.0f,
        0,
        0);
}

} // namespace

TEST_CASE("v2 retained select-all paint and hit regions stay bounded while scrolling", "[v2][ui][text-edit][bounded-selection-paint]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    std::string content{};
    for (std::size_t line = 0U; line < 1000U; line += 1U) {
        if (line != 0U) {
            content += '\n';
        }
        content += "select-all retained line " + std::to_string(line);
    }

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    const std::uint32_t text_length = static_cast<std::uint32_t>(content.size());
    GetRuntime().ClearTextGeometryProfile();
    ui_set_text_selection_range(text, 0U, text_length);
    ui_commit_frame();

    const auto first_profile = GetRuntime().text_geometry_profile();
    const auto* first_text = GetRuntime().Resolve(text);
    REQUIRE(first_text != nullptr);
    CHECK(first_text->selection_start == 0U);
    CHECK(first_text->selection_end == text_length);
    CHECK(first_profile.bounded_calls == 1U);
    CHECK(first_profile.unrestricted_calls == 0U);
    CHECK(first_profile.lines_visited <= 8U);
    CHECK(first_profile.rectangles_emitted <= 8U);
    CHECK(GetRuntime().Selection().state().hit_rects.size() == first_profile.rectangles_emitted);

    ui_set_scroll_offset(scroll, 0.0f, 6000.0f);
    GetRuntime().ClearTextGeometryProfile();
    ui_commit_frame();

    const auto scrolled_profile = GetRuntime().text_geometry_profile();
    const auto* scrolled_text = GetRuntime().Resolve(text);
    REQUIRE(scrolled_text != nullptr);
    CHECK(scrolled_text->selection_start == 0U);
    CHECK(scrolled_text->selection_end == text_length);
    CHECK(scrolled_profile.bounded_calls == 1U);
    CHECK(scrolled_profile.unrestricted_calls == 0U);
    CHECK(scrolled_profile.lines_visited <= 8U);
    CHECK(scrolled_profile.rectangles_emitted <= 8U);
    CHECK(GetRuntime().Selection().state().hit_rects.size() == scrolled_profile.rectangles_emitted);
}

TEST_CASE("v2 retained find highlight paint stays bounded while logical matches survive scrolling", "[v2][ui][text-edit][bounded-find-paint]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    std::string content{};
    for (std::size_t line = 0U; line < 1000U; line += 1U) {
        if (line != 0U) {
            content += '\n';
        }
        content += "find-highlight retained line " + std::to_string(line);
    }

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    const std::uint32_t text_length = static_cast<std::uint32_t>(content.size());
    REQUIRE(ui_push_text_find_highlight(text, 0U, text_length, 0x80FFD000U));
    GetRuntime().ClearTextGeometryProfile();
    ui_commit_frame();

    const auto first_profile = GetRuntime().text_geometry_profile();
    REQUIRE(GetRuntime().text_find_highlights_.size() == 1U);
    CHECK(GetRuntime().text_find_highlights_.front().start == 0U);
    CHECK(GetRuntime().text_find_highlights_.front().end == text_length);
    CHECK(first_profile.bounded_calls == 1U);
    CHECK(first_profile.lines_visited <= 8U);
    CHECK(first_profile.find_rectangles_emitted <= 8U);
    CHECK(first_profile.find_rectangles_emitted == first_profile.rectangles_emitted);

    ui_set_scroll_offset(scroll, 0.0f, 6000.0f);
    GetRuntime().ClearTextGeometryProfile();
    ui_commit_frame();

    const auto scrolled_profile = GetRuntime().text_geometry_profile();
    REQUIRE(GetRuntime().text_find_highlights_.size() == 1U);
    CHECK(GetRuntime().text_find_highlights_.front().start == 0U);
    CHECK(GetRuntime().text_find_highlights_.front().end == text_length);
    CHECK(scrolled_profile.bounded_calls == 1U);
    CHECK(scrolled_profile.lines_visited <= 8U);
    CHECK(scrolled_profile.find_rectangles_emitted <= 8U);
    CHECK(scrolled_profile.find_rectangles_emitted == scrolled_profile.rectangles_emitted);
}

TEST_CASE("v2 public text range rectangle ABI stays unrestricted across scrolling", "[v2][ui][text-edit][unrestricted-range-geometry]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    std::string content{};
    for (std::size_t line = 0U; line < 1000U; line += 1U) {
        if (line != 0U) {
            content += '\n';
        }
        content += "public geometry line " + std::to_string(line);
    }

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    const std::uint32_t text_length = static_cast<std::uint32_t>(content.size());
    GetRuntime().ClearTextGeometryProfile();
    const std::uint32_t first_count = ui_get_text_range_rect_count(text, 0U, text_length);
    REQUIRE(first_count > 1000U);
    CHECK(GetRuntime().text_geometry_profile().unrestricted_calls == 1U);
    CHECK(GetRuntime().text_geometry_profile().bounded_calls == 0U);
    CHECK(GetRuntime().text_geometry_profile().lines_visited == first_count);

    std::vector<float> rect_words(static_cast<std::size_t>(first_count) * 4U);
    REQUIRE(ui_copy_text_range_rects(text, 0U, text_length, rect_words.data(), first_count) == first_count);

    ui_set_scroll_offset(scroll, 0.0f, 6000.0f);
    ui_commit_frame();
    GetRuntime().ClearTextGeometryProfile();
    const std::uint32_t scrolled_count = ui_get_text_range_rect_count(text, 0U, text_length);
    CHECK(scrolled_count == first_count);
    CHECK(GetRuntime().text_geometry_profile().unrestricted_calls == 1U);
    CHECK(GetRuntime().text_geometry_profile().bounded_calls == 0U);
    CHECK(GetRuntime().text_geometry_profile().lines_visited == first_count);
}

TEST_CASE("v2 ui auto-scroll tick advances a dragged selection inside scroll views", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample =
        "The quick brown fox jumps over the lazy dog while editors drag selections near the viewport edge.";

    ui_set_root(root);
    ui_resize_window(140.0f, 80.0f);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* text_node = effindom::v2::ui::GetRuntime().Resolve(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->layout_height > scroll_node->layout_height);
    scroll_node->auto_scroll_speed = 12.0f;
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, 0U);
    REQUIRE(start_line == 0);
    const std::uint32_t visible_edge_index = GetRuntime().GetStringIndexFromPoint(
        *text_node,
        scroll_node->layout_width * 0.5f,
        (scroll_node->abs_y + scroll_node->layout_height - text_node->abs_y) - 1.0f);

    ui_set_interaction_time(500U);
    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text,
        text_node->abs_x + start_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        UI_INVALID_HANDLE,
        scroll_node->abs_x + (scroll_node->layout_width * 0.5f),
        scroll_node->abs_y + scroll_node->layout_height + 24.0f);
    REQUIRE(ui_selection_autoscroll(
        scroll_node->abs_x + (scroll_node->layout_width * 0.5f),
        scroll_node->abs_y + scroll_node->layout_height + 24.0f,
        30.0f) == scroll);
    REQUIRE((*GetRuntime().scroll_coordinator_).HasAutoScroll());
    REQUIRE(GetRuntime().ResolveMutable(text) != nullptr);
    CHECK(GetRuntime().ResolveMutable(text)->selection_end == visible_edge_index);
    CHECK(GetRuntime().ResolveMutable(text)->selection_end < std::strlen(kSample));

    ui_commit_frame();
    ui_commit_frame();
    ui_commit_frame();
    REQUIRE(GetRuntime().ResolveMutable(text) != nullptr);
    CHECK(GetRuntime().ResolveMutable(text)->selection_end > visible_edge_index);
    CHECK(GetRuntime().ResolveMutable(text)->selection_end <= std::strlen(kSample));

    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y > 0.0f);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y <= Approx(
        GetRuntime().Resolve(scroll)->scroll_content_height - GetRuntime().Resolve(scroll)->layout_height));
}

TEST_CASE("v2 ui selection auto-scroll stays inactive until the pointer drag crosses slop threshold", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample =
        "The quick brown fox jumps over the lazy dog while editors drag selections near the viewport edge.";

    ui_set_root(root);
    ui_resize_window(140.0f, 80.0f);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* text_node = effindom::v2::ui::GetRuntime().Resolve(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->layout_height > scroll_node->layout_height);

    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, 0U);
    REQUIRE(start_line == 0);
    const float edge_x = scroll_node->abs_x + (scroll_node->layout_width * 0.5f);
    const float edge_y = scroll_node->abs_y + scroll_node->layout_height + 24.0f;

    ui_set_interaction_time(500U);
    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text,
        text_node->abs_x + start_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));

    CHECK(ui_selection_autoscroll(edge_x, edge_y, 30.0f) == UI_INVALID_HANDLE);
    CHECK_FALSE((*GetRuntime().scroll_coordinator_).HasAutoScroll());
    REQUIRE(GetRuntime().ResolveMutable(text) != nullptr);
    CHECK(GetRuntime().ResolveMutable(text)->selection_start == 0U);
    CHECK(GetRuntime().ResolveMutable(text)->selection_end == 0U);

    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        text,
        text_node->abs_x + start_x + 2.0f,
        text_node->abs_y + (text_node->line_height * 0.5f));

    CHECK(ui_selection_autoscroll(edge_x, edge_y, 30.0f) == UI_INVALID_HANDLE);
    CHECK_FALSE((*GetRuntime().scroll_coordinator_).HasAutoScroll());
}

TEST_CASE("v2 ui selection and scroll drag autoscroll share the same edge-factor logic", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample =
        "The quick brown fox jumps over the lazy dog while editors drag selections near the viewport edge.";

    ui_set_root(root);
    ui_resize_window(140.0f, 80.0f);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* text_node = GetRuntime().Resolve(text);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->layout_height > scroll_node->layout_height);
    scroll_node->edge_hot_zone = 30.0f;

    const float pointer_x = scroll_node->abs_x + (scroll_node->layout_width * 0.5f);
    const float pointer_y = scroll_node->abs_y + scroll_node->layout_height - 3.0f;

    ui_set_interaction_time(500U);
    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text,
        scroll_node->abs_x + 48.0f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        text,
        pointer_x,
        pointer_y);

    REQUIRE(ui_selection_autoscroll(pointer_x, pointer_y, 30.0f) == scroll);
    REQUIRE((*GetRuntime().scroll_coordinator_).HasAutoScroll());
    const float selection_factor_x = (*GetRuntime().scroll_coordinator_).AutoScrollFactorX();
    const float selection_factor_y = (*GetRuntime().scroll_coordinator_).AutoScrollFactorY();

    GetRuntime().UpdateAutoScrollState(text, pointer_x, pointer_y);
    CHECK((*GetRuntime().scroll_coordinator_).HasAutoScroll());
    CHECK((*GetRuntime().scroll_coordinator_).ActiveAutoScrollHandle() == scroll);
    CHECK((*GetRuntime().scroll_coordinator_).AutoScrollFactorX() == Approx(selection_factor_x));
    CHECK((*GetRuntime().scroll_coordinator_).AutoScrollFactorY() == Approx(selection_factor_y));
}

TEST_CASE("v2 ui collapsed text range returns caret geometry for selection handle crossover", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "selection crossover";
    ui_set_root(root);
    ui_resize_window(320.0f, 120.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);

    ui_commit_frame();

    const auto* text_node = GetRuntime().Resolve(text);
    REQUIRE(text_node != nullptr);
    constexpr std::uint32_t kCaretIndex = 9U;
    const auto [caret_x, caret_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, kCaretIndex, false);
    REQUIRE(caret_line >= 0);

    REQUIRE(ui_get_text_range_rect_count(text, kCaretIndex, kCaretIndex) == 1U);
    float rect_words[4] = {};
    REQUIRE(ui_copy_text_range_rects(text, kCaretIndex, kCaretIndex, rect_words, 1U) == 1U);

    CHECK(rect_words[0] == Approx(text_node->abs_x + caret_x));
    CHECK(rect_words[1] == Approx(text_node->abs_y + GetRuntime().GetLineTopForIndex(*text_node, static_cast<std::size_t>(caret_line))));
    CHECK(rect_words[2] == Approx(0.5f));
    CHECK(rect_words[3] == Approx(GetRuntime().GetLineHeightForIndex(*text_node, static_cast<std::size_t>(caret_line))));
}

TEST_CASE("v2 ui cross-selection auto-scroll advances the endpoint as new text scrolls under the pointer", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(area != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    constexpr const char* kFirst = "First paragraph wraps enough to create a tall selection block for scrolling.";
    constexpr const char* kSecond = "Second paragraph should become selected once auto-scroll brings it under the pointer.";

    ui_set_root(root);
    ui_resize_window(160.0f, 100.0f);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(area, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_selection_area(area, true);
    for (const std::uint64_t handle : {first, second}) {
        ui_set_width(handle, 140.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(handle, 1U, 20.0f);
        ui_set_selectable(handle, true, 0x40007AFFU);
        ui_set_interactive(handle, true);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>(kFirst), static_cast<std::uint32_t>(std::strlen(kFirst)));
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>(kSecond), static_cast<std::uint32_t>(std::strlen(kSecond)));
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, area);
    ui_node_add_child(area, first);
    ui_node_add_child(area, second);

    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* second_node = GetRuntime().Resolve(second);
    auto* scroll_node = GetRuntime().ResolveMutable(scroll);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(second_node->abs_y >= scroll_node->abs_y + scroll_node->layout_height);
    scroll_node->auto_scroll_speed = 20.0f;

    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*first_node, 0U);
    REQUIRE(start_line == 0);
    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        first,
        first_node->abs_x + start_x + 0.5f,
        first_node->abs_y + (first_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        first,
        scroll_node->abs_x + (scroll_node->layout_width * 0.5f),
        scroll_node->abs_y + scroll_node->layout_height - 2.0f);
    REQUIRE(ui_selection_autoscroll(
        scroll_node->abs_x + (scroll_node->layout_width * 0.5f),
        scroll_node->abs_y + scroll_node->layout_height - 2.0f,
        30.0f) == scroll);
    REQUIRE((*GetRuntime().scroll_coordinator_).HasAutoScroll());

    for (int tick = 0; tick < 8; tick += 1) {
        ui_commit_frame();
    }

    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y > 0.0f);
    CHECK(GetRuntime().Selection().state().end_node_handle == second);
    CHECK(GetRuntime().BuildCrossSelectionText().find('\n') != std::string::npos);
}

TEST_CASE("v2 ui cross-selection drag can escape a nested scroll view to visible sibling text", "[v2][ui][cross-selection][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outside_text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(inner != UI_INVALID_HANDLE);
    REQUIRE(inner_text != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(outside_text != UI_INVALID_HANDLE);

    constexpr const char* kInner = "Inner selectable text";
    constexpr const char* kOutside = "Outside selectable text";

    ui_set_root(root);
    ui_resize_window(240.0f, 140.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(spacer, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 8.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t handle : {inner_text, outside_text}) {
        ui_set_width(handle, 180.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(handle, 1U, 20.0f);
        ui_set_selectable(handle, true, 0x40007AFFU);
        ui_set_interactive(handle, true);
    }
    ui_set_text(inner_text, reinterpret_cast<const std::uint8_t*>(kInner), static_cast<std::uint32_t>(std::strlen(kInner)));
    ui_set_text(
        outside_text,
        reinterpret_cast<const std::uint8_t*>(kOutside),
        static_cast<std::uint32_t>(std::strlen(kOutside)));
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, inner);
    ui_node_add_child(inner, inner_text);
    ui_node_add_child(root, spacer);
    ui_node_add_child(root, outside_text);
    ui_commit_frame();

    const auto* inner_node = GetRuntime().Resolve(inner_text);
    const auto* outside_node = GetRuntime().Resolve(outside_text);
    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(inner_node != nullptr);
    REQUIRE(outside_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(outside_node->abs_y > scroll_node->abs_y + scroll_node->layout_height);

    const auto [inner_x, inner_line] = GetRuntime().GetLocalPositionFromIndex(*inner_node, 0U);
    const auto [outside_x, outside_line] = GetRuntime().GetLocalPositionFromIndex(*outside_node, 7U);
    REQUIRE(inner_line == 0);
    REQUIRE(outside_line == 0);
    const float inner_y = inner_node->abs_y + (inner_node->line_height * 0.5f);
    const float outside_y = outside_node->abs_y + (outside_node->line_height * 0.5f);

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, inner_text, inner_node->abs_x + inner_x + 0.5f, inner_y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, outside_text, outside_node->abs_x + outside_x + 0.5f, outside_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, outside_text, outside_node->abs_x + outside_x + 0.5f, outside_y);

    CHECK(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().start_node_handle == inner_text);
    CHECK(GetRuntime().Selection().state().end_node_handle == outside_text);
    CHECK(GetRuntime().BuildCrossSelectionText().find("Outside") != std::string::npos);
}

TEST_CASE("v2 ui clip-to-bounds emits clip ops and scroll views clip by default", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(root, true);
    ui_set_width(child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, child);
    ui_commit_frame();

    const auto root_scene = ReadScene(ReadCommandBuffer());
    REQUIRE(root_scene.size() == 4U);
    CHECK(root_scene[0].opcode == OP_DRAW_NODE);
    CHECK(root_scene[0].handle == root);
    CHECK(root_scene[1].opcode == OP_PUSH_CLIP);
    CHECK(root_scene[1].handle == root);
    CHECK(root_scene[2].opcode == OP_DRAW_NODE);
    CHECK(root_scene[2].handle == child);
    CHECK(root_scene[3].opcode == OP_POP);
    CHECK(root_scene[3].handle == UI_INVALID_HANDLE);

    ui_reset();
    const std::uint64_t clip_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(clip_root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    ui_set_root(clip_root);
    ui_set_width(clip_root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(clip_root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(clip_root, scroll);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    const auto scene = ReadScene(ReadCommandBuffer());
    REQUIRE(scene.size() >= 5U);
    CHECK(scene[0].opcode == OP_DRAW_NODE);
    CHECK(scene[0].handle == clip_root);
    CHECK(scene[1].opcode == OP_DRAW_NODE);
    CHECK(scene[1].handle == scroll);
    CHECK(scene[2].opcode == OP_PUSH_CLIP);
    CHECK(scene[2].handle == scroll);
    CHECK(scene[3].opcode == OP_DRAW_NODE);
    CHECK(scene[3].handle == content);
    CHECK(scene[4].opcode == OP_POP);
    CHECK(scene[4].handle == UI_INVALID_HANDLE);

    ui_reset();
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t grid_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(grid_child != UI_INVALID_HANDLE);
    ui_set_root(grid);
    ui_set_width(grid, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(grid, true);
    ui_set_width(grid_child, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid_child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(grid, grid_child);
    ui_commit_frame();

    const auto grid_scene = ReadScene(ReadCommandBuffer());
    REQUIRE(grid_scene.size() == 4U);
    CHECK(grid_scene[0].opcode == OP_DRAW_NODE);
    CHECK(grid_scene[0].handle == grid);
    CHECK(grid_scene[1].opcode == OP_PUSH_CLIP);
    CHECK(grid_scene[1].handle == grid);
    CHECK(grid_scene[2].opcode == OP_DRAW_NODE);
    CHECK(grid_scene[2].handle == grid_child);
    CHECK(grid_scene[3].opcode == OP_POP);
    CHECK(grid_scene[3].handle == UI_INVALID_HANDLE);

    ui_reset();
    const std::uint64_t portal_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(portal_root != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(portal_child != UI_INVALID_HANDLE);
    ui_set_root(portal_root);
    ui_set_width(portal_root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal_root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(portal, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_set_clip_to_bounds(portal, true);
    ui_set_width(portal_child, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal_child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(portal_root, portal);
    ui_node_add_child(portal, portal_child);
    ui_commit_frame();

    const auto portal_scene = ReadScene(ReadCommandBuffer());
    REQUIRE(portal_scene.size() == 5U);
    CHECK(portal_scene[0].opcode == OP_DRAW_NODE);
    CHECK(portal_scene[0].handle == portal_root);
    CHECK(portal_scene[1].opcode == OP_DRAW_NODE);
    CHECK(portal_scene[1].handle == portal);
    CHECK(portal_scene[2].opcode == OP_PUSH_CLIP);
    CHECK(portal_scene[2].handle == portal);
    CHECK(portal_scene[3].opcode == OP_DRAW_NODE);
    CHECK(portal_scene[3].handle == portal_child);
    CHECK(portal_scene[4].opcode == OP_POP);
    CHECK(portal_scene[4].handle == UI_INVALID_HANDLE);

    ui_reset();
    const std::uint64_t hit_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t hit_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(hit_root != UI_INVALID_HANDLE);
    REQUIRE(hit_child != UI_INVALID_HANDLE);
    ui_set_root(hit_root);
    ui_set_width(hit_root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(hit_root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(hit_root, true);
    ui_set_width(hit_child, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(hit_child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(hit_child, true);
    ui_node_add_child(hit_root, hit_child);
    ui_commit_frame();

    const auto visual_bounds = ReadBounds(ReadCommandBuffer());
    const auto hit_bounds = ReadHitBounds(ReadCommandBuffer());
    REQUIRE(visual_bounds.find(hit_child) != visual_bounds.end());
    REQUIRE(hit_bounds.find(hit_child) != hit_bounds.end());
    CHECK(visual_bounds.at(hit_child).x == Approx(0.0f));
    CHECK(visual_bounds.at(hit_child).y == Approx(0.0f));
    CHECK(visual_bounds.at(hit_child).width == Approx(200.0f));
    CHECK(hit_bounds.at(hit_child).x == Approx(0.0f));
    CHECK(hit_bounds.at(hit_child).y == Approx(0.0f));
    CHECK(hit_bounds.at(hit_child).width == Approx(100.0f));
    CHECK(hit_bounds.at(hit_child).height == Approx(20.0f));

    ui_reset();
    const std::uint64_t padded_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t padded_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(padded_root != UI_INVALID_HANDLE);
    REQUIRE(padded_child != UI_INVALID_HANDLE);
    ui_set_root(padded_root);
    ui_set_width(padded_root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(padded_root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(padded_root, true);
    ui_set_padding(padded_root, 12.0f, 8.0f, 16.0f, 10.0f);
    ui_set_width(padded_child, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(padded_child, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(padded_child, true);
    ui_node_add_child(padded_root, padded_child);
    ui_commit_frame();

    const auto padded_words = ReadCommandBuffer();
    const auto padded_visual_bounds = ReadBounds(padded_words);
    const auto padded_hit_bounds = ReadHitBounds(padded_words);
    const auto padded_clip_bounds = ReadClipBounds(padded_words);
    const auto padded_clip_modes = ReadClipModes(padded_words);
    REQUIRE(padded_visual_bounds.find(padded_child) != padded_visual_bounds.end());
    REQUIRE(padded_hit_bounds.find(padded_child) != padded_hit_bounds.end());
    REQUIRE(padded_clip_bounds.find(padded_root) != padded_clip_bounds.end());
    REQUIRE(padded_clip_modes.find(padded_root) != padded_clip_modes.end());
    CHECK(padded_visual_bounds.at(padded_child).x == Approx(12.0f));
    CHECK(padded_visual_bounds.at(padded_child).y == Approx(8.0f));
    CHECK(padded_visual_bounds.at(padded_child).width == Approx(200.0f));
    CHECK(padded_hit_bounds.at(padded_child).x == Approx(12.0f));
    CHECK(padded_hit_bounds.at(padded_child).y == Approx(8.0f));
    CHECK(padded_hit_bounds.at(padded_child).width == Approx(72.0f));
    CHECK(padded_hit_bounds.at(padded_child).height == Approx(40.0f));
    CHECK(padded_clip_bounds.at(padded_root).x == Approx(12.0f));
    CHECK(padded_clip_bounds.at(padded_root).y == Approx(8.0f));
    CHECK(padded_clip_bounds.at(padded_root).width == Approx(72.0f));
    CHECK(padded_clip_bounds.at(padded_root).height == Approx(62.0f));
    CHECK(padded_clip_modes.at(padded_root) == ED_CLIP_MODE_RASTER_SAFE_VISUAL);

    ui_reset();
    const std::uint64_t bordered_padding_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t bordered_padding_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(bordered_padding_root != UI_INVALID_HANDLE);
    REQUIRE(bordered_padding_child != UI_INVALID_HANDLE);
    ui_set_root(bordered_padding_root);
    ui_set_width(bordered_padding_root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(bordered_padding_root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(bordered_padding_root, true);
    ui_set_box_style(
        bordered_padding_root,
        0x00000000U,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        4.0f,
        0xffffffffU,
        ED_BORDER_SOLID,
        0.0f,
        0.0f);
    ui_set_padding(bordered_padding_root, 1.0f, 1.0f, 1.0f, 1.0f);
    ui_set_width(bordered_padding_child, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(bordered_padding_child, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(bordered_padding_child, true);
    ui_node_add_child(bordered_padding_root, bordered_padding_child);
    ui_commit_frame();

    const auto bordered_root_words = ReadCommandBuffer();
    const auto bordered_root_visual_bounds = ReadBounds(bordered_root_words);
    const auto bordered_root_hit_bounds = ReadHitBounds(bordered_root_words);
    const auto bordered_root_clip_bounds = ReadClipBounds(bordered_root_words);
    const auto bordered_root_clip_modes = ReadClipModes(bordered_root_words);
    REQUIRE(bordered_root_visual_bounds.find(bordered_padding_child) != bordered_root_visual_bounds.end());
    REQUIRE(bordered_root_hit_bounds.find(bordered_padding_child) != bordered_root_hit_bounds.end());
    REQUIRE(bordered_root_clip_bounds.find(bordered_padding_root) != bordered_root_clip_bounds.end());
    REQUIRE(bordered_root_clip_modes.find(bordered_padding_root) != bordered_root_clip_modes.end());
    CHECK(bordered_root_visual_bounds.at(bordered_padding_child).x == Approx(5.0f));
    CHECK(bordered_root_visual_bounds.at(bordered_padding_child).y == Approx(5.0f));
    CHECK(bordered_root_hit_bounds.at(bordered_padding_child).x == Approx(5.0f));
    CHECK(bordered_root_hit_bounds.at(bordered_padding_child).y == Approx(5.0f));
    CHECK(bordered_root_hit_bounds.at(bordered_padding_child).width == Approx(90.0f));
    CHECK(bordered_root_hit_bounds.at(bordered_padding_child).height == Approx(40.0f));
    CHECK(bordered_root_clip_bounds.at(bordered_padding_root).x == Approx(5.0f));
    CHECK(bordered_root_clip_bounds.at(bordered_padding_root).y == Approx(5.0f));
    CHECK(bordered_root_clip_bounds.at(bordered_padding_root).width == Approx(90.0f));
    CHECK(bordered_root_clip_bounds.at(bordered_padding_root).height == Approx(70.0f));
    CHECK(bordered_root_clip_modes.at(bordered_padding_root) == ED_CLIP_MODE_RASTER_SAFE_VISUAL);

    ui_reset();
    const std::uint64_t bordered_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t bordered_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(bordered_scroll != UI_INVALID_HANDLE);
    REQUIRE(bordered_child != UI_INVALID_HANDLE);
    ui_set_root(bordered_scroll);
    ui_set_width(bordered_scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(bordered_scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_padding(bordered_scroll, 12.0f, 8.0f, 16.0f, 10.0f);
    ui_set_box_style(
        bordered_scroll,
        0x00000000U,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        4.0f,
        0xffffffffU,
        ED_BORDER_SOLID,
        0.0f,
        0.0f);
    ui_set_width(bordered_child, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(bordered_child, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(bordered_child, true);
    ui_node_add_child(bordered_scroll, bordered_child);
    ui_commit_frame();

    const auto bordered_words = ReadCommandBuffer();
    const auto bordered_visual_bounds = ReadBounds(bordered_words);
    const auto bordered_hit_bounds = ReadHitBounds(bordered_words);
    const auto bordered_clip_bounds = ReadClipBounds(bordered_words);
    const auto bordered_clip_modes = ReadClipModes(bordered_words);
    REQUIRE(bordered_visual_bounds.find(bordered_child) != bordered_visual_bounds.end());
    REQUIRE(bordered_hit_bounds.find(bordered_child) != bordered_hit_bounds.end());
    REQUIRE(bordered_clip_bounds.find(bordered_scroll) != bordered_clip_bounds.end());
    REQUIRE(bordered_clip_modes.find(bordered_scroll) != bordered_clip_modes.end());
    CHECK(bordered_visual_bounds.at(bordered_child).x == Approx(16.0f));
    CHECK(bordered_visual_bounds.at(bordered_child).y == Approx(12.0f));
    CHECK(bordered_hit_bounds.at(bordered_child).width == Approx(64.0f));
    CHECK(bordered_hit_bounds.at(bordered_child).height == Approx(40.0f));
    CHECK(bordered_clip_bounds.at(bordered_scroll).x == Approx(16.0f));
    CHECK(bordered_clip_bounds.at(bordered_scroll).y == Approx(12.0f));
    CHECK(bordered_clip_bounds.at(bordered_scroll).width == Approx(64.0f));
    CHECK(bordered_clip_bounds.at(bordered_scroll).height == Approx(54.0f));
    CHECK(bordered_clip_modes.at(bordered_scroll) == ED_CLIP_MODE_STRICT_CONTENT);
}

TEST_CASE("v2 ui scroll view momentum decays across commit frames", "[v2][ui][unit]") {
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

    ui_commit_frame();

    auto* scroll_node = const_cast<effindom::v2::ui::UINode*>(GetRuntime().Resolve(scroll));
    REQUIRE(scroll_node != nullptr);
    scroll_node->scroll_offset_y = 20.0f;
    scroll_node->scroll_velocity_y = 600.0f;
    scroll_node->friction = 0.5f;

    ui_commit_frame();
    CHECK(scroll_node->scroll_offset_y == Approx(30.0f));
    CHECK(scroll_node->scroll_velocity_y == Approx(300.0f));

    ui_commit_frame();
    CHECK(scroll_node->scroll_offset_y == Approx(35.0f));
    CHECK(scroll_node->scroll_velocity_y == Approx(150.0f));

    scroll_node->scroll_velocity_y = 0.05f;
    ui_commit_frame();
    CHECK(scroll_node->scroll_velocity_y == Approx(0.0f));
}

TEST_CASE("v2 ui scroll view momentum is independent of animation frame cadence", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    const auto run_momentum = [](bool split_frame) {
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

        auto& runtime = GetRuntime();
        runtime.CommitFrame(0.0);

        auto* scroll_node = const_cast<effindom::v2::ui::UINode*>(runtime.Resolve(scroll));
        REQUIRE(scroll_node != nullptr);
        scroll_node->scroll_offset_y = 20.0f;
        scroll_node->scroll_velocity_y = 600.0f;
        scroll_node->friction = 0.5f;

        if (split_frame) {
            runtime.CommitFrame(1000.0 / 120.0);
            runtime.CommitFrame(1000.0 / 60.0);
        } else {
            runtime.CommitFrame(1000.0 / 60.0);
        }

        return std::pair<float, float>{scroll_node->scroll_offset_y, scroll_node->scroll_velocity_y};
    };

    const auto [single_offset, single_velocity] = run_momentum(false);
    const auto [split_offset, split_velocity] = run_momentum(true);

    CHECK(split_offset == Approx(single_offset));
    CHECK(split_velocity == Approx(single_velocity));
}

TEST_CASE("v2 ui scroll helpers cover ancestor lookup, edge detection, and drag scrolling", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PackHandle;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t plain = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(plain != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(plain, 10.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(plain, 10.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(root, plain);
    ui_commit_frame();

    auto* runtime = &GetRuntime();
    auto* scroll_node = const_cast<effindom::v2::ui::UINode*>(runtime->Resolve(scroll));
    auto* plain_node = const_cast<effindom::v2::ui::UINode*>(runtime->Resolve(plain));
    REQUIRE(scroll_node != nullptr);
    REQUIRE(plain_node != nullptr);

    runtime->UpdateAutoScrollState(PackHandle(0U, 1U), 0.0f, 0.0f);
    CHECK_FALSE((*runtime->scroll_coordinator_).HasAutoScroll());
    CHECK(runtime->FindScrollableAncestorContainingPoint(PackHandle(0U, 1U), 0.0f, 0.0f) == UI_INVALID_HANDLE);

    scroll_node->children.push_back(PackHandle(0U, 1U));
    (*runtime->scroll_coordinator_).UpdateMetrics(scroll, *scroll_node);
    scroll_node->children.pop_back();
    (*runtime->scroll_coordinator_).UpdateMetrics(plain, *plain_node);

    scroll_node->scroll_enabled_x = false;
    scroll_node->scroll_enabled_y = false;
    (*runtime->scroll_coordinator_).ApplyOffset(scroll, *scroll_node, 40.0f, 50.0f, false);
    CHECK(scroll_node->scroll_offset_x == Approx(0.0f));
    CHECK(scroll_node->scroll_offset_y == Approx(0.0f));
    scroll_node->scroll_enabled_x = true;
    scroll_node->scroll_enabled_y = true;

    scroll_node->edge_hot_zone = 10.0f;
    runtime->UpdateAutoScrollState(content, scroll_node->abs_x + 1.0f, scroll_node->abs_y + 50.0f);
    CHECK((*runtime->scroll_coordinator_).HasAutoScroll());
    CHECK((*runtime->scroll_coordinator_).AutoScrollFactorX() == Approx(-0.9f));
    CHECK((*runtime->scroll_coordinator_).AutoScrollFactorY() == Approx(0.0f));

    runtime->UpdateAutoScrollState(content, scroll_node->abs_x + scroll_node->layout_width - 1.0f, scroll_node->abs_y + 50.0f);
    CHECK((*runtime->scroll_coordinator_).AutoScrollFactorX() == Approx(0.9f));

    runtime->UpdateAutoScrollState(content, scroll_node->abs_x + 50.0f, scroll_node->abs_y + 1.0f);
    CHECK((*runtime->scroll_coordinator_).AutoScrollFactorY() == Approx(-0.9f));

    runtime->UpdateAutoScrollState(content, scroll_node->abs_x - 20.0f, scroll_node->abs_y + 50.0f);
    CHECK((*runtime->scroll_coordinator_).AutoScrollFactorX() == Approx(-3.0f));

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, content, 30.0f, 60.0f);
    CHECK((*runtime->scroll_coordinator_).ActiveDragHandle() == scroll);
    ui_commit_frame();
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, UI_INVALID_HANDLE, 30.0f, 20.0f);
    CHECK((*runtime->scroll_coordinator_).ActiveDragWasMoved());
    CHECK(runtime->Resolve(scroll)->scroll_offset_y == Approx(40.0f));
    CHECK(runtime->Resolve(scroll)->scroll_velocity_y == Approx(2400.0f));
    UiTestPointerEvent(UI_EVENT_POINTER_UP, content, 30.0f, 20.0f);
    CHECK((*runtime->scroll_coordinator_).ActiveDragHandle() == UI_INVALID_HANDLE);

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, content, 30.0f, 40.0f);
    CHECK((*runtime->scroll_coordinator_).ActiveDragHandle() == scroll);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, content, 30.0f, 40.0f);
    CHECK(runtime->Resolve(scroll)->scroll_velocity_y == Approx(0.0f));

    (*runtime->scroll_coordinator_).SetAutoScroll(PackHandle(0U, 1U), 1.0f, 1.0f);
    ui_commit_frame();
    CHECK_FALSE((*runtime->scroll_coordinator_).HasAutoScroll());
    CHECK((*runtime->scroll_coordinator_).ActiveAutoScrollHandle() == UI_INVALID_HANDLE);
}

TEST_CASE("v2 ui routes hover click focus and html-style tab traversal", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

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
    ui_set_width(root, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, UI_FLEX_DIRECTION_ROW);

    for (const std::uint64_t child : {child_a, child_b, child_c, child_d}) {
        ui_set_width(child, 40.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(child, 20.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_interactive(child, true);
        ui_node_add_child(root, child);
    }
    ui_set_focusable(child_a, true, 0);
    ui_set_focusable(child_b, true, 0);
    ui_set_focusable(child_c, true, 2);
    ui_set_focusable(child_d, true, -1);
    ui_commit_frame();

    const auto interactive_flags = ReadInteractiveFlags(ReadCommandBuffer());
    REQUIRE(interactive_flags.find(root) != interactive_flags.end());
    REQUIRE(interactive_flags.find(child_a) != interactive_flags.end());
    REQUIRE(interactive_flags.find(child_b) != interactive_flags.end());
    REQUIRE(interactive_flags.find(child_c) != interactive_flags.end());
    CHECK_FALSE(interactive_flags.at(root));
    CHECK(interactive_flags.at(child_a));
    CHECK(interactive_flags.at(child_b));
    CHECK(interactive_flags.at(child_c));

    ResetInteractionLogs();
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, child_a, 5.0f, 5.0f);
    REQUIRE(g_pointer_events.size() == 2U);
    CHECK(g_pointer_events[0].handle == child_a);
    CHECK(g_pointer_events[0].event == UI_EVENT_POINTER_ENTER);
    CHECK(g_pointer_events[1].handle == child_a);
    CHECK(g_pointer_events[1].event == UI_EVENT_POINTER_MOVE);

    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, child_b, 10.0f, 5.0f);
    REQUIRE(g_pointer_events.size() == 5U);
    CHECK(g_pointer_events[2].handle == child_a);
    CHECK(g_pointer_events[2].event == UI_EVENT_POINTER_LEAVE);
    CHECK(g_pointer_events[3].handle == child_b);
    CHECK(g_pointer_events[3].event == UI_EVENT_POINTER_ENTER);
    CHECK(g_pointer_events[4].handle == child_b);
    CHECK(g_pointer_events[4].event == UI_EVENT_POINTER_MOVE);

    ResetInteractionLogs();
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, child_a, 5.0f, 5.0f);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == child_a);
    CHECK(g_focus_events[0].is_focused);
    REQUIRE(g_pointer_events.size() == 1U);
    CHECK(g_pointer_events[0].handle == child_a);
    CHECK(g_pointer_events[0].event == UI_EVENT_POINTER_DOWN);

    GetRuntime().SetFocus(UI_INVALID_HANDLE);
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == child_c);
    CHECK(g_focus_events[0].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == child_c);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[0].handle == child_c);
    CHECK_FALSE(g_focus_events[0].is_focused);
    CHECK(g_focus_events[1].handle == child_a);
    CHECK(g_focus_events[1].is_focused);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[1].handle == child_b);
    CHECK(g_focus_events[1].is_focused);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[1].handle == child_c);
    CHECK(g_focus_events[1].is_focused);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, UI_KEY_MOD_SHIFT);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[1].handle == child_b);
    CHECK(g_focus_events[1].is_focused);
}

TEST_CASE("v2 ui invalidates focus order and clears focus when detached or disabled", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_b = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_c = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child_a != UI_INVALID_HANDLE);
    REQUIRE(child_b != UI_INVALID_HANDLE);
    REQUIRE(child_c != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t child : {child_a, child_b, child_c}) {
        ui_set_width(child, 40.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(child, 20.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_interactive(child, true);
        ui_node_add_child(root, child);
    }
    ui_set_focusable(child_a, true, 0);
    ui_set_focusable(child_b, true, 0);
    ui_set_focusable(child_c, true, 2);

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    CHECK(GetRuntime().Focus().FocusedHandle() == child_c);

    ResetInteractionLogs();
    ui_node_remove_child(root, child_c);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == child_c);
    CHECK_FALSE(g_focus_events[0].is_focused);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == child_a);
    CHECK(g_focus_events[0].is_focused);

    ResetInteractionLogs();
    ui_set_focusable(child_a, false, 0);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == child_a);
    CHECK_FALSE(g_focus_events[0].is_focused);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == child_b);
    CHECK(g_focus_events[0].is_focused);
}

TEST_CASE("v2 ui Tab focus scrolls a focusable target into view inside a scroll view", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t target = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(target != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(content, UI_FLEX_DIRECTION_COLUMN);
    ui_set_width(spacer, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(target, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(target, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(target, true);
    ui_set_focusable(target, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, target);
    ui_commit_frame();

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);

    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == target);
    CHECK(g_focus_events[0].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == target);
    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(80.0f));

    ui_commit_frame();
    const auto hit_bounds = ReadHitBounds(ReadCommandBuffer());
    REQUIRE(hit_bounds.find(target) != hit_bounds.end());
    CHECK(hit_bounds.at(target).y == Approx(40.0f));
}

TEST_CASE("v2 ui Shift+Tab scrolls a focusable target back into view inside a scroll view", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t second = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(content, UI_FLEX_DIRECTION_COLUMN);
    ui_set_width(first, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(first, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(first, true);
    ui_set_focusable(first, true, 0);
    ui_set_width(spacer, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(second, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(second, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(second, true);
    ui_set_focusable(second, true, 1);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, first);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, second);
    ui_set_scroll_offset(scroll, 0.0f, 80.0f);
    ui_commit_frame();

    GetRuntime().SetFocus(second);
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, UI_KEY_MOD_SHIFT);

    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[0].handle == second);
    CHECK_FALSE(g_focus_events[0].is_focused);
    CHECK(g_focus_events[1].handle == first);
    CHECK(g_focus_events[1].is_focused);
    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(0.0f));

    ui_commit_frame();
    const auto hit_bounds = ReadHitBounds(ReadCommandBuffer());
    REQUIRE(hit_bounds.find(first) != hit_bounds.end());
    CHECK(hit_bounds.at(first).y == Approx(0.0f));
}

TEST_CASE("v2 ui mouse focus does not scroll a partially visible target into view", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t target = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(target != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(target, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(target, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(target, true);
    ui_set_focusable(target, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, target);
    ui_commit_frame();

    ResetInteractionLogs();
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, target, 10.0f, 10.0f);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, target, 10.0f, 10.0f);

    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == target);
    CHECK(g_focus_events[0].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == target);
    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_x == Approx(0.0f));
}

TEST_CASE("v2 ui coarse pointer focus does not scroll a focused target into view", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t target = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(target != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(content, UI_FLEX_DIRECTION_COLUMN);
    ui_set_width(spacer, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(target, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(target, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(target, true);
    ui_set_focusable(target, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, target);
    ui_commit_frame();

    ui_set_coarse_pointer_mode(true);
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);

    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == target);
    CHECK(g_focus_events[0].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == target);
    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(0.0f));
    ui_set_coarse_pointer_mode(false);
}

TEST_CASE("v2 ui scroll-only commits reuse glyph runs and shift hit bounds with a scene translate", "[v2][ui][scroll]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(content, UI_FLEX_DIRECTION_ROW);
    ui_set_width(spacer, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("scroll perf"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, text);

    ui_commit_frame();
    const auto initial_words = ReadCommandBuffer();
    CHECK(CountCommand(initial_words, CMD_SET_GLYPH_RUN) == 1U);
    const auto initial_bounds = ReadBounds(initial_words);
    const auto initial_hit_bounds = ReadHitBounds(initial_words);
    REQUIRE(initial_bounds.find(text) != initial_bounds.end());
    REQUIRE(initial_hit_bounds.find(text) != initial_hit_bounds.end());
    CHECK(initial_bounds.at(text).x == Approx(80.0f));
    CHECK(initial_hit_bounds.at(text).x == Approx(80.0f));

    ui_set_scroll_offset(scroll, 40.0f, 0.0f);
    ui_commit_frame();

    const auto scrolled_words = ReadCommandBuffer();
    CHECK(CountCommand(scrolled_words, CMD_SET_GLYPH_RUN) == 0U);
    const auto scrolled_bounds = ReadBounds(scrolled_words);
    const auto scrolled_hit_bounds = ReadHitBounds(scrolled_words);
    REQUIRE(scrolled_bounds.find(text) != scrolled_bounds.end());
    REQUIRE(scrolled_hit_bounds.find(text) != scrolled_hit_bounds.end());
    CHECK(scrolled_bounds.at(text).x == Approx(80.0f));
    CHECK(scrolled_hit_bounds.at(text).x == Approx(40.0f));

    const auto scene = ReadScene(scrolled_words);
    CHECK(std::any_of(scene.begin(), scene.end(), [scroll](const effindom::v2::ui::SceneInstruction& instruction) {
        return instruction.opcode == OP_PUSH_TRANSLATE &&
            instruction.handle == scroll &&
            std::abs(instruction.arg0 + 40.0f) < 0.001f &&
            std::abs(instruction.arg1) < 0.001f;
    }));
}

TEST_CASE("v2 ui scroll views cull fully clipped text until it enters the viewport", "[v2][ui][scroll]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(content, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(content, UI_FLEX_DIRECTION_ROW);
    ui_set_width(spacer, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("offscreen text"), 14U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, text);

    ui_commit_frame();

    const auto initial_words = ReadCommandBuffer();
    CHECK(CountCommand(initial_words, CMD_SET_GLYPH_RUN) == 0U);
    const auto initial_hit_bounds = ReadHitBounds(initial_words);
    REQUIRE(initial_hit_bounds.find(text) != initial_hit_bounds.end());
    CHECK(initial_hit_bounds.at(text).width == Approx(0.0f));
    CHECK(initial_hit_bounds.at(text).height == Approx(0.0f));
    const auto initial_scene = ReadScene(initial_words);
    CHECK(std::none_of(initial_scene.begin(), initial_scene.end(), [text](const effindom::v2::ui::SceneInstruction& instruction) {
        return instruction.opcode == OP_DRAW_NODE && instruction.handle == text;
    }));

    ui_set_scroll_offset(scroll, 140.0f, 0.0f);
    ui_commit_frame();

    const auto revealed_words = ReadCommandBuffer();
    CHECK(CountCommand(revealed_words, CMD_SET_GLYPH_RUN) == 1U);
    const auto revealed_hit_bounds = ReadHitBounds(revealed_words);
    REQUIRE(revealed_hit_bounds.find(text) != revealed_hit_bounds.end());
    CHECK(revealed_hit_bounds.at(text).width == Approx(100.0f));
    CHECK(revealed_hit_bounds.at(text).height == Approx(20.0f));
    const auto revealed_scene = ReadScene(revealed_words);
    CHECK(std::any_of(revealed_scene.begin(), revealed_scene.end(), [text](const effindom::v2::ui::SceneInstruction& instruction) {
        return instruction.opcode == OP_DRAW_NODE && instruction.handle == text;
    }));
}

TEST_CASE("v2 ui scroll views emit only the visible multiline text lines and refresh on vertical scroll", "[v2][ui][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    static constexpr char kLines[] = "A\nB\nC\nD\nE\nF\nG";
    ui_set_root(root);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 48.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 48.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(text, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kLines), sizeof(kLines) - 1U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->total_line_count == 7U);
    REQUIRE(node->visible_line_count == 7U);

    const auto initial_words = ReadCommandBuffer();
    CHECK(CountCommand(initial_words, CMD_SET_GLYPH_RUN) == 1U);
    const auto initial_runs = ReadGlyphRuns(initial_words);
    REQUIRE(initial_runs.find(text) != initial_runs.end());
    const auto& initial_run = initial_runs.at(text);
    REQUIRE_FALSE(initial_run.glyphs.empty());
    CHECK(initial_run.glyphs.size() < node->total_line_count);
    std::set<int> initial_baselines{};
    for (const auto& glyph : initial_run.glyphs) {
        initial_baselines.insert(static_cast<int>(std::lround(glyph.y * 10.0f)));
    }
    CHECK(initial_baselines.size() < node->total_line_count);
    const std::uint32_t initial_first_glyph = initial_run.glyphs.front().glyph_id;

    ui_set_scroll_offset(scroll, 0.0f, node->line_height * 2.1f);
    ui_commit_frame();

    const auto scrolled_words = ReadCommandBuffer();
    CHECK(CountCommand(scrolled_words, CMD_SET_GLYPH_RUN) == 1U);
    const auto scrolled_runs = ReadGlyphRuns(scrolled_words);
    REQUIRE(scrolled_runs.find(text) != scrolled_runs.end());
    const auto& scrolled_run = scrolled_runs.at(text);
    REQUIRE_FALSE(scrolled_run.glyphs.empty());
    CHECK(scrolled_run.glyphs.size() < node->total_line_count);
    CHECK(scrolled_run.glyphs.front().glyph_id != initial_first_glyph);
}

TEST_CASE("v2 ui non-wrapping horizontal text stays visible while scrolling through intrinsic overflow", "[v2][ui][scroll]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::Rect;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    static constexpr char kLongLine[] =
        "This is a very long line of text that should remain visible while we scroll horizontally through it without wrapping. "
        "We repeat the sentence so the fragment cache has enough horizontal slices to exercise mid-window culling cleanly.";
    ui_set_root(root);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kLongLine), sizeof(kLongLine) - 1U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* scroll_node = GetRuntime().Resolve(scroll);
    const auto* text_node = GetRuntime().Resolve(text);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node->scroll_content_width > 250.0f);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    REQUIRE(text_node->nonwrap_fragments.size() > 2U);
    const std::uint32_t fragment_generation = text_node->nonwrap_fragment_cache_generation;

    effindom::v2::ui::UiRuntime::ShapedTextRun full_line{};
    REQUIRE(GetRuntime().ShapeText(kLongLine, 1U, 16.0f, full_line));
    REQUIRE(full_line.glyphs.size() > text_node->nonwrap_fragments.size());

    const auto shape_visible_window = [&](const effindom::v2::ui::UINode& node) {
        const Rect visible_bounds = GetRuntime().ComputeVisibleBounds(node);
        const float x_offset = GetRuntime().GetAlignedLineXOffset(node, node.line_widths.front());
        const auto window = GetRuntime().ResolveNonWrappingFragmentWindow(
            node,
            0U,
            (visible_bounds.x - node.abs_x) - x_offset,
            ((visible_bounds.x + visible_bounds.width) - node.abs_x) - x_offset);
        REQUIRE(window.start < window.end);
        const auto& first_fragment = node.nonwrap_fragments[window.start];
        const auto& last_fragment = node.nonwrap_fragments[window.end - 1U];
        const std::uint32_t line_start = static_cast<std::uint32_t>(node.break_offsets.front());
        const std::uint32_t line_end = static_cast<std::uint32_t>(node.break_offsets[1]);
        REQUIRE(first_fragment.local_byte_start >= line_start);
        REQUIRE(last_fragment.local_byte_end <= line_end);
        effindom::v2::ui::UiRuntime::ShapedTextRun shaped{};
        const std::string_view line_text(
            node.text_content.data() + line_start,
            static_cast<std::size_t>(line_end - line_start));
        REQUIRE(GetRuntime().ShapeText(
            line_text.substr(
                static_cast<std::size_t>(first_fragment.local_byte_start - line_start),
                static_cast<std::size_t>(last_fragment.local_byte_end - first_fragment.local_byte_start)),
            node.font_id,
            node.font_size,
            shaped,
            node.is_obscured));
        return std::tuple{
            window,
            first_fragment.x,
            x_offset,
            shaped,
        };
    };

    const auto initial_words = ReadCommandBuffer();
    CHECK(CountCommand(initial_words, CMD_SET_GLYPH_RUN) == 1U);
    const auto initial_runs = ReadGlyphRuns(initial_words);
    REQUIRE(initial_runs.find(text) != initial_runs.end());
    const auto& initial_run = initial_runs.at(text);
    const auto [initial_window, initial_fragment_x, initial_x_offset, initial_shape] = shape_visible_window(*text_node);
    CHECK(initial_window.start == 0U);
    CHECK(initial_window.end < text_node->nonwrap_fragments.size());
    CHECK(initial_run.glyphs.size() == initial_shape.glyphs.size());
    CHECK(initial_run.glyphs.size() < full_line.glyphs.size());
    REQUIRE_FALSE(initial_run.glyphs.empty());
    CHECK(initial_run.glyphs.front().x == Approx(initial_x_offset + initial_fragment_x + initial_shape.glyphs.front().x).margin(0.01f));

    const float target_offset = (scroll_node->scroll_content_width - 140.0f) * 0.5f;
    REQUIRE(target_offset > 100.0f);

    ui_set_scroll_offset(scroll, target_offset, 0.0f);
    ui_commit_frame();

    text_node = GetRuntime().Resolve(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->nonwrap_fragment_cache_generation == fragment_generation);

    const auto scrolled_words = ReadCommandBuffer();
    CHECK(CountCommand(scrolled_words, CMD_SET_GLYPH_RUN) == 1U);
    const auto hit_bounds = ReadHitBounds(scrolled_words);
    REQUIRE(hit_bounds.find(text) != hit_bounds.end());
    CHECK(hit_bounds.at(text).width > 0.0f);
    CHECK(hit_bounds.at(text).height > 0.0f);
    const auto scrolled_runs = ReadGlyphRuns(scrolled_words);
    REQUIRE(scrolled_runs.find(text) != scrolled_runs.end());
    const auto& scrolled_run = scrolled_runs.at(text);
    const auto [scrolled_window, scrolled_fragment_x, scrolled_x_offset, scrolled_shape] = shape_visible_window(*text_node);
    CHECK((scrolled_window.start != initial_window.start || scrolled_window.end != initial_window.end));
    CHECK(scrolled_window.end <= text_node->nonwrap_fragments.size());
    CHECK(scrolled_run.glyphs.size() == scrolled_shape.glyphs.size());
    CHECK(scrolled_run.glyphs.size() < full_line.glyphs.size());
    REQUIRE_FALSE(scrolled_run.glyphs.empty());
    CHECK(scrolled_run.glyphs.front().x == Approx(scrolled_x_offset + scrolled_fragment_x + scrolled_shape.glyphs.front().x).margin(0.01f));
    const auto scene = ReadScene(scrolled_words);
    CHECK(std::any_of(scene.begin(), scene.end(), [text](const effindom::v2::ui::SceneInstruction& instruction) {
        return instruction.opcode == OP_DRAW_NODE && instruction.handle == text;
    }));

    const float stable_offset = std::min(target_offset + 1.0f, scroll_node->scroll_content_width - 140.0f);
    ui_set_scroll_offset(scroll, stable_offset, 0.0f);
    ui_commit_frame();

    text_node = GetRuntime().Resolve(text);
    REQUIRE(text_node != nullptr);
    const auto stable_words = ReadCommandBuffer();
    CHECK(CountCommand(stable_words, CMD_SET_GLYPH_RUN) == 0U);
    const auto [stable_window, stable_fragment_x, stable_x_offset, stable_shape] = shape_visible_window(*text_node);
    (void)stable_fragment_x;
    (void)stable_x_offset;
    (void)stable_shape;
    CHECK(stable_window.start == scrolled_window.start);
    CHECK(stable_window.end == scrolled_window.end);
}

TEST_CASE("v2 ui multiline non-wrap long proportional rows keep fragment windows until each row width ends", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 16.0f;

    const std::array<std::string, 10> patterns = {
        "WiimW MWiil ",
        "miiWW ilWm ",
        "Wmii lMWWi ",
        "iiWm WMlii ",
        "MWii llWmW ",
        "ilWm WMiiW ",
        "WiiM mlWii ",
        "mWii WllMi ",
        "iiWM Wmiil ",
        "Wlii mWMWi ",
    };
    for (std::size_t line_index = 0; line_index < patterns.size(); line_index += 1U) {
        if (!node.text_content.empty()) {
            node.text_content.push_back('\n');
        }
        const std::string& pattern = patterns[line_index];
        std::string line{};
        line.reserve(10000U);
        while (line.size() < 10000U) {
            line += pattern;
        }
        line.resize(10000U);
        node.text_content += line;
    }

    const auto paragraph = GetRuntime().LayoutParagraph(node, 220.0f);
    REQUIRE(paragraph.total_line_count == 10U);
    REQUIRE(node.nonwrap_fragment_cache_valid);
    REQUIRE(node.nonwrap_fragment_line_offsets.size() == 11U);

    const float viewport_width = 220.0f;
    for (std::size_t line_index = 0; line_index < 10U; line_index += 1U) {
        INFO(line_index);
        const std::uint32_t visible_start = GetRuntime().GetNonWrapVisibleLineStart(node, line_index);
        const std::uint32_t line_end = static_cast<std::uint32_t>(node.break_offsets[line_index + 1U]);
        const std::size_t fragment_start = node.nonwrap_fragment_line_offsets[line_index];
        const std::size_t fragment_end = node.nonwrap_fragment_line_offsets[line_index + 1U];
        REQUIRE(fragment_end > fragment_start);
        const auto& last_fragment = node.nonwrap_fragments[fragment_end - 1U];
        CHECK(last_fragment.local_byte_end == (line_end - visible_start));
        CHECK(last_fragment.x + last_fragment.width == Approx(node.line_widths[line_index]).margin(0.5f));

        const std::uint32_t max_offset = static_cast<std::uint32_t>(std::floor(std::max(node.line_widths[line_index] - 1.0f, 0.0f)));
        for (std::uint32_t offset = 0U; offset <= max_offset; offset += 1U) {
            const auto window = GetRuntime().ResolveNonWrappingFragmentWindow(
                node,
                line_index,
                static_cast<float>(offset),
                static_cast<float>(offset) + viewport_width);
            INFO(offset);
            CHECK(window.start < window.end);
        }
    }
}

TEST_CASE("v2 ui multiline non-wrap long proportional rows keep every overlapping row emitted while scrolling", "[v2][ui][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::array<std::string, 10> patterns = {
        "WiimW MWiil ",
        "miiWW ilWm ",
        "Wmii lMWWi ",
        "iiWm WMlii ",
        "MWii llWmW ",
        "ilWm WMiiW ",
        "WiiM mlWii ",
        "mWii WllMi ",
        "iiWM Wmiil ",
        "Wlii mWMWi ",
    };
    std::string text_value{};
    for (std::size_t line_index = 0; line_index < patterns.size(); line_index += 1U) {
        if (!text_value.empty()) {
            text_value.push_back('\n');
        }
        const std::string& pattern = patterns[line_index];
        std::string line{};
        line.reserve(10000U);
        while (line.size() < 10000U) {
            line += pattern;
        }
        line.resize(10000U);
        text_value += line;
    }

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 204.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 204.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 204.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(text_value.data()), static_cast<std::uint32_t>(text_value.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->total_line_count == 10U);
    REQUIRE(text_node->line_widths.size() == 10U);
    REQUIRE(scroll_node->scroll_content_width > 5000.0f);
    const float max_scroll_offset = std::max(scroll_node->scroll_content_width - 220.0f, 0.0f);

    std::vector<float> offsets{0.0f};
    offsets.reserve(text_node->nonwrap_fragments.size() * 3U);
    for (std::size_t line_index = 0; line_index < text_node->total_line_count; line_index += 1U) {
        const std::size_t fragment_start = text_node->nonwrap_fragment_line_offsets[line_index];
        const std::size_t fragment_end = text_node->nonwrap_fragment_line_offsets[line_index + 1U];
        for (std::size_t fragment_index = fragment_start; fragment_index < fragment_end; fragment_index += 1U) {
            const auto& fragment = text_node->nonwrap_fragments[fragment_index];
            const float boundary = fragment.x + fragment.width;
            offsets.push_back(std::max(boundary - 1.0f, 0.0f));
            offsets.push_back(boundary);
            offsets.push_back(boundary + 1.0f);
        }
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

    const auto count_rendered_rows = [&]() {
        const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
        REQUIRE(glyph_runs.find(text) != glyph_runs.end());
        const auto& run = glyph_runs.at(text);
        std::set<int> baselines{};
        for (const auto& glyph : run.glyphs) {
            baselines.insert(static_cast<int>(std::lround(glyph.y * 10.0f)));
        }
        return baselines.size();
    };

    CHECK(count_rendered_rows() == text_node->total_line_count);

    for (const float offset : offsets) {
        if (offset <= 0.0f || offset > max_scroll_offset) {
            continue;
        }
        const std::size_t expected_rows = static_cast<std::size_t>(std::count_if(
            text_node->line_widths.begin(),
            text_node->line_widths.end(),
            [offset](float width) {
                return width > offset;
            }));
        INFO(offset);
        INFO(expected_rows);
        ui_set_scroll_offset(scroll, offset, 0.0f);
        ui_commit_frame();
        CHECK(count_rendered_rows() == expected_rows);
    }
}

TEST_CASE("v2 ui multiline non-wrap near-equal 10k rows stay emitted until the shared max offset", "[v2][ui][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string base_pattern = "WiimW MWiil ";
    std::string base_line{};
    base_line.reserve(10000U);
    while (base_line.size() < 10000U) {
        base_line += base_pattern;
    }
    base_line.resize(10000U);

    std::string text_value{};
    for (std::size_t line_index = 0; line_index < 10U; line_index += 1U) {
        if (!text_value.empty()) {
            text_value.push_back('\n');
        }
        std::string line = base_line;
        line[5000U + line_index] = (line_index % 2U == 0U) ? 'W' : 'i';
        text_value += line;
    }

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 204.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 204.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 204.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(text_value.data()), static_cast<std::uint32_t>(text_value.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->total_line_count == 10U);
    REQUIRE(text_node->line_widths.size() == 10U);

    const float min_line_width = *std::min_element(text_node->line_widths.begin(), text_node->line_widths.end());
    const float max_line_width = *std::max_element(text_node->line_widths.begin(), text_node->line_widths.end());
    REQUIRE(max_line_width - min_line_width < 50.0f);
    const float max_scroll_offset = std::max(scroll_node->scroll_content_width - 220.0f, 0.0f);
    REQUIRE(max_scroll_offset > 1000.0f);
    REQUIRE(min_line_width > max_scroll_offset);

    std::vector<float> offsets{0.0f};
    for (std::size_t line_index = 0; line_index < text_node->total_line_count; line_index += 1U) {
        const std::size_t fragment_start = text_node->nonwrap_fragment_line_offsets[line_index];
        const std::size_t fragment_end = text_node->nonwrap_fragment_line_offsets[line_index + 1U];
        for (std::size_t fragment_index = fragment_start; fragment_index < fragment_end; fragment_index += 1U) {
            const auto& fragment = text_node->nonwrap_fragments[fragment_index];
            const float boundary = fragment.x + fragment.width;
            offsets.push_back(std::max(boundary - 1.0f, 0.0f));
            offsets.push_back(boundary);
            offsets.push_back(boundary + 1.0f);
        }
    }
    offsets.push_back(max_scroll_offset * 0.5f);
    offsets.push_back(max_scroll_offset - 1.0f);
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

    const auto count_rendered_rows = [&]() {
        const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
        REQUIRE(glyph_runs.find(text) != glyph_runs.end());
        const auto& run = glyph_runs.at(text);
        std::set<int> baselines{};
        for (const auto& glyph : run.glyphs) {
            baselines.insert(static_cast<int>(std::lround(glyph.y * 10.0f)));
        }
        return baselines.size();
    };

    CHECK(count_rendered_rows() == 10U);
    for (const float offset : offsets) {
        if (offset <= 0.0f || offset > max_scroll_offset) {
            continue;
        }
        INFO(offset);
        ui_set_scroll_offset(scroll, offset, 0.0f);
        ui_commit_frame();
        CHECK(count_rendered_rows() == 10U);
    }
}

TEST_CASE("v2 ui multiline non-wrap preserves the skipped short-line gap near max horizontal scroll", "[v2][ui][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string long_line(5000U, 'W');
    const std::string short_line = "short gap";
    const std::string text_value = long_line + "\n" + short_line + "\n" + long_line;

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(text_value.data()), static_cast<std::uint32_t>(text_value.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* text_node = GetRuntime().Resolve(text);
    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->total_line_count == 3U);
    REQUIRE(text_node->line_widths.size() == 3U);
    REQUIRE(text_node->line_widths[0] > text_node->line_widths[1]);
    REQUIRE(text_node->line_widths[2] > text_node->line_widths[1]);

    const auto count_rendered_baselines = [text]() {
        const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
        REQUIRE(glyph_runs.find(text) != glyph_runs.end());
        std::set<int> baselines{};
        for (const auto& glyph : glyph_runs.at(text).glyphs) {
            baselines.insert(static_cast<int>(std::lround(glyph.y * 10.0f)));
        }
        return baselines;
    };
    CHECK(count_rendered_baselines().size() == 3U);

    const float max_scroll_offset = std::max(scroll_node->scroll_content_width - 220.0f, 0.0f);
    REQUIRE(max_scroll_offset > text_node->line_widths[1]);

    ui_set_scroll_offset(scroll, std::max(max_scroll_offset - 1.0f, 0.0f), 0.0f);
    ui_commit_frame();

    const auto glyph_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(glyph_runs.find(text) != glyph_runs.end());
    const auto& run = glyph_runs.at(text);
    std::set<int> baselines{};
    for (const auto& glyph : run.glyphs) {
        baselines.insert(static_cast<int>(std::lround(glyph.y * 10.0f)));
    }
    REQUIRE(baselines.size() == 2U);

    const std::vector<int> baseline_values(baselines.begin(), baselines.end());
    const int expected_gap = static_cast<int>(std::lround(text_node->line_height * 20.0f));
    CHECK((baseline_values[1] - baseline_values[0]) == Approx(expected_gap).margin(10));

    ui_set_scroll_offset(scroll, 0.0f, 0.0f);
    ui_commit_frame();
    CHECK(count_rendered_baselines().size() == 3U);
}

TEST_CASE("v2 ui long non-wrap selection hit testing and caret placement stay correct while horizontally scrolled", "[v2][ui][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    static constexpr char kLongLine[] =
        "This is a long editable non wrapping line that we scroll horizontally so the caret and selection geometry must "
        "be resolved from the fragment cache instead of reshaping the whole line for every query.";
    ui_set_root(root);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kLongLine), sizeof(kLongLine) - 1U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_caret_color(text, 0xFF00FFAAU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* scroll_node = GetRuntime().Resolve(scroll);
    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node->scroll_content_width > 260.0f);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    REQUIRE(text_node->nonwrap_fragments.size() > 2U);

    const float target_offset = (scroll_node->scroll_content_width - 180.0f) * 0.5f;
    REQUIRE(target_offset > 40.0f);
    ui_set_scroll_offset(scroll, target_offset, 0.0f);
    ui_commit_frame();

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    const std::uint32_t anchor_index = GetRuntime().GetStringIndexFromPoint(
        *text_node,
        target_offset + (text_node->layout_width * 0.35f),
        text_node->line_height * 0.5f);
    REQUIRE(anchor_index > 4U);
    REQUIRE(anchor_index + 8U < static_cast<std::uint32_t>(text_node->text_content.size()));
    const std::uint32_t caret_index = anchor_index + 8U;

    text_node->selection_start = anchor_index;
    text_node->selection_end = caret_index;
    GetRuntime().SetFocus(text);
    text_node->is_dirty = true;
    ui_commit_frame();

    const auto [selection_left, selection_left_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, anchor_index);
    const auto [selection_right, selection_right_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, caret_index);
    REQUIRE(selection_left_line == 0);
    REQUIRE(selection_right_line == 0);
    REQUIRE_FALSE(GetRuntime().Selection().state().hit_rects.empty());
    const auto& hit_rect = GetRuntime().Selection().state().hit_rects.front();
    const float selection_probe_x = hit_rect.x + (hit_rect.width * 0.5f);
    const float selection_probe_y = hit_rect.y + (hit_rect.height * 0.5f);
    CHECK(hit_rect.x <= text_node->abs_x + selection_right);
    CHECK(hit_rect.x + hit_rect.width >= text_node->abs_x + selection_left);
    CHECK(ui_is_point_in_selection(selection_probe_x, selection_probe_y));

    GetRuntime().Selection().state().hit_rects.clear();
    GetRuntime().ClearTextGeometryProfile();
    CHECK(ui_is_point_in_selection(selection_probe_x, selection_probe_y));
    CHECK(GetRuntime().text_geometry_profile().bounded_calls == 1U);
    CHECK(GetRuntime().text_geometry_profile().unrestricted_calls == 0U);
    CHECK(GetRuntime().text_geometry_profile().lines_visited == 1U);

    text_node->selection_start = caret_index;
    text_node->selection_end = caret_index;
    text_node->is_dirty = true;
    ui_commit_frame();

    const auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    const auto [caret_local_x, caret_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, caret_index);
    CHECK(caret_line == 0);
    CHECK(carets.at(text).x == Approx(text_node->scene_x + caret_local_x).margin(0.5f));
    CHECK(carets.at(text).height >= 1.0f);
    CHECK(carets.at(text).color == 0xFF00FFAAU);
}

TEST_CASE("v2 ui replace-range edits patch the first short line without reshaping trailing long lines", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    std::string text = "Line one";
    const std::string long_line(10000U, 'W');
    for (std::size_t index = 0; index < 10U; index += 1U) {
        text.append("\n");
        text.append(long_line);
    }

    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(handle != UI_INVALID_HANDLE);
    auto* node = GetRuntime().ResolveMutable(handle);
    REQUIRE(node != nullptr);
    node->text_wrap = false;
    node->font_id = 1U;
    node->font_size = 20.0f;
    node->is_selectable = true;
    node->is_editable = true;
    node->text_content = text;

    const auto initial_layout = GetRuntime().LayoutParagraph(*node, 120.0f);
    REQUIRE(initial_layout.total_line_count == 11U);
    REQUIRE(node->nonwrap_fragment_cache_valid);
    const std::uint32_t initial_generation = node->nonwrap_fragment_cache_generation;
    REQUIRE(node->nonwrap_fragment_line_offsets.size() == 12U);
    const std::size_t trailing_fragment_start = node->nonwrap_fragment_line_offsets[1];
    const std::size_t trailing_fragment_end = node->nonwrap_fragment_line_offsets[2];
    REQUIRE(trailing_fragment_end > trailing_fragment_start);
    const std::vector<effindom::v2::ui::NonWrappingTextFragment> original_trailing_fragments(
        node->nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(trailing_fragment_start),
        node->nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(trailing_fragment_end));

    static constexpr char kInserted[] = "one";
    ui_replace_text_range(
        handle,
        static_cast<std::uint32_t>(std::string("Line one").size()),
        static_cast<std::uint32_t>(std::string("Line one").size()),
        reinterpret_cast<const std::uint8_t*>(kInserted),
        static_cast<std::uint32_t>(sizeof(kInserted) - 1U),
        static_cast<std::uint32_t>(std::string("Line oneone").size()));

    node = GetRuntime().ResolveMutable(handle);
    REQUIRE(node != nullptr);
    CHECK(node->text_content.substr(0U, 11U) == "Line oneone");
    CHECK(node->nonwrap_fragment_cache_valid);
    CHECK(node->text_layout_cache_valid);
    CHECK(node->nonwrap_fragment_cache_generation == initial_generation + 1U);
    CHECK(node->break_offsets.size() == 12U);
    CHECK(node->break_offsets[1] == 11);
    CHECK(node->selection_start == 11U);
    CHECK(node->selection_end == 11U);
    REQUIRE(node->nonwrap_fragment_line_offsets.size() == 12U);
    const std::size_t patched_trailing_fragment_start = node->nonwrap_fragment_line_offsets[1];
    const std::size_t patched_trailing_fragment_end = node->nonwrap_fragment_line_offsets[2];
    REQUIRE(patched_trailing_fragment_end - patched_trailing_fragment_start == original_trailing_fragments.size());
    for (std::size_t index = 0; index < original_trailing_fragments.size(); index += 1U) {
        const auto& original = original_trailing_fragments[index];
        const auto& patched = node->nonwrap_fragments[patched_trailing_fragment_start + index];
        CHECK(patched.line_index == original.line_index);
        CHECK(patched.local_byte_start == original.local_byte_start);
        CHECK(patched.local_byte_end == original.local_byte_end);
        CHECK(patched.x == Approx(original.x).margin(0.05f));
        CHECK(patched.width == Approx(original.width).margin(0.05f));
    }

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.text_wrap = false;
    expected.font_id = 1U;
    expected.font_size = 20.0f;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.text_content = node->text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 120.0f);

    REQUIRE(node->break_offsets == expected.break_offsets);
    REQUIRE(node->line_widths.size() == expected.line_widths.size());
    REQUIRE(node->nonwrap_fragment_line_offsets == expected.nonwrap_fragment_line_offsets);
    REQUIRE(node->nonwrap_fragments.size() == expected.nonwrap_fragments.size());
    CHECK(node->text_layout_cache_max_line_width == Approx(expected_layout.max_line_width).margin(0.05f));
    for (std::size_t index = 0; index < node->line_widths.size(); index += 1U) {
        CHECK(node->line_widths[index] == Approx(expected.line_widths[index]).margin(0.05f));
    }
}

TEST_CASE("v2 ui long non-wrap edits keep the fragment cache hot across ime paste and key mutations", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::Rect;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    static constexpr char kLongLine[] =
        "This is a very long line of editable text that we keep non-wrapped so incremental cache updates can be exercised "
        "while the viewport sits in the middle of the horizontal overflow and does not need the whole line reshaped.";
    ui_set_root(root);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kLongLine), sizeof(kLongLine) - 1U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    REQUIRE(text_node->nonwrap_fragments.size() > 2U);
    const float target_offset = (scroll_node->scroll_content_width - 140.0f) * 0.5f;
    REQUIRE(target_offset > 50.0f);
    ui_set_scroll_offset(scroll, target_offset, 0.0f);
    ui_commit_frame();

    auto shape_visible_window = [&](const effindom::v2::ui::UINode& node) {
        const Rect visible_bounds = GetRuntime().ComputeVisibleBounds(node);
        const float x_offset = GetRuntime().GetAlignedLineXOffset(node, node.line_widths.front());
        const auto window = GetRuntime().ResolveNonWrappingFragmentWindow(
            node,
            0U,
            (visible_bounds.x - node.abs_x) - x_offset,
            ((visible_bounds.x + visible_bounds.width) - node.abs_x) - x_offset);
        REQUIRE(window.start < window.end);
        const auto& first_fragment = node.nonwrap_fragments[window.start];
        const auto& last_fragment = node.nonwrap_fragments[window.end - 1U];
        effindom::v2::ui::UiRuntime::ShapedTextRun shaped{};
        REQUIRE(GetRuntime().ShapeText(
            std::string_view(node.text_content).substr(
                first_fragment.local_byte_start,
                last_fragment.local_byte_end - first_fragment.local_byte_start),
            node.font_id,
            node.font_size,
            shaped,
            node.is_obscured));
        return std::tuple{window, first_fragment.x, x_offset, shaped};
    };

    const auto base_words = ReadCommandBuffer();
    const auto base_runs = ReadGlyphRuns(base_words);
    REQUIRE(base_runs.find(text) != base_runs.end());
    effindom::v2::ui::UiRuntime::ShapedTextRun full_shape{};
    REQUIRE(GetRuntime().ShapeText(text_node->text_content, text_node->font_id, text_node->font_size, full_shape));
    const auto [base_window, base_fragment_x, base_x_offset, base_shape] = shape_visible_window(*text_node);
    CHECK(base_runs.at(text).glyphs.size() == base_shape.glyphs.size());
    CHECK(base_runs.at(text).glyphs.size() < full_shape.glyphs.size());
    CHECK(base_runs.at(text).glyphs.front().x == Approx(base_x_offset + base_fragment_x + base_shape.glyphs.front().x).margin(0.01f));

    const std::uint32_t initial_generation = text_node->nonwrap_fragment_cache_generation;
    const std::string ime_text = std::string("Z") + text_node->text_content;
    GetRuntime().HandleImeUpdate(
        text,
        reinterpret_cast<const std::uint8_t*>(ime_text.data()),
        static_cast<std::uint32_t>(ime_text.size()),
        1U);
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == initial_generation + 1U);
    CHECK_FALSE(text_node->nonwrap_render_fragment_window_valid);
    ui_commit_frame();
    const auto ime_words = ReadCommandBuffer();
    CHECK(CountCommand(ime_words, CMD_SET_GLYPH_RUN) == 1U);
    const auto ime_runs = ReadGlyphRuns(ime_words);
    REQUIRE(ime_runs.find(text) != ime_runs.end());
    const auto [ime_window, ime_fragment_x, ime_x_offset, ime_shape] = shape_visible_window(*text_node);
    CHECK(ime_runs.at(text).glyphs.size() == ime_shape.glyphs.size());
    CHECK(ime_runs.at(text).glyphs.size() < full_shape.glyphs.size() + 1U);
    CHECK(ime_runs.at(text).glyphs.front().x == Approx(ime_x_offset + ime_fragment_x + ime_shape.glyphs.front().x).margin(0.01f));

    text_node->selection_start = 8U;
    text_node->selection_end = 8U;
    const std::uint32_t ime_generation = text_node->nonwrap_fragment_cache_generation;
    static constexpr char kPaste[] = "++";
    GetRuntime().HandlePasteText(
        text,
        reinterpret_cast<const std::uint8_t*>(kPaste),
        static_cast<std::uint32_t>(sizeof(kPaste) - 1U));
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == ime_generation + 1U);
    ui_commit_frame();
    const auto paste_words = ReadCommandBuffer();
    CHECK(CountCommand(paste_words, CMD_SET_GLYPH_RUN) == 1U);

    GetRuntime().SetFocus(text);
    text_node->selection_start = 1U;
    text_node->selection_end = 1U;
    const std::uint32_t paste_generation = text_node->nonwrap_fragment_cache_generation;
    GetRuntime().HandleKeyEvent(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("Backspace"),
        9U,
        0U);
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == paste_generation + 1U);
    ui_commit_frame();
    const auto key_words = ReadCommandBuffer();
    CHECK(CountCommand(key_words, CMD_SET_GLYPH_RUN) == 1U);
    const float stable_offset = std::min(
        GetRuntime().Resolve(scroll)->scroll_offset_x + 1.0f,
        GetRuntime().Resolve(scroll)->scroll_content_width - 140.0f);
    ui_set_scroll_offset(scroll, stable_offset, 0.0f);
    ui_commit_frame();
    CHECK(CountCommand(ReadCommandBuffer(), CMD_SET_GLYPH_RUN) == 0U);

    const std::string multiline_text = std::string("A\n") + text_node->text_content;
    GetRuntime().HandleImeUpdate(
        text,
        reinterpret_cast<const std::uint8_t*>(multiline_text.data()),
        static_cast<std::uint32_t>(multiline_text.size()),
        2U);
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == paste_generation + 2U);
    REQUIRE(text_node->break_offsets.size() == 3U);
    CHECK(text_node->break_offsets[1] == 1);
    CHECK(text_node->break_offsets[2] == static_cast<std::int32_t>(multiline_text.size()));
}

TEST_CASE("v2 ui clearing a long non-wrap editable line drops horizontal overflow and resets scroll offset", "[v2][ui][scroll][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    const std::string long_line(5000U, 'W');

    ui_set_root(root);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(long_line.data()), static_cast<std::uint32_t>(long_line.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    const auto* initial_scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(initial_scroll_node != nullptr);
    REQUIRE(initial_scroll_node->scroll_content_width > 250.0f);

    ui_set_scroll_offset(scroll, 1'000'000.0f, 0.0f);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    const auto* scrolled_node = GetRuntime().Resolve(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scrolled_node != nullptr);
    REQUIRE(scrolled_node->scroll_offset_x > 0.0f);

    CHECK(GetRuntime().SelectAllText(text));
    GetRuntime().HandleKeyEvent(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("Backspace"),
        9U,
        0U);

    ui_commit_frame();

    text_node = GetRuntime().ResolveMutable(text);
    const auto* cleared_scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(cleared_scroll_node != nullptr);
    CHECK(text_node->text_content.empty());
    CHECK(cleared_scroll_node->scroll_content_width <= Approx(140.0f).margin(0.01f));
    CHECK(cleared_scroll_node->scroll_offset_x == Approx(0.0f).margin(0.01f));
}

TEST_CASE("v2 ui textbox set_text clamps each hard line independently", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string glyph = "\xC3\xA9";
    std::string long_line{};
    long_line.reserve(glyph.size() * kTextboxHardClampOverflowCodepoints);
    for (std::size_t index = 0U; index < kTextboxHardClampOverflowCodepoints; index += 1U) {
        long_line += glyph;
    }
    const std::string long_text = long_line + "\n" + long_line;
    std::string expected_line{};
    expected_line.reserve(glyph.size() * kTextboxHardClampMaxCodepoints);
    for (std::size_t index = 0U; index < kTextboxHardClampMaxCodepoints; index += 1U) {
        expected_line += glyph;
    }

    for (const bool wrap : {false, true}) {
        const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
        REQUIRE(handle != UI_INVALID_HANDLE);
        ui_set_semantic_role(handle, UI_SEMANTIC_TEXTBOX);
        ui_set_editable(handle, true);
        ui_set_font(handle, 1U, 16.0f);
        ui_set_text_limits(handle, std::numeric_limits<std::int32_t>::max(), 0);
        ui_set_text_wrapping(handle, wrap);
        ui_set_text(
            handle,
            reinterpret_cast<const std::uint8_t*>(long_text.data()),
            static_cast<std::uint32_t>(long_text.size()));

        const auto* node = GetRuntime().Resolve(handle);
        REQUIRE(node != nullptr);
        CHECK(node->text_content == expected_line + "\n" + expected_line);
        REQUIRE(node->text_line_starts.size() == 2U);
        CHECK(node->text_line_starts[0] == 0U);
        CHECK(node->text_line_starts[1] == static_cast<std::uint32_t>(expected_line.size() + 1U));
    }
}

TEST_CASE("v2 ui textbox replace range clamps non-wrap content and caret", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(handle != UI_INVALID_HANDLE);
    ui_set_semantic_role(handle, UI_SEMANTIC_TEXTBOX);
    ui_set_font(handle, 1U, 16.0f);
    ui_set_text_limits(handle, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_text_wrapping(handle, false);
    ui_set_selectable(handle, true, 0x40007AFFU);
    ui_set_editable(handle, true);

    const std::string near_limit(kTextboxHardClampMaxCodepoints - 1U, 'a');
    ui_set_text(
        handle,
        reinterpret_cast<const std::uint8_t*>(near_limit.data()),
        static_cast<std::uint32_t>(near_limit.size()));

    static constexpr char kInsert[] = "bc";
    ui_replace_text_range(
        handle,
        static_cast<std::uint32_t>(near_limit.size()),
        static_cast<std::uint32_t>(near_limit.size()),
        reinterpret_cast<const std::uint8_t*>(kInsert),
        static_cast<std::uint32_t>(sizeof(kInsert) - 1U),
        static_cast<std::uint32_t>(near_limit.size() + sizeof(kInsert) - 1U));

    const auto* node = GetRuntime().Resolve(handle);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_content.size() == kTextboxHardClampMaxCodepoints);
    CHECK(node->text_content[kTextboxHardClampMaxCodepoints - 1U] == 'b');
    CHECK(node->selection_start == static_cast<std::uint32_t>(kTextboxHardClampMaxCodepoints));
    CHECK(node->selection_end == static_cast<std::uint32_t>(kTextboxHardClampMaxCodepoints));
    REQUIRE(node->text_line_starts.size() == 1U);
    CHECK(node->text_line_starts[0] == 0U);

    const auto paragraph = GetRuntime().LayoutParagraph(*node, 120.0f);
    CHECK(paragraph.total_line_count == 1U);
    REQUIRE(paragraph.break_offsets.size() == 2U);
    CHECK(paragraph.break_offsets[0] == 0);
    CHECK(paragraph.break_offsets[1] == static_cast<std::int32_t>(kTextboxHardClampMaxCodepoints));
}

TEST_CASE("v2 ui textbox paste clamps wrapped content", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(handle != UI_INVALID_HANDLE);
    ui_set_semantic_role(handle, UI_SEMANTIC_TEXTBOX);
    ui_set_font(handle, 1U, 16.0f);
    ui_set_text_limits(handle, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_text_wrapping(handle, true);
    ui_set_selectable(handle, true, 0x40007AFFU);
    ui_set_editable(handle, true);

    const std::string near_limit(kTextboxHardClampMaxCodepoints - 1U, 'x');
    ui_set_text(
        handle,
        reinterpret_cast<const std::uint8_t*>(near_limit.data()),
        static_cast<std::uint32_t>(near_limit.size()));
    GetRuntime().SetFocus(handle);
    ui_set_text_selection_range(
        handle,
        static_cast<std::uint32_t>(near_limit.size()),
        static_cast<std::uint32_t>(near_limit.size()));

    GetRuntime().HandlePasteText(
        handle,
        reinterpret_cast<const std::uint8_t*>("yz"),
        2U);

    const auto* node = GetRuntime().Resolve(handle);
    REQUIRE(node != nullptr);
    CHECK(node->text_content.size() == kTextboxHardClampMaxCodepoints);
    CHECK(node->text_content.back() == 'y');
    CHECK(node->selection_start == static_cast<std::uint32_t>(kTextboxHardClampMaxCodepoints));
    CHECK(node->selection_end == static_cast<std::uint32_t>(kTextboxHardClampMaxCodepoints));
    REQUIRE(node->text_line_starts.size() == 1U);
}

TEST_CASE("v2 ui wrapped near-cap paste uses the single-line tail patch fast path", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(handle != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_node_add_child(root, handle);
    ui_resize_window(160.0f, 180.0f);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(handle, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(handle, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(handle, UI_SEMANTIC_TEXTBOX);
    ui_set_font(handle, 1U, 20.0f);
    ui_set_text_limits(handle, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_text_wrapping(handle, true);
    ui_set_selectable(handle, true, 0x40007AFFU);
    ui_set_editable(handle, true);

    const std::string near_limit(kTextboxHardClampMaxCodepoints - 1U, 'x');
    ui_set_text(
        handle,
        reinterpret_cast<const std::uint8_t*>(near_limit.data()),
        static_cast<std::uint32_t>(near_limit.size()));
    ui_commit_frame();

    const auto* initial = GetRuntime().Resolve(handle);
    REQUIRE(initial != nullptr);
    REQUIRE(initial->total_line_count > 1U);
    const std::uint32_t initial_tail_patch_generation = initial->wrapped_single_line_tail_patch_generation;

    GetRuntime().SetFocus(handle);
    ui_set_text_selection_range(
        handle,
        static_cast<std::uint32_t>(near_limit.size()),
        static_cast<std::uint32_t>(near_limit.size()));

    GetRuntime().HandlePasteText(
        handle,
        reinterpret_cast<const std::uint8_t*>("yz"),
        2U);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(handle);
    REQUIRE(node != nullptr);
    CHECK(node->text_content.size() == kTextboxHardClampMaxCodepoints);
    CHECK(node->text_content.back() == 'y');
    CHECK(node->selection_start == static_cast<std::uint32_t>(kTextboxHardClampMaxCodepoints));
    CHECK(node->selection_end == static_cast<std::uint32_t>(kTextboxHardClampMaxCodepoints));
    CHECK(node->wrapped_single_line_tail_patch_generation == initial_tail_patch_generation + 1U);
}

TEST_CASE("v2 ui becoming an editor clamps each hard line without imposing a total cap", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(handle != UI_INVALID_HANDLE);
    const std::string long_text = std::string(33000U, 'x') + "\n" + std::string(33000U, 'y');
    ui_set_text(
        handle,
        reinterpret_cast<const std::uint8_t*>(long_text.data()),
        static_cast<std::uint32_t>(long_text.size()));

    const auto* before = GetRuntime().Resolve(handle);
    REQUIRE(before != nullptr);
    REQUIRE(before->text_content.size() == long_text.size());

    ui_set_editable(handle, true);

    const auto* after = GetRuntime().Resolve(handle);
    REQUIRE(after != nullptr);
    CHECK(after->text_content.size() == (kTextboxHardClampMaxCodepoints * 2U) + 1U);
    REQUIRE(after->text_line_starts.size() == 2U);
    CHECK(after->text_line_starts[0] == 0U);
    CHECK(after->text_line_starts[1] == static_cast<std::uint32_t>(kTextboxHardClampMaxCodepoints + 1U));
    CHECK(after->text_content[kTextboxHardClampMaxCodepoints - 1U] == 'x');
    CHECK(after->text_content.back() == 'y');
}

TEST_CASE("v2 ui text snapshots expose realized static text and range rects", "[v2][ui][text-snapshots]") {
    ui_reset();
    RegisterTestFont();

    constexpr const char* kSample = "Find bridges reveal ranges cleanly.";
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_node_add_child(root, text);
    ui_resize_window(320.0f, 220.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 24.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_text_wrapping(text, true);
    ui_set_text(
        text,
        reinterpret_cast<const std::uint8_t*>(kSample),
        static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_commit_frame();

    const std::uint32_t byte_length = ui_get_text_document_utf8_length(text);
    REQUIRE(byte_length == std::strlen(kSample));
    std::vector<std::uint8_t> utf8(byte_length);
    REQUIRE(ui_copy_text_document_utf8(text, utf8.data(), byte_length));
    CHECK(std::string(utf8.begin(), utf8.end()) == kSample);
    float visible_x = 0.0f;
    float visible_y = 0.0f;
    float visible_width = 0.0f;
    float visible_height = 0.0f;
    REQUIRE(ui_get_text_visible_bounds(text, &visible_x, &visible_y, &visible_width, &visible_height));
    CHECK(visible_width > 0.0f);
    CHECK(visible_height > 0.0f);

    const std::size_t start_pos = std::string_view(kSample).find("bridges");
    REQUIRE(start_pos != std::string_view::npos);
    const std::uint32_t start = static_cast<std::uint32_t>(start_pos);
    const std::uint32_t end = start + 7U;
    const std::uint32_t rect_count = ui_get_text_range_rect_count(text, start, end);
    REQUIRE(rect_count > 0U);
    std::vector<float> rect_words(rect_count * 4U, 0.0f);
    REQUIRE(ui_copy_text_range_rects(text, start, end, rect_words.data(), rect_count) == rect_count);
    CHECK(rect_words[2] > 0.0f);
    CHECK(rect_words[3] > 0.0f);

    ui_set_editable(text, true);
    CHECK(ui_get_text_document_utf8_length(text) == std::numeric_limits<std::uint32_t>::max());
    CHECK(ui_get_text_visible_bounds(text, &visible_x, &visible_y, &visible_width, &visible_height));
    CHECK(ui_get_text_document_utf8_length(UI_INVALID_HANDLE) == std::numeric_limits<std::uint32_t>::max());
    CHECK_FALSE(ui_get_text_visible_bounds(UI_INVALID_HANDLE, &visible_x, &visible_y, &visible_width, &visible_height));
}

TEST_CASE("v2 ui text snapshot handle enumeration follows tree order and excludes editable text", "[v2][ui][text-snapshots]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t editable = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(editable != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 220.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t handle : {first, editable, second}) {
        ui_node_add_child(root, handle);
        ui_set_width(handle, 260.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(handle, 56.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(handle, 1U, 24.0f);
        ui_set_text_wrapping(handle, true);
        ui_set_text(handle, reinterpret_cast<const std::uint8_t*>("sample"), 6U);
    }
    ui_set_editable(editable, true);
    ui_commit_frame();

    const std::vector<std::uint64_t> handles = GetRuntime().GetTextSnapshotHandles();
    REQUIRE(handles.size() == 2U);
    CHECK(handles[0] == first);
    CHECK(handles[1] == second);

    REQUIRE(ui_get_text_snapshot_handle_count() == 2U);
    std::array<std::uint32_t, 4U> handle_words{};
    REQUIRE(ui_copy_text_snapshot_handles(handle_words.data(), 2U) == 2U);

    const auto decode_handle = [](std::uint32_t low, std::uint32_t high) {
        return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
    };
    CHECK(decode_handle(handle_words[0], handle_words[1]) == first);
    CHECK(decode_handle(handle_words[2], handle_words[3]) == second);
}

TEST_CASE("v2 ui missing Thai coverage requests stay stable across repeat commits", "[v2][ui][tofu-phase1]") {
    ui_reset();
    ResetInteractionLogs();
    RegisterBridgeBodyTestFont();

    constexpr const char* kThaiText = u8"ภาษาไทยภาษาไทย";
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 220.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t handle : {first, second}) {
        ui_node_add_child(root, handle);
        ui_set_width(handle, 260.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(handle, 56.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(handle, 1U, 24.0f);
        ui_set_text_wrapping(handle, true);
        ui_set_text(
            handle,
            reinterpret_cast<const std::uint8_t*>(kThaiText),
            static_cast<std::uint32_t>(std::strlen(kThaiText)));
    }

    ui_commit_frame();
    REQUIRE_FALSE(g_missing_font_coverage_requests.empty());
    const std::size_t initial_request_count = g_missing_font_coverage_requests.size();
    for (const auto& request : g_missing_font_coverage_requests) {
        CHECK(request.font_id == 1U);
        CHECK(request.coverage_kind == UI_MISSING_FONT_COVERAGE_THAI);
    }

    ui_commit_frame();
    CHECK(g_missing_font_coverage_requests.size() == initial_request_count);
}

TEST_CASE("v2 ui text find match emits retained highlight commands and clears cleanly", "[v2][ui][find]") {
    ui_reset();
    RegisterTestFont();

    constexpr const char* kSample = "Find bridges reveal ranges cleanly.";
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_node_add_child(root, text);
    ui_resize_window(320.0f, 220.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 24.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text(
        text,
        reinterpret_cast<const std::uint8_t*>(kSample),
        static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_commit_frame();

    const std::size_t start_pos = std::string_view(kSample).find("bridges");
    REQUIRE(start_pos != std::string_view::npos);
    const std::uint32_t start = static_cast<std::uint32_t>(start_pos);
    const std::uint32_t end = start + 7U;

    REQUIRE(ui_set_text_find_match(text, start, end));
    ui_commit_frame();
    auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK_FALSE(highlights.at(text).rects.empty());
    CHECK(highlights.at(text).color == EF_RGBA(0xFFU, 0xEBU, 0x3BU, 0x80U));

    ui_clear_text_find_match();
    ui_commit_frame();
    highlights = ReadHighlights(ReadCommandBuffer());
    CHECK(highlights.find(text) == highlights.end());
}

TEST_CASE("v2 ui reveal text range scrolls retained text into view", "[v2][ui][text-snapshots]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string sample =
        "Line 0 filler text keeps the viewport busy.\n"
        "Line 1 filler text keeps the viewport busy.\n"
        "Line 2 filler text keeps the viewport busy.\n"
        "Line 3 filler text keeps the viewport busy.\n"
        "Line 4 filler text keeps the viewport busy.\n"
        "Line 5 filler text keeps the viewport busy.\n"
        "Line 6 carries the FinalTarget marker for reveal.";
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(140.0f, 80.0f);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 960.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_text_wrapping(text, true);
    ui_set_text(
        text,
        reinterpret_cast<const std::uint8_t*>(sample.data()),
        static_cast<std::uint32_t>(sample.size()));
    ui_commit_frame();

    const auto* initial_scroll = GetRuntime().Resolve(scroll);
    REQUIRE(initial_scroll != nullptr);
    const std::size_t start_pos = sample.find("FinalTarget");
    REQUIRE(start_pos != std::string::npos);
    const std::uint32_t start = static_cast<std::uint32_t>(start_pos);
    const std::uint32_t end = start + 11U;
    const std::uint32_t before_count = ui_get_text_range_rect_count(text, start, end);
    REQUIRE(before_count > 0U);
    std::vector<float> before_rect_words(before_count * 4U, 0.0f);
    REQUIRE(ui_copy_text_range_rects(text, start, end, before_rect_words.data(), before_count) == before_count);
    float before_max_y = 0.0f;
    for (std::uint32_t index = 0; index < before_count; index += 1U) {
        const std::size_t base = static_cast<std::size_t>(index) * 4U;
        before_max_y = std::max(before_max_y, before_rect_words[base + 1U] + before_rect_words[base + 3U]);
    }
    CHECK(before_max_y > initial_scroll->abs_y + initial_scroll->layout_height);

    REQUIRE(ui_reveal_text_range(text, start, end));
    ui_commit_frame();

    const auto* revealed_scroll = GetRuntime().Resolve(scroll);
    REQUIRE(revealed_scroll != nullptr);
    CHECK(revealed_scroll->scroll_offset_y > 0.0f);
    const std::uint32_t after_count = ui_get_text_range_rect_count(text, start, end);
    REQUIRE(after_count == before_count);
    std::vector<float> after_rect_words(after_count * 4U, 0.0f);
    REQUIRE(ui_copy_text_range_rects(text, start, end, after_rect_words.data(), after_count) == after_count);
    float after_max_y = 0.0f;
    for (std::uint32_t index = 0; index < after_count; index += 1U) {
        const std::size_t base = static_cast<std::size_t>(index) * 4U;
        after_max_y = std::max(after_max_y, after_rect_words[base + 1U] + after_rect_words[base + 3U]);
    }
    CHECK(after_max_y <= Approx(revealed_scroll->abs_y + revealed_scroll->layout_height).margin(0.5f));
}

TEST_CASE("v2 ui wrapped replace-range newline at a hard-line end matches fresh layout", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(handle != UI_INVALID_HANDLE);
    ui_set_semantic_role(handle, UI_SEMANTIC_TEXTBOX);
    ui_set_font(handle, 1U, 20.0f);
    ui_set_text_limits(handle, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_text_wrapping(handle, true);
    ui_set_selectable(handle, true, 0x40007AFFU);
    ui_set_editable(handle, true);

    const std::string initial_text =
        "Line one\nLine two\nLine three\nLonger content so scrollbar policy is easy to spot.";
    ui_set_text(
        handle,
        reinterpret_cast<const std::uint8_t*>(initial_text.data()),
        static_cast<std::uint32_t>(initial_text.size()));

    auto* node = GetRuntime().ResolveMutable(handle);
    REQUIRE(node != nullptr);
    const auto initial_layout = GetRuntime().LayoutParagraph(*node, 260.0f);
    REQUIRE(initial_layout.total_line_count >= 5U);
    REQUIRE(node->text_layout_cache_valid);
    REQUIRE(node->visual_line_shape_cache_valid);

    static constexpr char kInsert[] = "\n";
    ui_replace_text_range(
        handle,
        static_cast<std::uint32_t>(initial_text.size()),
        static_cast<std::uint32_t>(initial_text.size()),
        reinterpret_cast<const std::uint8_t*>(kInsert),
        1U,
        static_cast<std::uint32_t>(initial_text.size() + 1U));

    node = GetRuntime().ResolveMutable(handle);
    REQUIRE(node != nullptr);
    REQUIRE(node->text_content == initial_text + "\n");
    CHECK(node->selection_start == initial_text.size() + 1U);
    CHECK(node->selection_end == initial_text.size() + 1U);
    if (!node->text_layout_cache_valid) {
        (void)GetRuntime().LayoutParagraph(*node, 260.0f);
    }

    effindom::v2::ui::UINode expected{};
    expected.is_text_node = true;
    expected.is_selectable = true;
    expected.is_editable = true;
    expected.semantic_role = UI_SEMANTIC_TEXTBOX;
    expected.font_id = node->font_id;
    expected.font_size = node->font_size;
    expected.text_wrap = true;
    expected.max_lines = 0;
    expected.text_content = node->text_content;
    const auto expected_layout = GetRuntime().LayoutParagraph(expected, 260.0f);

    REQUIRE(node->break_offsets == expected.break_offsets);
    REQUIRE(node->line_widths.size() == expected.line_widths.size());
    REQUIRE(node->logical_line_shapes.size() == expected.logical_line_shapes.size());
    REQUIRE(node->visual_line_shapes.size() == expected.visual_line_shapes.size());
    CHECK(node->total_line_count == expected_layout.total_line_count);
}

TEST_CASE("v2 ui line index helpers stay correct for deep multiline documents", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = false;
    node.font_id = 1U;
    node.font_size = 16.0f;
    for (std::size_t index = 0; index < 20000U; index += 1U) {
        node.text_content.append("row");
        node.text_content.append(std::to_string(index));
        if (index + 1U < 20000U) {
            node.text_content.push_back('\n');
        }
    }

    const auto paragraph = GetRuntime().LayoutParagraph(node, 120.0f);
    REQUIRE(paragraph.total_line_count == 20000U);

    const std::string needle = "row15000";
    const std::size_t target_offset = node.text_content.find(needle);
    REQUIRE(target_offset != std::string::npos);

    const auto [local_x, line_index] = GetRuntime().GetLocalPositionFromIndex(
        node,
        static_cast<std::uint32_t>(target_offset + 3U));
    CHECK(line_index == 15000);
    CHECK(local_x > 0.0f);
    CHECK(GetRuntime().IndexForLineBegin(node, static_cast<std::uint32_t>(target_offset + 3U)) ==
          static_cast<std::uint32_t>(target_offset));
    CHECK(GetRuntime().IndexForLineEnd(node, static_cast<std::uint32_t>(target_offset + 3U)) ==
          static_cast<std::uint32_t>(target_offset + needle.size()));
}

TEST_CASE("v2 ui multiline non-wrap commits cull long sibling lines after short-line edits", "[v2][ui][scroll]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string long_line(5000U, 'W');
    const std::string initial_text = std::string("short\n") + long_line;

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 56.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 56.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 56.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(initial_text.data()), static_cast<std::uint32_t>(initial_text.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->total_line_count == 2U);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    REQUIRE(text_node->nonwrap_fragment_line_offsets.size() == 3U);

    effindom::v2::ui::UiRuntime::ShapedTextRun short_shape{};
    effindom::v2::ui::UiRuntime::ShapedTextRun long_shape{};
    REQUIRE(GetRuntime().ShapeText("short", text_node->font_id, text_node->font_size, short_shape, text_node->is_obscured));
    REQUIRE(GetRuntime().ShapeText(long_line, text_node->font_id, text_node->font_size, long_shape, text_node->is_obscured));

    const auto base_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(base_runs.find(text) != base_runs.end());
    CHECK(base_runs.at(text).glyphs.size() < short_shape.glyphs.size() + long_shape.glyphs.size());

    GetRuntime().SetFocus(text);
    text_node->selection_start = 1U;
    text_node->selection_end = 1U;
    const std::uint32_t initial_generation = text_node->nonwrap_fragment_cache_generation;
    GetRuntime().HandleKeyEvent(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("!"),
        1U,
        0U);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_generation == initial_generation + 1U);

    ui_commit_frame();

    effindom::v2::ui::UiRuntime::ShapedTextRun edited_short_shape{};
    REQUIRE(GetRuntime().ShapeText("s!hort", text_node->font_id, text_node->font_size, edited_short_shape, text_node->is_obscured));
    const auto edited_runs = ReadGlyphRuns(ReadCommandBuffer());
    REQUIRE(edited_runs.find(text) != edited_runs.end());
    CHECK(edited_runs.at(text).glyphs.size() < edited_short_shape.glyphs.size() + long_shape.glyphs.size());
}

TEST_CASE("v2 ui multiline non-wrap textbox focus and selection changes preserve non-wrap caches", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::string long_line(5000U, 'W');
    const std::string initial_text = long_line + "\n" + std::string("test") + "\n" + long_line;

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 88.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(initial_text.data()), static_cast<std::uint32_t>(initial_text.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    REQUIRE(text_node->text_layout_cache_valid);

    GetRuntime().SetFocus(text);
    ui_commit_frame();
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->text_layout_cache_valid);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    const std::uint32_t first_line_insert = 1U;
    text_node->selection_start = first_line_insert;
    text_node->selection_end = first_line_insert;
    REQUIRE(GetRuntime().HandleTextEditingKey(*text_node, "!", 0U));

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->text_layout_cache_valid);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
    ui_commit_frame();
    CHECK(CountCommand(ReadCommandBuffer(), CMD_SET_GLYPH_RUN) == 1U);

    text_node->selection_start = 10U;
    text_node->selection_end = 10U;
    REQUIRE(GetRuntime().HandleTextEditingKey(*text_node, "ArrowRight", UI_KEY_MOD_SHIFT));
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);

    ui_commit_frame();
    const auto selection_words = ReadCommandBuffer();
    CHECK(CountCommand(selection_words, CMD_SET_GLYPH_RUN) == 0U);
    CHECK(CountCommand(selection_words, CMD_SET_BOUNDS) == 0U);
    CHECK(CountCommand(selection_words, CMD_SET_TEXT_FADE) == 0U);
    CHECK(CountCommand(selection_words, CMD_SET_CARET) >= 1U);
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->text_layout_cache_valid);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);

    GetRuntime().SetFocus(UI_INVALID_HANDLE);
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);

    ui_commit_frame();
    const auto blur_words = ReadCommandBuffer();
    CHECK(CountCommand(blur_words, CMD_SET_GLYPH_RUN) == 0U);
    CHECK(CountCommand(blur_words, CMD_SET_BOUNDS) == 0U);
    CHECK(CountCommand(blur_words, CMD_SET_TEXT_FADE) == 0U);
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->text_layout_cache_valid);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);
}

TEST_CASE("v2 ui focused select-all stays on the selection-only text path", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string long_line(5000U, 'W');
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 72.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(long_line.data()), static_cast<std::uint32_t>(long_line.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->text_layout_cache_valid);
    REQUIRE(text_node->nonwrap_fragment_cache_valid);

    GetRuntime().SetFocus(text);
    ui_commit_frame();
    ResetInteractionLogs();

    CHECK(GetRuntime().SelectAllText(text));
    REQUIRE(text_node->selection_start == 0U);
    REQUIRE(text_node->selection_end == static_cast<std::uint32_t>(text_node->text_content.size()));
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->nonwrap_fragment_cache_valid);
    CHECK(g_text_changes.empty());
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].handle == text);
    CHECK(g_selection_changes[0].start == 0U);
    CHECK(g_selection_changes[0].end == static_cast<std::uint32_t>(text_node->text_content.size()));

    ui_commit_frame();
    const auto words = ReadCommandBuffer();
    CHECK(CountCommand(words, CMD_SET_GLYPH_RUN) == 0U);
    CHECK(CountCommand(words, CMD_SET_BOUNDS) == 0U);
    CHECK(CountCommand(words, CMD_SET_TEXT_FADE) == 0U);
}

TEST_CASE("v2 ui wrapped paragraphs cache visual-line shapes for geometry lookups", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.text_wrap = true;
    node.font_id = 1U;
    node.font_size = 16.0f;
    node.text_content = u8"éclair beta gamma delta epsilon zeta";

    const auto paragraph = GetRuntime().LayoutParagraph(node, 84.0f);
    REQUIRE(paragraph.total_line_count >= 2U);
    REQUIRE(node.visual_line_shape_cache_valid);
    REQUIRE(node.visual_line_shapes.size() == paragraph.total_line_count);

    const auto visible_start_for_line = [&](std::size_t line_index) {
        std::uint32_t start = static_cast<std::uint32_t>(std::max(paragraph.break_offsets[line_index], 0));
        const std::uint32_t end = static_cast<std::uint32_t>(std::max(paragraph.break_offsets[line_index + 1U], 0));
        while (start < end && (node.text_content[start] == '\n' || node.text_content[start] == '\r')) {
            start += 1U;
        }
        return start;
    };

    for (std::size_t line_index = 0; line_index < paragraph.total_line_count; line_index += 1U) {
        const auto& cached_line_metadata = node.visual_line_shapes[line_index];
        const std::uint32_t expected_start = visible_start_for_line(line_index);
        const std::uint32_t expected_end = static_cast<std::uint32_t>(std::max(paragraph.break_offsets[line_index + 1U], 0));
        CHECK(cached_line_metadata.start == expected_start);
        CHECK(cached_line_metadata.end == expected_end);
        CHECK(cached_line_metadata.width == Approx(paragraph.line_widths[line_index]).margin(0.05f));
        CHECK_FALSE(cached_line_metadata.cache_materialized);
        CHECK(cached_line_metadata.cache_dirty);

        const auto* cached_line = GetRuntime().EnsureWrappedVisualLineShape(node, line_index);
        REQUIRE(cached_line != nullptr);
        CHECK_FALSE(cached_line->cache_dirty);
        CHECK(cached_line->cache_materialized);
        CHECK(cached_line->safe_slice_start == cached_line->start);
        CHECK(cached_line->safe_slice_end == cached_line->end);
        REQUIRE_FALSE(cached_line->cluster_stops.empty());
        CHECK(cached_line->cluster_stops.front().index == 0U);
        CHECK(cached_line->cluster_stops.back().index == (cached_line->end - cached_line->start));
        CHECK(cached_line->cluster_stops.back().x == Approx(cached_line->width).margin(0.05f));

        effindom::v2::ui::UINode visual_slice{};
        visual_slice.is_text_node = true;
        visual_slice.text_wrap = true;
        visual_slice.font_id = node.font_id;
        visual_slice.font_size = node.font_size;
        visual_slice.text_content = node.text_content.substr(
            static_cast<std::size_t>(cached_line->start),
            static_cast<std::size_t>(cached_line->end - cached_line->start));

        const auto slice_layout = GetRuntime().LayoutParagraph(visual_slice, std::nullopt);
        REQUIRE(slice_layout.total_line_count == 1U);
        REQUIRE(visual_slice.visual_line_shapes.size() == 1U);
        const auto* fresh_slice = GetRuntime().EnsureWrappedVisualLineShape(visual_slice, 0U);
        REQUIRE(fresh_slice != nullptr);
        REQUIRE(cached_line->glyphs.size() == fresh_slice->glyphs.size());
        REQUIRE(cached_line->cluster_stops.size() == fresh_slice->cluster_stops.size());
        for (std::size_t glyph_index = 0; glyph_index < cached_line->glyphs.size(); glyph_index += 1U) {
            CHECK(cached_line->glyphs[glyph_index].glyph_id == fresh_slice->glyphs[glyph_index].glyph_id);
            CHECK(cached_line->glyphs[glyph_index].font_id == fresh_slice->glyphs[glyph_index].font_id);
            CHECK(cached_line->glyphs[glyph_index].cluster == fresh_slice->glyphs[glyph_index].cluster);
            CHECK(cached_line->glyphs[glyph_index].x == Approx(fresh_slice->glyphs[glyph_index].x).margin(0.05f));
            CHECK(cached_line->glyphs[glyph_index].y == Approx(fresh_slice->glyphs[glyph_index].y).margin(0.05f));
        }
        for (std::size_t stop_index = 0; stop_index < cached_line->cluster_stops.size(); stop_index += 1U) {
            CHECK(cached_line->cluster_stops[stop_index].index == fresh_slice->cluster_stops[stop_index].index);
            CHECK(cached_line->cluster_stops[stop_index].x == Approx(fresh_slice->cluster_stops[stop_index].x).margin(0.05f));
        }
    }

    const auto* probe_line = GetRuntime().EnsureWrappedVisualLineShape(node, 1U);
    REQUIRE(probe_line != nullptr);
    REQUIRE(probe_line->cluster_stops.size() >= 2U);
    const std::size_t stop_index = probe_line->cluster_stops.size() > 2U ? 1U : 0U;
    const std::uint32_t expected_index = probe_line->start + probe_line->cluster_stops[stop_index].index;
    const float aligned_x =
        probe_line->cluster_stops[stop_index].x +
        ((probe_line->cluster_stops[stop_index + 1U].x - probe_line->cluster_stops[stop_index].x) * 0.25f);
    const float probe_x = GetRuntime().GetAlignedLineXOffset(node, probe_line->width) + aligned_x;
    const float probe_y = (node.line_height * 1.5f);

    CHECK(GetRuntime().GetStringIndexFromPoint(node, probe_x, probe_y) == expected_index);
    const auto [roundtrip_x, roundtrip_line] = GetRuntime().GetLocalPositionFromIndex(node, expected_index);
    CHECK(roundtrip_line == 1);
    CHECK(roundtrip_x ==
          Approx(GetRuntime().GetAlignedLineXOffset(node, probe_line->width) + probe_line->cluster_stops[stop_index].x)
              .margin(0.05f));
}

TEST_CASE("v2 ui wrapped selection-only commits keep visual-line caches hot", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    const std::string wrapped_text =
        "Alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu";

    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 84.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, true);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(wrapped_text.data()), static_cast<std::uint32_t>(wrapped_text.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    REQUIRE(text_node->text_layout_cache_valid);
    REQUIRE(text_node->visual_line_shape_cache_valid);
    REQUIRE(text_node->visual_line_shapes.size() == text_node->total_line_count);
    REQUIRE(text_node->total_line_count >= 3U);
    const std::size_t initial_cache_size = text_node->visual_line_shapes.size();
    const float second_line_width = text_node->visual_line_shapes[1].width;

    GetRuntime().SetFocus(text);
    ui_commit_frame();

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    const std::uint32_t selection_index = text_node->visual_line_shapes[1].start + 1U;
    text_node->selection_start = selection_index;
    text_node->selection_end = selection_index;
    REQUIRE(GetRuntime().HandleTextEditingKey(*text_node, "ArrowRight", UI_KEY_MOD_SHIFT));
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->visual_line_shape_cache_valid);
    CHECK(text_node->visual_line_shapes.size() == initial_cache_size);
    CHECK(text_node->visual_line_shapes[1].width == Approx(second_line_width).margin(0.05f));

    ui_commit_frame();
    const auto words = ReadCommandBuffer();
    CHECK(CountCommand(words, CMD_SET_GLYPH_RUN) == 0U);
    CHECK(CountCommand(words, CMD_SET_BOUNDS) == 0U);
    CHECK(CountCommand(words, CMD_SET_TEXT_FADE) == 0U);
    CHECK(CountCommand(words, CMD_SET_CARET) >= 1U);

    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->text_layout_cache_valid);
    CHECK(text_node->visual_line_shape_cache_valid);
    CHECK(text_node->visual_line_shapes.size() == initial_cache_size);
}

TEST_CASE("v2 ui rapid same-frame long-line edits coalesce pending follow-up work", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::string long_line(400U, 'W');
    const std::string append_text(32U, 'W');
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, true, false);
    ui_set_width(text, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_text_wrapping(text, false);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(long_line.data()), static_cast<std::uint32_t>(long_line.size()));
    ui_set_text_limits(text, std::numeric_limits<std::int32_t>::max(), 1);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    auto* text_node = GetRuntime().ResolveMutable(text);
    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(text_node != nullptr);
    REQUIRE(scroll_node != nullptr);
    REQUIRE(scroll_node->scroll_content_width > 140.0f);

    GetRuntime().SetFocus(text);
    ui_commit_frame();
    text_node = GetRuntime().ResolveMutable(text);
    REQUIRE(text_node != nullptr);
    text_node->selection_start = static_cast<std::uint32_t>(text_node->text_content.size());
    text_node->selection_end = static_cast<std::uint32_t>(text_node->text_content.size());
    ui_set_scroll_offset(scroll, 0.0f, 0.0f);
    ui_commit_frame();
    ResetInteractionLogs();

    GetRuntime().HandlePasteText(
        text,
        reinterpret_cast<const std::uint8_t*>(append_text.data()),
        static_cast<std::uint32_t>(append_text.size()));
    GetRuntime().HandlePasteText(
        text,
        reinterpret_cast<const std::uint8_t*>(append_text.data()),
        static_cast<std::uint32_t>(append_text.size()));

    CHECK(GetRuntime().pending_text_scroll_metric_handles_.size() == 1U);
    CHECK(GetRuntime().pending_text_scroll_metric_handles_.count(text) == 1U);
    CHECK(GetRuntime().pending_caret_visibility_handle_ == text);

    ui_commit_frame();
    CHECK(GetRuntime().pending_text_scroll_metric_handles_.empty());
    CHECK(GetRuntime().pending_caret_visibility_handle_ == UI_INVALID_HANDLE);
}

TEST_CASE("v2 ui modal semantic scope traps Tab focus inside the active subtree", "[v2][ui][input][modal]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t background = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t dialog_panel = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first_modal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t second_modal = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(background != UI_INVALID_HANDLE);
    REQUIRE(portal_root != UI_INVALID_HANDLE);
    REQUIRE(dialog_panel != UI_INVALID_HANDLE);
    REQUIRE(first_modal != UI_INVALID_HANDLE);
    REQUIRE(second_modal != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 160.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_width(background, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(background, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(background, true);
    ui_set_focusable(background, true, 0);

    ui_set_is_portal(portal_root, true);
    ui_set_width(dialog_panel, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(dialog_panel, 100.0f, UI_SIZE_UNIT_PIXEL);

    for (const std::uint64_t child : {first_modal, second_modal}) {
        ui_set_width(child, 72.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(child, 24.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_interactive(child, true);
        ui_set_focusable(child, true, 0);
    }

    ui_node_add_child(root, background);
    ui_node_add_child(root, portal_root);
    ui_node_add_child(portal_root, dialog_panel);
    ui_node_add_child(dialog_panel, first_modal);
    ui_node_add_child(dialog_panel, second_modal);
    ui_commit_frame();

    const std::uint32_t dialog_scope = ui_push_semantic_scope(dialog_panel);
    REQUIRE(dialog_scope != 0U);

    GetRuntime().SetFocus(UI_INVALID_HANDLE);
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == first_modal);
    CHECK(g_focus_events[0].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == first_modal);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[0].handle == first_modal);
    CHECK_FALSE(g_focus_events[0].is_focused);
    CHECK(g_focus_events[1].handle == second_modal);
    CHECK(g_focus_events[1].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == second_modal);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[1].handle == first_modal);
    CHECK(g_focus_events[1].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == first_modal);

    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, UI_KEY_MOD_SHIFT);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[1].handle == second_modal);
    CHECK(g_focus_events[1].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == second_modal);

    ui_remove_semantic_scope(dialog_scope);
    GetRuntime().SetFocus(UI_INVALID_HANDLE);
    ResetInteractionLogs();
    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("Tab"), 3U, 0U);
    REQUIRE(g_focus_events.size() == 1U);
    CHECK(g_focus_events[0].handle == background);
    CHECK(g_focus_events[0].is_focused);
}

TEST_CASE("v2 ui selectable single-line drag emits selection callback and highlight", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 80.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello"), 5U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*node, 0U);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*node, 5U);
    REQUIRE(start_line == 0);
    REQUIRE(end_line == 0);

    ui_set_interaction_time(111U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + start_x + 0.5f, node->abs_y + (node->line_height * 0.5f));
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, node->abs_x + end_x, node->abs_y + (node->line_height * 0.5f));
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + end_x, node->abs_y + (node->line_height * 0.5f));

    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].handle == text);
    CHECK(g_selection_changes[0].start == 0U);
    CHECK(g_selection_changes[0].end == 5U);

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK(highlights.at(text).rects.size() >= 1U);
    CHECK(highlights.at(text).color == 0x40007AFFU);
}

TEST_CASE("v2 ui selectable multiline drag emits multi-rect highlight", "[v2][ui][text-edit]") {
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
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    REQUIRE(node->visible_line_count >= 3U);
    const std::uint32_t start_index = static_cast<std::uint32_t>(node->break_offsets[0] + 1);
    const std::uint32_t end_index = static_cast<std::uint32_t>(node->break_offsets[2] + 1);
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*node, start_index);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*node, end_index);
    REQUIRE(start_line == 0);
    REQUIRE(end_line == 2);

    ui_set_interaction_time(222U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + start_x + 0.5f, node->abs_y + (node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        text,
        node->abs_x + end_x + 0.5f,
        node->abs_y + (static_cast<float>(end_line) * node->line_height) + (node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_UP,
        text,
        node->abs_x + end_x + 0.5f,
        node->abs_y + (static_cast<float>(end_line) * node->line_height) + (node->line_height * 0.5f));

    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].handle == text);
    CHECK(g_selection_changes[0].start == start_index);
    CHECK(g_selection_changes[0].end == end_index);

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK(highlights.at(text).rects.size() >= 3U);
}

TEST_CASE("v2 ui double click selects word and emits highlight", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto expected = GetRuntime().GetWordBoundaries(*node, 8U);
    const auto [click_x, click_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(click_line == 0);
    const float click_y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);
    ResetInteractionLogs();

    ui_set_interaction_time(450U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);

    REQUIRE_FALSE(g_selection_changes.empty());
    CHECK(g_selection_changes.back().handle == text);
    CHECK(g_selection_changes.back().start == expected.first);
    CHECK(g_selection_changes.back().end == expected.second);

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK(highlights.at(text).rects.size() >= 1U);
}

TEST_CASE("v2 ui select word at point selects the clicked word", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto expected = GetRuntime().GetWordBoundaries(*node, 8U);
    const auto [click_x, click_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(click_line == 0);
    const float click_y = node->abs_y + (node->line_height * 0.5f);

    CHECK(ui_select_word_at(text, node->abs_x + click_x + 0.5f, click_y));

    REQUIRE_FALSE(g_selection_changes.empty());
    CHECK(g_selection_changes.back().handle == text);
    CHECK(g_selection_changes.back().start == expected.first);
    CHECK(g_selection_changes.back().end == expected.second);
}

TEST_CASE("v2 ui touch double tap does not select the clicked word", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [click_x, click_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(click_line == 0);
    const float click_y = node->abs_y + (node->line_height * 0.5f);
    ui_set_interaction_time(100U);
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);
    ResetInteractionLogs();

    ui_set_interaction_time(450U);
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);

    CHECK(g_selection_changes.empty());
    const auto* final_node = GetRuntime().Resolve(text);
    REQUIRE(final_node != nullptr);
    CHECK(final_node->selection_start == final_node->selection_end);
}

TEST_CASE("v2 ui touch tap on editable text places the caret", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [tap_x, tap_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(tap_line == 0);
    const float tap_y = node->abs_y + (node->line_height * 0.5f);

    TouchPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + tap_x + 0.5f, tap_y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + tap_x + 0.5f, tap_y);

    const auto* final_node = GetRuntime().Resolve(text);
    REQUIRE(final_node != nullptr);
    CHECK(GetRuntime().Focus().FocusedHandle() == text);
    CHECK(final_node->selection_start == 8U);
    CHECK(final_node->selection_end == 8U);
    REQUIRE_FALSE(g_selection_changes.empty());
    CHECK(g_selection_changes.back().handle == text);
    CHECK(g_selection_changes.back().start == 8U);
    CHECK(g_selection_changes.back().end == 8U);
}

TEST_CASE("v2 ui touch drag does not create a text range selection", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*node, 1U);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*node, 12U);
    REQUIRE(start_line == 0);
    REQUIRE(end_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + start_x + 0.5f, y);
    TouchPointerEvent(UI_EVENT_POINTER_MOVE, text, node->abs_x + end_x + 0.5f, y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + end_x + 0.5f, y);

    CHECK(g_selection_changes.empty());
    const auto* final_node = GetRuntime().Resolve(text);
    REQUIRE(final_node != nullptr);
    CHECK(final_node->selection_start == final_node->selection_end);
}

TEST_CASE("v2 ui touch swipe over selected cross-selection text preserves the selection", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto expected = GetRuntime().GetWordBoundaries(*node, 8U);
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*node, expected.first);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*node, expected.second);
    REQUIRE(start_line == 0);
    REQUIRE(end_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    REQUIRE(ui_select_word_at(text, node->abs_x + start_x + 2.0f, y));
    REQUIRE(GetRuntime().Selection().state().cross_active);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().text == "brave");
    ResetInteractionLogs();
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + start_x + 2.0f, y);
    TouchPointerEvent(UI_EVENT_POINTER_MOVE, text, node->abs_x + end_x + 24.0f, y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + end_x + 24.0f, y);

    REQUIRE(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().area_handle == root);
    CHECK(GetRuntime().Selection().state().start_node_handle == text);
    CHECK(GetRuntime().Selection().state().end_node_handle == text);
    CHECK(GetRuntime().Selection().state().start_index == expected.first);
    CHECK(GetRuntime().Selection().state().end_index == expected.second);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text == "brave");
}

TEST_CASE("v2 ui touch tap outside selected cross-selection text clears the selection", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto selected = GetRuntime().GetWordBoundaries(*node, 8U);
    const auto [selected_x, selected_line] = GetRuntime().GetLocalPositionFromIndex(*node, selected.first);
    const auto [tap_x, tap_line] = GetRuntime().GetLocalPositionFromIndex(*node, 1U);
    REQUIRE(selected_line == 0);
    REQUIRE(tap_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    REQUIRE(ui_select_word_at(text, node->abs_x + selected_x + 2.0f, y));
    REQUIRE(GetRuntime().Selection().state().cross_active);
    ResetInteractionLogs();
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + tap_x + 0.5f, y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + tap_x + 0.5f, y);

    CHECK_FALSE(GetRuntime().Selection().state().cross_active);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text.empty());
}

TEST_CASE("v2 ui touch tap inside selected cross-selection text preserves the selection", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto expected = GetRuntime().GetWordBoundaries(*node, 8U);
    const auto [tap_x, tap_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(tap_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    REQUIRE(ui_select_word_at(text, node->abs_x + tap_x + 0.5f, y));
    REQUIRE(GetRuntime().Selection().state().cross_active);
    ResetInteractionLogs();
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + tap_x + 0.5f, y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + tap_x + 0.5f, y);

    REQUIRE(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().start_index == expected.first);
    CHECK(GetRuntime().Selection().state().end_index == expected.second);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().text == "brave");
}

TEST_CASE("v2 ui selection endpoint drag updates a single text selection through pointer events", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [word_x, word_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    const auto [drag_x, drag_line] = GetRuntime().GetLocalPositionFromIndex(*node, 1U);
    REQUIRE(word_line == 0);
    REQUIRE(drag_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);
    constexpr float kSelectionHandleDragAnchorYOffset = 20.0f;
    const float handle_drag_y = y + (node->line_height * 0.5f) + kSelectionHandleDragAnchorYOffset;

    REQUIRE(ui_select_word_at(text, node->abs_x + word_x + 0.5f, y));
    REQUIRE_FALSE(g_selection_changes.empty());
    ResetInteractionLogs();

    REQUIRE(ui_begin_selection_endpoint_drag(text, 0U));
    TouchPointerEvent(UI_EVENT_POINTER_MOVE, UI_INVALID_HANDLE, node->abs_x + drag_x + 0.5f, handle_drag_y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, UI_INVALID_HANDLE, node->abs_x + drag_x + 0.5f, handle_drag_y);

    const auto* final_node = GetRuntime().Resolve(text);
    REQUIRE(final_node != nullptr);
    CHECK(final_node->selection_start == 1U);
    CHECK(final_node->selection_end == 11U);
    REQUIRE_FALSE(g_selection_changes.empty());
    CHECK(g_selection_changes.back().handle == text);
    CHECK(g_selection_changes.back().start == 1U);
    CHECK(g_selection_changes.back().end == 11U);
}

TEST_CASE("v2 ui reverse mouse selection keeps the visual start as the keyboard focus edge", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [anchor_x, anchor_line] = GetRuntime().GetLocalPositionFromIndex(*node, 11U);
    const auto [focus_x, focus_line] = GetRuntime().GetLocalPositionFromIndex(*node, 6U);
    REQUIRE(anchor_line == 0);
    REQUIRE(focus_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + anchor_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, node->abs_x + focus_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + focus_x + 0.5f, y);

    auto* selected_node = GetRuntime().ResolveMutable(text);
    REQUIRE(selected_node != nullptr);
    CHECK(GetRuntime().Focus().FocusedHandle() == text);
    CHECK(selected_node->selection_start == 11U);
    CHECK(selected_node->selection_end == 6U);

    GetRuntime().HandleKeyEvent(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("ArrowLeft"),
        9U,
        UI_KEY_MOD_SHIFT);

    CHECK(selected_node->selection_start == 11U);
    CHECK(selected_node->selection_end == 5U);
}

TEST_CASE("v2 ui reverse mouse cross-selection keeps the visual start as the keyboard focus edge", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [anchor_x, anchor_line] = GetRuntime().GetLocalPositionFromIndex(*node, 11U);
    const auto [focus_x, focus_line] = GetRuntime().GetLocalPositionFromIndex(*node, 6U);
    REQUIRE(anchor_line == 0);
    REQUIRE(focus_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + anchor_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, node->abs_x + focus_x + 0.5f, y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + focus_x + 0.5f, y);

    REQUIRE(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Focus().FocusedHandle() == text);
    CHECK(GetRuntime().Selection().state().start_node_handle == text);
    CHECK(GetRuntime().Selection().state().start_index == 11U);
    CHECK(GetRuntime().Selection().state().end_node_handle == text);
    CHECK(GetRuntime().Selection().state().end_index == 6U);

    GetRuntime().HandleKeyEvent(
        UI_KEY_EVENT_DOWN,
        reinterpret_cast<const std::uint8_t*>("ArrowLeft"),
        9U,
        UI_KEY_MOD_SHIFT);

    CHECK(GetRuntime().Selection().state().start_node_handle == text);
    CHECK(GetRuntime().Selection().state().start_index == 11U);
    CHECK(GetRuntime().Selection().state().end_node_handle == text);
    CHECK(GetRuntime().Selection().state().end_index == 5U);
}

TEST_CASE("v2 ui selection handle pointer down preserves single text selection", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t handle = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    REQUIRE(handle != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(handle, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(handle, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(handle, true);
    ui_set_preserve_selection_on_pointer_down(handle, true);
    ui_node_add_child(root, text);
    ui_node_add_child(root, handle);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [word_x, word_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(word_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    REQUIRE(ui_select_word_at(text, node->abs_x + word_x + 0.5f, y));
    ResetInteractionLogs();
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, handle, 5.0f, 5.0f);

    const auto* final_node = GetRuntime().Resolve(text);
    REQUIRE(final_node != nullptr);
    CHECK(final_node->selection_start == 6U);
    CHECK(final_node->selection_end == 11U);
    CHECK(g_selection_changes.empty());
}

TEST_CASE("v2 ui selection endpoint drag updates cross-selection through pointer events", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [word_x, word_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    const auto [drag_x, drag_line] = GetRuntime().GetLocalPositionFromIndex(*node, 1U);
    REQUIRE(word_line == 0);
    REQUIRE(drag_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);
    constexpr float kSelectionHandleDragAnchorYOffset = 20.0f;
    const float handle_drag_y = y + kSelectionHandleDragAnchorYOffset;

    REQUIRE(ui_select_word_at(text, node->abs_x + word_x + 0.5f, y));
    REQUIRE(GetRuntime().Selection().state().cross_active);
    ResetInteractionLogs();

    REQUIRE(ui_begin_selection_endpoint_drag(root, 0U));
    TouchPointerEvent(UI_EVENT_POINTER_MOVE, UI_INVALID_HANDLE, node->abs_x + drag_x + 0.5f, handle_drag_y);
    TouchPointerEvent(UI_EVENT_POINTER_UP, UI_INVALID_HANDLE, node->abs_x + drag_x + 0.5f, handle_drag_y);

    REQUIRE(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().start_node_handle == text);
    CHECK(GetRuntime().Selection().state().end_node_handle == text);
    CHECK(GetRuntime().Selection().state().start_index == 1U);
    CHECK(GetRuntime().Selection().state().end_index == 11U);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text == "ello brave");
}

TEST_CASE("v2 ui selection handle pointer down preserves cross-selection", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t handle = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    REQUIRE(handle != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(handle, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(handle, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(handle, true);
    ui_set_preserve_selection_on_pointer_down(handle, true);
    ui_node_add_child(root, text);
    ui_node_add_child(root, handle);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [word_x, word_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(word_line == 0);
    const float y = node->abs_y + (node->line_height * 0.5f);

    REQUIRE(ui_select_word_at(text, node->abs_x + word_x + 0.5f, y));
    REQUIRE(GetRuntime().Selection().state().cross_active);
    ResetInteractionLogs();
    TouchPointerEvent(UI_EVENT_POINTER_DOWN, handle, 5.0f, 5.0f);

    CHECK(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().start_node_handle == text);
    CHECK(GetRuntime().Selection().state().end_node_handle == text);
    CHECK(GetRuntime().Selection().state().start_index == 6U);
    CHECK(GetRuntime().Selection().state().end_index == 11U);
    CHECK(g_cross_selection_changes.empty());
}

TEST_CASE("v2 ui triple click selects the clicked newline-delimited paragraph", "[v2][ui][text-edit]") {
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

    constexpr const char* kSample = "First paragraph\nSecond paragraph\nThird paragraph";
    ui_set_root(root);
    ui_resize_window(260.0f, 100.0f);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto expected = GetRuntime().GetParagraphBoundaries(*node, 20U);
    const auto [click_x, click_line] = GetRuntime().GetLocalPositionFromIndex(*node, 20U);
    const float click_y = node->abs_y + ((static_cast<float>(click_line) + 0.5f) * node->line_height);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);
    ui_set_interaction_time(250U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);
    ResetInteractionLogs();

    ui_set_interaction_time(400U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);

    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].handle == text);
    CHECK(g_selection_changes[0].start == expected.first);
    CHECK(g_selection_changes[0].end == expected.second);
}

TEST_CASE("v2 ui selection-area double click selects the clicked word", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "Hello brave world";
    ui_set_root(root);
    ui_resize_window(220.0f, 100.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto expected = GetRuntime().GetWordBoundaries(*node, 8U);
    const auto [click_x, click_line] = GetRuntime().GetLocalPositionFromIndex(*node, 8U);
    REQUIRE(click_line == 0);
    const float click_y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);
    ResetInteractionLogs();

    ui_set_interaction_time(450U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);

    REQUIRE(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().area_handle == root);
    CHECK(GetRuntime().Selection().state().start_node_handle == text);
    CHECK(GetRuntime().Selection().state().end_node_handle == text);
    CHECK(GetRuntime().Selection().state().start_index == expected.first);
    CHECK(GetRuntime().Selection().state().end_index == expected.second);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text == "brave");

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK_FALSE(highlights.at(text).rects.empty());
}

TEST_CASE("v2 ui selection-area triple click selects the clicked newline-delimited paragraph", "[v2][ui][cross-selection][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    constexpr const char* kSample = "First paragraph\nSecond paragraph\nThird paragraph";
    ui_set_root(root);
    ui_resize_window(260.0f, 120.0f);
    ui_set_selection_area(root, true);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kSample), static_cast<std::uint32_t>(std::strlen(kSample)));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto expected = GetRuntime().GetParagraphBoundaries(*node, 20U);
    const auto [click_x, click_line] = GetRuntime().GetLocalPositionFromIndex(*node, 20U);
    const float click_y = node->abs_y + ((static_cast<float>(click_line) + 0.5f) * node->line_height);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);
    ui_set_interaction_time(250U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);
    ResetInteractionLogs();

    ui_set_interaction_time(400U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + click_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + click_x + 0.5f, click_y);

    REQUIRE(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().area_handle == root);
    CHECK(GetRuntime().Selection().state().start_node_handle == text);
    CHECK(GetRuntime().Selection().state().end_node_handle == text);
    CHECK(GetRuntime().Selection().state().start_index == expected.first);
    CHECK(GetRuntime().Selection().state().end_index == expected.second);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text == "Second paragraph");

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK_FALSE(highlights.at(text).rects.empty());
}

TEST_CASE("v2 ui shift click extends selection from prior caret", "[v2][ui][text-edit]") {
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
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [anchor_x, anchor_line] = GetRuntime().GetLocalPositionFromIndex(*node, 5U);
    const auto [extend_x, extend_line] = GetRuntime().GetLocalPositionFromIndex(*node, 10U);
    REQUIRE(anchor_line == 0);
    REQUIRE(extend_line == 0);
    const float click_y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + anchor_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + anchor_x + 0.5f, click_y);
    ResetInteractionLogs();

    ui_set_interaction_time(300U);
    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text,
        node->abs_x + extend_x + 0.5f,
        click_y,
        -1,
        UI_POINTER_TYPE_MOUSE,
        0,
        0,
        0.0f,
        0.0f,
        0.0f,
        0,
        UI_KEY_MOD_SHIFT);
    UiTestPointerEvent(
        UI_EVENT_POINTER_UP,
        text,
        node->abs_x + extend_x + 0.5f,
        click_y,
        -1,
        UI_POINTER_TYPE_MOUSE,
        0,
        0,
        0.0f,
        0.0f,
        0.0f,
        0,
        UI_KEY_MOD_SHIFT);

    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].handle == text);
    CHECK(g_selection_changes[0].start == 5U);
    CHECK(g_selection_changes[0].end == 10U);
}

TEST_CASE("v2 ui plain click without shift places caret", "[v2][ui][text-edit]") {
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
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_caret_color(text, 0xFF123456U);
    ui_set_interactive(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const auto [first_x, first_line] = GetRuntime().GetLocalPositionFromIndex(*node, 5U);
    const auto [second_x, second_line] = GetRuntime().GetLocalPositionFromIndex(*node, 2U);
    REQUIRE(first_line == 0);
    REQUIRE(second_line == 0);
    const float click_y = node->abs_y + (node->line_height * 0.5f);

    ui_set_interaction_time(100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + first_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + first_x + 0.5f, click_y);
    ResetInteractionLogs();

    ui_set_interaction_time(300U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, node->abs_x + second_x + 0.5f, click_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, node->abs_x + second_x + 0.5f, click_y);

    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].start == 2U);
    CHECK(g_selection_changes[0].end == 2U);

    ui_commit_frame();
    const auto carets = ReadCarets(ReadCommandBuffer());
    REQUIRE(carets.find(text) != carets.end());
    CHECK(carets.at(text).color == 0xFF123456U);
}

TEST_CASE("v2 ui non-text clicks clear an existing text selection", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t button = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    REQUIRE(button != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(260.0f, 120.0f);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_interactive(text, true);
    ui_set_width(button, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(button, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_focusable(button, true, 0);
    ui_node_add_child(root, text);
    ui_node_add_child(root, button);
    ui_commit_frame();

    const auto* text_node = GetRuntime().Resolve(text);
    const auto* button_node = GetRuntime().Resolve(button);
    REQUIRE(text_node != nullptr);
    REQUIRE(button_node != nullptr);
    const auto [start_x, start_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, 0U);
    const auto [end_x, end_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, 5U);
    REQUIRE(start_line == 0);
    REQUIRE(end_line == 0);
    const float text_y = text_node->abs_y + (text_node->line_height * 0.5f);

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, text_node->abs_x + start_x + 0.5f, text_y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, text_node->abs_x + end_x + 0.5f, text_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, text_node->abs_x + end_x + 0.5f, text_y);
    REQUIRE_FALSE(g_selection_changes.empty());
    ResetInteractionLogs();

    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        button,
        button_node->abs_x + (button_node->layout_width * 0.5f),
        button_node->abs_y + (button_node->layout_height * 0.5f));
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].handle == text);
    CHECK(g_selection_changes[0].start == 5U);
    CHECK(g_selection_changes[0].end == 5U);
    CHECK(GetRuntime().Focus().FocusedHandle() == button);

    ui_commit_frame();
    auto highlights = ReadHighlights(ReadCommandBuffer());
    if (highlights.find(text) != highlights.end()) {
        CHECK(highlights.at(text).rects.empty());
    }

    auto* text_mut = GetRuntime().ResolveMutable(text);
    REQUIRE(text_mut != nullptr);
    text_mut->selection_start = 0U;
    text_mut->selection_end = 5U;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, UI_INVALID_HANDLE, -10.0f, -10.0f);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes[0].handle == text);
    CHECK(g_selection_changes[0].start == 5U);
    CHECK(g_selection_changes[0].end == 5U);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
}

TEST_CASE("v2 ui focusing another editable text clears the previous text selection", "[v2][ui][text-edit]") {
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
    ui_resize_window(420.0f, 120.0f);
    ui_set_width(root, 420.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    for (const std::uint64_t handle : {first, second}) {
        ui_set_width(handle, 180.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(handle, 1U, 20.0f);
        ui_set_text_limits(handle, std::numeric_limits<std::int32_t>::max(), 1);
        ui_set_semantic_role(handle, UI_SEMANTIC_TEXTBOX);
        ui_set_selectable(handle, true, 0x40007AFFU);
        ui_set_editable(handle, true);
        ui_set_interactive(handle, true);
        ui_node_add_child(root, handle);
    }
    ui_set_text(first, reinterpret_cast<const std::uint8_t*>("Hello world"), 11U);
    ui_set_text(second, reinterpret_cast<const std::uint8_t*>("Second field"), 12U);
    ui_commit_frame();

    const auto* first_node = GetRuntime().Resolve(first);
    const auto* second_node = GetRuntime().Resolve(second);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    const auto [first_start_x, first_start_line] = GetRuntime().GetLocalPositionFromIndex(*first_node, 0U);
    const auto [first_end_x, first_end_line] = GetRuntime().GetLocalPositionFromIndex(*first_node, 5U);
    const auto [second_click_x, second_click_line] = GetRuntime().GetLocalPositionFromIndex(*second_node, 0U);
    REQUIRE(first_start_line == 0);
    REQUIRE(first_end_line == 0);
    REQUIRE(second_click_line == 0);
    const float first_y = first_node->abs_y + (first_node->line_height * 0.5f);
    const float second_y = second_node->abs_y + (second_node->line_height * 0.5f);

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, first, first_node->abs_x + first_start_x + 0.5f, first_y);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, first, first_node->abs_x + first_end_x + 0.5f, first_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, first, first_node->abs_x + first_end_x + 0.5f, first_y);
    REQUIRE_FALSE(g_selection_changes.empty());
    REQUIRE(GetRuntime().Focus().FocusedHandle() == first);
    ResetInteractionLogs();

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, second, second_node->abs_x + second_click_x + 0.5f, second_y);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, second, second_node->abs_x + second_click_x + 0.5f, second_y);

    REQUIRE(g_selection_changes.size() == 2U);
    CHECK(g_selection_changes[0].handle == first);
    CHECK(g_selection_changes[0].start == 5U);
    CHECK(g_selection_changes[0].end == 5U);
    CHECK(g_selection_changes[1].handle == second);
    CHECK(g_selection_changes[1].start == 0U);
    CHECK(g_selection_changes[1].end == 0U);
    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[0].handle == first);
    CHECK_FALSE(g_focus_events[0].is_focused);
    CHECK(g_focus_events[1].handle == second);
    CHECK(g_focus_events[1].is_focused);
    CHECK(GetRuntime().Focus().FocusedHandle() == second);

    ui_commit_frame();
    const auto highlights = ReadHighlights(ReadCommandBuffer());
    if (highlights.find(first) != highlights.end()) {
        CHECK(highlights.at(first).rects.empty());
    }

    const auto* first_mut = GetRuntime().ResolveMutable(first);
    REQUIRE(first_mut != nullptr);
    CHECK(first_mut->selection_start == 5U);
    CHECK(first_mut->selection_end == 5U);
}
