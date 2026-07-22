#include "TestUiSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using Catch::Approx;
using effindom::v2::ui::GetRuntime;
using effindom::v2::ui::UiRuntime;

namespace {

void CheckRunsEqual(const UiRuntime::ShapedTextRun& actual, const UiRuntime::ShapedTextRun& expected) {
    CHECK(actual.font_id == expected.font_id);
    CHECK(actual.width == Approx(expected.width));
    CHECK(actual.height == Approx(expected.height));
    CHECK(actual.baseline == Approx(expected.baseline));
    CHECK(actual.ascent == Approx(expected.ascent));
    CHECK(actual.descent == Approx(expected.descent));
    REQUIRE(actual.glyphs.size() == expected.glyphs.size());
    for (std::size_t index = 0U; index < actual.glyphs.size(); index += 1U) {
        const auto& lhs = actual.glyphs[index];
        const auto& rhs = expected.glyphs[index];
        CHECK(lhs.glyph_id == rhs.glyph_id);
        CHECK(lhs.x == Approx(rhs.x));
        CHECK(lhs.y == Approx(rhs.y));
        CHECK(lhs.cluster == rhs.cluster);
        CHECK(lhs.font_id == rhs.font_id);
        CHECK(lhs.color == rhs.color);
        CHECK(lhs.font_size == Approx(rhs.font_size));
    }
}

void CheckVisualRunsEqual(const UiRuntime::ShapedTextRun& actual, const UiRuntime::ShapedTextRun& expected) {
    CHECK(actual.font_id == expected.font_id);
    CHECK(actual.width == Approx(expected.width));
    CHECK(actual.height == Approx(expected.height));
    CHECK(actual.baseline == Approx(expected.baseline));
    CHECK(actual.ascent == Approx(expected.ascent));
    CHECK(actual.descent == Approx(expected.descent));
    REQUIRE(actual.glyphs.size() == expected.glyphs.size());
    for (std::size_t index = 0U; index < actual.glyphs.size(); index += 1U) {
        const auto& lhs = actual.glyphs[index];
        const auto& rhs = expected.glyphs[index];
        CHECK(lhs.glyph_id == rhs.glyph_id);
        CHECK(lhs.x == Approx(rhs.x));
        CHECK(lhs.y == Approx(rhs.y));
        CHECK(lhs.font_id == rhs.font_id);
        CHECK(lhs.color == rhs.color);
        CHECK(lhs.font_size == Approx(rhs.font_size));
    }
}

void ResetShapingFixture() {
    ui_reset();
    GetRuntime().DestroyShapingBuffers();
    RegisterTestFont();
    GetRuntime().ClearShapingResourceProfile();
}

void CheckFreshPair(std::string_view text, float font_size, bool obscured = false) {
    UiRuntime::ShapedTextRun first{};
    UiRuntime::ShapedTextRun second{};
    REQUIRE(GetRuntime().ShapeText(text, 1U, font_size, first, obscured));
    REQUIRE(GetRuntime().ShapeText(text, 1U, font_size, second, obscured));
    CheckRunsEqual(second, first);
}

struct ParagraphSnapshot {
    float width;
    float height;
    float max_line_width;
    float line_height;
    std::size_t visible_line_count;
    std::size_t total_line_count;
    bool clipped;
    std::vector<std::int32_t> break_offsets;
    std::vector<float> line_widths;
    std::vector<float> line_heights;
    std::vector<float> line_ascents;
    std::vector<float> line_y_offsets;
};

ParagraphSnapshot SnapshotParagraph(const UiRuntime::ParagraphLayout& paragraph) {
    return ParagraphSnapshot{
        paragraph.width,
        paragraph.height,
        paragraph.max_line_width,
        paragraph.line_height,
        paragraph.visible_line_count,
        paragraph.total_line_count,
        paragraph.clipped,
        {paragraph.break_offsets.begin(), paragraph.break_offsets.end()},
        {paragraph.line_widths.begin(), paragraph.line_widths.end()},
        {paragraph.line_heights.begin(), paragraph.line_heights.end()},
        {paragraph.line_ascents.begin(), paragraph.line_ascents.end()},
        {paragraph.line_y_offsets.begin(), paragraph.line_y_offsets.end()},
    };
}

void CheckParagraphsEqual(const UiRuntime::ParagraphLayout& actual, const ParagraphSnapshot& expected) {
    CHECK(actual.width == Approx(expected.width));
    CHECK(actual.height == Approx(expected.height));
    CHECK(actual.max_line_width == Approx(expected.max_line_width));
    CHECK(actual.line_height == Approx(expected.line_height));
    CHECK(actual.visible_line_count == expected.visible_line_count);
    CHECK(actual.total_line_count == expected.total_line_count);
    CHECK(actual.clipped == expected.clipped);
    CHECK(actual.break_offsets == expected.break_offsets);
    CHECK(actual.line_widths == expected.line_widths);
    CHECK(actual.line_heights == expected.line_heights);
    CHECK(actual.line_ascents == expected.line_ascents);
    CHECK(actual.line_y_offsets == expected.line_y_offsets);
}

void InvalidateTextNode(std::uint64_t handle) {
    auto* node = GetRuntime().ResolveMutable(handle);
    REQUIRE(node != nullptr);
    GetRuntime().InvalidateTextLayoutCache(*node);
}

void CheckWarmResourceReuse() {
    const auto& profile = GetRuntime().shaping_resource_profile();
    CHECK(profile.buffer_creations == 0U);
    CHECK(profile.sized_font_creations == 0U);
    CHECK(profile.buffer_cache_hits > 0U);
    CHECK(profile.sized_font_cache_hits > 0U);
}

} // namespace

