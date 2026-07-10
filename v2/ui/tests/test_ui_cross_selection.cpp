#include "TestUiSupport.h"

TEST_CASE("v2 ui collapsed cross-selection inside a selection area clears selection and hides the caret", "[v2][ui][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(180.0f, 80.0f);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_selection_area(root, true);
    ui_set_width(text, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Hello selection area"), 20U);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_node_add_child(root, text);
    ui_commit_frame();

    const auto* text_node = GetRuntime().Resolve(text);
    REQUIRE(text_node != nullptr);
    const auto [drag_start_x, drag_start_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, 2U);
    const auto [caret_x, caret_line] = GetRuntime().GetLocalPositionFromIndex(*text_node, 5U);
    REQUIRE(drag_start_line == 0);
    REQUIRE(caret_line == 0);

    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text,
        text_node->abs_x + drag_start_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        text,
        text_node->abs_x + caret_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_UP,
        text,
        text_node->abs_x + caret_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    ui_commit_frame();

    auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(text) != highlights.end());
    CHECK_FALSE(highlights.at(text).rects.empty());

    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text,
        text_node->abs_x + caret_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_UP,
        text,
        text_node->abs_x + caret_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    ui_commit_frame();

    const auto carets = ReadCarets(ReadCommandBuffer());
    highlights = ReadHighlights(ReadCommandBuffer());
    CHECK(carets.find(text) == carets.end());
    CHECK(highlights.find(text) == highlights.end());
    CHECK_FALSE(GetRuntime().cross_selection_active_);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text.empty());
}

TEST_CASE("v2 ui pointer cross-selection switches between selection areas", "[v2][ui][coverage][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t area_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t area_b = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text_a = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t text_b = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(area_a != UI_INVALID_HANDLE);
    REQUIRE(area_b != UI_INVALID_HANDLE);
    REQUIRE(text_a != UI_INVALID_HANDLE);
    REQUIRE(text_b != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, area_a);
    ui_node_add_child(root, area_b);
    for (const std::uint64_t area : {area_a, area_b}) {
        ui_set_selection_area(area, true);
        ui_set_width(area, 220.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_height(area, 60.0f, UI_SIZE_UNIT_PIXEL);
    }
    for (const auto& item : {
             std::pair{text_a, "First"},
             std::pair{text_b, "Second"},
         }) {
        ui_set_width(item.first, 200.0f, UI_SIZE_UNIT_PIXEL);
        ui_set_font(item.first, 1U, 20.0f);
        ui_set_selectable(item.first, true, 0x40007AFFU);
        ui_set_text(item.first, reinterpret_cast<const std::uint8_t*>(item.second), static_cast<std::uint32_t>(std::strlen(item.second)));
    }
    ui_node_add_child(area_a, text_a);
    ui_node_add_child(area_b, text_b);
    ui_commit_frame();

    const auto* node_a = GetRuntime().Resolve(text_a);
    const auto* node_b = GetRuntime().Resolve(text_b);
    REQUIRE(node_a != nullptr);
    REQUIRE(node_b != nullptr);

    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text_a,
        node_a->abs_x + 1.0f,
        node_a->abs_y + (node_a->line_height * 0.5f));
    CHECK(GetRuntime().selection_area_handle_ == area_a);

    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text_b,
        node_b->abs_x + 1.0f,
        node_b->abs_y + (node_b->line_height * 0.5f));
    CHECK(GetRuntime().selection_area_handle_ == area_b);
    CHECK(GetRuntime().cross_selection_active_);
}

TEST_CASE("v2 ui selection area barrier stops CollectSelectionAreaNodes from entering subtree", "[v2][ui][unit][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    // area
    //   barrier (is_selection_area_barrier = true)
    //     inner_text  (selectable — must NOT be collected)
    //   outer_text    (selectable — must be collected)
    const std::uint64_t area        = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t barrier     = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner_text  = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t outer_text  = ui_create_node(UI_NODE_TEXT);
    REQUIRE(area        != UI_INVALID_HANDLE);
    REQUIRE(barrier     != UI_INVALID_HANDLE);
    REQUIRE(inner_text  != UI_INVALID_HANDLE);
    REQUIRE(outer_text  != UI_INVALID_HANDLE);

    ui_set_selection_area(area, true);
    ui_set_selection_area_barrier(barrier, true);
    for (const std::uint64_t t : {inner_text, outer_text}) {
        ui_set_font(t, 1U, 16.0f);
        ui_set_selectable(t, true, 0xFF0000FFU);
    }
    ui_node_add_child(barrier, inner_text);
    ui_node_add_child(area,    barrier);
    ui_node_add_child(area,    outer_text);

    std::vector<std::uint64_t> collected{};
    GetRuntime().CollectSelectionAreaNodes(area, collected);

    REQUIRE(collected.size() == 1U);
    CHECK(collected[0] == outer_text);
}

