#include "UiFixedPitchTabs.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using Catch::Approx;
using effindom::v2::ui::FixedPitchFontKey;
using effindom::v2::ui::FixedPitchMetricsCache;
using effindom::v2::ui::FixedPitchTabEligibility;
using effindom::v2::ui::FixedPitchTabModel;
using effindom::v2::ui::FixedPitchTabRejection;

namespace {

FixedPitchTabEligibility Eligible(std::string_view text = "a\tb") {
    return FixedPitchTabEligibility{
        text,
        false,
        false,
        true,
        true,
        false,
        true,
        8.0f,
    };
}

} // namespace

TEST_CASE("fixed-pitch tab columns cover boundaries adjacent tabs and trailing tabs", "[v2][ui][fixed-pitch-tabs]") {
    CHECK(FixedPitchTabModel::NextTabColumn(0U) == 4U);
    CHECK(FixedPitchTabModel::NextTabColumn(1U) == 4U);
    CHECK(FixedPitchTabModel::NextTabColumn(3U) == 4U);
    CHECK(FixedPitchTabModel::NextTabColumn(4U) == 8U);

    CHECK(FixedPitchTabModel::ColumnForByteOffset("\tab", 0U) == 0U);
    CHECK(FixedPitchTabModel::ColumnForByteOffset("\tab", 1U) == 4U);
    CHECK(FixedPitchTabModel::ColumnForByteOffset("a\tb", 2U) == 4U);
    CHECK(FixedPitchTabModel::ColumnForByteOffset("abc\tb", 4U) == 4U);
    CHECK(FixedPitchTabModel::ColumnForByteOffset("abcd\tb", 5U) == 8U);
    CHECK(FixedPitchTabModel::ColumnForByteOffset("a\t\tb", 3U) == 8U);
    CHECK(FixedPitchTabModel::ColumnForByteOffset("ab\t", 3U) == 4U);
    CHECK_FALSE(FixedPitchTabModel::ColumnForByteOffset("a\tb", 4U).has_value());
}

TEST_CASE("fixed-pitch tab X conversion rejects unsafe input without allocation", "[v2][ui][fixed-pitch-tabs]") {
    CHECK(FixedPitchTabModel::XForByteOffset("a\tb", 2U, 7.5f) == Approx(30.0f));
    CHECK_FALSE(FixedPitchTabModel::XForByteOffset("a\tb", 2U, 0.0f).has_value());
    CHECK_FALSE(FixedPitchTabModel::XForByteOffset("a\tb", 2U, std::numeric_limits<float>::infinity()).has_value());
    CHECK_FALSE(FixedPitchTabModel::ColumnForByteOffset(u8"a\u4f60\tb", 4U).has_value());
    CHECK_FALSE(FixedPitchTabModel::ColumnForByteOffset("a\n\tb", 3U).has_value());

    CHECK(FixedPitchTabModel::ByteOffsetForX("a\tb", 0.49f, 1.0f) == 0U);
    CHECK(FixedPitchTabModel::ByteOffsetForX("a\tb", 0.5f, 1.0f) == 1U);
    CHECK(FixedPitchTabModel::ByteOffsetForX("a\tb", 2.49f, 1.0f) == 1U);
    CHECK(FixedPitchTabModel::ByteOffsetForX("a\tb", 2.5f, 1.0f) == 2U);
    CHECK(FixedPitchTabModel::ByteOffsetForX("a\tb", 4.5f, 1.0f) == 3U);
    CHECK(FixedPitchTabModel::ByteOffsetForX("a\tb", -1.0f, 1.0f) == 0U);
    CHECK(FixedPitchTabModel::ByteOffsetForX("a\tb", 99.0f, 1.0f) == 3U);
    CHECK_FALSE(FixedPitchTabModel::ByteOffsetForX("a\tb", 1.0f, 0.0f).has_value());
    CHECK_FALSE(FixedPitchTabModel::ByteOffsetForX(u8"a\u4f60\tb", 1.0f, 1.0f).has_value());
}

TEST_CASE("fixed-pitch tab eligibility covers every explicit rejection", "[v2][ui][fixed-pitch-tabs]") {
    CHECK(FixedPitchTabModel::CheckEligibility(Eligible()) == FixedPitchTabRejection::None);

    auto input = Eligible("plain");
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::NoTabs);
    input = Eligible(u8"a\u4f60\tb");
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::NonAscii);
    input = Eligible("a\n\tb");
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::ShapingChangesCellAdvances);
    input = Eligible();
    input.has_rich_style_runs = true;
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::RichStyleRuns);
    input = Eligible();
    input.obscured = true;
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::Obscured);
    input = Eligible();
    input.primary_font_verified = false;
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::PrimaryFontUnverified);
    input = Eligible();
    input.all_glyphs_present = false;
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::MissingGlyph);
    input = Eligible();
    input.uses_fallback_font = true;
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::FallbackFont);
    input = Eligible();
    input.shaping_preserves_cell_advances = false;
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::ShapingChangesCellAdvances);
    input = Eligible();
    input.cell_width = 0.0f;
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::InvalidCellWidth);
    input = Eligible();
    input.cell_width = std::numeric_limits<float>::quiet_NaN();
    CHECK(FixedPitchTabModel::CheckEligibility(input) == FixedPitchTabRejection::InvalidCellWidth);
}

TEST_CASE("fixed-pitch metrics cache keys font generation and normalized size", "[v2][ui][fixed-pitch-tabs]") {
    FixedPitchMetricsCache cache{};
    const FixedPitchFontKey first = FixedPitchFontKey::Normalize(5U, 11U, 16.001f);
    const FixedPitchFontKey equivalent = FixedPitchFontKey::Normalize(5U, 11U, 16.002f);
    const FixedPitchFontKey next_generation = FixedPitchFontKey::Normalize(5U, 12U, 16.001f);
    const FixedPitchFontKey next_size = FixedPitchFontKey::Normalize(5U, 11U, 17.0f);

    CHECK(first == equivalent);
    CHECK_FALSE(first == next_generation);
    CHECK_FALSE(first == next_size);
    CHECK(cache.Store(first, 9.5f));
    CHECK(cache.Find(equivalent) == Approx(9.5f));
    CHECK(cache.Store(equivalent, 10.0f));
    CHECK(cache.size() == 1U);
    CHECK(cache.Find(first) == Approx(10.0f));
    CHECK(cache.Store(next_generation, 11.0f));
    CHECK(cache.Store(next_size, 12.0f));
    CHECK(cache.size() == 3U);
    CHECK_FALSE(cache.Store(FixedPitchFontKey{}, 0.0f));
    CHECK_FALSE(cache.Store(FixedPitchFontKey{}, std::numeric_limits<float>::quiet_NaN()));

    cache.EraseFontGeneration(5U, 11U);
    CHECK(cache.size() == 1U);
    CHECK(cache.Find(next_generation) == Approx(11.0f));
    cache.Clear();
    CHECK(cache.size() == 0U);
}