TEST_CASE("v2 fixed-pitch tab contract uses four-column stops", "[v2][ui][shaping-resources][tabs]") {
    constexpr std::uint32_t kColumnsPerTab = 4U;
    const auto next_tab_column = [=](std::uint32_t column) {
        return ((column / kColumnsPerTab) + 1U) * kColumnsPerTab;
    };

    CHECK(next_tab_column(0U) == 4U);
    CHECK(next_tab_column(1U) == 4U);
    CHECK(next_tab_column(3U) == 4U);
    CHECK(next_tab_column(4U) == 8U);
    CHECK(next_tab_column(next_tab_column(1U)) == 8U);
}

TEST_CASE("v2 low-level tab shaping preserves four-space expansion and exposes allocations", "[v2][ui][shaping-resources][tabs]") {
    ResetShapingFixture();
    const auto* font = GetRuntime().LookupFont(1U);
    REQUIRE(font != nullptr);

    struct Scenario {
        std::string_view tabbed;
        std::string_view expanded;
    };
    static constexpr Scenario kScenarios[] = {
        {"\tab", "    ab"},
        {"a\tb", "a    b"},
        {"abc\tb", "abc    b"},
        {"abcd\tb", "abcd    b"},
        {"a\t\tb", "a        b"},
        {"ab\t", "ab    "},
    };

    for (const Scenario& scenario : kScenarios) {
        INFO(scenario.tabbed);
        GetRuntime().ClearShapingResourceProfile();
        UiRuntime::ShapedTextRun tabbed{};
        UiRuntime::ShapedTextRun expanded{};
        REQUIRE(GetRuntime().ShapeTextWithFont(scenario.tabbed, *font, 1U, 17.0f, tabbed));
        const auto tab_profile = GetRuntime().shaping_resource_profile();
        REQUIRE(GetRuntime().ShapeTextWithFont(scenario.expanded, *font, 1U, 17.0f, expanded));
        CheckVisualRunsEqual(tabbed, expanded);
        CHECK(tab_profile.tab_expansion_calls == 1U);
        CHECK(tab_profile.tab_expanded_bytes == scenario.expanded.size());
        CHECK(tab_profile.tab_cluster_map_entries == scenario.expanded.size());
    }
}

