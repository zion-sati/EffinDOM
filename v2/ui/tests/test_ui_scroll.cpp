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


