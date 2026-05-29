#include "UiRuntime.h"

#include <algorithm>

namespace effindom::v2::ui {

namespace {

constexpr float kUnlimitedParagraphWidth = 100000.0f;
constexpr float kNonWrappingFragmentTargetWidth = 512.0f;
constexpr std::size_t kNonWrappingFragmentMaxCodepoints = 256U;
constexpr std::size_t kNonWrappingFragmentOverscanCount = 1U;

std::size_t NextUtf8Codepoint(std::string_view text, std::size_t offset, std::uint32_t* out_codepoint) {
    if (out_codepoint == nullptr || offset >= text.size()) {
        return text.size();
    }

    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80U) {
        *out_codepoint = lead;
        return offset + 1U;
    }

    const auto is_continuation = [&](std::size_t index) {
        return index < text.size() &&
               (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U;
    };

    if ((lead & 0xE0U) == 0xC0U && is_continuation(offset + 1U)) {
        *out_codepoint =
            ((static_cast<std::uint32_t>(lead & 0x1FU)) << 6U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 1U]) & 0x3FU);
        return offset + 2U;
    }
    if ((lead & 0xF0U) == 0xE0U && is_continuation(offset + 1U) && is_continuation(offset + 2U)) {
        *out_codepoint =
            ((static_cast<std::uint32_t>(lead & 0x0FU)) << 12U) |
            ((static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 1U]) & 0x3FU)) << 6U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 2U]) & 0x3FU);
        return offset + 3U;
    }
    if ((lead & 0xF8U) == 0xF0U &&
        is_continuation(offset + 1U) &&
        is_continuation(offset + 2U) &&
        is_continuation(offset + 3U)) {
        *out_codepoint =
            ((static_cast<std::uint32_t>(lead & 0x07U)) << 18U) |
            ((static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 1U]) & 0x3FU)) << 12U) |
            ((static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 2U]) & 0x3FU)) << 6U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 3U]) & 0x3FU);
        return offset + 4U;
    }

    *out_codepoint = 0xFFFD;
    return offset + 1U;
}

bool IsAsciiOnly(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char ch) {
        return static_cast<unsigned char>(ch) < 0x80U;
    });
}

void RebaseNonWrappingFragments(
    std::vector<NonWrappingTextFragment>& fragments,
    std::uint32_t local_byte_offset,
    float x_offset) {
    for (NonWrappingTextFragment& fragment : fragments) {
        fragment.local_byte_start += local_byte_offset;
        fragment.local_byte_end += local_byte_offset;
        fragment.x += x_offset;
    }
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

std::size_t LineIndexForBreakOffsets(const std::vector<std::int32_t>& break_offsets, std::uint32_t byte_index) {
    if (break_offsets.size() < 2U) {
        return 0U;
    }
    const std::uint32_t text_length = static_cast<std::uint32_t>(std::max(break_offsets.back(), 0));
    if (byte_index >= text_length) {
        return break_offsets.size() - 2U;
    }
    const auto it = std::upper_bound(
        break_offsets.begin() + 1,
        break_offsets.end(),
        static_cast<std::int32_t>(byte_index));
    return static_cast<std::size_t>(std::distance(break_offsets.begin(), it) - 1);
}

std::uint32_t SkipLeadingLineBreaks(std::string_view text, std::uint32_t start, std::uint32_t end) {
    std::uint32_t clamped_start = std::min<std::uint32_t>(start, static_cast<std::uint32_t>(text.size()));
    const std::uint32_t clamped_end = std::min<std::uint32_t>(end, static_cast<std::uint32_t>(text.size()));
    while (clamped_start < clamped_end) {
        if (text[clamped_start] == '\r') {
            if (clamped_start + 1U < clamped_end && text[clamped_start + 1U] == '\n') {
                clamped_start += 2U;
            } else {
                clamped_start += 1U;
            }
            continue;
        }
        if (text[clamped_start] == '\n') {
            clamped_start += 1U;
            continue;
        }
        break;
    }
    return clamped_start;
}

} // namespace

// See docs/v2/ui/TEXT_RUNTIME_OPTIMIZATIONS.md#non-wrap-fragment-cache. Render
// and geometry queries share the same overscanned fragment window so scroll-only
// commits stay proportional to the visible horizontal span.
std::vector<NonWrappingTextFragment> UiRuntime::BuildNonWrappingFragmentsForLineImpl(
    std::size_t line_index,
    std::string_view line_text,
    const ShapedTextRun& shaped) const {
    std::vector<NonWrappingTextFragment> fragments{};
    if (line_text.empty()) {
        return fragments;
    }

    float monospace_cell_width = 0.0f;
    if (TryResolveMonospaceFastPathMetrics(line_text, shaped, monospace_cell_width)) {
        const std::size_t columns_per_fragment = std::clamp<std::size_t>(
            static_cast<std::size_t>(std::floor(kNonWrappingFragmentTargetWidth / monospace_cell_width)),
            1U,
            kNonWrappingFragmentMaxCodepoints);
        for (std::size_t fragment_start = 0U; fragment_start < line_text.size(); fragment_start += columns_per_fragment) {
            const std::size_t fragment_end = std::min(fragment_start + columns_per_fragment, line_text.size());
            const float fragment_x = static_cast<float>(fragment_start) * monospace_cell_width;
            const float fragment_width =
                static_cast<float>(fragment_end - fragment_start) * monospace_cell_width;
            fragments.push_back(NonWrappingTextFragment{
                line_index,
                static_cast<std::uint32_t>(fragment_start),
                static_cast<std::uint32_t>(fragment_end),
                fragment_x,
                fragment_width,
            });
        }
        return fragments;
    }

    const std::vector<TextClusterStop> cluster_stops =
        BuildTextClusterStops(shaped.glyphs, shaped.width, line_text.size());
    std::size_t fragment_start = 0U;
    float fragment_x = 0.0f;
    std::size_t fragment_codepoints = 0U;
    for (std::size_t offset = 0U; offset < line_text.size();) {
        std::uint32_t codepoint = 0U;
        const std::size_t next = NextUtf8Codepoint(line_text, offset, &codepoint);
        (void)codepoint;
        fragment_codepoints += 1U;
        const float next_x = ClusterXForIndex(
            cluster_stops,
            shaped.width,
            static_cast<std::uint32_t>(next),
            line_text.size());
        const bool should_split =
            fragment_codepoints >= kNonWrappingFragmentMaxCodepoints ||
            (next_x - fragment_x) >= kNonWrappingFragmentTargetWidth;
        if (should_split) {
            // Fragments are intentionally stable byte/x windows, not semantic tokens.
            // The runtime can then replace one touched window and shift the suffix
            // metadata forward without rebuilding the whole visible hard line.
            fragments.push_back(NonWrappingTextFragment{
                line_index,
                static_cast<std::uint32_t>(fragment_start),
                static_cast<std::uint32_t>(next),
                fragment_x,
                std::max(next_x - fragment_x, 0.0f),
            });
            fragment_start = next;
            fragment_x = next_x;
            fragment_codepoints = 0U;
        }
        offset = next;
    }

    if (fragment_start < line_text.size()) {
        fragments.push_back(NonWrappingTextFragment{
            line_index,
            static_cast<std::uint32_t>(fragment_start),
            static_cast<std::uint32_t>(line_text.size()),
            fragment_x,
            std::max(shaped.width - fragment_x, 0.0f),
        });
    }

    return fragments;
}

