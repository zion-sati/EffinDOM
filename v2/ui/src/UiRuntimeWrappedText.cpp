#include "UiRuntime.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace effindom::v2::ui {

namespace {

constexpr float kUnknownBreakCandidateX = -1.0f;
constexpr std::size_t kWrappedBreakShardTargetCandidateCount = 256U;
constexpr std::uint32_t kWrappedBreakShardTargetCodeUnits = 8192U;

bool IsAsciiOnly(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char ch) {
        return static_cast<unsigned char>(ch) < 0x80U;
    });
}

bool HasClusterBoundaryAt(const std::vector<TextClusterStop>& stops, std::uint32_t local_index) {
    return std::any_of(stops.begin(), stops.end(), [local_index](const TextClusterStop& stop) {
        return stop.index == local_index;
    });
}

std::size_t FindBreakCandidateIndex(const CachedLogicalLineShape& line_shape, std::uint32_t local_index) {
    const auto it = std::lower_bound(
        line_shape.break_candidates.begin(),
        line_shape.break_candidates.end(),
        static_cast<std::int32_t>(local_index));
    if (it == line_shape.break_candidates.end() || *it != static_cast<std::int32_t>(local_index)) {
        return line_shape.break_candidates.size();
    }
    return static_cast<std::size_t>(std::distance(line_shape.break_candidates.begin(), it));
}

struct IncrementalTextDiff {
    std::uint32_t changed_start = 0U;
    std::uint32_t old_changed_end = 0U;
    std::uint32_t new_changed_end = 0U;
    std::int64_t byte_delta = 0;
};

bool ComputeIncrementalTextDiff(
    std::string_view previous_text,
    std::string_view next_text,
    IncrementalTextDiff& out) {
    out = IncrementalTextDiff{};
    if (previous_text == next_text) {
        return false;
    }

    const std::size_t old_length = previous_text.size();
    const std::size_t new_length = next_text.size();
    std::size_t common_prefix = 0U;
    const std::size_t shared_prefix_limit = std::min(old_length, new_length);
    while (common_prefix < shared_prefix_limit && previous_text[common_prefix] == next_text[common_prefix]) {
        common_prefix += 1U;
    }

    std::size_t common_suffix = 0U;
    while (common_suffix < (old_length - common_prefix) &&
           common_suffix < (new_length - common_prefix) &&
           previous_text[old_length - common_suffix - 1U] == next_text[new_length - common_suffix - 1U]) {
        common_suffix += 1U;
    }

    out.changed_start = static_cast<std::uint32_t>(common_prefix);
    out.old_changed_end = static_cast<std::uint32_t>(old_length - common_suffix);
    out.new_changed_end = static_cast<std::uint32_t>(new_length - common_suffix);
    out.byte_delta = static_cast<std::int64_t>(new_length) - static_cast<std::int64_t>(old_length);
    return true;
}

bool ContainsLineBreakInRange(std::string_view text, std::uint32_t start, std::uint32_t end) {
    if (start >= end || start >= text.size()) {
        return false;
    }
    const std::size_t clamped_end = std::min<std::size_t>(end, text.size());
    return text.find_first_of("\r\n", static_cast<std::size_t>(start)) < clamped_end;
}

