#include "TestUiSupport.h"

namespace {

const DebugTreeRecord& FindDebugRecord(const std::vector<DebugTreeRecord>& records, std::uint64_t handle) {
    const auto iter = std::find_if(records.begin(), records.end(), [handle](const DebugTreeRecord& record) {
        return record.handle == handle;
    });
    REQUIRE(iter != records.end());
    return *iter;
}

} // namespace

TEST_CASE("v2 ui debug tree exports retained identity layout style and semantic metadata", "[v2][ui][debug-tree]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_resize_window(320.0f, 240.0f);
    ui_set_root(root);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_padding(root, 10.0f, 12.0f, 14.0f, 16.0f);

    static constexpr std::string_view kNodeId = "debug-label";
    static constexpr std::string_view kLabel = "Debug label";
    ui_set_node_id(child, reinterpret_cast<const std::uint8_t*>(kNodeId.data()), static_cast<std::uint32_t>(kNodeId.size()));
    ui_set_semantic_role(child, UI_SEMANTIC_STATIC_TEXT);
    ui_set_semantic_label(child, reinterpret_cast<const std::uint8_t*>(kLabel.data()), static_cast<std::uint32_t>(kLabel.size()));
    ui_set_width(child, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_margin(child, 3.0f, 4.0f, 5.0f, 6.0f);
    ui_set_box_style(child, 0x11223344U, 2.0f, 3.0f, 4.0f, 5.0f, 1.5f, 0xAABBCCDDU, ED_BORDER_SOLID, 0.0f, 0.0f);
    ui_set_font(child, 1U, 18.0f);
    ui_set_text_color(child, 0x010203FFU);
    ui_set_text(child, reinterpret_cast<const std::uint8_t*>(kLabel.data()), static_cast<std::uint32_t>(kLabel.size()));
    ui_set_interactive(child, true);
    ui_set_focusable(child, true, 0);
    ui_node_add_child(root, child);

    ui_commit_frame();

    const std::vector<DebugTreeRecord> records = ReadDebugTreeRecords(ReadDebugTreeBuffer());
    REQUIRE(records.size() == 2U);

    const DebugTreeRecord& root_record = FindDebugRecord(records, root);
    CHECK(root_record.parent == UI_INVALID_HANDLE);
    CHECK(root_record.node_type == UI_NODE_FLEX_BOX);
    CHECK(root_record.bounds.width == Approx(320.0f));
    CHECK(root_record.bounds.height == Approx(240.0f));
    CHECK(root_record.padding.x == Approx(10.0f));
    CHECK(root_record.padding.y == Approx(12.0f));
    CHECK(root_record.padding.width == Approx(14.0f));
    CHECK(root_record.padding.height == Approx(16.0f));

    const DebugTreeRecord& child_record = FindDebugRecord(records, child);
    CHECK(child_record.parent == root);
    CHECK(child_record.node_type == UI_NODE_TEXT);
    CHECK((child_record.flags & kDebugTreeFlagVisibleNormal) != 0U);
    CHECK((child_record.flags & kDebugTreeFlagHasNodeId) != 0U);
    CHECK((child_record.flags & kDebugTreeFlagHasSemanticLabel) != 0U);
    CHECK((child_record.flags & kDebugTreeFlagHasBoxStyle) != 0U);
    CHECK((child_record.behavior_flags & kDebugTreeBehaviorText) != 0U);
    CHECK((child_record.behavior_flags & kDebugTreeBehaviorInteractive) != 0U);
    CHECK((child_record.behavior_flags & kDebugTreeBehaviorFocusable) != 0U);
    CHECK(child_record.semantic_role == UI_SEMANTIC_STATIC_TEXT);
    CHECK(child_record.node_id == std::string(kNodeId));
    CHECK(child_record.semantic_label == std::string(kLabel));
    CHECK(child_record.bounds.x == Approx(13.0f));
    CHECK(child_record.bounds.y == Approx(16.0f));
    CHECK(child_record.bounds.width == Approx(120.0f));
    CHECK(child_record.bounds.height == Approx(40.0f));
    CHECK(child_record.margin.x == Approx(3.0f));
    CHECK(child_record.margin.y == Approx(4.0f));
    CHECK(child_record.margin.width == Approx(5.0f));
    CHECK(child_record.margin.height == Approx(6.0f));
    CHECK(child_record.bg_color == 0x11223344U);
    CHECK(child_record.border_color == 0xAABBCCDDU);
    CHECK(child_record.border.x == Approx(1.5f));
    CHECK(child_record.radius_tl == Approx(2.0f));
    CHECK(child_record.radius_tr == Approx(3.0f));
    CHECK(child_record.radius_br == Approx(4.0f));
    CHECK(child_record.radius_bl == Approx(5.0f));
    CHECK(child_record.font_id == 1U);
    CHECK(child_record.font_size == Approx(18.0f));
    CHECK(child_record.text_color == 0x010203FFU);
}

TEST_CASE("v2 ui debug tree keeps offscreen scroll descendants with clipped bounds", "[v2][ui][debug-tree]") {
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

    ui_resize_window(120.0f, 80.0f);
    ui_set_root(root);
    ui_set_width(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_width(content, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(spacer, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(target, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(target, 20.0f, UI_SIZE_UNIT_PIXEL);

    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, target);

    ui_commit_frame();

    const std::vector<DebugTreeRecord> records = ReadDebugTreeRecords(ReadDebugTreeBuffer());
    REQUIRE(records.size() == 5U);

    const DebugTreeRecord& scroll_record = FindDebugRecord(records, scroll);
    CHECK(scroll_record.node_type == UI_NODE_SCROLLVIEW);
    CHECK((scroll_record.behavior_flags & kDebugTreeBehaviorScrollView) != 0U);
    CHECK((scroll_record.behavior_flags & kDebugTreeBehaviorScrollEnabledX) == 0U);
    CHECK((scroll_record.behavior_flags & kDebugTreeBehaviorScrollEnabledY) != 0U);
    CHECK(scroll_record.scroll_content_height == Approx(140.0f));
    CHECK(scroll_record.scroll_viewport_width == Approx(80.0f));
    CHECK(scroll_record.scroll_viewport_height == Approx(40.0f));

    const DebugTreeRecord& target_record = FindDebugRecord(records, target);
    CHECK(target_record.parent == content);
    CHECK(target_record.nearest_scroll_ancestor == scroll);
    CHECK(target_record.bounds.y == Approx(100.0f));
    CHECK(target_record.bounds.height == Approx(20.0f));
    CHECK((target_record.flags & kDebugTreeFlagClippedOrEmpty) != 0U);
    CHECK(target_record.visible_bounds.width == Approx(0.0f));
    CHECK(target_record.visible_bounds.height == Approx(0.0f));
}
