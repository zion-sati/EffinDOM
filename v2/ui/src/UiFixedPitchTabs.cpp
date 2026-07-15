#include "UiFixedPitchTabs.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace effindom::v2::ui {

FixedPitchFontKey FixedPitchFontKey::Normalize(
    std::uint32_t font_id,
    std::uint64_t font_generation,
    float font_size) {
    const float clamped_size = std::max(font_size, 1.0f);
    return FixedPitchFontKey{
        font_id,
        font_generation,
        std::max(64, static_cast<std::int32_t>(std::lround(clamped_size * 64.0f))),
        std::max(1U, static_cast<std::uint32_t>(std::lround(clamped_size))),
    };
}

std::uint32_t FixedPitchTabModel::NextTabColumn(std::uint32_t column) {
    constexpr std::uint32_t kLastSafeGroup =
        (std::numeric_limits<std::uint32_t>::max() / kTabColumns) - 1U;
    const std::uint32_t group = column / kTabColumns;
    if (group > kLastSafeGroup) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return (group + 1U) * kTabColumns;
}

std::optional<std::uint32_t> FixedPitchTabModel::ColumnForByteOffset(
    std::string_view text,
    std::size_t byte_offset) {
    if (byte_offset > text.size()) {
        return std::nullopt;
    }
    std::uint32_t column = 0U;
    for (std::size_t index = 0U; index < byte_offset; index += 1U) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte >= 0x80U || (byte < 0x20U && byte != '\t') || byte == 0x7FU) {
            return std::nullopt;
        }
        column = byte == '\t' ? NextTabColumn(column) : column + 1U;
    }
    return column;
}

std::optional<float> FixedPitchTabModel::XForByteOffset(
    std::string_view text,
    std::size_t byte_offset,
    float cell_width) {
    if (!std::isfinite(cell_width) || cell_width <= 0.0f) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> column = ColumnForByteOffset(text, byte_offset);
    return column.has_value()
        ? std::optional<float>(static_cast<float>(*column) * cell_width)
        : std::nullopt;
}

std::optional<std::size_t> FixedPitchTabModel::ByteOffsetForX(
    std::string_view text,
    float x,
    float cell_width) {
    if (!std::isfinite(x) || !std::isfinite(cell_width) || cell_width <= 0.0f) {
        return std::nullopt;
    }
    std::optional<std::uint32_t> end_column = ColumnForByteOffset(text, text.size());
    if (!end_column.has_value()) {
        return std::nullopt;
    }
    if (x <= 0.0f) {
        return 0U;
    }
    const float end_x = static_cast<float>(*end_column) * cell_width;
    if (x >= end_x) {
        return text.size();
    }

    std::size_t closest_offset = 0U;
    float closest_distance = x;
    for (std::size_t offset = 1U; offset <= text.size(); offset += 1U) {
        const std::optional<std::uint32_t> column = ColumnForByteOffset(text, offset);
        if (!column.has_value()) {
            return std::nullopt;
        }
        const float distance = std::fabs(x - static_cast<float>(*column) * cell_width);
        if (distance <= closest_distance) {
            closest_offset = offset;
            closest_distance = distance;
        }
    }
    return closest_offset;
}

FixedPitchTabRejection FixedPitchTabModel::CheckEligibility(
    const FixedPitchTabEligibility& input) {
    if (input.text.find('\t') == std::string_view::npos) {
        return FixedPitchTabRejection::NoTabs;
    }
    for (const char value : input.text) {
        const unsigned char byte = static_cast<unsigned char>(value);
        if (byte >= 0x80U) {
            return FixedPitchTabRejection::NonAscii;
        }
        if ((byte < 0x20U && byte != '\t') || byte == 0x7FU) {
            return FixedPitchTabRejection::ShapingChangesCellAdvances;
        }
    }
    if (input.has_rich_style_runs) {
        return FixedPitchTabRejection::RichStyleRuns;
    }
    if (input.obscured) {
        return FixedPitchTabRejection::Obscured;
    }
    if (!input.primary_font_verified) {
        return FixedPitchTabRejection::PrimaryFontUnverified;
    }
    if (!input.all_glyphs_present) {
        return FixedPitchTabRejection::MissingGlyph;
    }
    if (input.uses_fallback_font) {
        return FixedPitchTabRejection::FallbackFont;
    }
    if (!input.shaping_preserves_cell_advances) {
        return FixedPitchTabRejection::ShapingChangesCellAdvances;
    }
    if (!std::isfinite(input.cell_width) || input.cell_width <= 0.0f) {
        return FixedPitchTabRejection::InvalidCellWidth;
    }
    return FixedPitchTabRejection::None;
}

std::optional<float> FixedPitchMetricsCache::Find(const FixedPitchFontKey& key) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.key == key;
    });
    return found == entries_.end() ? std::nullopt : std::optional<float>(found->cell_width);
}

bool FixedPitchMetricsCache::Store(const FixedPitchFontKey& key, float cell_width) {
    if (!std::isfinite(cell_width) || cell_width <= 0.0f) {
        return false;
    }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.key == key;
    });
    if (found != entries_.end()) {
        found->cell_width = cell_width;
        return true;
    }
    entries_.push_back(Entry{key, cell_width});
    return true;
}

void FixedPitchMetricsCache::EraseFontGeneration(
    std::uint32_t font_id,
    std::uint64_t font_generation) {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
            return entry.key.font_id == font_id &&
                entry.key.font_generation == font_generation;
        }),
        entries_.end());
}

void FixedPitchMetricsCache::Clear() {
    entries_.clear();
}

std::size_t FixedPitchMetricsCache::size() const {
    return entries_.size();
}

} // namespace effindom::v2::ui
