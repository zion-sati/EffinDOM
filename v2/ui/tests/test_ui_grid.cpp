#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CommandBuilder.h"
#define private public
#include "UiRuntime.h"
#undef private
#include "effindom_ui.h"

#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using Catch::Approx;

#ifndef EFFINDOM_SOURCE_DIR
#define EFFINDOM_SOURCE_DIR "."
#endif

namespace {

std::vector<std::uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.is_open());
    stream.seekg(0, std::ios::end);
    const std::streamsize size = stream.tellg();
    REQUIRE(size >= 0);
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        REQUIRE(stream.good());
    }
    return bytes;
}

std::vector<std::uint32_t> ReadCommandBuffer() {
    std::uint32_t word_count = 0;
    const std::uint32_t* words = ui_get_command_buffer(&word_count);
    if (words == nullptr || word_count == 0U) {
        return {};
    }
    return std::vector<std::uint32_t>(words, words + word_count);
}

struct Bounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

std::unordered_map<std::uint64_t, Bounds> ReadBounds(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, Bounds> result{};
    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            result[handle] = Bounds{
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 3U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 4U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 5U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 6U]),
            };
            i += 16U;
            break;
        }
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }
    return result;
}

void RegisterNotoSans() {
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
}

} // namespace

TEST_CASE("v2 ui grid lays out fixed pixel tracks", "[v2][ui][grid]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t b = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(a != UI_INVALID_HANDLE);
    REQUIRE(b != UI_INVALID_HANDLE);

    const float columns[] = {100.0f, 120.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};
    const float rows[] = {30.0f, 40.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};

    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(grid, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 70.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(grid, 2U, columns, column_types);
    ui_grid_set_rows(grid, 2U, rows, row_types);
    ui_node_add_child(root, grid);
    ui_node_add_child(grid, a);
    ui_node_add_child(grid, b);
    ui_node_set_grid_placement(a, 0U, 0U, 1U, 1U);
    ui_node_set_grid_placement(b, 1U, 1U, 1U, 1U);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(a) != bounds.end());
    REQUIRE(bounds.find(b) != bounds.end());

    CHECK(bounds.at(a).x == Approx(0.0f));
    CHECK(bounds.at(a).y == Approx(0.0f));
    CHECK(bounds.at(a).width == Approx(100.0f));
    CHECK(bounds.at(a).height == Approx(30.0f));
    CHECK(bounds.at(b).x == Approx(100.0f));
    CHECK(bounds.at(b).y == Approx(30.0f));
    CHECK(bounds.at(b).width == Approx(120.0f));
    CHECK(bounds.at(b).height == Approx(40.0f));
}

TEST_CASE("v2 ui grid applies padding to track layout", "[v2][ui][grid]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t b = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(a != UI_INVALID_HANDLE);
    REQUIRE(b != UI_INVALID_HANDLE);

    const float columns[] = {100.0f, 120.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};
    const float rows[] = {30.0f, 40.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};

    ui_set_root(root);
    ui_set_width(root, 260.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(grid, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_padding(grid, 12.0f, 6.0f, 8.0f, 4.0f);
    ui_grid_set_columns(grid, 2U, columns, column_types);
    ui_grid_set_rows(grid, 2U, rows, row_types);
    ui_node_add_child(root, grid);
    ui_node_add_child(grid, a);
    ui_node_add_child(grid, b);
    ui_node_set_grid_placement(a, 0U, 0U, 1U, 1U);
    ui_node_set_grid_placement(b, 1U, 1U, 1U, 1U);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(a) != bounds.end());
    REQUIRE(bounds.find(b) != bounds.end());

    CHECK(bounds.at(a).x == Approx(12.0f));
    CHECK(bounds.at(a).y == Approx(6.0f));
    CHECK(bounds.at(a).width == Approx(100.0f));
    CHECK(bounds.at(a).height == Approx(30.0f));
    CHECK(bounds.at(b).x == Approx(112.0f));
    CHECK(bounds.at(b).y == Approx(36.0f));
    CHECK(bounds.at(b).width == Approx(120.0f));
    CHECK(bounds.at(b).height == Approx(40.0f));
}

TEST_CASE("v2 ui grid distributes star columns and mixed tracks", "[v2][ui][grid]") {
    ui_reset();
    RegisterNotoSans();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t sidebar = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t title = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(sidebar != UI_INVALID_HANDLE);
    REQUIRE(title != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);

    const float columns[] = {80.0f, 0.0f, 2.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_AUTO, UI_GRID_UNIT_STAR};
    const float rows[] = {40.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_PIXEL};

    constexpr const char* kTitle = "Long title";
    ui_set_root(root);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(grid, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(grid, 3U, columns, column_types);
    ui_grid_set_rows(grid, 1U, rows, row_types);
    ui_set_font(title, 1U, 20.0f);
    ui_set_text(title, reinterpret_cast<const std::uint8_t*>(kTitle), static_cast<std::uint32_t>(std::strlen(kTitle)));
    ui_node_add_child(root, grid);
    ui_node_add_child(grid, sidebar);
    ui_node_add_child(grid, title);
    ui_node_add_child(grid, content);
    ui_node_set_grid_placement(sidebar, 0U, 0U, 1U, 1U);
    ui_node_set_grid_placement(title, 0U, 1U, 1U, 1U);
    ui_node_set_grid_placement(content, 0U, 2U, 1U, 1U);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(sidebar) != bounds.end());
    REQUIRE(bounds.find(title) != bounds.end());
    REQUIRE(bounds.find(content) != bounds.end());

    const float auto_width = bounds.at(content).x - bounds.at(title).x;
    CHECK(bounds.at(sidebar).width == Approx(80.0f));
    CHECK(auto_width > 0.0f);
    CHECK(bounds.at(content).width == Approx(320.0f - 80.0f - auto_width));
}

TEST_CASE("v2 ui grid derives auto row height from wrapped text", "[v2][ui][grid]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterNotoSans();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    const float columns[] = {90.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_PIXEL};
    const float rows[] = {0.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_AUTO};
    constexpr const char* kWrapped = "The quick brown fox jumps over the lazy dog";

    ui_set_root(root);
    ui_set_width(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(grid, 90.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(grid, 1U, columns, column_types);
    ui_grid_set_rows(grid, 1U, rows, row_types);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kWrapped), static_cast<std::uint32_t>(std::strlen(kWrapped)));
    ui_node_add_child(root, grid);
    ui_node_add_child(grid, text);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(text) != bounds.end());
    const auto* text_node = GetRuntime().Resolve(text);
    REQUIRE(text_node != nullptr);
    CHECK(text_node->visible_line_count > 1U);
    CHECK(bounds.at(text).height == Approx(text_node->layout_height));
    CHECK(bounds.at(text).height > (text_node->font_size * 1.5f));
}