TEST_CASE("v2 production tab shaping baselines reject expansion across general fallback categories", "[v2][ui][shaping-resources][tabs]") {
    ResetShapingFixture();

    const auto check_general_fallback = [](std::string_view tabbed) {
        GetRuntime().ClearShapingResourceProfile();
        UiRuntime::ShapedTextRun first{};
        UiRuntime::ShapedTextRun second{};
        REQUIRE(GetRuntime().ShapeText(tabbed, 1U, 18.0f, first));
        REQUIRE(GetRuntime().ShapeText(tabbed, 1U, 18.0f, second));
        CheckRunsEqual(second, first);
        const auto& profile = GetRuntime().shaping_resource_profile();
        CHECK(profile.tab_expansion_calls == 0U);
        CHECK(profile.tab_expanded_bytes == 0U);
        CHECK(profile.tab_cluster_map_entries == 0U);
        CHECK(profile.fixed_pitch_tab_attempts == 2U);
        CHECK(profile.fixed_pitch_tab_successes == 0U);
        CHECK(profile.fixed_pitch_tab_rejections == 2U);
    };

    SECTION("shaping-sensitive ASCII") {
        check_general_fallback("office\taffinity");
    }

    SECTION("non-ASCII primary-font text") {
        check_general_fallback(u8"Caf\u00e9\tfin");
    }

    SECTION("BiDi text") {
        check_general_fallback(u8"\u05e9\u05dc\u05d5\u05dd\t\u05e2\u05d5\u05dc\u05dd");
    }

    SECTION("unresolved tofu text") {
        check_general_fallback(u8"status\t\u2620");
    }

    SECTION("fallback-font text") {
        const auto fallback_bytes = ReadFileBytes(
            std::string(EFFINDOM_SOURCE_DIR) + "/v2/ui/tests/fixtures/noto-sans-sc-cjk-subset.woff2");
        REQUIRE(GetRuntime().RegisterFont(42U, fallback_bytes.data(), static_cast<std::uint32_t>(fallback_bytes.size())));
        REQUIRE(GetRuntime().RegisterFontFallback(1U, 42U));
        check_general_fallback(u8"status\t\u4f60");
    }

    SECTION("styled range") {
        const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
        REQUIRE(handle != UI_INVALID_HANDLE);
        constexpr std::string_view content = "styled\ttab";
        ui_set_font(handle, 1U, 18.0f);
        ui_set_text(
            handle,
            reinterpret_cast<const std::uint8_t*>(content.data()),
            static_cast<std::uint32_t>(content.size()));
        const std::uint32_t style_words[] = {
            0U,
            static_cast<std::uint32_t>(content.size()),
            1U,
            effindom::v2::ui::CommandBuilder::FloatToWord(18.0f),
            0xFF223344U,
            0U,
            0U,
        };
        ui_set_text_style_runs(handle, 1U, style_words);
        const auto* node = GetRuntime().Resolve(handle);
        REQUIRE(node != nullptr);
        GetRuntime().ClearShapingResourceProfile();
        UiRuntime::ShapedTextRun shaped{};
        REQUIRE(GetRuntime().ShapeTextStyledRange(
            *node,
            0U,
            static_cast<std::uint32_t>(content.size()),
            shaped));
        const auto& profile = GetRuntime().shaping_resource_profile();
        CHECK(profile.tab_expansion_calls == 0U);
        CHECK(profile.tab_expanded_bytes == 0U);
        CHECK(profile.tab_cluster_map_entries == 0U);
        CHECK(profile.fixed_pitch_tab_attempts == 0U);
        CHECK(profile.fixed_pitch_tab_successes == 0U);
        CHECK(profile.fixed_pitch_tab_rejections == 0U);
    }
}

TEST_CASE("v2 obscured tabs reject general expansion and keep one obscured glyph per logical character", "[v2][ui][shaping-resources][tabs]") {
    ResetShapingFixture();
    GetRuntime().ClearShapingResourceProfile();

    UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText("a\tb", 1U, 18.0f, shaped, true));
    REQUIRE(shaped.glyphs.size() == 3U);
    CHECK(shaped.glyphs[0U].cluster == 0U);
    CHECK(shaped.glyphs[1U].cluster == 1U);
    CHECK(shaped.glyphs[2U].cluster == 2U);
    const auto& profile = GetRuntime().shaping_resource_profile();
    CHECK(profile.tab_expansion_calls == 0U);
    CHECK(profile.tab_expanded_bytes == 0U);
    CHECK(profile.tab_cluster_map_entries == 0U);
}

