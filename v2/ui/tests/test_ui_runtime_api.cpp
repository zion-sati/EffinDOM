#include "TestUiSupport.h"

TEST_CASE("v2 ui create_node exhausts the pool without duplicate indices", "[v2][ui][unit]") {
    ui_reset();

    std::set<std::uint32_t> indices{};
    for (std::size_t count = 1; count < effindom::v2::ui::kMaxNodes; count += 1) {
        const std::uint64_t handle = ui_create_node(UI_NODE_FLEX_BOX);
        REQUIRE(handle != UI_INVALID_HANDLE);
        indices.insert(HandleIndex(handle));
    }

    CHECK(indices.size() == effindom::v2::ui::kMaxNodes - 1U);
    CHECK(indices.count(0U) == 0U);
    CHECK(ui_create_node(UI_NODE_FLEX_BOX) == UI_INVALID_HANDLE);
}


TEST_CASE("v2 ui reuses indices with a newer generation after delete", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t first = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(first != UI_INVALID_HANDLE);

    ui_delete_node(first);

    const std::uint64_t second = ui_create_node(UI_NODE_TEXT);
    REQUIRE(second != UI_INVALID_HANDLE);

    CHECK(HandleIndex(second) == HandleIndex(first));
    CHECK(HandleGeneration(second) == HandleGeneration(first) + 1U);
    CHECK(second != first);
}


TEST_CASE("v2 ui runtime emits retained box style layer effect and gradient commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_box_style(root, 0xff112233U, 4.0f, 5.0f, 6.0f, 7.0f, 2.0f, 0xff445566U, ED_BORDER_DASHED, 8.0f, 3.0f);
    ui_set_layer_effect(root, 0.5f, 1.5f, ED_BLEND_MULTIPLY);
    const float offsets[] = {0.0f, 1.0f};
    const ui_color_t colors[] = {0xff0000ffU, 0x00ff00ffU};
    ui_set_linear_gradient(root, 10.0f, 20.0f, 30.0f, 40.0f, 2U, offsets, colors);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_BOX_STYLE) == 1U);
    CHECK(CountCommand(words, CMD_SET_LAYER_EFFECT) == 1U);
    CHECK(CountCommand(words, CMD_SET_LINEAR_GRADIENT) == 1U);

    const auto box_it = std::find(words.begin(), words.end(), CMD_SET_BOX_STYLE);
    REQUIRE(box_it != words.end());
    const std::size_t box_index = static_cast<std::size_t>(std::distance(words.begin(), box_it));
    CHECK(words[box_index + 3U] == 0xff112233U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[box_index + 4U]) == Approx(4.0f));
    CHECK(words[box_index + 10U] == ED_BORDER_DASHED);

    const auto layer_it = std::find(words.begin(), words.end(), CMD_SET_LAYER_EFFECT);
    REQUIRE(layer_it != words.end());
    const std::size_t layer_index = static_cast<std::size_t>(std::distance(words.begin(), layer_it));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[layer_index + 3U]) == Approx(0.5f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[layer_index + 4U]) == Approx(1.5f));
    CHECK(words[layer_index + 5U] == ED_BLEND_MULTIPLY);

    const auto gradient_it = std::find(words.begin(), words.end(), CMD_SET_LINEAR_GRADIENT);
    REQUIRE(gradient_it != words.end());
    const std::size_t gradient_index = static_cast<std::size_t>(std::distance(words.begin(), gradient_it));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[gradient_index + 3U]) == Approx(10.0f));
    CHECK(words[gradient_index + 7U] == 2U);
    CHECK(words[gradient_index + 9U] == 0xff0000ffU);
    CHECK(words[gradient_index + 11U] == 0x00ff00ffU);
}


TEST_CASE("v2 ui runtime emits retained background blur commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_background_blur(root, 9.0f);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_BACKGROUND_BLUR) == 1U);
    const auto backdrop_it = std::find(words.begin(), words.end(), CMD_SET_BACKGROUND_BLUR);
    REQUIRE(backdrop_it != words.end());
    const std::size_t backdrop_index = static_cast<std::size_t>(std::distance(words.begin(), backdrop_it));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[backdrop_index + 3U]) == Approx(9.0f));
}


TEST_CASE("v2 ui runtime emits retained drop shadow commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_drop_shadow(root, 0x00000044U, 2.0f, 6.0f, 18.0f, 4.0f);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_DROP_SHADOW) == 1U);
    const auto shadow_it = std::find(words.begin(), words.end(), CMD_SET_DROP_SHADOW);
    REQUIRE(shadow_it != words.end());
    const std::size_t shadow_index = static_cast<std::size_t>(std::distance(words.begin(), shadow_it));
    CHECK(words[shadow_index + 3U] == 0x00000044U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 4U]) == Approx(2.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 5U]) == Approx(6.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 6U]) == Approx(18.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[shadow_index + 7U]) == Approx(4.0f));
}


