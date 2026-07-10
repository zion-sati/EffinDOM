#include "TestUiSupport.h"

TEST_CASE("v2 ui semantic bounds track scrolled visual positions", "[v2][ui][semantic][scroll]") {
    ui_reset();

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
    ui_resize_window(200.0f, 100.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(content, UI_FLEX_DIRECTION_COLUMN);
    ui_set_width(spacer, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 50.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(target, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(target, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(target, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(
        target,
        reinterpret_cast<const std::uint8_t*>("Scrolled target"),
        15U);

    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, target);

    ui_set_scroll_offset(scroll, 0.0f, 30.0f);
    ui_commit_frame();

    const auto records = test_ui_support::ReadSemanticRecords(test_ui_support::ReadSemanticBuffer());
    const auto target_record = std::find_if(records.begin(), records.end(), [target](const test_ui_support::SemanticRecord& record) {
        return record.handle == target;
    });
    REQUIRE(target_record != records.end());
    CHECK(target_record->bounds.x == Approx(0.0f));
    CHECK(target_record->bounds.y == Approx(20.0f));
    CHECK(target_record->bounds.width == Approx(200.0f));
    CHECK(target_record->bounds.height == Approx(20.0f));
}