TEST_CASE("v2 verified fixed-pitch tabs shape to four-column stops without expansion", "[v2][ui][shaping-resources][tabs]") {
    ui_reset();
    RegisterMonoTestFont(5U);
    GetRuntime().ClearShapingResourceProfile();

    UiRuntime::ShapedTextRun cell{};
    REQUIRE(GetRuntime().ShapeText("x", 5U, 18.0f, cell));
    REQUIRE(cell.glyphs.size() == 1U);
    const float cell_width = cell.width;

    struct Scenario {
        std::string_view text;
        std::uint32_t columns;
        std::vector<std::uint32_t> clusters;
    };
    const std::vector<Scenario> scenarios = {
        {"\tab", 6U, {0U, 1U, 2U}},
        {"a\tb", 5U, {0U, 1U, 2U}},
        {"abc\tb", 5U, {0U, 1U, 2U, 3U, 4U}},
        {"abcd\tb", 9U, {0U, 1U, 2U, 3U, 4U, 5U}},
        {"a\t\tb", 9U, {0U, 1U, 2U, 3U}},
        {"ab\t", 4U, {0U, 1U, 2U}},
    };

    for (const Scenario& scenario : scenarios) {
        INFO(scenario.text);
        GetRuntime().ClearShapingResourceProfile();
        UiRuntime::ShapedTextRun shaped{};
        REQUIRE(GetRuntime().ShapeText(scenario.text, 5U, 18.0f, shaped));
        CHECK(shaped.width == Approx(static_cast<float>(scenario.columns) * cell_width));
        REQUIRE(shaped.glyphs.size() == scenario.clusters.size());
        for (std::size_t index = 0U; index < scenario.clusters.size(); index += 1U) {
            CHECK(shaped.glyphs[index].cluster == scenario.clusters[index]);
        }
        const auto stops = GetRuntime().BuildTextClusterStops(
            shaped.glyphs,
            shaped.width,
            scenario.text.size());
        for (std::size_t byte_offset = 0U; byte_offset <= scenario.text.size(); byte_offset += 1U) {
            const auto expected_column =
                effindom::v2::ui::FixedPitchTabModel::ColumnForByteOffset(
                    scenario.text,
                    byte_offset);
            REQUIRE(expected_column.has_value());
            const auto stop = std::find_if(stops.begin(), stops.end(), [&](const auto& candidate) {
                return candidate.index == byte_offset;
            });
            REQUIRE(stop != stops.end());
            CHECK(stop->x == Approx(static_cast<float>(*expected_column) * cell_width));
        }
        const auto& profile = GetRuntime().shaping_resource_profile();
        CHECK(profile.fixed_pitch_tab_attempts == 1U);
        CHECK(profile.fixed_pitch_tab_successes == 1U);
        CHECK(profile.fixed_pitch_tab_rejections == 0U);
        CHECK(profile.tab_expansion_calls == 0U);
        CHECK(profile.tab_expanded_bytes == 0U);
        CHECK(profile.tab_cluster_map_entries == 0U);
    }
}

TEST_CASE("v2 fixed-pitch tab metric cache is generation-safe across font replacement", "[v2][ui][shaping-resources][tabs]") {
    ui_reset();
    RegisterMonoTestFont(5U);
    GetRuntime().ClearShapingResourceProfile();
    UiRuntime::ShapedTextRun first{};
    REQUIRE(GetRuntime().ShapeText("a\tb", 5U, 18.0f, first));
    CHECK(GetRuntime().fixed_pitch_metrics_cache_.size() == 1U);

    RegisterMonoTestFont(5U);
    CHECK(GetRuntime().fixed_pitch_metrics_cache_.size() == 0U);
    GetRuntime().ClearShapingResourceProfile();
    UiRuntime::ShapedTextRun replacement{};
    REQUIRE(GetRuntime().ShapeText("a\tb", 5U, 18.0f, replacement));
    CheckRunsEqual(replacement, first);
    CHECK(GetRuntime().fixed_pitch_metrics_cache_.size() == 1U);
    CHECK(GetRuntime().shaping_resource_profile().fixed_pitch_tab_successes == 1U);

    REQUIRE(GetRuntime().UnregisterFont(5U));
    CHECK(GetRuntime().fixed_pitch_metrics_cache_.size() == 0U);
}

