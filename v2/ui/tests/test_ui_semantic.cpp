#include "TestUiSupport.h"

TEST_CASE("v2 ui semantic buffer packs full labels bounds and empty-label roles", "[v2][ui][semantic]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t button = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t heading = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t unlabeled = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(button != UI_INVALID_HANDLE);
    REQUIRE(heading != UI_INVALID_HANDLE);
    REQUIRE(unlabeled != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 180.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);

    const std::string button_label = std::string(300U, 'B') + u8"é";
    ui_set_width(button, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(button, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(button, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(
        button,
        reinterpret_cast<const std::uint8_t*>(button_label.data()),
        static_cast<std::uint32_t>(button_label.size()));

    const std::string heading_text = std::string(260U, 'A') + u8"é";
    ui_set_font(heading, 1U, 18.0f);
    ui_set_text(
        heading,
        reinterpret_cast<const std::uint8_t*>(heading_text.data()),
        static_cast<std::uint32_t>(heading_text.size()));
    ui_set_semantic_role(heading, UI_SEMANTIC_HEADING);

    ui_set_width(unlabeled, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(unlabeled, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(unlabeled, UI_SEMANTIC_FORM);

    ui_node_add_child(root, button);
    ui_node_add_child(root, heading);
    ui_node_add_child(root, unlabeled);
    ui_commit_frame();

    const auto semantic_words = ReadSemanticBuffer();
    const auto semantic_records = ReadSemanticRecords(semantic_words);
    const auto bounds = ReadBounds(ReadCommandBuffer());

    REQUIRE(semantic_words.size() >= 1U);
    REQUIRE(semantic_words.front() == 3U);
    REQUIRE(semantic_records.size() == 3U);
    REQUIRE(bounds.find(button) != bounds.end());
    REQUIRE(bounds.find(heading) != bounds.end());
    REQUIRE(bounds.find(unlabeled) != bounds.end());

    CHECK(semantic_records[0].role == UI_SEMANTIC_BUTTON);
    CHECK(semantic_records[0].handle == button);
    CHECK(semantic_records[0].label == button_label);
    CHECK(semantic_records[0].bounds.x == Approx(bounds.at(button).x));
    CHECK(semantic_records[0].bounds.y == Approx(bounds.at(button).y));
    CHECK(semantic_records[0].bounds.width == Approx(bounds.at(button).width));
    CHECK(semantic_records[0].bounds.height == Approx(bounds.at(button).height));

    CHECK(semantic_records[1].role == UI_SEMANTIC_HEADING);
    CHECK(semantic_records[1].handle == heading);
    CHECK(semantic_records[1].label == heading_text);
    CHECK(semantic_records[1].bounds.x == Approx(bounds.at(heading).x));
    CHECK(semantic_records[1].bounds.y == Approx(bounds.at(heading).y));
    CHECK(semantic_records[1].bounds.width == Approx(bounds.at(heading).width));
    CHECK(semantic_records[1].bounds.height == Approx(bounds.at(heading).height));

    CHECK(semantic_records[2].role == UI_SEMANTIC_FORM);
    CHECK(semantic_records[2].handle == unlabeled);
    CHECK(semantic_records[2].label.empty());
    CHECK(semantic_records[2].bounds.x == Approx(bounds.at(unlabeled).x));
    CHECK(semantic_records[2].bounds.y == Approx(bounds.at(unlabeled).y));
    CHECK(semantic_records[2].bounds.width == Approx(bounds.at(unlabeled).width));
    CHECK(semantic_records[2].bounds.height == Approx(bounds.at(unlabeled).height));
}

TEST_CASE("v2 ui semantic textbox records readonly and multiline state", "[v2][ui][semantic]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t single_line = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t multiline = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(single_line != UI_INVALID_HANDLE);
    REQUIRE(multiline != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(240.0f, 140.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_width(single_line, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(single_line, 1U, 18.0f);
    ui_set_text(single_line, reinterpret_cast<const std::uint8_t*>("Editable"), 8U);
    ui_set_text_limits(single_line, std::numeric_limits<std::int32_t>::max(), 1);
    ui_set_semantic_role(single_line, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(single_line, true, 0x40007AFFU);
    ui_set_editable(single_line, true);

    ui_set_width(multiline, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(multiline, 1U, 18.0f);
    ui_set_text(multiline, reinterpret_cast<const std::uint8_t*>("Read only"), 9U);
    ui_set_text_limits(multiline, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(multiline, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(multiline, true, 0x40007AFFU);

    ui_node_add_child(root, single_line);
    ui_node_add_child(root, multiline);
    ui_commit_frame();

    const auto semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 2U);

    CHECK((semantic_records[0].state_flags & kSemanticHasReadonly) != 0U);
    CHECK((semantic_records[0].state_flags & kSemanticIsReadonly) == 0U);
    CHECK((semantic_records[0].state_flags & kSemanticHasMultiline) != 0U);
    CHECK((semantic_records[0].state_flags & kSemanticIsMultiline) == 0U);

    CHECK((semantic_records[1].state_flags & kSemanticHasReadonly) != 0U);
    CHECK((semantic_records[1].state_flags & kSemanticIsReadonly) != 0U);
    CHECK((semantic_records[1].state_flags & kSemanticHasMultiline) != 0U);
    CHECK((semantic_records[1].state_flags & kSemanticIsMultiline) != 0U);
}

TEST_CASE("v2 ui default textbox semantic labels truncate long text content", "[v2][ui][semantic]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t textbox = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(textbox != UI_INVALID_HANDLE);

    const std::string long_text(1100U, 'A');
    const std::string expected_label = std::string(1000U, 'A') + "...";

    ui_set_root(root);
    ui_resize_window(240.0f, 120.0f);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(textbox, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(textbox, 1U, 18.0f);
    ui_set_text(textbox, reinterpret_cast<const std::uint8_t*>(long_text.data()), static_cast<std::uint32_t>(long_text.size()));
    ui_set_text_limits(textbox, std::numeric_limits<std::int32_t>::max(), 0);
    ui_set_semantic_role(textbox, UI_SEMANTIC_TEXTBOX);
    ui_set_selectable(textbox, true, 0x40007AFFU);
    ui_set_editable(textbox, true);
    ui_node_add_child(root, textbox);
    ui_commit_frame();

    const auto semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 1U);
    CHECK(semantic_records.front().role == UI_SEMANTIC_TEXTBOX);
    CHECK(semantic_records.front().label == expected_label);
}

TEST_CASE("v2 ui semantic buffer clips bounds and omits fully hidden records", "[v2][ui][semantic]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t clip = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t partial = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t hidden = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(clip != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(partial != UI_INVALID_HANDLE);
    REQUIRE(hidden != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(160.0f, 100.0f);
    ui_set_width(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_width(clip, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(clip, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(clip, true);

    ui_set_width(spacer, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 28.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_width(partial, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(partial, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(partial, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(partial, reinterpret_cast<const std::uint8_t*>("Clipped"), 7U);

    ui_set_width(hidden, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(hidden, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(hidden, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(hidden, reinterpret_cast<const std::uint8_t*>("Hidden"), 6U);

    ui_node_add_child(root, clip);
    ui_node_add_child(clip, spacer);
    ui_node_add_child(clip, partial);
    ui_node_add_child(clip, hidden);
    ui_commit_frame();

    const auto semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 1U);
    CHECK(semantic_records[0].handle == partial);
    CHECK(semantic_records[0].label == "Clipped");
    CHECK(semantic_records[0].bounds.x == Approx(0.0f));
    CHECK(semantic_records[0].bounds.y == Approx(28.0f));
    CHECK(semantic_records[0].bounds.width == Approx(80.0f));
    CHECK(semantic_records[0].bounds.height == Approx(12.0f));
}

TEST_CASE("v2 ui semantic buffer follows portal paint order", "[v2][ui][semantic]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t later = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(portal_root != UI_INVALID_HANDLE);
    REQUIRE(later != UI_INVALID_HANDLE);
    REQUIRE(portal_child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(300.0f, 200.0f);
    ui_set_width(root, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 200.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_width(first, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(first, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(first, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(first, reinterpret_cast<const std::uint8_t*>("First"), 5U);

    ui_set_is_portal(portal_root, true);
    ui_set_width(portal_child, 50.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal_child, 22.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(portal_child, UI_SEMANTIC_DIALOG);
    ui_set_semantic_label(portal_child, reinterpret_cast<const std::uint8_t*>("Portal"), 6U);

    ui_set_width(later, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(later, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(later, UI_SEMANTIC_LINK);
    ui_set_semantic_label(later, reinterpret_cast<const std::uint8_t*>("Later"), 5U);

    ui_node_add_child(root, first);
    ui_node_add_child(root, portal_root);
    ui_node_add_child(root, later);
    ui_node_add_child(portal_root, portal_child);
    ui_commit_frame();

    const auto semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 3U);
    CHECK(semantic_records[0].handle == first);
    CHECK(semantic_records[1].handle == later);
    CHECK(semantic_records[2].handle == portal_child);
    CHECK(semantic_records[2].role == UI_SEMANTIC_DIALOG);
    CHECK(semantic_records[2].label == "Portal");
}

TEST_CASE("v2 ui semantic scope stack follows the topmost active modal subtree", "[v2][ui][semantic]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t background = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal_root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t dialog_panel = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t dialog_button = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t menu_panel = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t menu_item = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(background != UI_INVALID_HANDLE);
    REQUIRE(portal_root != UI_INVALID_HANDLE);
    REQUIRE(dialog_panel != UI_INVALID_HANDLE);
    REQUIRE(dialog_button != UI_INVALID_HANDLE);
    REQUIRE(menu_panel != UI_INVALID_HANDLE);
    REQUIRE(menu_item != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(320.0f, 220.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 220.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_width(background, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(background, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(background, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(background, reinterpret_cast<const std::uint8_t*>("Background"), 10U);

    ui_set_is_portal(portal_root, true);

    ui_set_width(dialog_panel, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(dialog_panel, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(dialog_panel, UI_SEMANTIC_DIALOG);
    ui_set_semantic_label(dialog_panel, reinterpret_cast<const std::uint8_t*>("Dialog"), 6U);

    ui_set_width(dialog_button, 72.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(dialog_button, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(dialog_button, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(dialog_button, reinterpret_cast<const std::uint8_t*>("OK"), 2U);

    ui_set_width(menu_panel, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(menu_panel, 48.0f, UI_SIZE_UNIT_PIXEL);

    ui_set_width(menu_item, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(menu_item, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(menu_item, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(menu_item, reinterpret_cast<const std::uint8_t*>("Reload Page"), 11U);

    ui_node_add_child(root, background);
    ui_node_add_child(root, portal_root);
    ui_node_add_child(portal_root, dialog_panel);
    ui_node_add_child(dialog_panel, dialog_button);
    ui_node_add_child(dialog_panel, menu_panel);
    ui_node_add_child(menu_panel, menu_item);

    ui_commit_frame();
    auto semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 4U);
    CHECK(semantic_records[0].handle == background);
    CHECK(semantic_records[1].handle == dialog_panel);
    CHECK(semantic_records[2].handle == dialog_button);
    CHECK(semantic_records[3].handle == menu_item);

    const std::uint32_t dialog_scope = ui_push_semantic_scope(dialog_panel);
    REQUIRE(dialog_scope != 0U);
    ui_commit_frame();
    semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 3U);
    CHECK(semantic_records[0].handle == dialog_panel);
    CHECK(semantic_records[1].handle == dialog_button);
    CHECK(semantic_records[2].handle == menu_item);

    const std::uint32_t menu_scope = ui_push_semantic_scope(menu_panel);
    REQUIRE(menu_scope != 0U);
    ui_commit_frame();
    semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 1U);
    CHECK(semantic_records[0].handle == menu_item);

    ui_remove_semantic_scope(menu_scope);
    ui_commit_frame();
    semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 3U);
    CHECK(semantic_records[0].handle == dialog_panel);
    CHECK(semantic_records[1].handle == dialog_button);
    CHECK(semantic_records[2].handle == menu_item);

    ui_remove_semantic_scope(dialog_scope);
    ui_commit_frame();
    semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 4U);
    CHECK(semantic_records[0].handle == background);
    CHECK(semantic_records[1].handle == dialog_panel);
    CHECK(semantic_records[2].handle == dialog_button);
    CHECK(semantic_records[3].handle == menu_item);
}

TEST_CASE("v2 ui semantic buffer omits hidden and collapsed nodes", "[v2][ui][semantic]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t visible = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t hidden = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t collapsed = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(visible != UI_INVALID_HANDLE);
    REQUIRE(hidden != UI_INVALID_HANDLE);
    REQUIRE(collapsed != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_resize_window(260.0f, 120.0f);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(root, 1U);

    ui_set_width(visible, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(visible, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(visible, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(visible, reinterpret_cast<const std::uint8_t*>("Visible"), 7U);

    ui_set_width(hidden, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(hidden, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(hidden, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(hidden, reinterpret_cast<const std::uint8_t*>("Hidden"), 6U);
    ui_set_visibility(hidden, UI_VISIBILITY_HIDDEN);

    ui_set_width(collapsed, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(collapsed, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_semantic_role(collapsed, UI_SEMANTIC_BUTTON);
    ui_set_semantic_label(collapsed, reinterpret_cast<const std::uint8_t*>("Collapsed"), 9U);
    ui_set_visibility(collapsed, UI_VISIBILITY_COLLAPSED);

    ui_node_add_child(root, visible);
    ui_node_add_child(root, hidden);
    ui_node_add_child(root, collapsed);
    ui_commit_frame();

    auto semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 1U);
    CHECK(semantic_records.front().handle == visible);
    CHECK(semantic_records.front().label == "Visible");

    ui_set_visibility(hidden, UI_VISIBILITY_NORMAL);
    ui_commit_frame();
    semantic_records = ReadSemanticRecords(ReadSemanticBuffer());
    REQUIRE(semantic_records.size() == 2U);
    CHECK(semantic_records[0].handle == visible);
    CHECK(semantic_records[1].handle == hidden);
}
