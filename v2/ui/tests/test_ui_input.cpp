#include "TestUiSupport.h"

TEST_CASE("v2 ui trusts host click counts and only classifies missing counts", "[v2][ui][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        root,
        20.0f,
        20.0f,
        -1,
        UI_POINTER_TYPE_MOUSE,
        0,
        1U,
        0.5f,
        1.0f,
        1.0f,
        4);
    CHECK(runtime.Input().state().click_count == 4U);

    ui_set_interaction_time(1000U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, root, 40.0f, 40.0f);
    CHECK(runtime.Input().state().click_count == 1U);
    ui_set_interaction_time(1100U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, root, 40.0f, 40.0f);
    CHECK(runtime.Input().state().click_count == 2U);
    ui_set_interaction_time(1200U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, root, 40.0f, 40.0f);
    CHECK(runtime.Input().state().click_count == 3U);
    ui_set_interaction_time(1300U);
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, root, 40.0f, 40.0f);
    CHECK(runtime.Input().state().click_count == 4U);
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
    (*runtime.scroll_coordinator_).BeginPointerDrag(scroll);
    runtime.Input().state().primary_pointer_down = false;
    runtime.Input().state().last_pointer_logical_x = 40.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;

    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, content, 48.0f, 54.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(60.0f));
    CHECK(scroll_node->scroll_velocity_x == Approx(0.0f));
    CHECK(scroll_node->scroll_velocity_y == Approx(0.0f));
}

TEST_CASE("v2 ui touch endpoint drag suppresses stale editable caret drag", "[v2][ui][text-selection]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(text);
    ui_set_width(text, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("alpha beta gamma"), 16U);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    auto* node = runtime.ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 6U;
    node->selection_end = 10U;
    runtime.Selection().state().active_handle = text;
    runtime.Selection().state().active_dragged = false;
    runtime.Input().state().touch_text_tap_handle = text;
    runtime.Input().state().touch_text_tap_moved = true;

    REQUIRE(runtime.BeginSelectionEndpointDrag(text, 0U));
    CHECK(runtime.Input().state().touch_text_tap_handle == UI_INVALID_HANDLE);
    CHECK_FALSE(runtime.Input().state().touch_text_tap_moved);
    CHECK(runtime.Selection().state().handle_drag_active);

    runtime.Input().state().primary_pointer_down = true;
    const auto [target_x, target_line] = runtime.GetLocalPositionFromIndex(*node, 0U);
    REQUIRE(target_line == 0);
    const float target_y = node->abs_y + (node->line_height * 0.5f);
    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        text,
        node->abs_x + target_x + 0.5f,
        target_y,
        41,
        UI_POINTER_TYPE_TOUCH);

    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 10U);
    CHECK(runtime.Selection().state().handle_drag_active);
}