std::vector<std::int32_t> BuildUtf8CharacterCandidates(std::string_view text) {
    std::vector<std::int32_t> candidates{0};
    for (std::size_t index = 0; index < text.size(); index += 1U) {
        if (index + 1U == text.size() ||
            (static_cast<unsigned char>(text[index + 1U]) & 0xC0U) != 0x80U) {
            candidates.push_back(static_cast<std::int32_t>(index + 1U));
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

bool HasSafeBreakCandidateX(const CachedLogicalLineShape& shape, std::size_t candidate_index) {
    return candidate_index < shape.break_candidate_x_offsets.size() &&
        shape.break_candidate_x_offsets[candidate_index] >= 0.0f;
}

std::size_t CachedLogicalLineTextLength(const CachedLogicalLineShape& shape) {
    return static_cast<std::size_t>(shape.end - shape.visible_start);
}

float CachedLogicalLineXForLocalIndex(const CachedLogicalLineShape& shape, std::uint32_t local_index) {
    const std::size_t line_text_length = CachedLogicalLineTextLength(shape);
    if (local_index == 0U) {
        return 0.0f;
    }
    if (local_index >= line_text_length) {
        return shape.width;
    }
    if (shape.monospace_wrapped_metrics_eligible && shape.monospace_cell_width > 0.0f) {
        return std::min(static_cast<float>(local_index) * shape.monospace_cell_width, shape.width);
    }
    float x = shape.width;
    for (const TextClusterStop& stop : shape.cluster_stops) {
        if (stop.index > local_index) {
            break;
        }
        x = stop.x;
    }
    if (local_index >= line_text_length) {
        x = shape.width;
    }
    return x;
}

float MeasureCachedLogicalLineRangeWidth(
    const CachedLogicalLineShape& shape,
    std::int32_t start,
    std::int32_t end) {
    if (end <= start) {
        return 0.0f;
    }

    const std::uint32_t clamped_start = static_cast<std::uint32_t>(std::max(start, 0));
    const std::uint32_t clamped_end = static_cast<std::uint32_t>(std::max(end, 0));
    if (shape.monospace_wrapped_metrics_eligible && shape.monospace_cell_width > 0.0f) {
        return std::max(
            static_cast<float>(clamped_end - clamped_start) * shape.monospace_cell_width,
            0.0f);
    }

    const float start_x = CachedLogicalLineXForLocalIndex(shape, clamped_start);
    const float end_x = CachedLogicalLineXForLocalIndex(shape, clamped_end);
    return std::max(end_x - start_x, 0.0f);
}

std::size_t FindWrappedBreakShardIndex(
    const std::vector<CachedLogicalLineShape::WrappedBreakShard>& shards,
    std::size_t candidate_index) {
    const auto it = std::lower_bound(
        shards.begin(),
        shards.end(),
        candidate_index,
        [](const CachedLogicalLineShape::WrappedBreakShard& shard, std::size_t target_candidate_index) {
            return shard.end_candidate_index < target_candidate_index;
        });
    if (it == shards.end()) {
        return shards.empty() ? 0U : (shards.size() - 1U);
    }
    return static_cast<std::size_t>(std::distance(shards.begin(), it));
}

void RebuildWrappedBreakShards(CachedLogicalLineShape& shape) {
    shape.break_shards.clear();
    if (shape.break_candidates.empty()) {
        return;
    }

    const std::size_t last_candidate_index = shape.break_candidates.size() - 1U;
    std::size_t shard_start_candidate = 0U;
    while (shard_start_candidate < last_candidate_index) {
        std::size_t shard_end_candidate = std::min(
            last_candidate_index,
            shard_start_candidate + kWrappedBreakShardTargetCandidateCount);
        const std::uint32_t shard_start_offset =
            static_cast<std::uint32_t>(std::max(shape.break_candidates[shard_start_candidate], 0));
        while (shard_end_candidate < last_candidate_index &&
               (static_cast<std::uint32_t>(std::max(shape.break_candidates[shard_end_candidate], 0)) - shard_start_offset) <
                   kWrappedBreakShardTargetCodeUnits) {
            shard_end_candidate += 1U;
        }
        while (shard_end_candidate > shard_start_candidate + 1U &&
               !HasSafeBreakCandidateX(shape, shard_end_candidate)) {
            shard_end_candidate -= 1U;
        }
        if (!HasSafeBreakCandidateX(shape, shard_end_candidate)) {
            std::size_t next_safe_candidate = std::min(last_candidate_index, shard_start_candidate + 1U);
            while (next_safe_candidate < last_candidate_index &&
                   !HasSafeBreakCandidateX(shape, next_safe_candidate)) {
                next_safe_candidate += 1U;
            }
            shard_end_candidate = next_safe_candidate;
        }
        if (shard_end_candidate <= shard_start_candidate) {
            shard_end_candidate = std::min(last_candidate_index, shard_start_candidate + 1U);
        }
        shape.break_shards.push_back(CachedLogicalLineShape::WrappedBreakShard{
            shard_start_candidate,
            shard_end_candidate,
            HasSafeBreakCandidateX(shape, shard_start_candidate)
                ? shape.break_candidate_x_offsets[shard_start_candidate]
                : 0.0f,
            HasSafeBreakCandidateX(shape, shard_end_candidate)
                ? shape.break_candidate_x_offsets[shard_end_candidate]
                : shape.width,
        });
        shard_start_candidate = shard_end_candidate;
    }
}

} // namespace

void UiRuntime::EnsureCachedLogicalLineBreakCandidates(
    std::string_view source_text,
    CachedLogicalLineShape& shape) const {
    if (shape.break_candidate_cache_valid) {
        return;
    }

    shape.break_candidates = {0};
    if (shape.end < shape.visible_start || shape.end > source_text.size()) {
        shape.break_candidate_cache_valid = true;
        return;
    }

    const std::string_view line_text(
        source_text.data() + static_cast<std::size_t>(shape.visible_start),
        static_cast<std::size_t>(shape.end - shape.visible_start));
    shape.break_candidates = ComputeBreakCandidates(line_text);
    if (shape.break_candidates.empty() || shape.break_candidates.front() != 0) {
        shape.break_candidates.insert(shape.break_candidates.begin(), 0);
    }
    if (shape.break_candidates.back() != static_cast<std::int32_t>(line_text.size())) {
        shape.break_candidates.push_back(static_cast<std::int32_t>(line_text.size()));
    }

    shape.break_candidate_x_offsets.assign(shape.break_candidates.size(), kUnknownBreakCandidateX);
    shape.monospace_wrapped_metrics_eligible =
        shape.monospace_fast_path_eligible && shape.monospace_cell_width > 0.0f;
    for (std::size_t candidate_index = 0U; candidate_index < shape.break_candidates.size(); candidate_index += 1U) {
        const std::uint32_t local_index = static_cast<std::uint32_t>(std::max(shape.break_candidates[candidate_index], 0));
        if (local_index == 0U) {
            shape.break_candidate_x_offsets[candidate_index] = 0.0f;
            continue;
        }
        if (local_index >= line_text.size()) {
            shape.break_candidate_x_offsets[candidate_index] = shape.width;
            continue;
        }
        if (shape.monospace_wrapped_metrics_eligible) {
            shape.break_candidate_x_offsets[candidate_index] =
                std::min(static_cast<float>(local_index) * shape.monospace_cell_width, shape.width);
            continue;
        }
        if (HasClusterBoundaryAt(shape.cluster_stops, local_index)) {
            shape.break_candidate_x_offsets[candidate_index] = CachedLogicalLineXForLocalIndex(shape, local_index);
        }
    }

    RebuildWrappedBreakShards(shape);
    shape.break_candidate_cache_valid = true;
}



bool UiRuntime::TryBuildWrappedVisualLineShapeFromLogicalLineShape(
    const CachedLogicalLineShape& line_shape,
    std::uint32_t slice_start,
    std::uint32_t slice_end,
    CachedVisualLineShape& out_shape) const {
    out_shape = CachedVisualLineShape{};
    const std::uint32_t clamped_slice_start = std::clamp(slice_start, line_shape.visible_start, line_shape.end);
    const std::uint32_t clamped_slice_end = std::clamp(slice_end, clamped_slice_start, line_shape.end);
    if (clamped_slice_end <= clamped_slice_start) {
        return false;
    }

    const std::uint32_t local_slice_start = clamped_slice_start - line_shape.visible_start;
    const std::uint32_t local_slice_end = clamped_slice_end - line_shape.visible_start;
    const auto is_safe_boundary = [&](std::uint32_t local_index) {
        if (local_index == 0U || local_index == static_cast<std::uint32_t>(line_shape.end - line_shape.visible_start)) {
            return true;
        }
        if (!line_shape.break_candidate_cache_valid) {
            return false;
        }
        return FindBreakCandidateIndex(line_shape, local_index) < line_shape.break_candidates.size() &&
            HasClusterBoundaryAt(line_shape.cluster_stops, local_index);
    };
    if (!line_shape.ascii_only &&
        (!is_safe_boundary(local_slice_start) || !is_safe_boundary(local_slice_end))) {
        return false;
    }

    const float slice_x = CachedLogicalLineXForLocalIndex(line_shape, local_slice_start);
    const float slice_right = CachedLogicalLineXForLocalIndex(line_shape, local_slice_end);
    const float slice_width = std::max(slice_right - slice_x, 0.0f);

    out_shape.logical_line_index = 0U;
    out_shape.start = clamped_slice_start;
    out_shape.end = clamped_slice_end;
    out_shape.safe_slice_start = clamped_slice_start;
    out_shape.safe_slice_end = clamped_slice_end;
    out_shape.width = slice_width;
    out_shape.height = line_shape.height;
    out_shape.baseline = line_shape.baseline;
    out_shape.ascent = line_shape.ascent;
    out_shape.descent = line_shape.descent;
    out_shape.cache_materialized = true;
    out_shape.cache_dirty = false;

    const std::uint32_t slice_length = local_slice_end - local_slice_start;
    out_shape.glyphs.reserve(line_shape.glyphs.size());
    for (std::size_t index = 0; index < line_shape.glyphs.size(); index += 1U) {
        const GlyphPlacement& glyph = line_shape.glyphs[index];
        const float glyph_left = glyph.x;
        const float glyph_right =
            index + 1U < line_shape.glyphs.size()
            ? std::max(line_shape.glyphs[index + 1U].x, glyph_left)
            : line_shape.width;
        const bool overlaps_slice =
            glyph_right > slice_x &&
            glyph_left < slice_right;
        const bool cluster_inside_slice =
            glyph.cluster >= local_slice_start &&
            glyph.cluster < local_slice_end;
        if (!overlaps_slice && !cluster_inside_slice) {
            continue;
        }
        GlyphPlacement rebased = glyph;
        rebased.x -= slice_x;
        rebased.cluster = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(glyph.cluster) - static_cast<std::int64_t>(local_slice_start),
            0,
            static_cast<std::int64_t>(slice_length)));
        out_shape.glyphs.push_back(rebased);
    }
    out_shape.cluster_stops = BuildTextClusterStops(out_shape.glyphs, out_shape.width, slice_length);
    return true;
}