UiRuntime::NonWrappingFragmentWindow UiRuntime::ResolveNonWrappingFragmentWindowImpl(
    const UINode& node,
    std::size_t line_index,
    float visible_left,
    float visible_right) const {
    if (!node.nonwrap_fragment_cache_valid ||
        node.nonwrap_fragment_line_offsets.size() < 2U ||
        line_index + 1U >= node.nonwrap_fragment_line_offsets.size()) {
        return {};
    }

    const std::size_t line_fragment_start = node.nonwrap_fragment_line_offsets[line_index];
    const std::size_t line_fragment_end = node.nonwrap_fragment_line_offsets[line_index + 1U];
    if (line_fragment_start >= line_fragment_end) {
        return {};
    }

    const float range_left = std::min(visible_left, visible_right);
    const float range_right = std::max(visible_left, visible_right);
    if (range_right <= range_left) {
        return {};
    }

    const auto first_visible = std::lower_bound(
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start),
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end),
        range_left,
        [](const NonWrappingTextFragment& fragment, float left) {
            return (fragment.x + fragment.width) <= left;
        });
    const auto last_visible = std::lower_bound(
        first_visible,
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end),
        range_right,
        [](const NonWrappingTextFragment& fragment, float right) {
            return fragment.x < right;
        });
    if (first_visible == last_visible) {
        return {};
    }

    const std::size_t first_index =
        static_cast<std::size_t>(std::distance(node.nonwrap_fragments.begin(), first_visible));
    const std::size_t last_index =
        static_cast<std::size_t>(std::distance(node.nonwrap_fragments.begin(), last_visible));
    // The overscan is tiny on purpose: enough to keep glyph joins/caret moves stable
    // around the viewport edge, but still bounded so horizontal scrolling stays
    // proportional to the visible slice instead of total line length.
    return NonWrappingFragmentWindow{
        std::max(line_fragment_start, first_index - std::min(first_index - line_fragment_start, kNonWrappingFragmentOverscanCount)),
        std::min(line_fragment_end, last_index + kNonWrappingFragmentOverscanCount),
    };
}

bool UiRuntime::TryBuildFragmentGeometrySliceFromLogicalLineShapeImpl(
    const UINode& node,
    std::size_t line_index,
    std::uint32_t slice_start,
    std::uint32_t slice_end,
    FragmentGeometrySlice& out) const {
    out = FragmentGeometrySlice{};
    if (!node.logical_line_shape_cache_valid ||
        line_index >= node.logical_line_shapes.size()) {
        return false;
    }

    const CachedLogicalLineShape& line_shape = node.logical_line_shapes[line_index];
    const std::uint32_t clamped_slice_start = std::clamp(slice_start, line_shape.visible_start, line_shape.end);
    const std::uint32_t clamped_slice_end = std::clamp(slice_end, clamped_slice_start, line_shape.end);
    if (clamped_slice_end <= clamped_slice_start) {
        return false;
    }

    const std::uint32_t local_slice_start = clamped_slice_start - line_shape.visible_start;
    const std::uint32_t local_slice_end = clamped_slice_end - line_shape.visible_start;
    const std::size_t line_text_length = static_cast<std::size_t>(line_shape.end - line_shape.visible_start);
    const float slice_x = ClusterXForIndex(line_shape.cluster_stops, line_shape.width, local_slice_start, line_text_length);
    const float slice_right = ClusterXForIndex(line_shape.cluster_stops, line_shape.width, local_slice_end, line_text_length);
    const float slice_width = std::max(slice_right - slice_x, 0.0f);

    out.line_start = line_shape.raw_start;
    out.line_end = line_shape.end;
    out.slice_start = clamped_slice_start;
    out.slice_end = clamped_slice_end;
    out.slice_x = slice_x;
    out.full_line_width = line_shape.width;
    out.shaped.font_id = node.font_id;
    out.shaped.width = slice_width;
    out.shaped.height = line_shape.height;
    out.shaped.baseline = line_shape.baseline;
    out.shaped.ascent = line_shape.ascent;
    out.shaped.descent = line_shape.descent;

    const std::uint32_t slice_length = local_slice_end - local_slice_start;
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
        out.shaped.glyphs.push_back(rebased);
    }

    out.cluster_stops = BuildTextClusterStops(out.shaped.glyphs, out.shaped.width, slice_length);
    return true;
}