TEST_CASE("v2 ui touch editable focus commits on tap release and yields moved gestures to scrolling", "[v2][ui][text-edit][touch-focus]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_resize_window(260.0f, 120.0f);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 20.0f);
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_set_focusable(text, true, 0);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("alpha beta gamma"), 16U);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);
    ui_commit_frame();

    const auto* node = GetRuntime().Resolve(text);
    REQUIRE(node != nullptr);
    const float x = node->abs_x + 24.0f;
    const float y = node->abs_y + 60.0f;

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, x, y, 71, UI_POINTER_TYPE_TOUCH);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, x, y - 30.0f, 71, UI_POINTER_TYPE_TOUCH);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    const auto* moved_scroll = GetRuntime().Resolve(scroll);
    REQUIRE(moved_scroll != nullptr);
    CHECK(moved_scroll->scroll_offset_y > 0.0f);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, x, y - 30.0f, 71, UI_POINTER_TYPE_TOUCH);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);

    const auto* scrolled_text = GetRuntime().Resolve(text);
    REQUIRE(scrolled_text != nullptr);
    const float tap_x = scrolled_text->abs_x + 12.0f;
    const float tap_y = scrolled_text->abs_y + 10.0f;
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, tap_x, tap_y, 72, UI_POINTER_TYPE_TOUCH);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, tap_x, tap_y, 72, UI_POINTER_TYPE_TOUCH);
    CHECK(GetRuntime().Focus().FocusedHandle() == text);

    const auto* focused_node = GetRuntime().Resolve(text);
    REQUIRE(focused_node != nullptr);
    const std::uint32_t initial_caret = focused_node->selection_end;
    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, text, tap_x, tap_y, 73, UI_POINTER_TYPE_TOUCH);
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, text, tap_x + 80.0f, tap_y, 73, UI_POINTER_TYPE_TOUCH);
    const auto* dragged_node = GetRuntime().Resolve(text);
    REQUIRE(dragged_node != nullptr);
    CHECK(dragged_node->selection_end != initial_caret);
    UiTestPointerEvent(UI_EVENT_POINTER_UP, text, tap_x + 80.0f, tap_y, 73, UI_POINTER_TYPE_TOUCH);
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
    runtime.Input().state().last_hovered_handle = content;
    runtime.Input().state().last_pointer_logical_x = 40.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;

    ui_on_wheel_event(0.0f, 28.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(0.0f));
    CHECK(scroll_node->smooth_scroll_target_y == Approx(28.0f));
    CHECK(runtime.NeedsAnimationFrame());

    ui_on_wheel_event(0.0f, 28.0f);
    CHECK(scroll_node->smooth_scroll_target_y == Approx(56.0f));

    ui_commit_frame(0.0);
    CHECK(scroll_node->scroll_offset_y > 0.0f);
    CHECK(scroll_node->scroll_offset_y < 56.0f);

    for (int frame = 1; frame <= 60 && runtime.NeedsAnimationFrame(); frame += 1) {
        ui_commit_frame(static_cast<double>(frame) * (1000.0 / 60.0));
    }
    CHECK(scroll_node->scroll_offset_y == Approx(56.0f));
    CHECK_FALSE(runtime.NeedsAnimationFrame());

    ui_set_smooth_scrolling(scroll, false);
    ui_on_wheel_event(0.0f, 20.0f);
    CHECK(scroll_node->scroll_offset_y == Approx(76.0f));
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
    ui_set_flex_direction(root, UI_FLEX_DIRECTION_ROW);

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
    runtime.Input().state().last_hovered_handle = gap;
    runtime.Input().state().last_pointer_logical_x = 164.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;
    ui_on_wheel_event(0.0f, 20.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(0.0f));

    runtime.Input().state().last_hovered_handle = track;
    runtime.Input().state().last_pointer_logical_x = 172.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;
    ui_on_wheel_event(0.0f, 16.0f);
    CHECK(scroll_node->scroll_offset_y == Approx(0.0f));
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
    ui_set_flex_direction(root, UI_FLEX_DIRECTION_ROW);
    ui_set_width(scroll_box, 176.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll_box, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(scroll_box, UI_FLEX_DIRECTION_ROW);
    ui_set_width(scroll, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(gap, 8.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(gap, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(track, 8.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(track, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_proxy_target(scroll_box, scroll);
    ui_set_smooth_scrolling(scroll, false);

    ui_node_add_child(root, scroll_box);
    ui_node_add_child(scroll_box, scroll);
    ui_node_add_child(scroll_box, gap);
    ui_node_add_child(scroll_box, track);
    ui_node_add_child(scroll, content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.Input().state().last_hovered_handle = gap;
    runtime.Input().state().last_pointer_logical_x = 164.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;

    ui_on_wheel_event(0.0f, 20.0f);

    const auto* scroll_node = runtime.Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_offset_y == Approx(20.0f));

    ui_touch_scroll_begin(gap, 164.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == scroll);

    ui_touch_scroll_update(0.0f, 16.0f);
    CHECK(runtime.Resolve(scroll)->scroll_offset_y == Approx(36.0f));
    CHECK(runtime.Resolve(scroll)->scroll_velocity_y == Approx(960.0f));

    ui_touch_scroll_end();
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == UI_INVALID_HANDLE);
    CHECK(runtime.Resolve(scroll)->scroll_velocity_y == Approx(960.0f));
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
    ui_set_smooth_scrolling(outer_scroll, false);
    ui_set_smooth_scrolling(inner_scroll, false);

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
    runtime.Input().state().last_hovered_handle = scroll_box;
    runtime.Input().state().last_pointer_logical_x = 40.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;
    ui_on_wheel_event(0.0f, -20.0f);

    REQUIRE(runtime.Resolve(outer_scroll) != nullptr);
    REQUIRE(runtime.Resolve(inner_scroll) != nullptr);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(20.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(0.0f));

    ui_touch_scroll_begin(scroll_box, 40.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == inner_scroll);
    ui_touch_scroll_update(0.0f, -15.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(5.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(0.0f));

    ui_set_scroll_offset(outer_scroll, 0.0f, 20.0f);
    ui_set_scroll_offset(inner_scroll, 0.0f, 140.0f);
    ui_commit_frame();

    ui_on_wheel_event(0.0f, 20.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(40.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(140.0f));

    ui_touch_scroll_begin(scroll_box, 40.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == inner_scroll);
    ui_touch_scroll_update(0.0f, 15.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == outer_scroll);
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
    ui_set_smooth_scrolling(outer_scroll, false);
    ui_set_smooth_scrolling(inner_scroll, false);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, scroll_box);
    ui_node_add_child(outer_content, spacer);
    ui_node_add_child(scroll_box, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    CHECK(runtime.ResolveScrollTarget(inner_content, 116.0f, 40.0f) == outer_scroll);

    runtime.Input().state().last_hovered_handle = inner_content;
    runtime.Input().state().last_pointer_logical_x = 116.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;
    ui_on_wheel_event(0.0f, 20.0f);

    const auto* outer_node = runtime.Resolve(outer_scroll);
    const auto* inner_node = runtime.Resolve(inner_scroll);
    REQUIRE(outer_node != nullptr);
    REQUIRE(inner_node != nullptr);
    CHECK(outer_node->scroll_offset_y == Approx(20.0f));
    CHECK(inner_node->scroll_offset_y == Approx(0.0f));
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
    ui_set_smooth_scrolling(outer_scroll, false);
    ui_set_smooth_scrolling(inner_scroll, false);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.Input().state().last_pointer_logical_x = 40.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;
    ui_on_wheel_event(24.0f, 0.0f);

    const auto* outer_node = runtime.Resolve(outer_scroll);
    const auto* inner_node = runtime.Resolve(inner_scroll);
    REQUIRE(outer_node != nullptr);
    REQUIRE(inner_node != nullptr);
    CHECK(outer_node->scroll_offset_x == Approx(24.0f));
    CHECK(inner_node->scroll_offset_y == Approx(0.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == inner_scroll);
    ui_touch_scroll_update(30.0f, 0.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == outer_scroll);
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
    ui_set_smooth_scrolling(outer_scroll, false);
    ui_set_smooth_scrolling(inner_scroll, false);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    ui_set_scroll_offset(outer_scroll, 0.0f, 40.0f);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.Input().state().last_pointer_logical_x = 40.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;
    ui_on_wheel_event(0.0f, -20.0f);

    const auto* outer_node = runtime.Resolve(outer_scroll);
    const auto* inner_node = runtime.Resolve(inner_scroll);
    REQUIRE(outer_node != nullptr);
    REQUIRE(inner_node != nullptr);
    CHECK(outer_node->scroll_offset_y == Approx(20.0f));
    CHECK(inner_node->scroll_offset_y == Approx(0.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == inner_scroll);
    ui_touch_scroll_update(0.0f, -15.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(5.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(0.0f));

    ui_set_scroll_offset(outer_scroll, 0.0f, 20.0f);
    ui_set_scroll_offset(inner_scroll, 0.0f, 140.0f);
    ui_commit_frame();

    ui_on_wheel_event(0.0f, 20.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_y == Approx(40.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(140.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == inner_scroll);
    ui_touch_scroll_update(0.0f, 15.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == outer_scroll);
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
    ui_set_smooth_scrolling(outer_scroll, false);
    ui_set_smooth_scrolling(inner_scroll, false);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, outer_content);
    ui_node_add_child(outer_content, inner_scroll);
    ui_node_add_child(inner_scroll, inner_content);
    ui_commit_frame();

    ui_set_scroll_offset(outer_scroll, 30.0f, 0.0f);
    ui_set_scroll_offset(inner_scroll, 140.0f, 0.0f);
    ui_commit_frame();

    auto& runtime = GetRuntime();
    runtime.Input().state().last_pointer_logical_x = 40.0f;
    runtime.Input().state().last_pointer_logical_y = 40.0f;

    ui_on_wheel_event(20.0f, 0.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(50.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_x == Approx(140.0f));

    ui_touch_scroll_begin(inner_scroll, 40.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == inner_scroll);
    ui_touch_scroll_update(15.0f, 0.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == outer_scroll);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(65.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_x == Approx(140.0f));

    ui_set_scroll_offset(outer_scroll, 65.0f, 0.0f);
    ui_set_scroll_offset(inner_scroll, 0.0f, 0.0f);
    ui_commit_frame();

    runtime.Input().state().last_pointer_logical_x = 10.0f;
    ui_on_wheel_event(-20.0f, 0.0f);
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(45.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_x == Approx(0.0f));

    ui_touch_scroll_begin(inner_scroll, 10.0f, 40.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == inner_scroll);
    ui_touch_scroll_update(-15.0f, 0.0f);
    CHECK((*runtime.scroll_coordinator_).ActiveDragHandle() == outer_scroll);
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
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_x == Approx(1800.0f));
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_y == Approx(0.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(24.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_x == Approx(0.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_y == Approx(1440.0f));

    runtime.CommitFrame();
    CHECK(runtime.Resolve(outer_scroll)->scroll_offset_x == Approx(30.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_offset_y == Approx(24.0f));
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_x == Approx(1800.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_y == Approx(1440.0f));

    ui_touch_scroll_end();
    CHECK(runtime.Resolve(outer_scroll)->scroll_velocity_x == Approx(1800.0f));
    CHECK(runtime.Resolve(inner_scroll)->scroll_velocity_y == Approx(1440.0f));
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
    CHECK((*GetRuntime().scroll_coordinator_).ActiveDragHandle() == scroll);
    CHECK(ui_touch_scroll_allows_pull_to_refresh());

    ui_touch_scroll_update(0.0f, 28.0f);
    REQUIRE(GetRuntime().Resolve(scroll) != nullptr);
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(28.0f));
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_y == Approx(1680.0f));
    CHECK_FALSE(ui_touch_scroll_allows_pull_to_refresh());

    ui_touch_scroll_end();
    CHECK((*GetRuntime().scroll_coordinator_).ActiveDragHandle() == UI_INVALID_HANDLE);
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_y == Approx(1680.0f));

    ui_commit_frame();
    CHECK(GetRuntime().Resolve(scroll)->scroll_offset_y == Approx(56.0f));
    CHECK(GetRuntime().Resolve(scroll)->scroll_velocity_y == Approx(1596.0f));
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

    CHECK(android_velocity == Approx(1680.0f * 0.955f));
    CHECK(apple_velocity == Approx(1680.0f * 0.960f));
    CHECK(apple_velocity > android_velocity);
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

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, first, first_node->abs_x + start_x + 0.5f, first_node->abs_y + (first_node->line_height * 0.5f));
    UiTestPointerEvent(UI_EVENT_POINTER_MOVE, last, last_node->abs_x + end_x, last_node->abs_y + (last_node->line_height * 0.5f));
    UiTestPointerEvent(UI_EVENT_POINTER_UP, last, last_node->abs_x + end_x, last_node->abs_y + (last_node->line_height * 0.5f));

    ui_commit_frame();
    auto highlights = ReadHighlights(ReadCommandBuffer());
    REQUIRE(highlights.find(middle) != highlights.end());
    CHECK_FALSE(highlights.at(middle).rects.empty());

    UiTestPointerEvent(UI_EVENT_POINTER_DOWN, root, 2.0f, 2.0f);
    CHECK_FALSE(GetRuntime().Selection().state().cross_active);
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
    CHECK(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().active_handle == UI_INVALID_HANDLE);
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
    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        text,
        text_node->abs_x + click_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_UP,
        text,
        text_node->abs_x + click_x + 0.5f,
        text_node->abs_y + (text_node->line_height * 0.5f));

    CHECK_FALSE(GetRuntime().Selection().state().cross_active);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
    CHECK(g_cross_selection_changes.back().handle == root);
    CHECK(g_cross_selection_changes.back().text.empty());

    ui_commit_frame();
    highlights = ReadHighlights(ReadCommandBuffer());
    if (highlights.find(text) != highlights.end()) {
        CHECK(highlights.at(text).rects.empty());
    }
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

    UiTestPointerEvent(
        UI_EVENT_POINTER_DOWN,
        first,
        first_node->abs_x + start_x + 0.5f,
        first_node->abs_y + (first_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_MOVE,
        second,
        second_node->abs_x + end_x + 0.5f,
        second_node->abs_y + (second_node->line_height * 0.5f));
    UiTestPointerEvent(
        UI_EVENT_POINTER_UP,
        second,
        second_node->abs_x + end_x + 0.5f,
        second_node->abs_y + (second_node->line_height * 0.5f));

    CHECK(GetRuntime().Selection().state().cross_active);
    CHECK(GetRuntime().Selection().state().start_node_handle == first);
    CHECK(GetRuntime().Selection().state().start_index == 2U);
    CHECK(GetRuntime().Selection().state().end_node_handle == second);
    CHECK(GetRuntime().Selection().state().end_index == 3U);
    ResetInteractionLogs();

    ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowRight"), 10U, UI_KEY_MOD_SHIFT);

    CHECK(GetRuntime().Selection().state().start_node_handle == first);
    CHECK(GetRuntime().Selection().state().start_index == 2U);
    CHECK(GetRuntime().Selection().state().end_node_handle == second);
    CHECK(GetRuntime().Selection().state().end_index == 4U);
    REQUIRE_FALSE(g_cross_selection_changes.empty());
}