TEST_CASE("v2 shaping fresh resources preserve complete output across representative text", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();

    CheckFreshPair("office affinity", 16.0f);
    CheckFreshPair(u8"Cafe\u0301 \U0001F30D", 19.0f);
    CheckFreshPair(u8"\u05E9\u05DC\u05D5\u05DD \u05E2\u05D5\u05DC\u05DD", 21.0f);
    CheckFreshPair("one\ttwo", 17.0f);
    CheckFreshPair("secret", 18.0f, true);

    const auto& profile = GetRuntime().shaping_resource_profile();
    // Missing-glyph segmentation shapes additional resolved runs. Keep these
    // exact baseline counts so later pooling/caching slices prove they removed
    // allocations rather than merely preserving top-level call counts.
    CHECK(profile.buffer_creations == 0U);
    CHECK(profile.buffer_destructions == 0U);
    CHECK(profile.buffer_cache_hits == 12U);
    CHECK(profile.sized_font_creations == 5U);
    CHECK(profile.sized_font_destructions == 0U);
    CHECK(profile.sized_font_cache_hits == 11U);
    CHECK(profile.sized_font_evictions == 0U);
}

TEST_CASE("v2 shaping fresh resource counters expose bounded run shards", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();

    const std::string long_text(20U * 1024U, 'a');
    UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText(long_text, 1U, 16.0f, shaped));
    REQUIRE(shaped.glyphs.size() == long_text.size());

    const auto& profile = GetRuntime().shaping_resource_profile();
    CHECK(profile.buffer_creations == 0U);
    CHECK(profile.buffer_destructions == 0U);
    CHECK(profile.buffer_cache_hits == 2U);
    CHECK(profile.sized_font_creations == 1U);
    CHECK(profile.sized_font_destructions == 0U);
    CHECK(profile.sized_font_cache_hits == 1U);
}

TEST_CASE("v2 shaping resource counters can reset without changing fonts", "[v2][ui][shaping-resources]") {
    ui_reset();
    GetRuntime().DestroyShapingBuffers();
    GetRuntime().ClearShapingResourceProfile();
    RegisterTestFont();
    REQUIRE(GetRuntime().shaping_resource_profile().buffer_creations == 1U);

    GetRuntime().ClearShapingResourceProfile();
    const auto& cleared = GetRuntime().shaping_resource_profile();
    CHECK(cleared.buffer_creations == 0U);
    CHECK(cleared.buffer_destructions == 0U);
    CHECK(cleared.sized_font_creations == 0U);
    CHECK(cleared.sized_font_destructions == 0U);

    UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText("still registered", 1U, 16.0f, shaped));
    CHECK_FALSE(shaped.glyphs.empty());
}

TEST_CASE("v2 shaping buffer leases are reentrant and deterministically reusable", "[v2][ui][shaping-resources]") {
    GetRuntime().DestroyShapingBuffers();
    GetRuntime().ClearShapingResourceProfile();

    hb_buffer_t* first_buffer = nullptr;
    hb_buffer_t* second_buffer = nullptr;
    {
        auto first = GetRuntime().AcquireShapingBuffer();
        auto second = GetRuntime().AcquireShapingBuffer();
        REQUIRE(first);
        REQUIRE(second);
        first_buffer = first.get();
        second_buffer = second.get();
        CHECK(first_buffer != second_buffer);
        CHECK(GetRuntime().shaping_resource_profile().buffer_creations == 2U);
    }

    auto reused = GetRuntime().AcquireShapingBuffer();
    REQUIRE(reused);
    CHECK(reused.get() == first_buffer);
    CHECK(GetRuntime().shaping_resource_profile().buffer_cache_hits == 1U);
}

TEST_CASE("v2 shaping buffer pool survives reset and destroys every allocation", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();
    GetRuntime().Reset();

    UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText("after reset", 1U, 16.0f, shaped));
    CHECK(GetRuntime().shaping_resource_profile().buffer_creations == 0U);
    CHECK(GetRuntime().shaping_resource_profile().buffer_cache_hits == 1U);

    GetRuntime().DestroyShapingBuffers();
    CHECK(GetRuntime().shaping_resource_profile().buffer_destructions == 1U);
}