// See docs/v2/ui/TEXT_RUNTIME_OPTIMIZATIONS.md#incremental-non-wrap-patching.
// We only restitch the touched fragment band plus overscan, then shift the tail
// metadata forward instead of throwing away the whole paragraph cache.
bool UiRuntime::TryApplyIncrementalNonWrapLayoutCacheImpl(UINode& node, std::string_view previous_text) const {
    const bool disable_soft_wrap =
        !node.text_wrap || (node.semantic_role == UI_SEMANTIC_TEXTBOX && node.max_lines == 1);
    const std::size_t line_count = node.break_offsets.size() > 1U ? (node.break_offsets.size() - 1U) : 0U;
    if (!node.is_text_node ||
        !disable_soft_wrap ||
        !node.text_layout_cache_valid ||
        !node.nonwrap_fragment_cache_valid ||
        line_count == 0U ||
        node.line_widths.size() != line_count ||
        node.nonwrap_fragment_line_offsets.size() != (line_count + 1U)) {
        return false;
    }

    const std::string_view next_text = node.text_content;
    IncrementalTextDiff diff{};
    if (!ComputeIncrementalTextDiff(previous_text, next_text, diff)) {
        return false;
    }

    const auto clear_logical_line_shapes = [&]() {
        node.logical_line_shape_cache_valid = false;
        node.logical_line_shapes.clear();
    };
    const auto build_break_offsets_from_line_starts = [&]() -> std::vector<std::int32_t> {
        std::vector<std::int32_t> next_break_offsets{0};
        next_break_offsets.reserve(node.text_line_starts.size() + 1U);
        for (std::size_t index = 0; index < node.text_line_starts.size(); index += 1U) {
            next_break_offsets.push_back(static_cast<std::int32_t>(GetTextLineEnd(node, index)));
        }
        return next_break_offsets;
    };

    const auto touched_line_start = [](
                                        std::string_view text,
                                        const std::vector<std::int32_t>& break_offsets,
                                        std::uint32_t changed_start) -> std::size_t {
        if (break_offsets.size() < 2U) {
            return 0U;
        }
        std::size_t line_index = LineIndexForBreakOffsets(break_offsets, changed_start);
        if (changed_start < text.size() &&
            (text[changed_start] == '\n' || text[changed_start] == '\r') &&
            line_index > 0U) {
            line_index -= 1U;
        }
        return line_index;
    };

    const auto touched_line_end = [](
                                      std::string_view text,
                                      const std::vector<std::int32_t>& break_offsets,
                                      std::uint32_t changed_start,
                                      std::uint32_t changed_end) -> std::size_t {
        if (break_offsets.size() < 2U) {
            return 0U;
        }
        const std::uint32_t probe =
            changed_end > changed_start
            ? (changed_end - 1U)
            : changed_start;
        std::size_t line_index = LineIndexForBreakOffsets(break_offsets, probe);
        if (probe < text.size() &&
            (text[probe] == '\n' || text[probe] == '\r') &&
            line_index > 0U) {
            line_index -= 1U;
        }
        if (changed_end > changed_start &&
            changed_end <= text.size() &&
            (text[changed_end - 1U] == '\n' || text[changed_end - 1U] == '\r')) {
            line_index = std::max(line_index, LineIndexForBreakOffsets(break_offsets, changed_end));
        }
        if (changed_end == changed_start &&
            changed_start < text.size() &&
            (text[changed_start] == '\n' || text[changed_start] == '\r') &&
            line_index > 0U) {
            line_index -= 1U;
        }
        return line_index;
    };

    struct LineMetricCache {
        std::vector<float> heights{};
        std::vector<float> ascents{};
    };
    const FontMetrics primary_line_box_metrics = ResolvePrimaryLineBoxMetrics(node);
    const auto compute_updated_line_metrics = [&](
                                                  const std::vector<std::int32_t>& current_break_offsets,
                                                  std::size_t current_line_count,
                                                  std::size_t replaced_start_line,
                                                  const std::vector<CachedLogicalLineShape>& replacement_shapes,
                                                  std::size_t replaced_old_line_count) -> std::optional<LineMetricCache> {
        LineMetricCache metrics{};
        metrics.heights.reserve(current_line_count);
        metrics.ascents.reserve(current_line_count);
        const bool can_reuse_old_line_heights =
            node.line_heights.size() == line_count;
        const bool can_reuse_old_line_ascents =
            node.line_ascents.size() == line_count;
        const bool can_reuse_old_logical_shapes =
            node.logical_line_shape_cache_valid &&
            node.logical_line_shapes.size() == line_count;
        for (std::size_t current_line = 0U; current_line < current_line_count; current_line += 1U) {
            if (current_line >= replaced_start_line &&
                current_line < (replaced_start_line + replacement_shapes.size())) {
                const CachedLogicalLineShape& shape = replacement_shapes[current_line - replaced_start_line];
                const FontMetrics line_metrics =
                    ResolveLineMetrics(node, primary_line_box_metrics, shape.ascent, shape.descent);
                metrics.heights.push_back(line_metrics.height);
                metrics.ascents.push_back(line_metrics.ascent);
                continue;
            }

            const std::size_t old_line_index =
                current_line < replaced_start_line
                ? current_line
                : (current_line + replaced_old_line_count - replacement_shapes.size());
            if (can_reuse_old_line_heights &&
                can_reuse_old_line_ascents &&
                old_line_index < node.line_heights.size() &&
                old_line_index < node.line_ascents.size()) {
                metrics.heights.push_back(std::max(node.line_heights[old_line_index], 1.0f));
                metrics.ascents.push_back(std::max(node.line_ascents[old_line_index], 0.0f));
                continue;
            }
            if (can_reuse_old_logical_shapes) {
                if (old_line_index < node.logical_line_shapes.size()) {
                    const CachedLogicalLineShape& shape = node.logical_line_shapes[old_line_index];
                    const FontMetrics line_metrics =
                        ResolveLineMetrics(node, primary_line_box_metrics, shape.ascent, shape.descent);
                    metrics.heights.push_back(line_metrics.height);
                    metrics.ascents.push_back(line_metrics.ascent);
                    continue;
                }
            }

            if (current_line + 1U >= current_break_offsets.size()) {
                return std::nullopt;
            }
            const std::uint32_t raw_start =
                static_cast<std::uint32_t>(std::max(current_break_offsets[current_line], 0));
            const std::uint32_t raw_end =
                static_cast<std::uint32_t>(std::max(current_break_offsets[current_line + 1U], 0));
            CachedLogicalLineShape shape{};
            if (!BuildCachedLogicalLineShape(node, next_text, raw_start, raw_end, shape)) {
                return std::nullopt;
            }
            const FontMetrics line_metrics =
                ResolveLineMetrics(node, primary_line_box_metrics, shape.ascent, shape.descent);
            metrics.heights.push_back(line_metrics.height);
            metrics.ascents.push_back(line_metrics.ascent);
        }
        return metrics;
    };

    const bool previous_changed_has_line_break =
        ContainsLineBreakInRange(previous_text, diff.changed_start, diff.old_changed_end);
    const bool next_changed_has_line_break =
        ContainsLineBreakInRange(next_text, diff.changed_start, diff.new_changed_end);
    const auto apply_structural_line_patch = [&](const std::vector<std::int32_t>& next_break_offsets) -> bool {
        const std::size_t next_line_count =
            next_break_offsets.size() > 1U ? (next_break_offsets.size() - 1U) : 0U;
        if (next_line_count == 0U) {
            return false;
        }

        const std::size_t old_start_line = touched_line_start(previous_text, node.break_offsets, diff.changed_start);
        const std::size_t old_end_line =
            touched_line_end(previous_text, node.break_offsets, diff.changed_start, diff.old_changed_end);
        const std::size_t new_start_line = touched_line_start(next_text, next_break_offsets, diff.changed_start);
        const std::size_t new_end_line =
            touched_line_end(next_text, next_break_offsets, diff.changed_start, diff.new_changed_end);
        if (old_end_line < old_start_line ||
            new_end_line < new_start_line ||
            old_end_line + 1U >= node.nonwrap_fragment_line_offsets.size()) {
            return false;
        }

        const std::size_t old_fragment_start = node.nonwrap_fragment_line_offsets[old_start_line];
        const std::size_t old_fragment_end = node.nonwrap_fragment_line_offsets[old_end_line + 1U];
        if (old_fragment_start > old_fragment_end || old_fragment_end > node.nonwrap_fragments.size()) {
            return false;
        }

        std::vector<CachedLogicalLineShape> replacement_shapes{};
        std::vector<float> replacement_line_widths{};
        std::vector<std::size_t> replacement_fragment_counts{};
        std::vector<NonWrappingTextFragment> replacement_fragments{};
        const std::size_t replacement_line_count = new_end_line - new_start_line + 1U;
        replacement_shapes.reserve(replacement_line_count);
        replacement_line_widths.reserve(replacement_line_count);
        replacement_fragment_counts.reserve(replacement_line_count);

        std::size_t replacement_fragment_total = 0U;
        for (std::size_t target_line = new_start_line; target_line <= new_end_line; target_line += 1U) {
            const std::uint32_t raw_start =
                static_cast<std::uint32_t>(std::max(next_break_offsets[target_line], 0));
            const std::uint32_t raw_end =
                static_cast<std::uint32_t>(std::max(next_break_offsets[target_line + 1U], 0));
            CachedLogicalLineShape shape{};
            if (!BuildCachedLogicalLineShape(node, next_text, raw_start, raw_end, shape)) {
                return false;
            }
            const std::string_view line_text(
                next_text.data() + static_cast<std::size_t>(shape.visible_start),
                static_cast<std::size_t>(shape.end - shape.visible_start));
            std::vector<NonWrappingTextFragment> line_fragments =
                BuildNonWrappingFragmentsForLine(target_line, line_text, UiRuntime::ShapedTextRun{
                    node.font_id,
                    shape.width,
                    shape.height,
                    shape.baseline,
                    shape.ascent,
                    shape.descent,
                    shape.glyphs,
                });
            replacement_line_widths.push_back(shape.width);
            replacement_fragment_counts.push_back(line_fragments.size());
            replacement_fragment_total += line_fragments.size();
            replacement_fragments.insert(
                replacement_fragments.end(),
                line_fragments.begin(),
                line_fragments.end());
            replacement_shapes.push_back(std::move(shape));
        }

        const std::int64_t line_delta =
            static_cast<std::int64_t>(replacement_line_count) -
            static_cast<std::int64_t>(old_end_line - old_start_line + 1U);
        const std::int64_t fragment_delta =
            static_cast<std::int64_t>(replacement_fragment_total) -
            static_cast<std::int64_t>(old_fragment_end - old_fragment_start);
        const auto updated_line_metrics = compute_updated_line_metrics(
            next_break_offsets,
            next_line_count,
            new_start_line,
            replacement_shapes,
            old_end_line - old_start_line + 1U);
        if (!updated_line_metrics.has_value()) {
            return false;
        }

        std::vector<NonWrappingTextFragment> updated_fragments{};
        updated_fragments.reserve(
            node.nonwrap_fragments.size() -
            (old_fragment_end - old_fragment_start) +
            replacement_fragment_total);
        updated_fragments.insert(
            updated_fragments.end(),
            node.nonwrap_fragments.begin(),
            node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(old_fragment_start));
        updated_fragments.insert(
            updated_fragments.end(),
            replacement_fragments.begin(),
            replacement_fragments.end());
        for (std::size_t index = old_fragment_end; index < node.nonwrap_fragments.size(); index += 1U) {
            NonWrappingTextFragment shifted = node.nonwrap_fragments[index];
            if (line_delta != 0) {
                shifted.line_index = static_cast<std::size_t>(
                    static_cast<std::int64_t>(shifted.line_index) + line_delta);
            }
            updated_fragments.push_back(shifted);
        }

        std::vector<float> updated_line_widths{};
        updated_line_widths.reserve(next_line_count);
        updated_line_widths.insert(
            updated_line_widths.end(),
            node.line_widths.begin(),
            node.line_widths.begin() + static_cast<std::ptrdiff_t>(old_start_line));
        updated_line_widths.insert(
            updated_line_widths.end(),
            replacement_line_widths.begin(),
            replacement_line_widths.end());
        updated_line_widths.insert(
            updated_line_widths.end(),
            node.line_widths.begin() + static_cast<std::ptrdiff_t>(old_end_line + 1U),
            node.line_widths.end());

        std::vector<std::size_t> updated_line_offsets{};
        updated_line_offsets.reserve(next_line_count + 1U);
        updated_line_offsets.insert(
            updated_line_offsets.end(),
            node.nonwrap_fragment_line_offsets.begin(),
            node.nonwrap_fragment_line_offsets.begin() + static_cast<std::ptrdiff_t>(old_start_line + 1U));
        std::size_t fragment_offset = updated_line_offsets.empty() ? 0U : updated_line_offsets.back();
        for (const std::size_t fragment_count : replacement_fragment_counts) {
            fragment_offset += fragment_count;
            updated_line_offsets.push_back(fragment_offset);
        }
        for (std::size_t boundary = old_end_line + 2U; boundary < node.nonwrap_fragment_line_offsets.size(); boundary += 1U) {
            updated_line_offsets.push_back(static_cast<std::size_t>(
                static_cast<std::int64_t>(node.nonwrap_fragment_line_offsets[boundary]) + fragment_delta));
        }

        const bool can_update_logical_shapes =
            node.logical_line_shape_cache_valid &&
            node.logical_line_shapes.size() == line_count;
        std::vector<CachedLogicalLineShape> updated_logical_line_shapes{};
        if (can_update_logical_shapes) {
            updated_logical_line_shapes.reserve(next_line_count);
            updated_logical_line_shapes.insert(
                updated_logical_line_shapes.end(),
                node.logical_line_shapes.begin(),
                node.logical_line_shapes.begin() + static_cast<std::ptrdiff_t>(old_start_line));
            for (CachedLogicalLineShape& shape : replacement_shapes) {
                updated_logical_line_shapes.push_back(std::move(shape));
            }
            for (std::size_t index = old_end_line + 1U; index < node.logical_line_shapes.size(); index += 1U) {
                CachedLogicalLineShape shifted = node.logical_line_shapes[index];
                shifted.raw_start = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(shifted.raw_start) + diff.byte_delta);
                shifted.visible_start = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(shifted.visible_start) + diff.byte_delta);
                shifted.end = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(shifted.end) + diff.byte_delta);
                updated_logical_line_shapes.push_back(std::move(shifted));
            }
        }

        node.break_offsets = next_break_offsets;
        node.line_widths = std::move(updated_line_widths);
        node.line_heights = std::move(updated_line_metrics->heights);
        node.line_ascents = std::move(updated_line_metrics->ascents);
        node.line_y_offsets.clear();
        node.line_y_offsets.reserve(node.line_heights.size() + 1U);
        node.line_y_offsets.push_back(0.0f);
        for (const float height : node.line_heights) {
            node.line_y_offsets.push_back(node.line_y_offsets.back() + std::max(height, 0.0f));
        }
        node.line_height = node.line_heights.empty()
            ? 0.0f
            : *std::max_element(node.line_heights.begin(), node.line_heights.end());
        node.total_line_count = next_line_count;
        node.visible_line_count = node.max_lines > 0
            ? std::min<std::size_t>(next_line_count, static_cast<std::size_t>(node.max_lines))
            : next_line_count;
        node.text_layout_cache_max_line_width = node.line_widths.empty()
            ? 0.0f
            : *std::max_element(node.line_widths.begin(), node.line_widths.end());
        node.text_layout_cache_width_limit = kUnlimitedParagraphWidth;
        node.text_layout_cache_valid = true;
        if (can_update_logical_shapes) {
            node.logical_line_shape_cache_valid = true;
            node.logical_line_shapes = std::move(updated_logical_line_shapes);
        } else {
            clear_logical_line_shapes();
        }
        node.nonwrap_fragment_cache_valid = true;
        node.nonwrap_fragments = std::move(updated_fragments);
        node.nonwrap_fragment_line_offsets = std::move(updated_line_offsets);
        node.nonwrap_fragment_cache_generation += 1U;
        node.cached_nonwrap_geometry_slices.clear();
        node.nonwrap_render_fragment_window_valid = false;
        node.nonwrap_render_fragment_start = 0U;
        node.nonwrap_render_fragment_end = 0U;
        node.text_render_window_valid = false;
        node.text_render_line_start = 0U;
        node.text_render_line_end = 0U;
        return true;
    };
    if (previous_changed_has_line_break || next_changed_has_line_break) {
        const bool inserts_before_existing_line_break =
            diff.old_changed_end == diff.changed_start &&
            diff.changed_start < previous_text.size() &&
            (previous_text[diff.changed_start] == '\n' || previous_text[diff.changed_start] == '\r');
        const bool removes_text_before_existing_line_break =
            diff.old_changed_end > diff.changed_start &&
            diff.old_changed_end < previous_text.size() &&
            (previous_text[diff.old_changed_end] == '\n' || previous_text[diff.old_changed_end] == '\r');
        if (inserts_before_existing_line_break || removes_text_before_existing_line_break) {
            return false;
        }
        // Newline structure changes invalidate the cheap same-line patch assumptions.
        // Rebuild the hard-line map first, then restitch only the affected logical-line
        // shard range against the fresh line starts instead of trusting the old spans.
        RebuildTextLineStarts(node);
        return apply_structural_line_patch(build_break_offsets_from_line_starts());
    }

    const std::vector<std::int32_t> next_break_offsets = build_break_offsets_from_line_starts();
    if (next_break_offsets.size() != node.break_offsets.size()) {
        return apply_structural_line_patch(next_break_offsets);
    }

    if (line_count == 1U &&
        node.break_offsets.size() == 2U &&
        node.line_widths.size() == 1U &&
        !ContainsLineBreakInRange(previous_text, 0U, static_cast<std::uint32_t>(previous_text.size())) &&
        !ContainsLineBreakInRange(next_text, 0U, static_cast<std::uint32_t>(next_text.size()))) {
        const std::size_t line_fragment_start = node.nonwrap_fragment_line_offsets[0];
        const std::size_t line_fragment_end = node.nonwrap_fragment_line_offsets[1];
        if (line_fragment_start > line_fragment_end) {
            return false;
        }

        const auto find_fragment_index = [&](std::uint32_t byte_index) -> std::size_t {
            if (line_fragment_start >= line_fragment_end) {
                return line_fragment_start;
            }
            const auto it = std::lower_bound(
                node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start),
                node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end),
                byte_index,
                [](const NonWrappingTextFragment& fragment, std::uint32_t index) {
                    return fragment.local_byte_end <= index;
                });
            if (it == node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end)) {
                return line_fragment_end - 1U;
            }
            return static_cast<std::size_t>(std::distance(node.nonwrap_fragments.begin(), it));
        };

        std::size_t replace_fragment_start = line_fragment_start;
        std::size_t replace_fragment_end = line_fragment_start;
        if (line_fragment_start < line_fragment_end) {
            const std::uint32_t last_old_changed_byte =
                diff.old_changed_end > diff.changed_start ? (diff.old_changed_end - 1U) : diff.changed_start;
            const std::size_t first_touched = find_fragment_index(diff.changed_start);
            const std::size_t last_touched = find_fragment_index(last_old_changed_byte);
            replace_fragment_start =
                std::max(line_fragment_start, first_touched - std::min(first_touched - line_fragment_start, kNonWrappingFragmentOverscanCount));
            replace_fragment_end =
                std::min(line_fragment_end, last_touched + 1U + kNonWrappingFragmentOverscanCount);
        }

        if ((replace_fragment_end - replace_fragment_start) > 2U) {
            ShapedTextRun full_shape{};
            if (!ShapeText(next_text, node.font_id, node.font_size, full_shape, node.is_obscured)) {
                return false;
            }

            node.break_offsets = {0, static_cast<std::int32_t>(next_text.size())};
            node.line_widths = {full_shape.width};
            const FontMetrics full_line_metrics =
                ResolveLineMetrics(node, primary_line_box_metrics, full_shape.ascent, full_shape.descent);
            node.line_ascents = {full_line_metrics.ascent};
            node.line_heights = {full_line_metrics.height};
            node.line_y_offsets = {0.0f, node.line_heights.front()};
            node.line_height = node.line_heights.front();
            node.total_line_count = 1U;
            node.visible_line_count = node.max_lines > 0
                ? std::min<std::size_t>(1U, static_cast<std::size_t>(node.max_lines))
                : 1U;
            node.text_layout_cache_max_line_width = full_shape.width;
            node.text_layout_cache_width_limit = kUnlimitedParagraphWidth;
            node.text_layout_cache_valid = true;
            node.logical_line_shape_cache_valid = true;
            float monospace_cell_width = 0.0f;
            const bool monospace_fast_path_eligible =
                TryResolveMonospaceFastPathMetrics(next_text, full_shape, monospace_cell_width);
            node.logical_line_shapes = {
                CachedLogicalLineShape{
                    0U,
                    0U,
                    static_cast<std::uint32_t>(next_text.size()),
                    full_shape.width,
                    full_shape.height,
                    full_shape.baseline,
                    IsAsciiOnly(next_text),
                    false,
                    {},
                    {},
                    {},
                    full_shape.glyphs,
                    BuildTextClusterStops(full_shape.glyphs, full_shape.width, next_text.size()),
                    full_shape.ascent,
                    full_shape.descent,
                    monospace_fast_path_eligible,
                    monospace_fast_path_eligible,
                    monospace_cell_width,
                }
            };
            node.nonwrap_fragment_cache_valid = true;
            node.nonwrap_fragments = BuildNonWrappingFragmentsForLine(0U, next_text, full_shape);
            node.nonwrap_fragment_line_offsets = {0U, node.nonwrap_fragments.size()};
            node.nonwrap_fragment_cache_generation += 1U;
            node.cached_nonwrap_geometry_slices.clear();
            node.nonwrap_render_fragment_window_valid = false;
            node.nonwrap_render_fragment_start = 0U;
            node.nonwrap_render_fragment_end = 0U;
            node.text_render_window_valid = false;
            node.text_render_line_start = 0U;
            node.text_render_line_end = 0U;
            return true;
        }

        const std::uint32_t old_replace_start =
            replace_fragment_start < line_fragment_end ? node.nonwrap_fragments[replace_fragment_start].local_byte_start : 0U;
        const std::uint32_t old_replace_end =
            replace_fragment_start < line_fragment_end && replace_fragment_end > replace_fragment_start
            ? node.nonwrap_fragments[replace_fragment_end - 1U].local_byte_end
            : static_cast<std::uint32_t>(previous_text.size());
        const std::uint32_t new_replace_start = old_replace_start;
        const std::uint32_t new_replace_end = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(old_replace_end) + diff.byte_delta,
            static_cast<std::int64_t>(new_replace_start),
            static_cast<std::int64_t>(next_text.size())));

        ShapedTextRun replacement_shape{};
        const std::string_view replacement_text = next_text.substr(
            static_cast<std::size_t>(new_replace_start),
            static_cast<std::size_t>(new_replace_end - new_replace_start));
        if (!ShapeText(replacement_text, node.font_id, node.font_size, replacement_shape, node.is_obscured)) {
            return false;
        }

        std::vector<NonWrappingTextFragment> replacement_fragments =
            BuildNonWrappingFragmentsForLine(0U, replacement_text, replacement_shape);
        const float replace_x =
            replace_fragment_start < line_fragment_end
            ? node.nonwrap_fragments[replace_fragment_start].x
            : (!node.nonwrap_fragments.empty()
               ? (node.nonwrap_fragments[line_fragment_end - 1U].x + node.nonwrap_fragments[line_fragment_end - 1U].width)
               : 0.0f);
        RebaseNonWrappingFragments(replacement_fragments, new_replace_start, replace_x);

        float removed_width = 0.0f;
        for (std::size_t index = replace_fragment_start; index < replace_fragment_end; index += 1U) {
            removed_width += node.nonwrap_fragments[index].width;
        }
        const float width_delta = replacement_shape.width - removed_width;

        // Prefix fragments stay byte/x stable, the touched band is replaced in place, and
        // the suffix only needs cheap byte and x shifts. That is the core non-wrap edit
        // optimization: one local reshape, one metadata slide, no full-line rebuild.
        std::vector<NonWrappingTextFragment> updated_fragments{};
        updated_fragments.reserve(
            (line_fragment_end - line_fragment_start) -
            (replace_fragment_end - replace_fragment_start) +
            replacement_fragments.size());
        updated_fragments.insert(
            updated_fragments.end(),
            node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start),
            node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(replace_fragment_start));
        updated_fragments.insert(
            updated_fragments.end(),
            replacement_fragments.begin(),
            replacement_fragments.end());
        for (std::size_t index = replace_fragment_end; index < line_fragment_end; index += 1U) {
            NonWrappingTextFragment shifted = node.nonwrap_fragments[index];
            shifted.local_byte_start = static_cast<std::uint32_t>(static_cast<std::int64_t>(shifted.local_byte_start) + diff.byte_delta);
            shifted.local_byte_end = static_cast<std::uint32_t>(static_cast<std::int64_t>(shifted.local_byte_end) + diff.byte_delta);
            shifted.x += width_delta;
            updated_fragments.push_back(shifted);
        }

        node.break_offsets = {0, static_cast<std::int32_t>(next_text.size())};
        node.line_widths = {std::max(0.0f, node.line_widths.front() + width_delta)};
        const FontMetrics replacement_line_metrics =
            ResolveLineMetrics(node, primary_line_box_metrics, replacement_shape.ascent, replacement_shape.descent);
        const float previous_line_height = node.line_heights.empty() ? primary_line_box_metrics.height : node.line_heights.front();
        const float previous_line_ascent = node.line_ascents.empty() ? primary_line_box_metrics.ascent : node.line_ascents.front();
        node.line_ascents = {std::max(previous_line_ascent, replacement_line_metrics.ascent)};
        node.line_heights = {std::max(previous_line_height, replacement_line_metrics.height)};
        node.line_y_offsets = {0.0f, node.line_heights.front()};
        node.line_height = node.line_heights.front();
        node.total_line_count = 1U;
        node.visible_line_count = node.max_lines > 0
            ? std::min<std::size_t>(1U, static_cast<std::size_t>(node.max_lines))
            : 1U;
        node.text_layout_cache_max_line_width = node.line_widths.front();
        node.text_layout_cache_width_limit = kUnlimitedParagraphWidth;
        node.text_layout_cache_valid = true;
        clear_logical_line_shapes();
        node.nonwrap_fragment_cache_valid = true;
        node.nonwrap_fragments = std::move(updated_fragments);
        node.nonwrap_fragment_line_offsets = {0U, node.nonwrap_fragments.size()};
        node.nonwrap_fragment_cache_generation += 1U;
        node.cached_nonwrap_geometry_slices.clear();
        node.nonwrap_render_fragment_window_valid = false;
        node.nonwrap_render_fragment_start = 0U;
        node.nonwrap_render_fragment_end = 0U;
        node.text_render_window_valid = false;
        node.text_render_line_start = 0U;
        node.text_render_line_end = 0U;
        return true;
    }

    std::size_t line_index = LineIndexForBreakOffsets(node.break_offsets, diff.changed_start);
    if (line_index > 0U &&
        diff.old_changed_end == diff.changed_start &&
        diff.changed_start < previous_text.size() &&
        (previous_text[diff.changed_start] == '\n' || previous_text[diff.changed_start] == '\r')) {
        line_index -= 1U;
    }
    const std::uint32_t old_line_raw_start =
        static_cast<std::uint32_t>(std::max(node.break_offsets[line_index], 0));
    const std::uint32_t old_line_raw_end =
        static_cast<std::uint32_t>(std::max(node.break_offsets[line_index + 1U], 0));
    const std::uint32_t old_line_visible_start =
        SkipLeadingLineBreaks(previous_text, old_line_raw_start, old_line_raw_end);
    if (diff.changed_start < old_line_visible_start || diff.old_changed_end > old_line_raw_end) {
        return false;
    }

    const std::size_t line_fragment_start = node.nonwrap_fragment_line_offsets[line_index];
    const std::size_t line_fragment_end = node.nonwrap_fragment_line_offsets[line_index + 1U];
    if (line_fragment_start > line_fragment_end) {
        return false;
    }

    const auto find_fragment_index = [&](std::uint32_t byte_index) -> std::size_t {
        if (line_fragment_start >= line_fragment_end) {
            return line_fragment_start;
        }
        const std::uint32_t local_index =
            byte_index > old_line_visible_start ? (byte_index - old_line_visible_start) : 0U;
        const auto it = std::lower_bound(
            node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start),
            node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end),
            local_index,
            [](const NonWrappingTextFragment& fragment, std::uint32_t index) {
                return fragment.local_byte_end <= index;
            });
        if (it == node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end)) {
            return line_fragment_end - 1U;
        }
        return static_cast<std::size_t>(std::distance(node.nonwrap_fragments.begin(), it));
    };

    std::size_t replace_fragment_start = line_fragment_start;
    std::size_t replace_fragment_end = line_fragment_start;
    if (line_fragment_start < line_fragment_end) {
        const std::uint32_t last_old_changed_byte =
            diff.old_changed_end > diff.changed_start
            ? (diff.old_changed_end - 1U)
            : std::max(diff.changed_start, old_line_visible_start);
        const std::size_t first_touched = find_fragment_index(diff.changed_start);
        const std::size_t last_touched = find_fragment_index(last_old_changed_byte);
        replace_fragment_start =
            std::max(line_fragment_start, first_touched - std::min(first_touched - line_fragment_start, kNonWrappingFragmentOverscanCount));
        replace_fragment_end =
            std::min(line_fragment_end, last_touched + 1U + kNonWrappingFragmentOverscanCount);
    }

    const std::uint32_t old_replace_start =
        replace_fragment_start < line_fragment_end
        ? node.nonwrap_fragments[replace_fragment_start].local_byte_start
        : 0U;
    const std::uint32_t old_replace_end =
        replace_fragment_start < line_fragment_end && replace_fragment_end > replace_fragment_start
        ? node.nonwrap_fragments[replace_fragment_end - 1U].local_byte_end
        : 0U;
    const std::uint32_t new_line_raw_end = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(old_line_raw_end) + diff.byte_delta,
        static_cast<std::int64_t>(old_line_raw_start),
        static_cast<std::int64_t>(next_text.size())));
    const std::uint32_t new_line_visible_start =
        SkipLeadingLineBreaks(next_text, old_line_raw_start, new_line_raw_end);
    if (new_line_visible_start != old_line_visible_start) {
        return false;
    }
    const std::uint32_t new_replace_start = old_replace_start;
    const std::uint32_t new_replace_end = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(old_replace_end) + diff.byte_delta,
        static_cast<std::int64_t>(new_replace_start),
        static_cast<std::int64_t>(new_line_raw_end - new_line_visible_start)));

    ShapedTextRun replacement_shape{};
    const std::string_view replacement_text = next_text.substr(
        static_cast<std::size_t>(new_line_visible_start + new_replace_start),
        static_cast<std::size_t>(new_replace_end - new_replace_start));
    if (!ShapeText(replacement_text, node.font_id, node.font_size, replacement_shape, node.is_obscured)) {
        return false;
    }

    std::vector<NonWrappingTextFragment> replacement_fragments =
        BuildNonWrappingFragmentsForLine(line_index, replacement_text, replacement_shape);
    const float replace_x =
        replace_fragment_start < line_fragment_end
        ? node.nonwrap_fragments[replace_fragment_start].x
        : (line_fragment_start < line_fragment_end
           ? (node.nonwrap_fragments[line_fragment_end - 1U].x + node.nonwrap_fragments[line_fragment_end - 1U].width)
           : 0.0f);
    RebaseNonWrappingFragments(replacement_fragments, new_replace_start, replace_x);

    float removed_width = 0.0f;
    for (std::size_t index = replace_fragment_start; index < replace_fragment_end; index += 1U) {
        removed_width += node.nonwrap_fragments[index].width;
    }
    const float width_delta = replacement_shape.width - removed_width;

    std::vector<NonWrappingTextFragment> updated_fragments{};
    updated_fragments.reserve(
        node.nonwrap_fragments.size() -
        (replace_fragment_end - replace_fragment_start) +
        replacement_fragments.size());
    updated_fragments.insert(
        updated_fragments.end(),
        node.nonwrap_fragments.begin(),
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start));
    updated_fragments.insert(
        updated_fragments.end(),
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start),
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(replace_fragment_start));
    updated_fragments.insert(
        updated_fragments.end(),
        replacement_fragments.begin(),
        replacement_fragments.end());
    for (std::size_t index = replace_fragment_end; index < line_fragment_end; index += 1U) {
        NonWrappingTextFragment shifted = node.nonwrap_fragments[index];
        shifted.local_byte_start = static_cast<std::uint32_t>(static_cast<std::int64_t>(shifted.local_byte_start) + diff.byte_delta);
        shifted.local_byte_end = static_cast<std::uint32_t>(static_cast<std::int64_t>(shifted.local_byte_end) + diff.byte_delta);
        shifted.x += width_delta;
        updated_fragments.push_back(shifted);
    }
    for (std::size_t index = line_fragment_end; index < node.nonwrap_fragments.size(); index += 1U) {
        updated_fragments.push_back(node.nonwrap_fragments[index]);
    }

    std::vector<std::int32_t> updated_break_offsets = node.break_offsets;
    for (std::size_t index = line_index + 1U; index < updated_break_offsets.size(); index += 1U) {
        updated_break_offsets[index] = static_cast<std::int32_t>(
            static_cast<std::int64_t>(updated_break_offsets[index]) + diff.byte_delta);
    }
    std::vector<float> updated_line_widths = node.line_widths;
    updated_line_widths[line_index] = std::max(0.0f, updated_line_widths[line_index] + width_delta);
    std::vector<std::size_t> updated_line_offsets = node.nonwrap_fragment_line_offsets;
    const std::int64_t fragment_delta =
        static_cast<std::int64_t>(replacement_fragments.size()) -
        static_cast<std::int64_t>(replace_fragment_end - replace_fragment_start);
    for (std::size_t index = line_index + 1U; index < updated_line_offsets.size(); index += 1U) {
        updated_line_offsets[index] = static_cast<std::size_t>(
            static_cast<std::int64_t>(updated_line_offsets[index]) + fragment_delta);
    }
    CachedLogicalLineShape updated_line_shape{};
    if (!BuildCachedLogicalLineShape(node, next_text, old_line_raw_start, new_line_raw_end, updated_line_shape)) {
        return false;
    }
    const auto updated_line_metrics = compute_updated_line_metrics(
        updated_break_offsets,
        line_count,
        line_index,
        std::vector<CachedLogicalLineShape>{updated_line_shape},
        1U);
    if (!updated_line_metrics.has_value()) {
        return false;
    }

    node.break_offsets = std::move(updated_break_offsets);
    node.line_widths = std::move(updated_line_widths);
    node.line_heights = std::move(updated_line_metrics->heights);
    node.line_ascents = std::move(updated_line_metrics->ascents);
    node.line_y_offsets.clear();
    node.line_y_offsets.reserve(node.line_heights.size() + 1U);
    node.line_y_offsets.push_back(0.0f);
    for (const float height : node.line_heights) {
        node.line_y_offsets.push_back(node.line_y_offsets.back() + std::max(height, 0.0f));
    }
    node.line_height = node.line_heights.empty()
        ? 0.0f
        : *std::max_element(node.line_heights.begin(), node.line_heights.end());
    node.total_line_count = line_count;
    node.visible_line_count = node.max_lines > 0
        ? std::min<std::size_t>(line_count, static_cast<std::size_t>(node.max_lines))
        : line_count;
    node.text_layout_cache_max_line_width = node.line_widths.empty()
        ? 0.0f
        : *std::max_element(node.line_widths.begin(), node.line_widths.end());
    node.text_layout_cache_width_limit = kUnlimitedParagraphWidth;
    node.text_layout_cache_valid = true;
    clear_logical_line_shapes();
    node.nonwrap_fragment_cache_valid = true;
    node.nonwrap_fragments = std::move(updated_fragments);
    node.nonwrap_fragment_line_offsets = std::move(updated_line_offsets);
    node.nonwrap_fragment_cache_generation += 1U;
    node.cached_nonwrap_geometry_slices.clear();
    node.nonwrap_render_fragment_window_valid = false;
    node.nonwrap_render_fragment_start = 0U;
    node.nonwrap_render_fragment_end = 0U;
    node.text_render_window_valid = false;
    node.text_render_line_start = 0U;
    node.text_render_line_end = 0U;
    return true;
}

} // namespace effindom::v2::ui
