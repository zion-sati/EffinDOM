#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace effindom::v2::ui {

enum class FixedPitchTabRejection : std::uint8_t {
    None,
    NoTabs,
    NonAscii,
    RichStyleRuns,
    Obscured,
    PrimaryFontUnverified,
    MissingGlyph,
    FallbackFont,
    ShapingChangesCellAdvances,
    InvalidCellWidth,
};

struct FixedPitchTabEligibility {
    std::string_view text{};
    bool has_rich_style_runs = false;
    bool obscured = false;
    bool primary_font_verified = false;
    bool all_glyphs_present = false;
    bool uses_fallback_font = false;
    bool shaping_preserves_cell_advances = false;
    float cell_width = 0.0f;
};

struct FixedPitchFontKey {
    std::uint32_t font_id = 0U;
    std::uint64_t font_generation = 0U;
    std::int32_t scale = 0;
    std::uint32_t ppem = 0U;

    static FixedPitchFontKey Normalize(
        std::uint32_t font_id,
        std::uint64_t font_generation,
        float font_size);

    bool operator==(const FixedPitchFontKey& other) const {
        return font_id == other.font_id &&
            font_generation == other.font_generation &&
            scale == other.scale &&
            ppem == other.ppem;
    }
};

class FixedPitchTabModel {
public:
    static constexpr std::uint32_t kTabColumns = 4U;

    static std::uint32_t NextTabColumn(std::uint32_t column);
    static std::optional<std::uint32_t> ColumnForByteOffset(
        std::string_view text,
        std::size_t byte_offset);
    static std::optional<float> XForByteOffset(
        std::string_view text,
        std::size_t byte_offset,
        float cell_width);
    static std::optional<std::size_t> ByteOffsetForX(
        std::string_view text,
        float x,
        float cell_width);
    static FixedPitchTabRejection CheckEligibility(
        const FixedPitchTabEligibility& input);
};

class FixedPitchMetricsCache {
public:
    std::optional<float> Find(const FixedPitchFontKey& key) const;
    bool Store(const FixedPitchFontKey& key, float cell_width);
    void EraseFontGeneration(std::uint32_t font_id, std::uint64_t font_generation);
    void Clear();
    std::size_t size() const;

private:
    struct Entry {
        FixedPitchFontKey key{};
        float cell_width = 0.0f;
    };

    std::vector<Entry> entries_{};
};

} // namespace effindom::v2::ui