TEST_CASE("v2 ui selection area barrier stops FindSelectionAreaAncestor from crossing subtree boundary", "[v2][ui][unit][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t barrier = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inner = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t nested_area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t nested_inner = ui_create_node(UI_NODE_TEXT);
    REQUIRE(area != UI_INVALID_HANDLE);
    REQUIRE(barrier != UI_INVALID_HANDLE);
    REQUIRE(inner != UI_INVALID_HANDLE);
    REQUIRE(nested_area != UI_INVALID_HANDLE);
    REQUIRE(nested_inner != UI_INVALID_HANDLE);

    ui_set_selection_area(area, true);
    ui_set_selection_area_barrier(barrier, true);
    ui_set_selection_area(nested_area, true);
    ui_node_add_child(barrier, inner);
    ui_node_add_child(nested_area, nested_inner);
    ui_node_add_child(barrier, nested_area);
    ui_node_add_child(area, barrier);

    CHECK(GetRuntime().FindSelectionAreaAncestor(inner) == UI_INVALID_HANDLE);
    CHECK(GetRuntime().FindSelectionAreaAncestor(nested_inner) == nested_area);
}

TEST_CASE("v2 ui selection area barrier does not prevent nested SelectionArea inside barrier", "[v2][ui][unit][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    // area
    //   barrier
    //     nested_area (its own SelectionArea root)
    //       nested_text
    const std::uint64_t area         = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t barrier      = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t nested_area  = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t nested_text  = ui_create_node(UI_NODE_TEXT);
    REQUIRE(area        != UI_INVALID_HANDLE);
    REQUIRE(barrier     != UI_INVALID_HANDLE);
    REQUIRE(nested_area != UI_INVALID_HANDLE);
    REQUIRE(nested_text != UI_INVALID_HANDLE);

    ui_set_selection_area(area, true);
    ui_set_selection_area_barrier(barrier, true);
    ui_set_selection_area(nested_area, true);
    ui_set_font(nested_text, 1U, 16.0f);
    ui_set_selectable(nested_text, true, 0xFF0000FFU);
    ui_node_add_child(nested_area, nested_text);
    ui_node_add_child(barrier, nested_area);
    ui_node_add_child(area, barrier);

    // outer area sees nothing (barrier blocks)
    std::vector<std::uint64_t> outer_collected{};
    GetRuntime().CollectSelectionAreaNodes(area, outer_collected);
    CHECK(outer_collected.empty());

    // nested_area collects its own text independently
    std::vector<std::uint64_t> inner_collected{};
    GetRuntime().CollectSelectionAreaNodes(nested_area, inner_collected);
    REQUIRE(inner_collected.size() == 1U);
    CHECK(inner_collected[0] == nested_text);
}

TEST_CASE("v2 ui SetSelectionAreaBarrier toggles flag and marks collection dirty", "[v2][ui][unit][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();

    const std::uint64_t area    = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t node    = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(area != UI_INVALID_HANDLE);
    REQUIRE(node != UI_INVALID_HANDLE);

    ui_set_selection_area(area, true);
    ui_node_add_child(area, node);

    auto& runtime = GetRuntime();
    runtime.selection_area_handle_ = area;
    runtime.selection_area_nodes_dirty_ = false;

    // Enable barrier — must mark dirty
    ui_set_selection_area_barrier(node, true);
    CHECK(runtime.Resolve(node)->is_selection_area_barrier);
    CHECK(runtime.selection_area_nodes_dirty_);

    runtime.selection_area_nodes_dirty_ = false;

    // Disable barrier — must mark dirty again
    ui_set_selection_area_barrier(node, false);
    CHECK_FALSE(runtime.Resolve(node)->is_selection_area_barrier);
    CHECK(runtime.selection_area_nodes_dirty_);

    // Invalid handle returns false gracefully
    CHECK_FALSE(runtime.SetSelectionAreaBarrier(UI_INVALID_HANDLE, true));
}