std::vector<std::int32_t> UiRuntime::ComputeWrappedSegmentBreaks(
    const UINode& node,
    std::string_view segment,
    const CachedLogicalLineShape* cached_shape,
    float width_limit) const {
    const auto compute_with_candidates = [&](const std::vector<std::int32_t>& candidates) {
        std::vector<std::int32_t> breaks{0};
        const std::vector<std::int32_t> character_candidates = BuildUtf8CharacterCandidates(segment);
        std::unordered_map<std::uint64_t, float> width_cache{};
        const auto measure_range = [&](std::int32_t start, std::int32_t end) -> float {
            if (end <= start) {
                return 0.0f;
            }
            if (cached_shape != nullptr && cached_shape->monospace_wrapped_metrics_eligible) {
                return MeasureCachedLogicalLineRangeWidth(*cached_shape, start, end);
            }
            if (cached_shape != nullptr && cached_shape->ascii_only) {
                return MeasureCachedLogicalLineRangeWidth(*cached_shape, start, end);
            }

            const std::uint64_t key =
                (static_cast<std::uint64_t>(static_cast<std::uint32_t>(start)) << 32U) |
                static_cast<std::uint32_t>(end);
            const auto found = width_cache.find(key);
            if (found != width_cache.end()) {
                return found->second;
            }
            const float width = MeasureSingleLineWidth(
                std::string_view(
                    segment.data() + static_cast<std::size_t>(start),
                    static_cast<std::size_t>(end - start)),
                node.font_id,
                node.font_size,
                node.is_obscured);
            width_cache.emplace(key, width);
            return width;
        };

        const auto find_forced_break = [&](std::int32_t line_start) -> std::int32_t {
            const auto start_it = std::lower_bound(character_candidates.begin(), character_candidates.end(), line_start);
            const std::size_t start_index =
                start_it != character_candidates.end() && *start_it == line_start
                ? static_cast<std::size_t>(std::distance(character_candidates.begin(), start_it))
                : static_cast<std::size_t>(std::max<std::ptrdiff_t>(
                    0,
                    static_cast<std::ptrdiff_t>(std::distance(character_candidates.begin(), start_it)) - 1));
            std::int32_t last_fit = line_start;
            for (std::size_t index = start_index + 1U; index < character_candidates.size(); index += 1U) {
                const std::int32_t next = character_candidates[index];
                const float width = measure_range(line_start, next);
                if (width <= width_limit) {
                    last_fit = next;
                    continue;
                }
                break;
            }
            if (last_fit > line_start) {
                return last_fit;
            }
            return character_candidates[std::min(start_index + 1U, character_candidates.size() - 1U)];
        };

        std::int32_t line_start = 0;
        while (line_start < static_cast<std::int32_t>(segment.size())) {
            std::int32_t last_fit = line_start;
            const auto start_it = std::upper_bound(candidates.begin(), candidates.end(), line_start);
            for (auto it = start_it; it != candidates.end(); ++it) {
                const std::int32_t next = *it;
                const float width = measure_range(line_start, next);
                if (width <= width_limit) {
                    last_fit = next;
                    continue;
                }
                break;
            }
            const std::int32_t next_break =
                last_fit > line_start
                ? last_fit
                : find_forced_break(line_start);
            breaks.push_back(next_break);
            line_start = next_break;
        }
        if (breaks.back() != static_cast<std::int32_t>(segment.size())) {
            breaks.push_back(static_cast<std::int32_t>(segment.size()));
        }
        return breaks;
    };

    std::vector<std::int32_t> computed_candidates{};
    const std::vector<std::int32_t>* candidates = nullptr;
    if (cached_shape != nullptr && cached_shape->break_candidate_cache_valid) {
        candidates = &cached_shape->break_candidates;
    } else {
        computed_candidates = ComputeBreakCandidates(segment);
        candidates = &computed_candidates;
    }
    return compute_with_candidates(*candidates);
}



std::vector<std::int32_t> UiRuntime::ComputeIncrementalWrappedSegmentBreaks(
    const UINode& node,
    std::string_view segment,
    const CachedLogicalLineShape& cached_shape,
    float width_limit,
    const std::vector<std::int32_t>& previous_breaks,
    std::uint32_t changed_start,
    std::uint32_t old_changed_end,
    std::uint32_t new_changed_end,
    std::int64_t byte_delta,
    std::size_t start_candidate_index) const {
    if (previous_breaks.size() < 2U) {
        return ComputeWrappedSegmentBreaks(node, segment, &cached_shape, width_limit);
    }

    const auto first_changed_break = static_cast<std::size_t>(
        std::max<std::ptrdiff_t>(
            0,
            static_cast<std::ptrdiff_t>(
                std::lower_bound(
                    previous_breaks.begin(),
                    previous_breaks.end(),
                    static_cast<std::int32_t>(changed_start)) -
                previous_breaks.begin()) - 1));
    const std::int32_t rewrap_start = previous_breaks[std::min(first_changed_break, previous_breaks.size() - 2U)];
    std::vector<std::int32_t> next_breaks{};
    next_breaks.reserve(previous_breaks.size() + 4U);
    next_breaks.insert(
        next_breaks.end(),
        previous_breaks.begin(),
        previous_breaks.begin() + static_cast<std::ptrdiff_t>(first_changed_break + 1U));

    std::vector<std::int32_t> candidates = cached_shape.break_candidate_cache_valid
        ? cached_shape.break_candidates
        : ComputeBreakCandidates(segment);
    if (candidates.empty() || candidates.front() != 0) {
        candidates.insert(candidates.begin(), 0);
    }
    if (candidates.back() != static_cast<std::int32_t>(segment.size())) {
        candidates.push_back(static_cast<std::int32_t>(segment.size()));
    }

    std::unordered_map<std::uint64_t, float> width_cache{};
    const auto measure_range = [&](std::int32_t start, std::int32_t end) -> float {
        if (end <= start) {
            return 0.0f;
        }
        if (cached_shape.monospace_wrapped_metrics_eligible) {
            return MeasureCachedLogicalLineRangeWidth(cached_shape, start, end);
        }
        if (cached_shape.ascii_only) {
            return MeasureCachedLogicalLineRangeWidth(cached_shape, start, end);
        }

        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(start)) << 32U) |
            static_cast<std::uint32_t>(end);
        const auto found = width_cache.find(key);
        if (found != width_cache.end()) {
            return found->second;
        }
        const float width = MeasureSingleLineWidth(
            std::string_view(
                segment.data() + static_cast<std::size_t>(start),
                static_cast<std::size_t>(end - start)),
            node.font_id,
            node.font_size,
            node.is_obscured);
        width_cache.emplace(key, width);
        return width;
    };

    const auto measure_candidate_range = [&](std::size_t start_candidate, std::size_t end_candidate) -> float {
        if (end_candidate <= start_candidate || end_candidate >= candidates.size()) {
            return 0.0f;
        }
        if (HasSafeBreakCandidateX(cached_shape, start_candidate) &&
            HasSafeBreakCandidateX(cached_shape, end_candidate)) {
            return std::max(
                cached_shape.break_candidate_x_offsets[end_candidate] -
                    cached_shape.break_candidate_x_offsets[start_candidate],
                0.0f);
        }
        return measure_range(candidates[start_candidate], candidates[end_candidate]);
    };

    const auto find_last_fitting_candidate = [&](std::size_t line_start_candidate_index,
                                                 std::size_t probe_candidate_index) -> std::size_t {
        std::size_t last_fit_candidate_index = line_start_candidate_index;
        if (probe_candidate_index >= candidates.size()) {
            return last_fit_candidate_index;
        }

        const auto scan_candidates = [&](std::size_t start_candidate, std::size_t end_candidate_exclusive) {
            for (std::size_t candidate_index = start_candidate;
                 candidate_index < end_candidate_exclusive;
                 candidate_index += 1U) {
                const float width = measure_candidate_range(line_start_candidate_index, candidate_index);
                if (width <= width_limit) {
                    last_fit_candidate_index = candidate_index;
                    continue;
                }
                return false;
            }
            return true;
        };

        if (!cached_shape.break_shards.empty() &&
            HasSafeBreakCandidateX(cached_shape, line_start_candidate_index)) {
            std::size_t shard_index = FindWrappedBreakShardIndex(cached_shape.break_shards, probe_candidate_index);
            for (; shard_index < cached_shape.break_shards.size(); shard_index += 1U) {
                const CachedLogicalLineShape::WrappedBreakShard& shard = cached_shape.break_shards[shard_index];
                if (shard.end_candidate_index < probe_candidate_index) {
                    continue;
                }
                const float width_to_shard_end =
                    measure_candidate_range(line_start_candidate_index, shard.end_candidate_index);
                if (width_to_shard_end <= width_limit) {
                    last_fit_candidate_index = shard.end_candidate_index;
                    probe_candidate_index = shard.end_candidate_index + 1U;
                    continue;
                }
                const std::size_t local_probe_start = std::max(probe_candidate_index, shard.start_candidate_index + 1U);
                (void)scan_candidates(
                    local_probe_start,
                    std::min(shard.end_candidate_index + 1U, candidates.size()));
                return last_fit_candidate_index;
            }
        }

        (void)scan_candidates(probe_candidate_index, candidates.size());
        return last_fit_candidate_index;
    };

    const auto line_start_it = std::lower_bound(candidates.begin(), candidates.end(), rewrap_start);
    std::size_t line_start_candidate_index =
        line_start_it != candidates.end() && *line_start_it == rewrap_start
        ? static_cast<std::size_t>(std::distance(candidates.begin(), line_start_it))
        : static_cast<std::size_t>(std::max<std::ptrdiff_t>(
            0,
            static_cast<std::ptrdiff_t>(std::distance(candidates.begin(), line_start_it)) - 1));
    std::size_t next_probe_candidate_index =
        start_candidate_index < candidates.size()
        ? start_candidate_index
        : std::min(line_start_candidate_index + 1U, candidates.size());
    std::size_t old_break_index = first_changed_break + 1U;
    while (line_start_candidate_index + 1U < candidates.size()) {
        const std::size_t last_fit_candidate_index = find_last_fitting_candidate(
            line_start_candidate_index,
            std::max(line_start_candidate_index + 1U, next_probe_candidate_index));
        if (last_fit_candidate_index == line_start_candidate_index) {
            return ComputeWrappedSegmentBreaks(node, segment, &cached_shape, width_limit);
        }
        const std::int32_t line_start = candidates[line_start_candidate_index];
        const std::int32_t last_fit = candidates[last_fit_candidate_index];
        next_breaks.push_back(last_fit);
        const std::int32_t old_line_start =
            static_cast<std::int32_t>(line_start) - static_cast<std::int32_t>(byte_delta);
        if (static_cast<std::uint32_t>(line_start) >= new_changed_end &&
            static_cast<std::uint32_t>(std::max(old_line_start, 0)) >= old_changed_end &&
            old_break_index < previous_breaks.size() &&
            last_fit == static_cast<std::int32_t>(
                static_cast<std::int64_t>(previous_breaks[old_break_index]) + byte_delta)) {
            for (std::size_t suffix_index = old_break_index + 1U; suffix_index < previous_breaks.size(); suffix_index += 1U) {
                next_breaks.push_back(static_cast<std::int32_t>(
                    static_cast<std::int64_t>(previous_breaks[suffix_index]) + byte_delta));
            }
            return next_breaks;
        }

        line_start_candidate_index = last_fit_candidate_index;
        next_probe_candidate_index = line_start_candidate_index + 1U;
        old_break_index += 1U;
    }

    if (next_breaks.back() != static_cast<std::int32_t>(segment.size())) {
        next_breaks.push_back(static_cast<std::int32_t>(segment.size()));
    }
    return next_breaks;
}



