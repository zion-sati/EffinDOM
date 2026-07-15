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

TEST_CASE("v2 ui semantic projector promotes bare text and honors semantic scope", "[v2][ui][semantic][scope]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t bare_text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t button = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t button_text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t scoped_button = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(bare_text != UI_INVALID_HANDLE);
    REQUIRE(button != UI_INVALID_HANDLE);
    REQUIRE(button_text != UI_INVALID_HANDLE);
    REQUIRE(scoped_button != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 160.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_text(bare_text, reinterpret_cast<const std::uint8_t*>("Standalone"), 10U);
    ui_set_width(bare_text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(bare_text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(button, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(button, reinterpret_cast<const std::uint8_t*>("Save"), 4U);
    ui_set_width(button, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(button, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_text(button_text, reinterpret_cast<const std::uint8_t*>("Suppressed"), 10U);
    ui_set_width(button_text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(button_text, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(scoped_button, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(scoped_button, reinterpret_cast<const std::uint8_t*>("Scoped"), 6U);
    ui_set_width(scoped_button, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scoped_button, 20.0f, UI_SIZE_UNIT_PIXEL);

    ui_node_add_child(root, bare_text);
    ui_node_add_child(root, button);
    ui_node_add_child(button, button_text);
    ui_node_add_child(root, scoped_button);
    ui_commit_frame();

    const auto unscoped_records = test_ui_support::ReadSemanticRecords(test_ui_support::ReadSemanticBuffer());
    REQUIRE(std::count_if(unscoped_records.begin(), unscoped_records.end(), [bare_text](const test_ui_support::SemanticRecord& record) {
        return record.handle == bare_text && record.role == UI_SEMANTIC_STATIC_TEXT;
    }) == 1);
    REQUIRE(std::count_if(unscoped_records.begin(), unscoped_records.end(), [button](const test_ui_support::SemanticRecord& record) {
        return record.handle == button && record.role == UI_SEMANTIC_BUTTON;
    }) == 1);
    CHECK(std::none_of(unscoped_records.begin(), unscoped_records.end(), [button_text](const test_ui_support::SemanticRecord& record) {
        return record.handle == button_text;
    }));

    const std::uint32_t scope_token = ui_push_semantic_scope(scoped_button);
    REQUIRE(scope_token != 0U);
    ui_commit_frame();
    const auto scoped_records = test_ui_support::ReadSemanticRecords(test_ui_support::ReadSemanticBuffer());
    REQUIRE(scoped_records.size() == 1U);
    CHECK(scoped_records.front().handle == scoped_button);
    ui_remove_semantic_scope(scope_token);
}

TEST_CASE("v2 ui semantic projector clips multiline textbox to nested scroll viewports", "[v2][ui][semantic][textbox][scroll]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t inner_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t textbox = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(inner_scroll != UI_INVALID_HANDLE);
    REQUIRE(textbox != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(200.0f, 100.0f);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer_scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 50.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(inner_scroll, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inner_scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(textbox, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(textbox, 90.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_text(textbox, reinterpret_cast<const std::uint8_t*>("Multiline textbox"), 17U);
    ui_set_text_limits(textbox, 1024, 0);
    ui_set_semantic_role(textbox, UI_SEMANTIC_TEXTBOX);

    ui_node_add_child(root, outer_scroll);
    ui_node_add_child(outer_scroll, inner_scroll);
    ui_node_add_child(inner_scroll, textbox);
    ui_commit_frame();

    const auto records = test_ui_support::ReadSemanticRecords(test_ui_support::ReadSemanticBuffer());
    const auto textbox_record = std::find_if(records.begin(), records.end(), [textbox](const test_ui_support::SemanticRecord& record) {
        return record.handle == textbox;
    });
    REQUIRE(textbox_record != records.end());
    CHECK(textbox_record->bounds.width == Approx(100.0f));
    CHECK(textbox_record->bounds.height == Approx(50.0f));
}