TEST_CASE("v2 sized font cache normalizes keys and preserves distinct metrics", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();
    UiRuntime::ShapedTextRun first{};
    UiRuntime::ShapedTextRun equivalent{};
    UiRuntime::ShapedTextRun larger{};
    REQUIRE(GetRuntime().ShapeText("metrics", 1U, 16.001f, first));
    REQUIRE(GetRuntime().ShapeText("metrics", 1U, 16.002f, equivalent));
    REQUIRE(GetRuntime().ShapeText("metrics", 1U, 17.0f, larger));

    CHECK(first.width == Approx(equivalent.width));
    CHECK(larger.width > first.width);
    const auto& profile = GetRuntime().shaping_resource_profile();
    CHECK(profile.sized_font_creations == 2U);
    CHECK(profile.sized_font_cache_hits == 1U);
}

TEST_CASE("v2 sized font cache evicts deterministic least recently used entries", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();
    UiRuntime::ShapedTextRun shaped{};
    for (int size = 10; size <= 17; size += 1) {
        REQUIRE(GetRuntime().ShapeText("cache", 1U, static_cast<float>(size), shaped));
    }
    REQUIRE(GetRuntime().ShapeText("cache", 1U, 10.0f, shaped));
    REQUIRE(GetRuntime().ShapeText("cache", 1U, 18.0f, shaped));
    REQUIRE(GetRuntime().ShapeText("cache", 1U, 11.0f, shaped));

    const auto& profile = GetRuntime().shaping_resource_profile();
    CHECK(profile.sized_font_creations == 10U);
    CHECK(profile.sized_font_cache_hits == 1U);
    CHECK(profile.sized_font_evictions == 2U);
    CHECK(profile.sized_font_destructions == 2U);
}

TEST_CASE("v2 sized font cache is destroyed on replacement and unregister", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();
    UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText("old", 1U, 16.0f, shaped));
    CHECK(GetRuntime().shaping_resource_profile().sized_font_creations == 1U);

    RegisterTestFont();
    CHECK(GetRuntime().shaping_resource_profile().sized_font_destructions == 1U);
    REQUIRE(GetRuntime().ShapeText("new", 1U, 16.0f, shaped));
    CHECK(GetRuntime().shaping_resource_profile().sized_font_creations == 2U);
    CHECK(GetRuntime().shaping_resource_profile().sized_font_cache_hits == 0U);

    REQUIRE(GetRuntime().UnregisterFont(1U));
    CHECK(GetRuntime().shaping_resource_profile().sized_font_destructions == 2U);
}

TEST_CASE("v2 sized font cache survives app reset with registered fonts", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();
    UiRuntime::ShapedTextRun shaped{};
    REQUIRE(GetRuntime().ShapeText("before", 1U, 16.0f, shaped));
    GetRuntime().Reset();
    REQUIRE(GetRuntime().ShapeText("after", 1U, 16.0f, shaped));

    const auto& profile = GetRuntime().shaping_resource_profile();
    CHECK(profile.sized_font_creations == 1U);
    CHECK(profile.sized_font_cache_hits == 1U);
}