bool UiRuntime::TryMaterializeWrappedVisualLineShape(UINode& node, std::size_t visual_line_index) const {
    if (!node.visual_line_shape_cache_valid || visual_line_index >= node.visual_line_shapes.size()) {
        return false;
    }

    CachedVisualLineShape& visual_line = node.visual_line_shapes[visual_line_index];
    if (visual_line.cache_materialized && !visual_line.cache_dirty) {
        return true;
    }
    if (!node.logical_line_shape_cache_valid || visual_line.logical_line_index >= node.logical_line_shapes.size()) {
        return false;
    }

    const CachedLogicalLineShape& logical_line = node.logical_line_shapes[visual_line.logical_line_index];
    CachedVisualLineShape rebuilt{};
    if (!TryBuildWrappedVisualLineShapeFromLogicalLineShape(
            logical_line,
            visual_line.start,
            visual_line.end,
            rebuilt)) {
        const std::string_view line_text(
            node.text_content.data() + static_cast<std::size_t>(visual_line.start),
            static_cast<std::size_t>(visual_line.end - visual_line.start));
        ShapedTextRun shaped{};
        if (!ShapeText(line_text, node.font_id, node.font_size, shaped, node.is_obscured)) {
            return false;
        }
        rebuilt.start = visual_line.start;
        rebuilt.end = visual_line.end;
        rebuilt.safe_slice_start = visual_line.start;
        rebuilt.safe_slice_end = visual_line.end;
        rebuilt.width = shaped.width;
        rebuilt.height = shaped.height;
        rebuilt.baseline = shaped.baseline;
        rebuilt.ascent = shaped.ascent;
        rebuilt.descent = shaped.descent;
        rebuilt.glyphs = std::move(shaped.glyphs);
        rebuilt.cluster_stops = BuildTextClusterStops(
            rebuilt.glyphs,
            rebuilt.width,
            static_cast<std::size_t>(visual_line.end - visual_line.start));
        rebuilt.cache_materialized = true;
        rebuilt.cache_dirty = false;
    }

    visual_line.safe_slice_start = rebuilt.safe_slice_start;
    visual_line.safe_slice_end = rebuilt.safe_slice_end;
    visual_line.width = rebuilt.width;
    visual_line.height = rebuilt.height;
    visual_line.baseline = rebuilt.baseline;
    visual_line.ascent = rebuilt.ascent;
    visual_line.descent = rebuilt.descent;
    visual_line.glyphs = std::move(rebuilt.glyphs);
    visual_line.cluster_stops = std::move(rebuilt.cluster_stops);
    visual_line.cache_materialized = true;
    visual_line.cache_dirty = false;
    return true;
}

const CachedVisualLineShape* UiRuntime::EnsureWrappedVisualLineShape(const UINode& node, std::size_t visual_line_index) const {
    UINode& mutable_node = const_cast<UINode&>(node);
    if (!TryMaterializeWrappedVisualLineShape(mutable_node, visual_line_index)) {
        return nullptr;
    }
    return &mutable_node.visual_line_shapes[visual_line_index];
}

