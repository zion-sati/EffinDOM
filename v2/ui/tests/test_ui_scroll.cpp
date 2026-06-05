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


TEST_CASE("v2 ui scroll views preserve intrinsic auto-width rows with auto-width columns", "[v2][ui][unit][layout]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t row = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t left_column = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t right_column = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t left_text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t right_text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(row != UI_INVALID_HANDLE);
    REQUIRE(left_column != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(right_column != UI_INVALID_HANDLE);
    REQUIRE(left_text != UI_INVALID_HANDLE);
    REQUIRE(right_text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(420.0f, 260.0f);
    ui_set_width(root, 420.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_flex_direction(row, 1U);
    ui_set_width(row, 0.0f, UI_SIZE_UNIT_AUTO);
    ui_set_flex_direction(left_column, 0U);
    ui_set_width(left_column, 0.0f, UI_SIZE_UNIT_AUTO);
    ui_set_flex_direction(right_column, 0U);
    ui_set_width(right_column, 0.0f, UI_SIZE_UNIT_AUTO);
    ui_set_width(spacer, 96.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 1.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_font(left_text, 1U, 18.0f);
    ui_set_text(
        left_text,
        reinterpret_cast<const std::uint8_t*>("Always show horizontal scrollbar"),
        32U);
    ui_set_font(right_text, 1U, 18.0f);
    ui_set_text(
        right_text,
        reinterpret_cast<const std::uint8_t*>("Visibility: Normal - keep layout reserved and content rendered"),
        62U);

    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, row);
    ui_node_add_child(row, left_column);
    ui_node_add_child(row, spacer);
    ui_node_add_child(row, right_column);
    ui_node_add_child(left_column, left_text);
    ui_node_add_child(right_column, right_text);

    ui_commit_frame();

    const auto* scroll_node = GetRuntime().Resolve(scroll);
    REQUIRE(scroll_node != nullptr);
    CHECK(scroll_node->scroll_content_width > 240.0f);

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(row) != bounds.end());
    CHECK(bounds.at(row).width > 240.0f);
}


TEST_CASE("v2 ui scroll auto-width columns stretch to fill scroll viewport", "[v2][ui][unit][layout]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t panel = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(panel != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(500.0f, 400.0f);
    ui_set_width(root, 500.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 400.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 400.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 300.0f, UI_SIZE_UNIT_PIXEL);

    // Panel: vertical Column, width auto — should stretch to fill scroll viewport
    ui_set_flex_direction(panel, 0U);
    ui_set_width(panel, 0.0f, UI_SIZE_UNIT_AUTO);

    // Child: fillWidth — should fill the panel's stretched width
    ui_set_fill_width(child, true);
    ui_set_height(child, 40.0f, UI_SIZE_UNIT_PIXEL);

    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, panel);
    ui_node_add_child(panel, child);

    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(panel) != bounds.end());
    REQUIRE(bounds.find(child) != bounds.end());
    CHECK(bounds.at(panel).width > 380.0f);
    CHECK(bounds.at(child).width > 380.0f);
}


TEST_CASE("v2 ui fill-size accounts for sibling padding and border", "[v2][ui][unit][layout]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    // Simulate HomeView pattern:
    //   Row (fills parent)
    //     SiblingA: explicit width + padding 24 + border 1 + margin 12
    //     Spacer
    //     MainPanel: fillSize (fillWidth in horizontal parent)
    // MainPanel must fit without overflow.

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t row = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t fixed_sibling = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t fill_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(row != UI_INVALID_HANDLE);
    REQUIRE(fixed_sibling != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(fill_child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(800.0f, 400.0f);
    ui_set_width(root, 800.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 400.0f, UI_SIZE_UNIT_PIXEL);

    // Row: horizontal flex, fills parent height.
    ui_set_flex_direction(row, 1U);
    ui_set_fill_height(row, true);

    // Fixed sibling: 250px width, with 24px padding, 1px border, 12px margin (like Panel).
    ui_set_width(fixed_sibling, 250.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_padding(fixed_sibling, 24.0f, 24.0f, 24.0f, 24.0f);
    ui_set_border_width(fixed_sibling, 1.0f);
    ui_set_margin(fixed_sibling, 12.0f, 12.0f, 12.0f, 12.0f);
    ui_set_fill_height(fixed_sibling, true);

    // Spacer: 24px wide.
    ui_set_width(spacer, 24.0f, UI_SIZE_UNIT_PIXEL);

    // Fill child: fillWidth in Row context (MainPanel equivalent).
    ui_set_fill_width(fill_child, true);
    ui_set_fill_height(fill_child, true);
    ui_set_padding(fill_child, 24.0f, 24.0f, 24.0f, 24.0f);
    ui_set_border_width(fill_child, 1.0f);
    ui_set_margin(fill_child, 12.0f, 12.0f, 12.0f, 12.0f);

    ui_node_add_child(root, row);
    ui_node_add_child(row, fixed_sibling);
    ui_node_add_child(row, spacer);
    ui_node_add_child(row, fill_child);

    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(row) != bounds.end());
    REQUIRE(bounds.find(fixed_sibling) != bounds.end());
    REQUIRE(bounds.find(fill_child) != bounds.end());

    // Fixed sibling: 250 content + 48 padding + 2 border + 24 margin = 324px outer.
    const float fixed_outer = 250.0f + 48.0f + 2.0f + 24.0f;
    CHECK(bounds.at(fixed_sibling).width == fixed_outer);

    // Row should contain both children without overflow.
    const float row_width = bounds.at(row).width;

    // Fill child right edge must not exceed Row right edge.
    const float fill_right = bounds.at(fill_child).x + bounds.at(fill_child).width;
    const float row_right = bounds.at(row).x + row_width;
    CHECK(fill_right <= row_right + 0.5f);

    // Fill child should have meaningful width (not collapsed to 0).
    CHECK(bounds.at(fill_child).width > 100.0f);

    // Total used: fixed outer + spacer + fill child + fill margins
    const float fill_outer = bounds.at(fill_child).width + 48.0f + 2.0f + 24.0f;
    const float total_used = fixed_outer + 24.0f + fill_outer;
    CHECK(total_used <= row_width + 1.0f);
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