TEST_CASE("v2 ui clear selection collapses an existing single-node text selection", "[v2][ui][unit][selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    ResetInteractionLogs();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_root(root);
    ui_node_add_child(root, text);
    ui_set_font(text, 1U, 18.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("hello world"), 11U);
    ui_commit_frame();

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 1U;
    node->selection_end = 5U;
    node->is_dirty = true;
    ResetInteractionLogs();

    ui_clear_selection(text);

    node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    CHECK(node->selection_start == 5U);
    CHECK(node->selection_end == 5U);

    CHECK_FALSE(GetRuntime().ClearSelection(UI_INVALID_HANDLE));
}

TEST_CASE("v2 ui clear selection clears active cross-selection when target text is inside area", "[v2][ui][unit][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    ResetInteractionLogs();

    const std::uint64_t area = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(area != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(area);
    ui_set_selection_area(area, true);
    ui_node_add_child(area, text);
    ui_set_font(text, 1U, 18.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("cross selection"), 15U);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area;
    runtime.start_node_handle_ = text;
    runtime.start_index_ = 1U;
    runtime.end_node_handle_ = text;
    runtime.end_index_ = 4U;
    runtime.selection_area_nodes_.assign(1U, text);
    runtime.selection_area_nodes_dirty_ = false;

    ResetInteractionLogs();
    ui_clear_selection(text);

    CHECK_FALSE(runtime.cross_selection_active_);
    CHECK(runtime.selection_area_handle_ == UI_INVALID_HANDLE);
    CHECK(runtime.start_node_handle_ == UI_INVALID_HANDLE);
    CHECK(runtime.end_node_handle_ == UI_INVALID_HANDLE);
    CHECK(runtime.selection_area_nodes_.empty());
    REQUIRE(g_cross_selection_changes.size() == 1U);
    CHECK(g_cross_selection_changes[0].handle == area);
    CHECK(g_cross_selection_changes[0].text.empty());
}

TEST_CASE("v2 ui retarget selection moves active cross-selection endpoints between pooled text nodes", "[v2][ui][unit][cross-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    ResetInteractionLogs();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t area_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t area_b = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text_a = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t text_b = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(area_a != UI_INVALID_HANDLE);
    REQUIRE(area_b != UI_INVALID_HANDLE);
    REQUIRE(text_a != UI_INVALID_HANDLE);
    REQUIRE(text_b != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_selection_area(area_a, true);
    ui_set_selection_area(area_b, true);
    ui_node_add_child(root, area_a);
    ui_node_add_child(root, area_b);
    ui_node_add_child(area_a, text_a);
    ui_node_add_child(area_b, text_b);
    for (const std::uint64_t handle : {text_a, text_b}) {
        ui_set_font(handle, 1U, 18.0f);
        ui_set_selectable(handle, true, 0x40007AFFU);
    }
    ui_set_text(text_a, reinterpret_cast<const std::uint8_t*>("alpha"), 5U);
    ui_set_text(text_b, reinterpret_cast<const std::uint8_t*>("alpha"), 5U);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.cross_selection_active_ = true;
    runtime.selection_area_handle_ = area_a;
    runtime.start_node_handle_ = text_a;
    runtime.start_index_ = 1U;
    runtime.end_node_handle_ = text_a;
    runtime.end_index_ = 4U;
    runtime.selection_area_nodes_.assign(1U, text_a);
    runtime.selection_area_nodes_dirty_ = false;

    ResetInteractionLogs();
    ui_retarget_selection(text_a, text_b);

    CHECK(runtime.cross_selection_active_);
    CHECK(runtime.selection_area_handle_ == area_b);
    CHECK(runtime.start_node_handle_ == text_b);
    CHECK(runtime.end_node_handle_ == text_b);
    CHECK_FALSE(runtime.selection_area_nodes_dirty_);
    REQUIRE(runtime.selection_area_nodes_.size() == 1U);
    CHECK(runtime.selection_area_nodes_.front() == text_b);
    REQUIRE(g_cross_selection_changes.size() == 2U);
    CHECK(g_cross_selection_changes[0].handle == area_a);
    CHECK(g_cross_selection_changes[0].text.empty());
    CHECK(g_cross_selection_changes[1].handle == area_b);

    CHECK_FALSE(runtime.RetargetSelection(UI_INVALID_HANDLE, text_b));
}