// See docs/v2/ui/TEXT_RUNTIME_OPTIMIZATIONS.md#wrapped-break-metrics. Wrapped
// incremental relayout must preserve clean prefix/suffix cache state and only
// rebuild the affected logical-line neighborhood when the reused metadata is
// still aligned with the edit span.
bool UiRuntime::TryApplyIncrementalWrappedLayoutCache(UINode& node, std::string_view previous_text) const {
    const bool disable_soft_wrap =
        !node.text_wrap || (node.semantic_role == UI_SEMANTIC_TEXTBOX && node.max_lines == 1);
    const std::size_t line_count = node.break_offsets.size() > 1U ? (node.break_offsets.size() - 1U) : 0U;
    if (!node.is_text_node ||
        disable_soft_wrap ||
        !node.text_layout_cache_valid ||
        !node.visual_line_shape_cache_valid ||
        !node.logical_line_shape_cache_valid ||
        node.logical_line_shapes.empty() ||
        line_count == 0U ||
        node.line_widths.size() != line_count ||
        node.visual_line_shapes.size() != line_count ||
        node.text_layout_cache_width_limit < 0.0f) {
        return false;
    }

    const std::string_view next_text = node.text_content;
    IncrementalTextDiff diff{};
    if (!ComputeIncrementalTextDiff(previous_text, next_text, diff)) {
        return false;
    }

    const auto logical_line_index_for_position = [](
                                                     const std::vector<CachedLogicalLineShape>& shapes,
                                                     std::uint32_t pos) -> std::size_t {
        if (shapes.empty()) {
            return 0U;
        }
        const auto it = std::upper_bound(
            shapes.begin(),
            shapes.end(),
            pos,
            [](std::uint32_t value, const CachedLogicalLineShape& shape) {
                return value < shape.end;
            });
        if (it == shapes.end()) {
            return shapes.size() - 1U;
        }
        return static_cast<std::size_t>(std::distance(shapes.begin(), it));
    };
    const auto touched_old_line_start = [&](std::uint32_t changed_start) -> std::size_t {
        std::size_t line_index = logical_line_index_for_position(node.logical_line_shapes, changed_start);
        if (changed_start < previous_text.size() &&
            (previous_text[changed_start] == '\n' || previous_text[changed_start] == '\r') &&
            line_index > 0U) {
            line_index -= 1U;
        }
        return line_index;
    };
    const auto touched_old_line_end = [&](std::uint32_t changed_start, std::uint32_t changed_end) -> std::size_t {
        const std::uint32_t probe =
            changed_end > changed_start
            ? (changed_end - 1U)
            : changed_start;
        std::size_t line_index = logical_line_index_for_position(node.logical_line_shapes, probe);
        if (probe < previous_text.size() &&
            (previous_text[probe] == '\n' || previous_text[probe] == '\r') &&
            line_index > 0U) {
            line_index -= 1U;
        }
        if (changed_end > changed_start &&
            changed_end <= previous_text.size() &&
            (previous_text[changed_end - 1U] == '\n' || previous_text[changed_end - 1U] == '\r')) {
            // Deleting a hard-line break merges the following logical line into the preceding
            // one, so the old touched range must still include that following line before we
            // splice the cached wrapped rows. See docs/v2/ui/TEXT_RUNTIME_OPTIMIZATIONS.md#wrapped-break-metrics.
            line_index = std::max(
                line_index,
                logical_line_index_for_position(node.logical_line_shapes, changed_end));
        }
        if (changed_end == changed_start &&
            changed_start < previous_text.size() &&
            (previous_text[changed_start] == '\n' || previous_text[changed_start] == '\r') &&
            line_index > 0U) {
            line_index -= 1U;
        }
        return line_index;
    };
    const auto touched_new_line_start = [&](std::uint32_t changed_start) -> std::size_t {
        std::size_t line_index = LineIndexForTextLineStarts(node, changed_start);
        if (changed_start < next_text.size() &&
            changed_start < previous_text.size() &&
            (next_text[changed_start] == '\n' || next_text[changed_start] == '\r') &&
            (previous_text[changed_start] == '\n' || previous_text[changed_start] == '\r') &&
            changed_start == GetTextLineStart(node, line_index) &&
            line_index > 0U) {
            line_index -= 1U;
        }
        return line_index;
    };
    const auto touched_new_line_end = [&](std::uint32_t changed_start, std::uint32_t changed_end) -> std::size_t {
        const std::uint32_t probe =
            changed_end > changed_start
            ? (changed_end - 1U)
            : changed_start;
        std::size_t line_index = LineIndexForTextLineStarts(node, probe);
        if (changed_end > changed_start &&
            changed_end <= next_text.size() &&
            (next_text[changed_end - 1U] == '\n' || next_text[changed_end - 1U] == '\r')) {
            line_index = std::max(line_index, LineIndexForTextLineStarts(node, changed_end));
        }
        return line_index;
    };
    struct VisualLineRange {
        std::size_t start = 0U;
        std::size_t end = 0U;
    };
    const auto build_visual_ranges = [](
                                         const std::vector<CachedLogicalLineShape>& logical_shapes,
                                         const std::vector<std::int32_t>& break_offsets,
                                         std::vector<VisualLineRange>& out_ranges) -> bool {
        out_ranges.clear();
        out_ranges.reserve(logical_shapes.size());
        if (break_offsets.size() < 2U) {
            return logical_shapes.empty();
        }

        std::size_t cursor = 0U;
        for (const CachedLogicalLineShape& logical_shape : logical_shapes) {
            if (cursor >= break_offsets.size() - 1U) {
                return false;
            }
            const std::size_t start = cursor;
            do {
                cursor += 1U;
                if (cursor >= break_offsets.size()) {
                    return false;
                }
            } while (cursor < break_offsets.size() - 1U &&
                     static_cast<std::uint32_t>(std::max(break_offsets[cursor], 0)) < logical_shape.end);
            if (static_cast<std::uint32_t>(std::max(break_offsets[cursor], 0)) != logical_shape.end) {
                return false;
            }
            out_ranges.push_back(VisualLineRange{start, cursor});
        }
        return cursor == (break_offsets.size() - 1U);
    };
    const auto try_build_ascii_append_shape = [&](
                                                std::size_t old_line_index,
                                                CachedLogicalLineShape& out_shape) -> bool {
        if (old_line_index >= node.logical_line_shapes.size()) {
            return false;
        }
        CachedLogicalLineShape& old_shape = node.logical_line_shapes[old_line_index];
        if (!old_shape.break_candidate_cache_valid) {
            EnsureCachedLogicalLineBreakCandidates(previous_text, old_shape);
        }
        if (!old_shape.ascii_only ||
            !old_shape.break_candidate_cache_valid ||
            diff.changed_start != old_shape.end ||
            diff.old_changed_end != old_shape.end ||
            diff.new_changed_end <= diff.changed_start) {
            return false;
        }

        const std::string_view appended_text(
            next_text.data() + static_cast<std::size_t>(diff.changed_start),
            static_cast<std::size_t>(diff.new_changed_end - diff.changed_start));
        if (!IsAsciiOnly(appended_text)) {
            return false;
        }

        ShapedTextRun appended_shape{};
        if (!ShapeText(appended_text, node.font_id, node.font_size, appended_shape, node.is_obscured)) {
            return false;
        }
        float appended_monospace_cell_width = 0.0f;
        const bool appended_monospace_fast_path_eligible =
            TryResolveMonospaceFastPathMetrics(appended_text, appended_shape, appended_monospace_cell_width);

        const std::uint32_t old_length = old_shape.end - old_shape.visible_start;
        const float old_width = old_shape.width;
        out_shape = old_shape;
        out_shape.end = diff.new_changed_end;
        out_shape.width = old_shape.width + appended_shape.width;
        out_shape.height = std::max(old_shape.height, appended_shape.height);
        out_shape.baseline = std::max(old_shape.baseline, appended_shape.baseline);
        out_shape.ascent = std::max(old_shape.ascent, appended_shape.ascent);
        out_shape.descent = std::max(old_shape.descent, appended_shape.descent);

        out_shape.glyphs.reserve(old_shape.glyphs.size() + appended_shape.glyphs.size());
        for (const GlyphPlacement& appended_glyph : appended_shape.glyphs) {
            GlyphPlacement shifted = appended_glyph;
            shifted.x += old_width;
            shifted.cluster += old_length;
            out_shape.glyphs.push_back(shifted);
        }

        out_shape.cluster_stops.reserve(old_shape.cluster_stops.size() + appended_shape.glyphs.size() + 2U);
        const std::vector<TextClusterStop> appended_stops =
            BuildTextClusterStops(appended_shape.glyphs, appended_shape.width, appended_text.size());
        for (const TextClusterStop& stop : appended_stops) {
            if (stop.index == 0U) {
                continue;
            }
            out_shape.cluster_stops.push_back(TextClusterStop{
                old_length + stop.index,
                old_width + stop.x,
            });
        }

        out_shape.break_candidates.reserve(old_shape.break_candidates.size() + appended_text.size() + 2U);
        std::vector<std::int32_t> appended_candidates = ComputeBreakCandidates(appended_text);
        if (appended_candidates.empty() || appended_candidates.front() != 0) {
            appended_candidates.insert(appended_candidates.begin(), 0);
        }
        if (appended_candidates.back() != static_cast<std::int32_t>(appended_text.size())) {
            appended_candidates.push_back(static_cast<std::int32_t>(appended_text.size()));
        }
        out_shape.monospace_wrapped_metrics_eligible =
            old_shape.monospace_wrapped_metrics_eligible &&
            appended_monospace_fast_path_eligible &&
            std::fabs(appended_monospace_cell_width - old_shape.monospace_cell_width) <= 0.01f;
        out_shape.break_candidate_x_offsets.reserve(old_shape.break_candidate_x_offsets.size() + appended_candidates.size());
        for (const std::int32_t appended_candidate : appended_candidates) {
            if (appended_candidate <= 0) {
                continue;
            }
            const std::uint32_t local_index = static_cast<std::uint32_t>(std::max(appended_candidate, 0));
            out_shape.break_candidates.push_back(static_cast<std::int32_t>(old_length + local_index));
            out_shape.break_candidate_x_offsets.push_back(out_shape.monospace_wrapped_metrics_eligible
                ? std::min(old_width + (static_cast<float>(local_index) * old_shape.monospace_cell_width), out_shape.width)
                : old_width + ClusterXForIndex(appended_stops, appended_shape.width, local_index, appended_text.size()));
        }
        RebuildWrappedBreakShards(out_shape);
        out_shape.break_candidate_cache_valid = true;
        return true;
    };

    const bool previous_changed_has_line_break =
        ContainsLineBreakInRange(previous_text, diff.changed_start, diff.old_changed_end);
    const bool next_changed_has_line_break =
        ContainsLineBreakInRange(next_text, diff.changed_start, diff.new_changed_end);

    const std::size_t old_start_line = touched_old_line_start(diff.changed_start);
    const std::size_t old_end_line = touched_old_line_end(diff.changed_start, diff.old_changed_end);
    const std::size_t new_start_line = touched_new_line_start(diff.changed_start);
    const std::size_t new_end_line = touched_new_line_end(diff.changed_start, diff.new_changed_end);
    if (old_end_line < old_start_line || new_end_line < new_start_line) {
        return false;
    }
    const bool same_logical_line_patch =
        !previous_changed_has_line_break &&
        !next_changed_has_line_break &&
        new_start_line == new_end_line &&
        old_start_line == old_end_line;
    std::vector<CachedLogicalLineShape> replacement_shapes{};
    replacement_shapes.reserve(new_end_line - new_start_line + 1U);
    for (std::size_t target_line = new_start_line; target_line <= new_end_line; target_line += 1U) {
        CachedLogicalLineShape shape{};
        const bool used_append_fast_path =
            same_logical_line_patch &&
            target_line == new_start_line &&
            try_build_ascii_append_shape(old_start_line, shape);
        if (!used_append_fast_path &&
            !BuildCachedLogicalLineShape(
                node,
                next_text,
                GetTextLineStart(node, target_line),
                GetTextLineEnd(node, target_line),
                shape)) {
            return false;
        }
        replacement_shapes.push_back(std::move(shape));
    }

    std::vector<CachedLogicalLineShape> updated_logical_shapes{};
    updated_logical_shapes.reserve(
        node.logical_line_shapes.size() -
        (old_end_line - old_start_line + 1U) +
        replacement_shapes.size());
    updated_logical_shapes.insert(
        updated_logical_shapes.end(),
        node.logical_line_shapes.begin(),
        node.logical_line_shapes.begin() + static_cast<std::ptrdiff_t>(old_start_line));
    for (const CachedLogicalLineShape& shape : replacement_shapes) {
        updated_logical_shapes.push_back(shape);
    }
    for (std::size_t index = old_end_line + 1U; index < node.logical_line_shapes.size(); index += 1U) {
        CachedLogicalLineShape shifted = node.logical_line_shapes[index];
        shifted.raw_start = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(shifted.raw_start) + diff.byte_delta);
        shifted.visible_start = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(shifted.visible_start) + diff.byte_delta);
        shifted.end = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(shifted.end) + diff.byte_delta);
        updated_logical_shapes.push_back(std::move(shifted));
    }

    std::vector<VisualLineRange> old_visual_ranges{};
    if (!build_visual_ranges(node.logical_line_shapes, node.break_offsets, old_visual_ranges) ||
        old_visual_ranges.size() != node.logical_line_shapes.size()) {
        return false;
    }

    if (same_logical_line_patch &&
        old_start_line == 0U &&
        old_end_line == 0U &&
        new_start_line == 0U &&
        new_end_line == 0U &&
        node.logical_line_shapes.size() == 1U &&
        replacement_shapes.size() == 1U) {
        CachedLogicalLineShape& logical_shape = replacement_shapes.front();
        if (logical_shape.visible_start != logical_shape.end) {
            EnsureCachedLogicalLineBreakCandidates(next_text, logical_shape);
        }
        const CachedLogicalLineShape& old_shape = node.logical_line_shapes.front();
        const VisualLineRange& old_range = old_visual_ranges.front();
        std::vector<std::int32_t> old_local_breaks{0};
        old_local_breaks.reserve((old_range.end - old_range.start) + 1U);
        for (std::size_t visual_index = old_range.start; visual_index < old_range.end; visual_index += 1U) {
            old_local_breaks.push_back(static_cast<std::int32_t>(
                node.visual_line_shapes[visual_index].end - old_shape.visible_start));
        }
        if (old_local_breaks.size() < 2U) {
            return false;
        }
        const std::size_t first_changed_visual = static_cast<std::size_t>(std::max<std::ptrdiff_t>(
            0,
            static_cast<std::ptrdiff_t>(
                std::lower_bound(
                    old_local_breaks.begin(),
                    old_local_breaks.end(),
                    static_cast<std::int32_t>(diff.changed_start - old_shape.visible_start)) -
                old_local_breaks.begin()) - 1));
        const std::size_t start_candidate_index =
            first_changed_visual > 0U &&
            (old_range.start + first_changed_visual - 1U) < node.visual_line_shapes.size()
            ? node.visual_line_shapes[old_range.start + first_changed_visual - 1U].resume_candidate_index
            : 1U;
        const std::vector<std::int32_t> next_local_breaks = ComputeIncrementalWrappedSegmentBreaks(
            node,
            std::string_view(
                next_text.data() + static_cast<std::size_t>(logical_shape.visible_start),
                static_cast<std::size_t>(logical_shape.end - logical_shape.visible_start)),
            logical_shape,
            node.text_layout_cache_width_limit,
            old_local_breaks,
            diff.changed_start - old_shape.visible_start,
            diff.old_changed_end - old_shape.visible_start,
            diff.new_changed_end - logical_shape.visible_start,
            diff.byte_delta,
            start_candidate_index);

        const std::size_t prefix_visual_count = first_changed_visual;
        node.break_offsets.resize(prefix_visual_count + 1U);
        node.line_widths.resize(prefix_visual_count);
        node.line_heights.resize(prefix_visual_count);
        node.line_ascents.resize(prefix_visual_count);
        node.visual_line_shapes.resize(prefix_visual_count);

        const FontMetrics primary_line_box_metrics = ResolvePrimaryLineBoxMetrics(node);
        for (std::size_t boundary_index = first_changed_visual + 1U;
             boundary_index < next_local_breaks.size();
             boundary_index += 1U) {
            const std::int32_t local_start = next_local_breaks[boundary_index - 1U];
            const std::int32_t local_end = next_local_breaks[boundary_index];
            if (local_end < local_start) {
                return false;
            }
            const std::uint32_t absolute_start = logical_shape.visible_start + static_cast<std::uint32_t>(local_start);
            const std::uint32_t absolute_end = logical_shape.visible_start + static_cast<std::uint32_t>(local_end);
            if (absolute_end < absolute_start || absolute_end > next_text.size()) {
                return false;
            }
            const std::uint32_t local_slice_start = absolute_start - logical_shape.visible_start;
            const std::uint32_t local_slice_end = absolute_end - logical_shape.visible_start;
            const float shaped_start_x = CachedLogicalLineXForLocalIndex(logical_shape, local_slice_start);
            const float shaped_end_x = CachedLogicalLineXForLocalIndex(logical_shape, local_slice_end);
            const CachedVisualLineShape visual_shape{
                0U,
                absolute_start,
                absolute_end,
                absolute_start,
                absolute_end,
                logical_shape.break_candidate_cache_valid
                    ? std::min(
                        FindBreakCandidateIndex(logical_shape, local_slice_end) + 1U,
                        logical_shape.break_candidates.size())
                    : 0U,
                std::max(shaped_end_x - shaped_start_x, 0.0f),
                logical_shape.height,
                logical_shape.baseline,
                false,
                true,
                {},
                {},
                logical_shape.ascent,
                logical_shape.descent,
            };
            const FontMetrics line_metrics =
                ResolveLineMetrics(node, primary_line_box_metrics, visual_shape.ascent, visual_shape.descent);
            node.break_offsets.push_back(static_cast<std::int32_t>(visual_shape.end));
            node.line_widths.push_back(visual_shape.width);
            node.line_heights.push_back(line_metrics.height);
            node.line_ascents.push_back(line_metrics.ascent);
            node.visual_line_shapes.push_back(visual_shape);
        }

        node.line_y_offsets.clear();
        node.line_y_offsets.reserve(node.line_heights.size() + 1U);
        node.line_y_offsets.push_back(0.0f);
        for (const float height : node.line_heights) {
            node.line_y_offsets.push_back(node.line_y_offsets.back() + std::max(height, 0.0f));
        }
        node.line_height = node.line_heights.empty()
            ? std::max(primary_line_box_metrics.height, 1.0f)
            : *std::max_element(node.line_heights.begin(), node.line_heights.end());
        node.total_line_count = node.line_widths.size();
        node.visible_line_count = node.max_lines > 0
            ? std::min<std::size_t>(node.total_line_count, static_cast<std::size_t>(node.max_lines))
            : node.total_line_count;
        node.text_layout_cache_max_line_width = node.line_widths.empty()
            ? 0.0f
            : *std::max_element(node.line_widths.begin(), node.line_widths.end());
        node.text_layout_cache_valid = true;
        node.logical_line_shape_cache_valid = true;
        node.logical_line_shapes.front() = std::move(logical_shape);
        node.visual_line_shape_cache_valid = true;
        node.wrapped_single_line_tail_patch_generation += 1U;
        node.nonwrap_fragment_cache_valid = false;
        node.nonwrap_fragment_line_offsets.clear();
        node.nonwrap_fragments.clear();
        node.cached_nonwrap_geometry_slices.clear();
        node.nonwrap_render_fragment_window_valid = false;
        node.nonwrap_render_fragment_start = 0U;
        node.nonwrap_render_fragment_end = 0U;
        node.text_render_window_valid = false;
        node.text_render_line_start = 0U;
        node.text_render_line_end = 0U;
        return node.break_offsets.size() == (node.line_widths.size() + 1U) &&
            node.visual_line_shapes.size() == node.line_widths.size();
    }

    std::vector<std::int32_t> updated_break_offsets{0};
    std::vector<float> updated_line_widths{};
    std::vector<float> updated_line_heights{};
    std::vector<float> updated_line_ascents{};
    std::vector<CachedVisualLineShape> updated_visual_line_shapes{};
    updated_break_offsets.reserve(line_count + 8U);
    updated_line_widths.reserve(line_count + 8U);
    updated_line_heights.reserve(line_count + 8U);
    updated_line_ascents.reserve(line_count + 8U);
    updated_visual_line_shapes.reserve(line_count + 8U);
    const FontMetrics primary_line_box_metrics = ResolvePrimaryLineBoxMetrics(node);
    float updated_max_height = std::max(primary_line_box_metrics.height, 1.0f);
    const auto append_updated_visual_shape = [&](CachedVisualLineShape&& visual_shape) {
        const std::int32_t visual_end = static_cast<std::int32_t>(visual_shape.end);
        const FontMetrics line_metrics =
            ResolveLineMetrics(node, primary_line_box_metrics, visual_shape.ascent, visual_shape.descent);
        if (!updated_visual_line_shapes.empty() && updated_break_offsets.back() == visual_end) {
            updated_line_widths.back() = visual_shape.width;
            updated_line_heights.back() = line_metrics.height;
            updated_line_ascents.back() = line_metrics.ascent;
            updated_max_height = std::max(updated_max_height, line_metrics.height);
            updated_visual_line_shapes.back() = std::move(visual_shape);
            return;
        }
        updated_break_offsets.push_back(visual_end);
        updated_line_widths.push_back(visual_shape.width);
        updated_line_heights.push_back(line_metrics.height);
        updated_line_ascents.push_back(line_metrics.ascent);
        updated_max_height = std::max(updated_max_height, line_metrics.height);
        updated_visual_line_shapes.push_back(std::move(visual_shape));
    };
    const std::int64_t logical_line_delta =
        static_cast<std::int64_t>(replacement_shapes.size()) -
        static_cast<std::int64_t>(old_end_line - old_start_line + 1U);
    const auto append_visual_line = [&](
                                        std::size_t old_visual_index,
                                        const CachedVisualLineShape& visual_shape,
                                        std::int64_t byte_shift,
                                        std::int64_t logical_line_shift) {
        CachedVisualLineShape shifted = visual_shape;
        shifted.logical_line_index = static_cast<std::size_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(shifted.logical_line_index) + logical_line_shift,
            0,
            static_cast<std::int64_t>(updated_logical_shapes.size() - 1U)));
        shifted.start = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(shifted.start) + byte_shift,
            0,
            static_cast<std::int64_t>(next_text.size())));
        shifted.end = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(shifted.end) + byte_shift,
            0,
            static_cast<std::int64_t>(next_text.size())));
        shifted.safe_slice_start = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(shifted.safe_slice_start) + byte_shift,
            0,
            static_cast<std::int64_t>(next_text.size())));
        shifted.safe_slice_end = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(shifted.safe_slice_end) + byte_shift,
            0,
            static_cast<std::int64_t>(next_text.size())));
        const float cached_line_height =
            old_visual_index < node.line_heights.size()
            ? std::max(node.line_heights[old_visual_index], 1.0f)
            : ResolveLineMetrics(node, primary_line_box_metrics, shifted.ascent, shifted.descent).height;
        const float cached_line_ascent =
            old_visual_index < node.line_ascents.size()
            ? std::max(node.line_ascents[old_visual_index], 0.0f)
            : ResolveLineMetrics(node, primary_line_box_metrics, shifted.ascent, shifted.descent).ascent;
        const std::int32_t visual_end = static_cast<std::int32_t>(shifted.end);
        if (!updated_visual_line_shapes.empty() && updated_break_offsets.back() == visual_end) {
            updated_line_widths.back() = shifted.width;
            updated_line_heights.back() = cached_line_height;
            updated_line_ascents.back() = cached_line_ascent;
            updated_max_height = std::max(updated_max_height, cached_line_height);
            updated_visual_line_shapes.back() = std::move(shifted);
            return;
        }
        updated_break_offsets.push_back(visual_end);
        updated_line_widths.push_back(shifted.width);
        updated_line_heights.push_back(cached_line_height);
        updated_line_ascents.push_back(cached_line_ascent);
        updated_max_height = std::max(updated_max_height, cached_line_height);
        updated_visual_line_shapes.push_back(std::move(shifted));
    };
    const auto append_rebuilt_visual_line = [&](
                                             std::size_t logical_line_index,
                                             const CachedLogicalLineShape& logical_shape,
                                             std::int32_t local_start,
                                             std::int32_t local_end) {
        if (local_end < local_start) {
            return false;
        }
        const std::uint32_t absolute_start = logical_shape.visible_start + static_cast<std::uint32_t>(local_start);
        const std::uint32_t absolute_end = logical_shape.visible_start + static_cast<std::uint32_t>(local_end);
        if (absolute_end < absolute_start || absolute_end > next_text.size()) {
            return false;
        }

        const std::uint32_t local_slice_start = absolute_start - logical_shape.visible_start;
        const std::uint32_t local_slice_end = absolute_end - logical_shape.visible_start;
        const float shaped_start_x = CachedLogicalLineXForLocalIndex(logical_shape, local_slice_start);
        const float shaped_end_x = CachedLogicalLineXForLocalIndex(logical_shape, local_slice_end);
        append_updated_visual_shape(CachedVisualLineShape{
            logical_line_index,
            absolute_start,
            absolute_end,
            absolute_start,
            absolute_end,
            logical_shape.break_candidate_cache_valid
                ? std::min(
                    FindBreakCandidateIndex(logical_shape, local_slice_end) + 1U,
                    logical_shape.break_candidates.size())
                : 0U,
            std::max(shaped_end_x - shaped_start_x, 0.0f),
            logical_shape.height,
            logical_shape.baseline,
            false,
            true,
            {},
            {},
            logical_shape.ascent,
            logical_shape.descent,
        });
        return true;
    };

    for (std::size_t old_line = 0U; old_line < old_start_line; old_line += 1U) {
        const VisualLineRange& range = old_visual_ranges[old_line];
        for (std::size_t visual_index = range.start; visual_index < range.end; visual_index += 1U) {
            append_visual_line(visual_index, node.visual_line_shapes[visual_index], 0, 0);
        }
    }

    for (std::size_t target_line = new_start_line; target_line <= new_end_line; target_line += 1U) {
        CachedLogicalLineShape& logical_shape = replacement_shapes[target_line - new_start_line];
        if (logical_shape.visible_start != logical_shape.end) {
            EnsureCachedLogicalLineBreakCandidates(next_text, logical_shape);
        }
        if (same_logical_line_patch && target_line == new_start_line) {
            const CachedLogicalLineShape& old_shape = node.logical_line_shapes[old_start_line];
            const VisualLineRange& old_range = old_visual_ranges[old_start_line];
            std::vector<std::int32_t> old_local_breaks{0};
            old_local_breaks.reserve((old_range.end - old_range.start) + 1U);
            for (std::size_t visual_index = old_range.start; visual_index < old_range.end; visual_index += 1U) {
                old_local_breaks.push_back(static_cast<std::int32_t>(
                    node.visual_line_shapes[visual_index].end - old_shape.visible_start));
            }
            if (old_local_breaks.size() < 2U) {
                return false;
            }
            const std::size_t first_changed_visual = static_cast<std::size_t>(std::max<std::ptrdiff_t>(
                0,
                static_cast<std::ptrdiff_t>(
                    std::lower_bound(
                        old_local_breaks.begin(),
                        old_local_breaks.end(),
                        static_cast<std::int32_t>(diff.changed_start - old_shape.visible_start)) -
                    old_local_breaks.begin()) - 1));
            for (std::size_t visual_index = old_range.start;
                 visual_index < (old_range.start + first_changed_visual);
                 visual_index += 1U) {
                append_visual_line(visual_index, node.visual_line_shapes[visual_index], 0, 0);
            }
            const std::size_t start_candidate_index =
                first_changed_visual > 0U &&
                (old_range.start + first_changed_visual - 1U) < node.visual_line_shapes.size()
                ? node.visual_line_shapes[old_range.start + first_changed_visual - 1U].resume_candidate_index
                : 1U;

            const std::vector<std::int32_t> next_local_breaks = ComputeIncrementalWrappedSegmentBreaks(
                node,
                std::string_view(
                    next_text.data() + static_cast<std::size_t>(logical_shape.visible_start),
                    static_cast<std::size_t>(logical_shape.end - logical_shape.visible_start)),
                logical_shape,
                node.text_layout_cache_width_limit,
                old_local_breaks,
                diff.changed_start - old_shape.visible_start,
                diff.old_changed_end - old_shape.visible_start,
                diff.new_changed_end - logical_shape.visible_start,
                diff.byte_delta,
                start_candidate_index);
            for (std::size_t boundary_index = first_changed_visual + 1U;
                 boundary_index < next_local_breaks.size();
                 boundary_index += 1U) {
                if (!append_rebuilt_visual_line(
                        target_line,
                        logical_shape,
                        next_local_breaks[boundary_index - 1U],
                        next_local_breaks[boundary_index])) {
                    return false;
                }
            }
            continue;
        }

        if (logical_shape.visible_start == logical_shape.end) {
            append_updated_visual_shape(CachedVisualLineShape{
                target_line,
                logical_shape.visible_start,
                logical_shape.end,
                logical_shape.visible_start,
                logical_shape.end,
                0U,
                logical_shape.width,
                logical_shape.height,
                logical_shape.baseline,
                true,
                false,
                logical_shape.glyphs,
                logical_shape.cluster_stops,
                logical_shape.ascent,
                logical_shape.descent,
            });
            continue;
        }

        const std::string_view segment(
            next_text.data() + static_cast<std::size_t>(logical_shape.visible_start),
            static_cast<std::size_t>(logical_shape.end - logical_shape.visible_start));
        const std::vector<std::int32_t> local_breaks =
            ComputeWrappedSegmentBreaks(node, segment, &logical_shape, node.text_layout_cache_width_limit);
        for (std::size_t boundary_index = 1U; boundary_index < local_breaks.size(); boundary_index += 1U) {
            if (!append_rebuilt_visual_line(
                    target_line,
                    logical_shape,
                    local_breaks[boundary_index - 1U],
                    local_breaks[boundary_index])) {
                return false;
            }
        }
    }

    for (std::size_t old_line = old_end_line + 1U; old_line < old_visual_ranges.size(); old_line += 1U) {
        const VisualLineRange& range = old_visual_ranges[old_line];
        for (std::size_t visual_index = range.start; visual_index < range.end; visual_index += 1U) {
            append_visual_line(visual_index, node.visual_line_shapes[visual_index], diff.byte_delta, logical_line_delta);
        }
    }

    node.break_offsets = std::move(updated_break_offsets);
    node.line_widths = std::move(updated_line_widths);
    node.line_heights = std::move(updated_line_heights);
    node.line_ascents = std::move(updated_line_ascents);
    node.line_y_offsets.clear();
    node.line_y_offsets.reserve(node.line_heights.size() + 1U);
    node.line_y_offsets.push_back(0.0f);
    for (const float height : node.line_heights) {
        node.line_y_offsets.push_back(node.line_y_offsets.back() + std::max(height, 0.0f));
    }
    node.line_height = updated_max_height;
    node.total_line_count = node.line_widths.size();
    node.visible_line_count = node.max_lines > 0
        ? std::min<std::size_t>(node.total_line_count, static_cast<std::size_t>(node.max_lines))
        : node.total_line_count;
    node.text_layout_cache_max_line_width = node.line_widths.empty()
        ? 0.0f
        : *std::max_element(node.line_widths.begin(), node.line_widths.end());
    node.text_layout_cache_valid = true;
    node.logical_line_shape_cache_valid = true;
    node.logical_line_shapes = std::move(updated_logical_shapes);
    node.visual_line_shape_cache_valid = true;
    node.visual_line_shapes = std::move(updated_visual_line_shapes);
    node.nonwrap_fragment_cache_valid = false;
    node.nonwrap_fragment_line_offsets.clear();
    node.nonwrap_fragments.clear();
    node.cached_nonwrap_geometry_slices.clear();
    node.nonwrap_render_fragment_window_valid = false;
    node.nonwrap_render_fragment_start = 0U;
    node.nonwrap_render_fragment_end = 0U;
    node.text_render_window_valid = false;
    node.text_render_line_start = 0U;
    node.text_render_line_end = 0U;
    return node.break_offsets.size() == (node.line_widths.size() + 1U) &&
        node.visual_line_shapes.size() == node.line_widths.size();
}



} // namespace effindom::v2::ui