TEST_CASE("v2 ui runtime emits retained image and svg commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t image = ui_create_node(UI_NODE_IMAGE);
    const std::uint64_t svg = ui_create_node(UI_NODE_SVG);
    REQUIRE(image != UI_INVALID_HANDLE);
    REQUIRE(svg != UI_INVALID_HANDLE);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(image, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(image, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(svg, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(svg, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, image);
    ui_node_add_child(root, svg);

    ui_set_image(image, 21U, ED_OBJECT_FIT_COVER, ED_IMAGE_SAMPLING_NEAREST, 0U);
    ui_set_svg(svg, 22U, 0xff1122ffU, ED_IMAGE_SAMPLING_CUBIC_MITCHELL, 0U);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_IMAGE) == 1U);
    CHECK(CountCommand(words, CMD_SET_SVG) == 1U);

    const auto image_it = std::find(words.begin(), words.end(), CMD_SET_IMAGE);
    REQUIRE(image_it != words.end());
    const std::size_t image_index = static_cast<std::size_t>(std::distance(words.begin(), image_it));
    CHECK(words[image_index + 3U] == 21U);
    CHECK(words[image_index + 4U] == ED_OBJECT_FIT_COVER);
    CHECK(words[image_index + 5U] == ED_IMAGE_SAMPLING_NEAREST);
    CHECK(words[image_index + 6U] == 8U);

    const auto svg_it = std::find(words.begin(), words.end(), CMD_SET_SVG);
    REQUIRE(svg_it != words.end());
    const std::size_t svg_index = static_cast<std::size_t>(std::distance(words.begin(), svg_it));
    CHECK(words[svg_index + 3U] == 22U);
    CHECK(words[svg_index + 4U] == 0xff1122ffU);
    CHECK(words[svg_index + 5U] == ED_IMAGE_SAMPLING_CUBIC_MITCHELL);
    CHECK(words[svg_index + 6U] == 8U);

    ui_set_svg(svg, 0U, 0xff1122ffU, ED_IMAGE_SAMPLING_LINEAR, 0U);
    ui_commit_frame();
    const std::vector<std::uint32_t> cleared_words = ReadCommandBuffer();
    CHECK(CountCommand(cleared_words, CMD_SET_SVG) == 1U);
    const auto cleared_svg_it = std::find(cleared_words.begin(), cleared_words.end(), CMD_SET_SVG);
    REQUIRE(cleared_svg_it != cleared_words.end());
    const std::size_t cleared_svg_index = static_cast<std::size_t>(std::distance(cleared_words.begin(), cleared_svg_it));
    CHECK(cleared_words[cleared_svg_index + 3U] == 0U);
    CHECK(cleared_words[cleared_svg_index + 4U] == 0xff1122ffU);
    CHECK(cleared_words[cleared_svg_index + 5U] == ED_IMAGE_SAMPLING_LINEAR);
    CHECK(cleared_words[cleared_svg_index + 6U] == 8U);
}


TEST_CASE("v2 ui runtime emits retained nine-patch image commands", "[v2][ui][unit]") {
    ui_reset();

    const std::uint64_t image = ui_create_node(UI_NODE_IMAGE);
    REQUIRE(image != UI_INVALID_HANDLE);
    ui_set_root(image);
    ui_set_width(image, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(image, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_image_nine(image, 33U, 4.0f, 5.0f, 6.0f, 7.0f, ED_IMAGE_SAMPLING_NEAREST, 0U);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();

    CHECK(CountCommand(words, CMD_SET_IMAGE_NINE) == 1U);
    const auto image_nine_it = std::find(words.begin(), words.end(), CMD_SET_IMAGE_NINE);
    REQUIRE(image_nine_it != words.end());
    const std::size_t image_nine_index = static_cast<std::size_t>(std::distance(words.begin(), image_nine_it));
    CHECK(words[image_nine_index + 3U] == 33U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[image_nine_index + 4U]) == Approx(4.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[image_nine_index + 7U]) == Approx(7.0f));
    CHECK(words[image_nine_index + 8U] == ED_IMAGE_SAMPLING_NEAREST);
    CHECK(words[image_nine_index + 9U] == 8U);
}


TEST_CASE("v2 ui arena api resets and deleting root clears the tree", "[v2][ui][unit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    CHECK(ui_arena_alloc(0U) == 0U);

    const std::uintptr_t first = ui_arena_alloc(16U);
    const std::uintptr_t second = ui_arena_alloc(32U);
    REQUIRE(first != 0U);
    REQUIRE(second != 0U);
    CHECK(second > first);
    CHECK(ui_arena_alloc(static_cast<std::uint32_t>(effindom::v2::ui::kFrameArenaCapacity)) == 0U);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_width(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(child, 10.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(child, 10.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, child);
    ui_commit_frame();
    CHECK(ui_arena_alloc(16U) == first);

    auto* mutable_root = const_cast<effindom::v2::ui::UINode*>(GetRuntime().Resolve(root));
    REQUIRE(mutable_root != nullptr);
    mutable_root->children.push_back(UI_INVALID_HANDLE);
    ui_commit_frame();

    ui_delete_node(root);
    CHECK(GetRuntime().root_handle() == UI_INVALID_HANDLE);
    CHECK(GetRuntime().Resolve(child)->parent_handle == UI_INVALID_HANDLE);

    ui_commit_frame();
    const std::vector<std::uint32_t> words = ReadCommandBuffer();
    CHECK(CountCommand(words, CMD_DELETE_NODE) == 1U);
    CHECK(CountCommand(words, CMD_COMMIT_PAINT_ORDER) == 1U);
    CHECK(CountCommand(words, CMD_COMMIT_SCENE) == 1U);

    ui_reset();
    CHECK(GetRuntime().root_handle() == UI_INVALID_HANDLE);
    CHECK(GetRuntime().window_width() == Approx(800.0f));
    CHECK(GetRuntime().window_height() == Approx(600.0f));
}