TEST_CASE("v2 shaping resources stay warm across retained wrapped nonwrapped and obscured layouts", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    const std::string content = "office affinity\nsecret text with wrapping and\ttabs";
    ui_set_width(text, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_set_text_wrapping(text, true);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    const auto wrapped_cold = SnapshotParagraph(GetRuntime().LayoutParagraph(*node, 120.0f));
    InvalidateTextNode(text);
    GetRuntime().ClearShapingResourceProfile();
    const auto wrapped_warm = GetRuntime().LayoutParagraph(*node, 120.0f);
    CheckParagraphsEqual(wrapped_warm, wrapped_cold);
    CheckWarmResourceReuse();

    REQUIRE(GetRuntime().SetTextWrapping(text, false));
    const auto nonwrapped_cold = SnapshotParagraph(GetRuntime().LayoutParagraph(*node, 120.0f));
    InvalidateTextNode(text);
    GetRuntime().ClearShapingResourceProfile();
    const auto nonwrapped_warm = GetRuntime().LayoutParagraph(*node, 120.0f);
    CheckParagraphsEqual(nonwrapped_warm, nonwrapped_cold);
    CheckWarmResourceReuse();

    REQUIRE(GetRuntime().SetTextObscured(text, true));
    const auto obscured_cold = SnapshotParagraph(GetRuntime().LayoutParagraph(*node, 120.0f));
    InvalidateTextNode(text);
    GetRuntime().ClearShapingResourceProfile();
    const auto obscured_warm = GetRuntime().LayoutParagraph(*node, 120.0f);
    CheckParagraphsEqual(obscured_warm, obscured_cold);
    CheckWarmResourceReuse();
}

TEST_CASE("v2 shaping resources stay warm through measurement and long retained shards", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();

    const std::string measured = u8"Measured Cafe\u0301 office";
    float cold_width = 0.0f;
    float cold_height = 0.0f;
    GetRuntime().MeasureText(
        reinterpret_cast<const std::uint8_t*>(measured.data()),
        static_cast<std::uint32_t>(measured.size()),
        1U,
        19.0f,
        140.0f,
        &cold_width,
        &cold_height);
    GetRuntime().ClearShapingResourceProfile();
    float warm_width = 0.0f;
    float warm_height = 0.0f;
    GetRuntime().MeasureText(
        reinterpret_cast<const std::uint8_t*>(measured.data()),
        static_cast<std::uint32_t>(measured.size()),
        1U,
        19.0f,
        140.0f,
        &warm_width,
        &warm_height);
    CHECK(warm_width == Approx(cold_width));
    CHECK(warm_height == Approx(cold_height));
    CheckWarmResourceReuse();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    const std::string long_text(20U * 1024U, 'a');
    ui_set_font(text, 1U, 16.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(long_text.data()), static_cast<std::uint32_t>(long_text.size()));
    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    const auto cold = SnapshotParagraph(GetRuntime().LayoutParagraph(*node, std::nullopt));
    InvalidateTextNode(text);
    GetRuntime().ClearShapingResourceProfile();
    const auto warm = GetRuntime().LayoutParagraph(*node, std::nullopt);
    CheckParagraphsEqual(warm, cold);
    CheckWarmResourceReuse();
}

TEST_CASE("v2 shaping resources stay warm through rich immediate preparation", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    constexpr std::string_view content = "HelloWorld";
    ui_set_width(text, 280.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    const std::uint32_t runs_words[] = {
        0U, 5U, 1U, effindom::v2::ui::CommandBuilder::FloatToWord(18.0f), 0xff223344U, 0U, 0U,
        5U, 10U, 1U, effindom::v2::ui::CommandBuilder::FloatToWord(32.0f), 0xff556677U, 0U, 0U,
    };
    ui_set_text_style_runs(text, 2U, runs_words);
    REQUIRE(ui_prepare_node(text) == 1U);
    const std::vector<std::uint32_t> cold_commands = GetRuntime().pending_prepare_commands_;

    InvalidateTextNode(text);
    GetRuntime().pending_prepare_commands_.clear();
    GetRuntime().ClearShapingResourceProfile();
    REQUIRE(ui_prepare_node(text) == 1U);
    CHECK(GetRuntime().pending_prepare_commands_ == cold_commands);
    CheckWarmResourceReuse();
}

TEST_CASE("v2 dynamic text preparation does not allocate HarfBuzz resources after warmup", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_font(text, 1U, 20.0f);
    ui_set_dynamic_text_charset(text, reinterpret_cast<const std::uint8_t*>("0123456789"), 10U);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("1234"), 4U);
    REQUIRE(ui_prepare_node(text) == 1U);

    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("4321"), 4U);
    GetRuntime().pending_prepare_commands_.clear();
    GetRuntime().ClearShapingResourceProfile();
    REQUIRE(ui_prepare_node(text) == 1U);
    const auto& profile = GetRuntime().shaping_resource_profile();
    CHECK(profile.buffer_creations == 0U);
    CHECK(profile.sized_font_creations == 0U);
    const auto& dynamic_profile = GetRuntime().last_dynamic_text_prepare_profile();
    CHECK(dynamic_profile.fast_path_successes >= 1U);
    CHECK(dynamic_profile.cache_hits >= 4U);
}