TEST_CASE("v2 ui grid supports spanning across tracks", "[v2][ui][grid]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t span = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(span != UI_INVALID_HANDLE);

    const float columns[] = {50.0f, 70.0f, 80.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};
    const float rows[] = {20.0f, 30.0f, 40.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};

    ui_set_root(root);
    ui_set_width(root, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(grid, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 90.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(grid, 3U, columns, column_types);
    ui_grid_set_rows(grid, 3U, rows, row_types);
    ui_node_add_child(root, grid);
    ui_node_add_child(grid, span);
    ui_node_set_grid_placement(span, 1U, 1U, 2U, 2U);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(span) != bounds.end());
    CHECK(bounds.at(span).x == Approx(50.0f));
    CHECK(bounds.at(span).y == Approx(20.0f));
    CHECK(bounds.at(span).width == Approx(150.0f));
    CHECK(bounds.at(span).height == Approx(70.0f));
}

TEST_CASE("v2 ui grid supports star rows and nested grids", "[v2][ui][grid]") {
    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t outer = ui_create_node(UI_NODE_GRID);
    const std::uint64_t inner = ui_create_node(UI_NODE_GRID);
    const std::uint64_t leaf_a = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t leaf_b = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(outer != UI_INVALID_HANDLE);
    REQUIRE(inner != UI_INVALID_HANDLE);
    REQUIRE(leaf_a != UI_INVALID_HANDLE);
    REQUIRE(leaf_b != UI_INVALID_HANDLE);

    const float outer_columns[] = {120.0f};
    const std::uint8_t outer_column_types[] = {UI_GRID_UNIT_PIXEL};
    const float outer_rows[] = {1.0f, 2.0f};
    const std::uint8_t outer_row_types[] = {UI_GRID_UNIT_STAR, UI_GRID_UNIT_STAR};
    const float inner_columns[] = {40.0f, 60.0f};
    const std::uint8_t inner_column_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};
    const float inner_rows[] = {30.0f};
    const std::uint8_t inner_row_types[] = {UI_GRID_UNIT_PIXEL};

    ui_set_root(root);
    ui_set_width(root, 140.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(outer, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer, 150.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(outer, 1U, outer_columns, outer_column_types);
    ui_grid_set_rows(outer, 2U, outer_rows, outer_row_types);
    ui_grid_set_columns(inner, 2U, inner_columns, inner_column_types);
    ui_grid_set_rows(inner, 1U, inner_rows, inner_row_types);
    ui_node_add_child(root, outer);
    ui_node_add_child(outer, inner);
    ui_node_add_child(inner, leaf_a);
    ui_node_add_child(inner, leaf_b);
    ui_node_set_grid_placement(inner, 1U, 0U, 1U, 1U);
    ui_node_set_grid_placement(leaf_a, 0U, 0U, 1U, 1U);
    ui_node_set_grid_placement(leaf_b, 0U, 1U, 1U, 1U);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(inner) != bounds.end());
    REQUIRE(bounds.find(leaf_a) != bounds.end());
    REQUIRE(bounds.find(leaf_b) != bounds.end());

    CHECK(bounds.at(inner).y == Approx(50.0f));
    CHECK(bounds.at(inner).height == Approx(100.0f));
    CHECK(bounds.at(leaf_a).x == Approx(0.0f));
    CHECK(bounds.at(leaf_b).x == Approx(40.0f));
    CHECK(bounds.at(leaf_a).width == Approx(40.0f));
    CHECK(bounds.at(leaf_b).width == Approx(60.0f));
}

TEST_CASE("v2 ui grid helper paths restore point percent and undefined styles", "[v2][ui][grid]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PackHandle;

    ui_reset();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t point_child = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t percent_child = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t undefined_child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(point_child != UI_INVALID_HANDLE);
    REQUIRE(percent_child != UI_INVALID_HANDLE);
    REQUIRE(undefined_child != UI_INVALID_HANDLE);

    const float columns[] = {60.0f, 60.0f, 60.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL, UI_GRID_UNIT_PIXEL};
    const float rows[] = {40.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_PIXEL};

    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(grid, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(grid, 3U, columns, column_types);
    ui_grid_set_rows(grid, 1U, rows, row_types);
    ui_set_width(point_child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(point_child, 10.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(percent_child, 50.0f, UI_SIZE_UNIT_PERCENT);
    ui_set_height(percent_child, 75.0f, UI_SIZE_UNIT_PERCENT);
    ui_node_add_child(root, grid);
    ui_node_add_child(grid, point_child);
    ui_node_add_child(grid, percent_child);
    ui_node_add_child(grid, undefined_child);
    ui_node_set_grid_placement(point_child, 0U, 0U, 1U, 1U);
    ui_node_set_grid_placement(percent_child, 0U, 1U, 1U, 1U);
    ui_node_set_grid_placement(undefined_child, 0U, 2U, 1U, 1U);

    auto* undefined_node = const_cast<effindom::v2::ui::UINode*>(GetRuntime().Resolve(undefined_child));
    auto* grid_node = const_cast<effindom::v2::ui::UINode*>(GetRuntime().Resolve(grid));
    REQUIRE(undefined_node != nullptr);
    REQUIRE(grid_node != nullptr);
    YGNodeStyleSetWidth(undefined_node->yg_node, YGUndefined);
    YGNodeStyleSetHeight(undefined_node->yg_node, YGUndefined);
    grid_node->children.push_back(PackHandle(0U, 1U));

    ui_commit_frame();

    CHECK(YGNodeStyleGetWidth(GetRuntime().Resolve(point_child)->yg_node).unit == YGUnitPoint);
    CHECK(YGNodeStyleGetHeight(GetRuntime().Resolve(point_child)->yg_node).unit == YGUnitPoint);
    CHECK(YGNodeStyleGetWidth(GetRuntime().Resolve(percent_child)->yg_node).unit == YGUnitPercent);
    CHECK(YGNodeStyleGetHeight(GetRuntime().Resolve(percent_child)->yg_node).unit == YGUnitPercent);
    CHECK(YGNodeStyleGetWidth(GetRuntime().Resolve(undefined_child)->yg_node).unit == YGUnitUndefined);
    CHECK(YGNodeStyleGetHeight(GetRuntime().Resolve(undefined_child)->yg_node).unit == YGUnitUndefined);
}

TEST_CASE("v2 ui grid distributes spanning auto tracks before zeroing remaining star space", "[v2][ui][grid]") {
    ui_reset();
    RegisterNotoSans();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t grid = ui_create_node(UI_NODE_GRID);
    const std::uint64_t span = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t star = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(grid != UI_INVALID_HANDLE);
    REQUIRE(span != UI_INVALID_HANDLE);
    REQUIRE(star != UI_INVALID_HANDLE);

    const float columns[] = {0.0f, 0.0f, 1.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_AUTO, UI_GRID_UNIT_AUTO, UI_GRID_UNIT_STAR};
    const float rows[] = {30.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_PIXEL};

    ui_set_root(root);
    ui_set_width(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(grid, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(grid, 30.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(grid, 3U, columns, column_types);
    ui_grid_set_rows(grid, 1U, rows, row_types);
    constexpr const char* kWide = "Wide auto span";
    ui_set_font(span, 1U, 20.0f);
    ui_set_text(span, reinterpret_cast<const std::uint8_t*>(kWide), static_cast<std::uint32_t>(std::strlen(kWide)));
    ui_node_add_child(root, grid);
    ui_node_add_child(grid, span);
    ui_node_add_child(grid, star);
    ui_node_set_grid_placement(span, 0U, 0U, 1U, 2U);
    ui_node_set_grid_placement(star, 0U, 2U, 1U, 1U);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(span) != bounds.end());
    const auto* star_node = effindom::v2::ui::GetRuntime().Resolve(star);
    REQUIRE(star_node != nullptr);

    CHECK(bounds.at(span).width > 80.0f);
    CHECK(star_node->abs_x == Approx(bounds.at(span).width));
    CHECK(star_node->layout_width == Approx(0.0f));
    CHECK(star_node->layout_height == Approx(30.0f));
}

TEST_CASE("v2 ui shared size scope aligns grouped auto columns across sibling grids", "[v2][ui][grid]") {
    ui_reset();
    RegisterNotoSans();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t first = ui_create_node(UI_NODE_GRID);
    const std::uint64_t second = ui_create_node(UI_NODE_GRID);
    const std::uint64_t first_label = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t first_shortcut = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second_label = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t second_shortcut = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(second != UI_INVALID_HANDLE);
    REQUIRE(first_label != UI_INVALID_HANDLE);
    REQUIRE(first_shortcut != UI_INVALID_HANDLE);
    REQUIRE(second_label != UI_INVALID_HANDLE);
    REQUIRE(second_shortcut != UI_INVALID_HANDLE);

    const float columns[] = {1.0f, 0.0f};
    const std::uint8_t column_types[] = {UI_GRID_UNIT_STAR, UI_GRID_UNIT_AUTO};
    const float rows[] = {32.0f};
    const std::uint8_t row_types[] = {UI_GRID_UNIT_PIXEL};
    constexpr const char* kLabel = "Action";
    constexpr const char* kShort = "A";
    constexpr const char* kLong = "Ctrl+Shift+Alt+K";

    ui_set_root(root);
    ui_set_is_shared_size_scope(root, true);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(first, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(first, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(second, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(second, 32.0f, UI_SIZE_UNIT_PIXEL);
    ui_grid_set_columns(first, 2U, columns, column_types);
    ui_grid_set_rows(first, 1U, rows, row_types);
    ui_grid_set_columns(second, 2U, columns, column_types);
    ui_grid_set_rows(second, 1U, rows, row_types);
    ui_grid_set_column_shared_size_group(first, 1U, reinterpret_cast<const std::uint8_t*>("Shortcut"), 8U);
    ui_grid_set_column_shared_size_group(second, 1U, reinterpret_cast<const std::uint8_t*>("Shortcut"), 8U);
    for (const std::uint64_t handle : {first_label, first_shortcut, second_label, second_shortcut}) {
        ui_set_font(handle, 1U, 18.0f);
    }
    ui_set_text(first_label, reinterpret_cast<const std::uint8_t*>(kLabel), static_cast<std::uint32_t>(std::strlen(kLabel)));
    ui_set_text(first_shortcut, reinterpret_cast<const std::uint8_t*>(kShort), static_cast<std::uint32_t>(std::strlen(kShort)));
    ui_set_text(second_label, reinterpret_cast<const std::uint8_t*>(kLabel), static_cast<std::uint32_t>(std::strlen(kLabel)));
    ui_set_text(second_shortcut, reinterpret_cast<const std::uint8_t*>(kLong), static_cast<std::uint32_t>(std::strlen(kLong)));

    ui_node_add_child(root, first);
    ui_node_add_child(root, second);
    ui_node_add_child(first, first_label);
    ui_node_add_child(first, first_shortcut);
    ui_node_add_child(second, second_label);
    ui_node_add_child(second, second_shortcut);
    ui_node_set_grid_placement(first_label, 0U, 0U, 1U, 1U);
    ui_node_set_grid_placement(first_shortcut, 0U, 1U, 1U, 1U);
    ui_node_set_grid_placement(second_label, 0U, 0U, 1U, 1U);
    ui_node_set_grid_placement(second_shortcut, 0U, 1U, 1U, 1U);

    ui_commit_frame();
    const auto bounds = ReadBounds(ReadCommandBuffer());
    REQUIRE(bounds.find(first_shortcut) != bounds.end());
    REQUIRE(bounds.find(second_shortcut) != bounds.end());
    REQUIRE(bounds.find(first_label) != bounds.end());
    REQUIRE(bounds.find(second_label) != bounds.end());

    CHECK(bounds.at(first_shortcut).x == Approx(bounds.at(second_shortcut).x));
    CHECK(bounds.at(first_shortcut).width == Approx(bounds.at(second_shortcut).width));
    CHECK(bounds.at(first_label).width == Approx(bounds.at(second_label).width));
}
