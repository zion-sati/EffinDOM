#include "TestUiSupport.h"

TEST_CASE("v2 ui portal children are deferred after later main-tree siblings", "[v2][ui][unit]") {
    ui_reset();

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
    ui_node_add_child(root, child_a);
    ui_node_add_child(root, child_b);
    ui_node_add_child(child_b, child_c);
    ui_node_add_child(root, child_d);
    ui_set_is_portal(child_b, true);

    ui_commit_frame();
    const auto paint_order = ReadPaintOrder(ReadCommandBuffer());

    REQUIRE(paint_order == std::vector<std::uint64_t>{root, child_a, child_b, child_d, child_c});
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_b) == 1);
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_c) == 1);
}


TEST_CASE("v2 ui portal children added after the initial frame still render in the deferred scene", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(portal, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_node_add_child(root, portal);
    ui_commit_frame();

    const std::uint64_t portal_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(portal_child != UI_INVALID_HANDLE);
    ui_set_width(portal_child, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal_child, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(portal, portal_child);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto portal_child_bounds = bounds.find(portal_child);
    REQUIRE(portal_child_bounds != bounds.end());
    CHECK(portal_child_bounds->second.x == Approx(0.0f));
    CHECK(portal_child_bounds->second.y == Approx(0.0f));

    const auto portal_scene = ReadScene(ReadCommandBuffer());
    REQUIRE(std::count_if(
                portal_scene.begin(),
                portal_scene.end(),
                [portal_child](const effindom::v2::ui::SceneInstruction& instruction) {
                    return instruction.opcode == OP_DRAW_NODE && instruction.handle == portal_child;
                }) == 1);
}


TEST_CASE("v2 ui portal children added after the initial frame keep absolute popup geometry", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t overlay = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t panel = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(overlay != UI_INVALID_HANDLE);
    REQUIRE(panel != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_set_position_type(portal, UI_POSITION_ABSOLUTE);
    ui_set_position(portal, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(portal, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, portal);
    ui_commit_frame();

    ui_set_position_type(overlay, UI_POSITION_ABSOLUTE);
    ui_set_position(overlay, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(overlay, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(overlay, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_position_type(panel, UI_POSITION_ABSOLUTE);
    ui_set_position(panel, 40.0f, 52.0f, NAN, NAN);
    ui_set_width(panel, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(panel, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(overlay, panel);
    ui_node_add_child(portal, overlay);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto overlay_bounds = bounds.find(overlay);
    const auto panel_bounds = bounds.find(panel);
    REQUIRE(overlay_bounds != bounds.end());
    REQUIRE(panel_bounds != bounds.end());
    CHECK(overlay_bounds->second.x == Approx(0.0f));
    CHECK(overlay_bounds->second.y == Approx(0.0f));
    CHECK(overlay_bounds->second.width == Approx(200.0f));
    CHECK(overlay_bounds->second.height == Approx(120.0f));
    CHECK(panel_bounds->second.x == Approx(40.0f));
    CHECK(panel_bounds->second.y == Approx(52.0f));
    CHECK(panel_bounds->second.width == Approx(120.0f));
    CHECK(panel_bounds->second.height == Approx(32.0f));

    const auto scene = ReadScene(ReadCommandBuffer());
    REQUIRE(std::count_if(
                scene.begin(),
                scene.end(),
                [overlay](const effindom::v2::ui::SceneInstruction& instruction) {
                    return instruction.opcode == OP_DRAW_NODE && instruction.handle == overlay;
                }) == 1);
    REQUIRE(std::count_if(
                scene.begin(),
                scene.end(),
                [panel](const effindom::v2::ui::SceneInstruction& instruction) {
                    return instruction.opcode == OP_DRAW_NODE && instruction.handle == panel;
                }) == 1);
}


TEST_CASE("v2 ui portal popup nodes added after the initial frame still emit first-frame paint data", "[v2][ui][unit]") {
    ui_reset();
    RegisterTestFont();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t panel = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t label = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(panel != UI_INVALID_HANDLE);
    REQUIRE(label != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_set_position_type(portal, UI_POSITION_ABSOLUTE);
    ui_set_position(portal, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(portal, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, portal);
    ui_commit_frame();

    ui_set_position_type(panel, UI_POSITION_ABSOLUTE);
    ui_set_position(panel, 24.0f, 36.0f, NAN, NAN);
    ui_set_width(panel, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(panel, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_box_style(panel, 0xff223344U, 6.0f, 6.0f, 6.0f, 6.0f, 0.0f, 0U, ED_BORDER_SOLID, 0.0f, 0.0f);
    ui_set_font(label, 1U, 18.0f);
    ui_set_text(label, reinterpret_cast<const std::uint8_t*>("Popup"), 5U);
    ui_set_text_color(label, 0xffffffffU);
    ui_node_add_child(panel, label);
    ui_node_add_child(portal, panel);
    ui_commit_frame();

    const auto words = ReadCommandBuffer();
    CHECK(CountCommand(words, CMD_SET_BOX_STYLE) == 1U);
    const auto glyph_runs = ReadGlyphRuns(words);
    const auto label_run = glyph_runs.find(label);
    REQUIRE(label_run != glyph_runs.end());
    CHECK(label_run->second.font_id == 1U);
    CHECK(label_run->second.font_size == Approx(18.0f));
    CHECK_FALSE(label_run->second.glyphs.empty());
}


TEST_CASE("v2 ui deferred portal children escape a clipped parent after the clip pop", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t owner = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t inline_child = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t popup = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(owner != UI_INVALID_HANDLE);
    REQUIRE(inline_child != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(popup != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(owner, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(owner, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(owner, true);
    ui_set_width(inline_child, 24.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(inline_child, 16.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_is_portal(portal, true);
    ui_set_position_type(portal, UI_POSITION_ABSOLUTE);
    ui_set_position(portal, 0.0f, 0.0f, NAN, NAN);
    ui_set_width(portal, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_position_type(popup, UI_POSITION_ABSOLUTE);
    ui_set_position(popup, 0.0f, 40.0f, NAN, NAN);
    ui_set_width(popup, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(popup, 48.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(popup, true);
    ui_node_add_child(root, owner);
    ui_node_add_child(owner, inline_child);
    ui_node_add_child(owner, portal);
    ui_node_add_child(portal, popup);
    ui_commit_frame();

    const auto scene = ReadScene(ReadCommandBuffer());
    const auto owner_clip = std::find_if(
        scene.begin(),
        scene.end(),
        [owner](const effindom::v2::ui::SceneInstruction& instruction) {
            return instruction.opcode == OP_PUSH_CLIP && instruction.handle == owner;
        });
    const auto owner_pop = std::find_if(
        scene.begin(),
        scene.end(),
        [](const effindom::v2::ui::SceneInstruction& instruction) {
            return instruction.opcode == OP_POP && instruction.handle == UI_INVALID_HANDLE;
        });
    const auto popup_draw = std::find_if(
        scene.begin(),
        scene.end(),
        [popup](const effindom::v2::ui::SceneInstruction& instruction) {
            return instruction.opcode == OP_DRAW_NODE && instruction.handle == popup;
        });

    REQUIRE(owner_clip != scene.end());
    REQUIRE(owner_pop != scene.end());
    REQUIRE(popup_draw != scene.end());
    CHECK(owner_clip < owner_pop);
    CHECK(owner_pop < popup_draw);

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto hit_bounds = ReadHitBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(popup) != bounds.end());
    REQUIRE(hit_bounds.find(popup) != hit_bounds.end());
    CHECK(bounds.at(popup).x == Approx(0.0f));
    CHECK(bounds.at(popup).y == Approx(40.0f));
    CHECK(bounds.at(popup).width == Approx(120.0f));
    CHECK(bounds.at(popup).height == Approx(48.0f));
    CHECK(hit_bounds.at(popup).x == Approx(0.0f));
    CHECK(hit_bounds.at(popup).y == Approx(40.0f));
    CHECK(hit_bounds.at(popup).width == Approx(120.0f));
    CHECK(hit_bounds.at(popup).height == Approx(48.0f));
}


TEST_CASE("v2 ui deferred portal children use viewport-space bounds inside a scrolled ancestor", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t owner = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t portal = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t popup = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(owner != UI_INVALID_HANDLE);
    REQUIRE(portal != UI_INVALID_HANDLE);
    REQUIRE(popup != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(spacer, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(owner, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(owner, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_clip_to_bounds(owner, true);
    ui_set_is_portal(portal, true);
    ui_set_width(portal, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(portal, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_position_type(popup, UI_POSITION_ABSOLUTE);
    ui_set_position(popup, 0.0f, 40.0f, NAN, NAN);
    ui_set_width(popup, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(popup, 48.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_interactive(popup, true);

    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, owner);
    ui_node_add_child(owner, portal);
    ui_node_add_child(portal, popup);

    ui_set_scroll_offset(scroll, 0.0f, 60.0f);
    ui_commit_frame();

    const auto bounds = ReadBounds(ReadCommandBuffer());
    const auto hit_bounds = ReadHitBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(popup) != bounds.end());
    REQUIRE(hit_bounds.find(popup) != hit_bounds.end());
    CHECK(bounds.at(popup).x == Approx(0.0f));
    CHECK(bounds.at(popup).y == Approx(60.0f));
    CHECK(bounds.at(popup).width == Approx(120.0f));
    CHECK(bounds.at(popup).height == Approx(48.0f));
    CHECK(hit_bounds.at(popup).x == Approx(0.0f));
    CHECK(hit_bounds.at(popup).y == Approx(60.0f));
    CHECK(hit_bounds.at(popup).width == Approx(120.0f));
    CHECK(hit_bounds.at(popup).height == Approx(48.0f));
}


TEST_CASE("v2 ui nested portals stay deferred in portal DFS order", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_b = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_c = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_d = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child_e = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child_a != UI_INVALID_HANDLE);
    REQUIRE(child_b != UI_INVALID_HANDLE);
    REQUIRE(child_c != UI_INVALID_HANDLE);
    REQUIRE(child_d != UI_INVALID_HANDLE);
    REQUIRE(child_e != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_node_add_child(root, child_a);
    ui_node_add_child(root, child_b);
    ui_node_add_child(child_b, child_c);
    ui_node_add_child(child_c, child_d);
    ui_node_add_child(root, child_e);
    ui_set_is_portal(child_b, true);
    ui_set_is_portal(child_c, true);

    ui_commit_frame();
    const auto paint_order = ReadPaintOrder(ReadCommandBuffer());

    REQUIRE(paint_order == std::vector<std::uint64_t>{root, child_a, child_b, child_e, child_c, child_d});
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_b) == 1);
    CHECK(std::count(paint_order.begin(), paint_order.end(), child_c) == 1);
}