TEST_CASE("v2 fallback registration invalidates retained layout and then reuses shaping resources", "[v2][ui][shaping-resources]") {
    ui_reset();
    GetRuntime().DestroyShapingBuffers();
    RegisterMonoTestFont(5U);
    (void)ui_unregister_font_fallback(5U, 4U);
    GetRuntime().ClearShapingResourceProfile();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    constexpr std::string_view content = u8"status \U0001F30D";
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 5U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);

    const auto missing_paragraph = GetRuntime().LayoutParagraph(*node, 180.0f);
    const auto missing_plan = GetRuntime().BuildPreparedTextRenderPlan(*node, missing_paragraph, 180.0f);
    CHECK(std::none_of(
        missing_plan.glyphs.begin(),
        missing_plan.glyphs.end(),
        [](const auto& glyph) { return glyph.font_id == 4U; }));

    RegisterEmojiTestFont(4U);
    ui_register_font_fallback(5U, 4U);
    GetRuntime().ClearShapingResourceProfile();
    const auto resolved_paragraph = GetRuntime().LayoutParagraph(*node, 180.0f);
    const auto resolved_plan = GetRuntime().BuildPreparedTextRenderPlan(*node, resolved_paragraph, 180.0f);
    const auto resolved_paragraph_snapshot = SnapshotParagraph(resolved_paragraph);
    CHECK(std::any_of(
        resolved_plan.glyphs.begin(),
        resolved_plan.glyphs.end(),
        [](const auto& glyph) { return glyph.font_id == 4U; }));

    InvalidateTextNode(text);
    GetRuntime().ClearShapingResourceProfile();
    const auto warm_paragraph = GetRuntime().LayoutParagraph(*node, 180.0f);
    const auto warm_plan = GetRuntime().BuildPreparedTextRenderPlan(*node, warm_paragraph, 180.0f);
    CheckParagraphsEqual(warm_paragraph, resolved_paragraph_snapshot);
    REQUIRE(warm_plan.glyphs.size() == resolved_plan.glyphs.size());
    for (std::size_t index = 0U; index < warm_plan.glyphs.size(); index += 1U) {
        const auto& actual = warm_plan.glyphs[index];
        const auto& expected = resolved_plan.glyphs[index];
        CHECK(actual.glyph_id == expected.glyph_id);
        CHECK(actual.x == Approx(expected.x));
        CHECK(actual.y == Approx(expected.y));
        CHECK(actual.cluster == expected.cluster);
        CHECK(actual.font_id == expected.font_id);
        CHECK(actual.font_size == Approx(expected.font_size));
    }
    CheckWarmResourceReuse();
}

TEST_CASE("v2 warm editable commits shape text without creating HarfBuzz resources", "[v2][ui][shaping-resources]") {
    ResetShapingFixture();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    constexpr std::string_view initial = "alpha beta gamma";
    ui_set_root(root);
    ui_resize_window(320.0f, 120.0f);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 18.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(initial.data()), static_cast<std::uint32_t>(initial.size()));
    ui_set_selectable(text, true, 0x40007AFFU);
    ui_set_editable(text, true);
    ui_node_add_child(root, text);
    ui_commit_frame();

    for (std::uint32_t edit_index = 0U; edit_index < 2U; edit_index += 1U) {
        auto* node = GetRuntime().ResolveMutable(text);
        REQUIRE(node != nullptr);
        const std::uint32_t insertion = static_cast<std::uint32_t>(node->text_content.size());
        const auto edit = effindom::v2::ui::TextEdit::Create(node->text_content, insertion, insertion, "!");
        REQUIRE(edit.has_value());
        GetRuntime().ClearTextCommitProfile();
        REQUIRE(GetRuntime().ApplyTextEdit(text, *node, *edit, insertion + 1U, insertion + 1U));
        GetRuntime().CommitFrame();

        const auto& profile = GetRuntime().last_text_commit_profile();
        CHECK(profile.exact_text_edit_applications == 1U);
        CHECK(profile.harfbuzz_shape_calls > 0U);
        CHECK(profile.harfbuzz_shape_bytes > 0U);
        CHECK(profile.shaping_buffer_creations == 0U);
        CHECK(profile.shaping_sized_font_creations == 0U);
        CHECK(profile.shaping_buffer_cache_hits > 0U);
        CHECK(profile.shaping_sized_font_cache_hits > 0U);
        CHECK(profile.shaping_sized_font_evictions == 0U);
    }
}
