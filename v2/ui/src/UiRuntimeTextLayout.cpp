#include "UiRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <unordered_map>

#include <hb-ot.h>
#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/udata.h>
#include <unicode/unistr.h>

namespace effindom::v2::ui {

namespace {

constexpr float kUnlimitedParagraphWidth = 100000.0f;
using ProfileClock = std::chrono::steady_clock;

double ElapsedMilliseconds(ProfileClock::time_point start, ProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

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

std::vector<TextClusterStop> BuildClusterStops(
    const std::vector<GlyphPlacement>& glyphs,
    float shaped_width,
    std::size_t line_length) {
    std::vector<TextClusterStop> stops{};
    stops.reserve(glyphs.size() + 2U);
    stops.push_back(TextClusterStop{0U, 0.0f});
    for (const GlyphPlacement& glyph : glyphs) {
        stops.push_back(TextClusterStop{
            static_cast<std::uint32_t>(std::min<std::size_t>(glyph.cluster, line_length)),
            glyph.x,
        });
    }
    stops.push_back(TextClusterStop{static_cast<std::uint32_t>(line_length), shaped_width});

    std::stable_sort(stops.begin(), stops.end(), [](const TextClusterStop& lhs, const TextClusterStop& rhs) {
        return lhs.index < rhs.index;
    });

    std::vector<TextClusterStop> deduped{};
    deduped.reserve(stops.size());
    for (const TextClusterStop& stop : stops) {
        if (!deduped.empty() && deduped.back().index == stop.index) {
            deduped.back().x = std::min(deduped.back().x, stop.x);
            continue;
        }
        deduped.push_back(stop);
    }
    return deduped;
}

std::size_t FindBreakCandidateIndex(const CachedLogicalLineShape& line_shape, std::uint32_t local_index) {
    if (line_shape.break_candidates.empty()) {
        return 0U;
    }
    const auto it = std::lower_bound(
        line_shape.break_candidates.begin(),
        line_shape.break_candidates.end(),
        static_cast<std::int32_t>(local_index));
    return static_cast<std::size_t>(std::distance(line_shape.break_candidates.begin(), it));
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

} // namespace

// See docs/v2/ui/TEXT_RUNTIME_OPTIMIZATIONS.md#layout-cache-tiers and
// #wrapped-break-metrics. Paragraph layout owns the retained logical-line cache,
// while wrapped/non-wrap hot paths consume that cache from their own files.
bool UiRuntime::BuildCachedLogicalLineShapeImpl(
    const UINode& node,
    std::string_view source_text,
    std::uint32_t raw_start,
    std::uint32_t raw_end,
    CachedLogicalLineShape& out_shape) const {
    out_shape = CachedLogicalLineShape{};
    if (raw_end < raw_start || raw_end > source_text.size()) {
        return false;
    }

    const std::uint32_t visible_start = SkipLeadingLineBreaks(source_text, raw_start, raw_end);
    const std::string_view line_text(
        source_text.data() + static_cast<std::size_t>(visible_start),
        static_cast<std::size_t>(raw_end - visible_start));
    ShapedTextRun shaped{};
    if (!ShapeTextStyledRange(node, visible_start, raw_end, shaped)) {
        return false;
    }

    float monospace_cell_width = 0.0f;
    const bool monospace_fast_path_eligible =
        TryResolveMonospaceFastPathMetrics(line_text, shaped, monospace_cell_width);

    out_shape = CachedLogicalLineShape{
        raw_start,
        visible_start,
        raw_end,
        shaped.width,
        shaped.height,
        shaped.baseline,
        IsAsciiOnly(line_text),
        line_text.find('\t') != std::string_view::npos,
        false,
        {},
        {},
        {},
        shaped.glyphs,
        BuildTextClusterStops(shaped.glyphs, shaped.width, line_text.size()),
        shaped.ascent,
        shaped.descent,
        monospace_fast_path_eligible,
        monospace_fast_path_eligible,
        monospace_cell_width,
    };
    return true;
}

bool UiRuntime::TryResolveMonospaceFastPathMetrics(
    std::string_view text,
    const ShapedTextRun& shaped,
    float& out_cell_width) const {
    out_cell_width = 0.0f;
    if (text.empty() || !IsAsciiOnly(text)) {
        return false;
    }

    const std::uint32_t primary_font_id = shaped.font_id;
    for (const GlyphPlacement& glyph : shaped.glyphs) {
        if (glyph.font_id != primary_font_id) {
            return false;
        }
    }

    const RegisteredFont* font = LookupFont(primary_font_id);
    const bool bundled_mono_font =
        primary_font_id == 5U || primary_font_id == 6U;
    if (font == nullptr ||
        font->font == nullptr ||
        !(font->is_ascii_fixed_pitch || bundled_mono_font)) {
        return false;
    }
    const std::optional<std::uint32_t> columns =
        FixedPitchTabModel::ColumnForByteOffset(text, text.size());
    if (!columns.has_value() || *columns == 0U) {
        return false;
    }
    out_cell_width = shaped.width / static_cast<float>(*columns);
    if (!std::isfinite(out_cell_width) || out_cell_width <= 0.0f) {
        return false;
    }
    if (text.find('\t') == std::string_view::npos) {
        return true;
    }
    const std::vector<TextClusterStop> stops =
        BuildTextClusterStops(shaped.glyphs, shaped.width, text.size());
    for (std::size_t offset = 0U; offset <= text.size(); offset += 1U) {
        const std::optional<float> expected_x =
            FixedPitchTabModel::XForByteOffset(text, offset, out_cell_width);
        if (!expected_x.has_value()) {
            return false;
        }
        const auto stop = std::find_if(stops.begin(), stops.end(), [&](const TextClusterStop& candidate) {
            return candidate.index == offset;
        });
        if (stop == stops.end() || std::fabs(stop->x - *expected_x) > 0.01f) {
            return false;
        }
    }
    return true;
}

UiRuntime::ParagraphLayout UiRuntime::LayoutParagraphImpl(const UINode& node, std::optional<float> max_width) const {
    ParagraphLayout paragraph{};
    if (!node.is_text_node) {
        return paragraph;
    }
    const bool profile_active = text_commit_profile_active_;
    const ProfileClock::time_point paragraph_start = profile_active ? ProfileClock::now() : ProfileClock::time_point{};
    const FontMetrics primary_line_box_metrics = ResolvePrimaryLineBoxMetrics(node);
    paragraph.line_height = primary_line_box_metrics.height;

    const bool disable_soft_wrap =
        !node.text_wrap || (IsSingleLineEditorTextNode(node));
    const float width_limit =
        disable_soft_wrap
        ? kUnlimitedParagraphWidth
        : (max_width.has_value() ? std::max(max_width.value(), 0.0f) : kUnlimitedParagraphWidth);
    const auto populate_from_node = [&](const UINode& source) {
        paragraph.break_offsets = source.break_offsets;
        paragraph.line_widths = source.line_widths;
        paragraph.line_heights = source.line_heights;
        paragraph.line_ascents = source.line_ascents;
        paragraph.line_y_offsets = source.line_y_offsets;
        paragraph.max_line_width = source.text_layout_cache_max_line_width;
        paragraph.width = source.text_layout_cache_max_line_width;
        paragraph.line_height = std::max(source.line_height, 1.0f);
        paragraph.visible_line_count = source.visible_line_count;
        paragraph.total_line_count = source.total_line_count;
        paragraph.clipped = paragraph.total_line_count > paragraph.visible_line_count;
        paragraph.height =
            paragraph.visible_line_count < paragraph.line_y_offsets.size()
            ? paragraph.line_y_offsets[paragraph.visible_line_count]
            : (static_cast<float>(paragraph.visible_line_count) * paragraph.line_height);
    };
    // Width-limit equality is the guard that lets selection-only or scroll-only work
    // reuse the retained paragraph result without touching shaping again.
    if (node.text_layout_cache_valid && std::abs(node.text_layout_cache_width_limit - width_limit) < 0.001f) {
        if (profile_active) {
            current_text_commit_profile_.layout_paragraph_calls += 1U;
            current_text_commit_profile_.layout_paragraph_cache_hits += 1U;
            current_text_commit_profile_.wrapped_layout_calls += disable_soft_wrap ? 0U : 1U;
            current_text_commit_profile_.nonwrap_layout_calls += disable_soft_wrap ? 1U : 0U;
            current_text_commit_profile_.layout_paragraph_total_ms +=
                ElapsedMilliseconds(paragraph_start, ProfileClock::now());
        }
        populate_from_node(node);
        return paragraph;
    }

    const RegisteredFont* font = LookupFont(node.font_id);
    if (node.text_content.empty()) {
        ShapedTextRun empty_line{};
        if (font != nullptr && font->font != nullptr) {
            (void)ShapeText(std::string_view{}, node.font_id, node.font_size, empty_line, node.is_obscured);
        } else {
            empty_line.font_id = node.font_id;
            empty_line.height = primary_line_box_metrics.height;
            empty_line.baseline = primary_line_box_metrics.ascent;
            empty_line.ascent = empty_line.baseline;
            empty_line.descent = empty_line.height - empty_line.ascent;
        }
        UINode& mutable_node = const_cast<UINode&>(node);
        mutable_node.break_offsets = {0, 0};
        mutable_node.line_widths = {0.0f};
        mutable_node.line_heights = {primary_line_box_metrics.height};
        mutable_node.line_ascents = {primary_line_box_metrics.ascent};
        mutable_node.line_y_offsets = {0.0f, mutable_node.line_heights.front()};
        mutable_node.total_line_count = 1U;
        mutable_node.visible_line_count = node.max_lines > 0
            ? std::min<std::size_t>(1U, static_cast<std::size_t>(node.max_lines))
            : 1U;
        mutable_node.line_height = mutable_node.line_heights.front();
        mutable_node.text_layout_cache_max_line_width = 0.0f;
        mutable_node.visual_line_shape_cache_valid = !disable_soft_wrap;
        mutable_node.visual_line_shapes.clear();
        if (!disable_soft_wrap) {
            mutable_node.visual_line_shapes.push_back(CachedVisualLineShape{
                0U,
                0U,
                0U,
                0U,
                0U,
                0U,
                0.0f,
                empty_line.height,
                empty_line.baseline,
                true,
                false,
                empty_line.glyphs,
                BuildTextClusterStops(empty_line.glyphs, 0.0f, 0U),
                empty_line.ascent,
                empty_line.descent,
            });
        }
        mutable_node.nonwrap_fragment_cache_valid = disable_soft_wrap;
        mutable_node.nonwrap_fragment_line_offsets = {0U, 0U};
        mutable_node.nonwrap_fragments.clear();
        mutable_node.nonwrap_fragment_cache_generation += disable_soft_wrap ? 1U : 0U;
        mutable_node.text_layout_cache_width_limit = width_limit;
        mutable_node.text_layout_cache_valid = true;
        populate_from_node(mutable_node);
        return paragraph;
    }
    if constexpr (kRequiresRegisteredIcuData) {
        if (!icu_data_registered_) {
            return paragraph;
        }
    }
    if (font == nullptr || font->font == nullptr) {
        return paragraph;
    }

    const auto ensure_logical_line_shapes = [&]() -> const std::vector<CachedLogicalLineShape>& {
        UINode& mutable_node = const_cast<UINode&>(node);
        if (mutable_node.logical_line_shape_cache_valid) {
            if (profile_active) {
                current_text_commit_profile_.logical_line_shape_cache_hits += 1U;
            }
            return mutable_node.logical_line_shapes;
        }
        const ProfileClock::time_point logical_shapes_start = profile_active ? ProfileClock::now() : ProfileClock::time_point{};
        if (mutable_node.text_line_starts_dirty || mutable_node.text_line_starts.empty()) {
            RebuildTextLineStarts(mutable_node);
        }

        mutable_node.logical_line_shapes.clear();
        const std::size_t hard_line_count = mutable_node.text_line_starts.size();
        mutable_node.logical_line_shapes.reserve(hard_line_count);
        for (std::size_t index = 0; index < hard_line_count; index += 1U) {
            const std::uint32_t raw_start = GetTextLineStart(mutable_node, index);
            const std::uint32_t end = GetTextLineEnd(mutable_node, index);
            CachedLogicalLineShape shape{};
            if (!BuildCachedLogicalLineShapeImpl(node, node.text_content, raw_start, end, shape)) {
                return mutable_node.logical_line_shapes;
            }
            mutable_node.logical_line_shapes.push_back(std::move(shape));
        }
        mutable_node.logical_line_shape_cache_valid = true;
        if (profile_active) {
            current_text_commit_profile_.logical_line_shape_cache_builds += 1U;
            current_text_commit_profile_.logical_line_shapes_built += static_cast<std::uint32_t>(hard_line_count);
            current_text_commit_profile_.logical_line_shape_build_ms +=
                ElapsedMilliseconds(logical_shapes_start, ProfileClock::now());
        }
        return mutable_node.logical_line_shapes;
    };

    const auto& logical_line_shapes = ensure_logical_line_shapes();

    std::vector<std::int32_t> break_offsets{0};
    std::vector<std::size_t> wrapped_visual_line_logical_indices{};
    break_offsets.reserve(logical_line_shapes.size() + 1U);
    if (disable_soft_wrap) {
        for (const CachedLogicalLineShape& line : logical_line_shapes) {
            break_offsets.push_back(static_cast<std::int32_t>(line.end));
        }
    } else {
        UINode& mutable_node = const_cast<UINode&>(node);
        for (std::size_t line_index = 0; line_index < mutable_node.logical_line_shapes.size(); line_index += 1U) {
            CachedLogicalLineShape& line = mutable_node.logical_line_shapes[line_index];
            if (line.visible_start == line.end) {
                break_offsets.push_back(static_cast<std::int32_t>(line.end));
                wrapped_visual_line_logical_indices.push_back(line_index);
                continue;
            }
            if (profile_active) {
                if (line.break_candidate_cache_valid) {
                    current_text_commit_profile_.break_candidate_cache_hits += 1U;
                } else {
                    const ProfileClock::time_point break_candidate_start = ProfileClock::now();
                    EnsureCachedLogicalLineBreakCandidates(node.text_content, line);
                    current_text_commit_profile_.break_candidate_cache_builds += 1U;
                    current_text_commit_profile_.break_candidate_build_ms +=
                        ElapsedMilliseconds(break_candidate_start, ProfileClock::now());
                }
            } else {
                EnsureCachedLogicalLineBreakCandidates(node.text_content, line);
            }
            const std::string_view segment(
                node.text_content.data() + static_cast<std::size_t>(line.visible_start),
                static_cast<std::size_t>(line.end - line.visible_start));
            std::vector<std::int32_t> segment_breaks{};
            if (profile_active) {
                const ProfileClock::time_point segment_breaks_start = ProfileClock::now();
                segment_breaks = ComputeWrappedSegmentBreaks(node, segment, &line, width_limit);
                current_text_commit_profile_.wrapped_segment_break_calls += 1U;
                current_text_commit_profile_.wrapped_segment_break_ms +=
                    ElapsedMilliseconds(segment_breaks_start, ProfileClock::now());
            } else {
                segment_breaks = ComputeWrappedSegmentBreaks(node, segment, &line, width_limit);
            }
            for (std::size_t index = 1; index < segment_breaks.size(); index += 1U) {
                break_offsets.push_back(static_cast<std::int32_t>(line.visible_start) + segment_breaks[index]);
                wrapped_visual_line_logical_indices.push_back(line_index);
            }
        }
    }
    const std::size_t total_line_count = break_offsets.size() > 1U ? break_offsets.size() - 1U : 0U;
    std::vector<float> line_widths{};
    std::vector<float> line_heights{};
    std::vector<float> line_ascents{};
    line_widths.reserve(total_line_count);
    line_heights.reserve(total_line_count);
    line_ascents.reserve(total_line_count);
    float max_line_width = 0.0f;
    float max_line_height = paragraph.line_height;
    std::vector<CachedVisualLineShape> visual_line_shapes{};
    std::vector<std::size_t> nonwrap_fragment_line_offsets{};
    std::vector<NonWrappingTextFragment> nonwrap_fragments{};
    if (!disable_soft_wrap) {
        visual_line_shapes.reserve(total_line_count);
        wrapped_visual_line_logical_indices.reserve(total_line_count);
    }
    if (disable_soft_wrap) {
        nonwrap_fragment_line_offsets.reserve(total_line_count + 1U);
        nonwrap_fragment_line_offsets.push_back(0U);
    }
    for (std::size_t index = 0; index < total_line_count; index += 1U) {
        const std::int32_t start = break_offsets[index];
        const std::int32_t end = break_offsets[index + 1U];
        std::uint32_t line_start = static_cast<std::uint32_t>(start);
        std::string_view line_text(
            node.text_content.data() + line_start,
            static_cast<std::size_t>(end - start));
        while (!line_text.empty() && (line_text.front() == '\n' || line_text.front() == '\r')) {
            line_text.remove_prefix(1U);
            line_start += 1U;
        }
        const std::size_t logical_line_index =
            !disable_soft_wrap && index < wrapped_visual_line_logical_indices.size()
            ? wrapped_visual_line_logical_indices[index]
            : index;
        ShapedTextRun shaped{};
        if (disable_soft_wrap &&
            index < logical_line_shapes.size() &&
            logical_line_shapes[index].visible_start == line_start &&
            logical_line_shapes[index].end == static_cast<std::uint32_t>(end)) {
            shaped.font_id = node.font_id;
            shaped.width = logical_line_shapes[index].width;
            shaped.height = logical_line_shapes[index].height;
            shaped.baseline = logical_line_shapes[index].baseline;
            shaped.ascent = logical_line_shapes[index].ascent;
            shaped.descent = logical_line_shapes[index].descent;
            shaped.glyphs = logical_line_shapes[index].glyphs;
        } else if (disable_soft_wrap ||
                   logical_line_index >= logical_line_shapes.size()) {
            (void)ShapeTextStyledRange(node, line_start, static_cast<std::uint32_t>(end), shaped);
        }

        float visual_width = shaped.width;
        float visual_height = shaped.height;
        float visual_baseline = shaped.baseline;
        float visual_ascent = shaped.ascent;
        float visual_descent = shaped.descent;
        if (!disable_soft_wrap) {
            std::size_t resume_candidate_index = 0U;
            if (logical_line_index < logical_line_shapes.size()) {
                const CachedLogicalLineShape& logical_line = logical_line_shapes[logical_line_index];
                const std::uint32_t local_start =
                    line_start >= logical_line.visible_start ? (line_start - logical_line.visible_start) : 0U;
                const std::uint32_t local_end =
                    static_cast<std::uint32_t>(end) >= logical_line.visible_start
                    ? (static_cast<std::uint32_t>(end) - logical_line.visible_start)
                    : 0U;
                if (logical_line.has_tabs) {
                    visual_width = MeasureSingleLineWidth(
                        std::string_view(
                            node.text_content.data() + static_cast<std::size_t>(line_start),
                            static_cast<std::size_t>(end - static_cast<std::int32_t>(line_start))),
                        node.font_id,
                        node.font_size,
                        node.is_obscured);
                } else {
                    visual_width = std::max(
                        ClusterXForIndex(
                            logical_line.cluster_stops,
                            logical_line.width,
                            local_end,
                            static_cast<std::size_t>(logical_line.end - logical_line.visible_start)) -
                        ClusterXForIndex(
                            logical_line.cluster_stops,
                            logical_line.width,
                            local_start,
                            static_cast<std::size_t>(logical_line.end - logical_line.visible_start)),
                        0.0f);
                }
                visual_height = logical_line.height;
                visual_baseline = logical_line.baseline;
                visual_ascent = logical_line.ascent;
                visual_descent = logical_line.descent;
                resume_candidate_index = logical_line.break_candidate_cache_valid
                    ? std::min(
                        FindBreakCandidateIndex(logical_line, local_end) + 1U,
                        logical_line.break_candidates.size())
                    : 0U;
            }
            const FontMetrics line_metrics =
                ResolveLineMetrics(node, primary_line_box_metrics, visual_ascent, visual_descent);
            max_line_height = std::max(max_line_height, line_metrics.height);
            max_line_width = std::max(max_line_width, visual_width);
            line_widths.push_back(visual_width);
            line_heights.push_back(line_metrics.height);
            line_ascents.push_back(line_metrics.ascent);
            visual_line_shapes.push_back(CachedVisualLineShape{
                logical_line_index,
                line_start,
                static_cast<std::uint32_t>(end),
                line_start,
                static_cast<std::uint32_t>(end),
                resume_candidate_index,
                visual_width,
                visual_height,
                visual_baseline,
                false,
                true,
                {},
                {},
                visual_baseline,
                std::max(visual_height - visual_baseline, 0.0f),
            });
        } else {
            const FontMetrics line_metrics =
                ResolveLineMetrics(node, primary_line_box_metrics, shaped.ascent, shaped.descent);
            max_line_height = std::max(max_line_height, line_metrics.height);
            max_line_width = std::max(max_line_width, shaped.width);
            line_widths.push_back(shaped.width);
            line_heights.push_back(line_metrics.height);
            line_ascents.push_back(line_metrics.ascent);
        }
        if (disable_soft_wrap) {
            const ProfileClock::time_point fragment_build_start = profile_active ? ProfileClock::now() : ProfileClock::time_point{};
            const std::vector<NonWrappingTextFragment> line_fragments =
                BuildNonWrappingFragmentsForLine(index, line_text, shaped);
            nonwrap_fragments.insert(
                nonwrap_fragments.end(),
                line_fragments.begin(),
                line_fragments.end());
            nonwrap_fragment_line_offsets.push_back(nonwrap_fragments.size());
            if (profile_active) {
                current_text_commit_profile_.nonwrap_fragment_line_builds += 1U;
                current_text_commit_profile_.nonwrap_fragment_build_ms +=
                    ElapsedMilliseconds(fragment_build_start, ProfileClock::now());
            }
        }
    }
    UINode& mutable_node = const_cast<UINode&>(node);
    mutable_node.break_offsets = std::move(break_offsets);
    mutable_node.line_widths = std::move(line_widths);
    mutable_node.line_heights = std::move(line_heights);
    mutable_node.line_ascents = std::move(line_ascents);
    mutable_node.line_y_offsets.clear();
    mutable_node.line_y_offsets.reserve(mutable_node.line_heights.size() + 1U);
    mutable_node.line_y_offsets.push_back(0.0f);
    for (const float height : mutable_node.line_heights) {
        mutable_node.line_y_offsets.push_back(mutable_node.line_y_offsets.back() + std::max(height, 0.0f));
    }
    mutable_node.total_line_count = total_line_count;
    mutable_node.visible_line_count = total_line_count;
    if (node.max_lines > 0) {
        mutable_node.visible_line_count =
            std::min(mutable_node.visible_line_count, static_cast<std::size_t>(node.max_lines));
    }
    mutable_node.line_height = max_line_height;
    mutable_node.text_layout_cache_max_line_width = max_line_width;
    mutable_node.visual_line_shape_cache_valid = !disable_soft_wrap;
    mutable_node.visual_line_shapes = !disable_soft_wrap
        ? std::move(visual_line_shapes)
        : std::vector<CachedVisualLineShape>{};
    mutable_node.nonwrap_fragment_cache_valid = disable_soft_wrap;
    mutable_node.nonwrap_fragment_line_offsets = disable_soft_wrap
        ? std::move(nonwrap_fragment_line_offsets)
        : std::vector<std::size_t>{};
    mutable_node.nonwrap_fragments = disable_soft_wrap
        ? std::move(nonwrap_fragments)
        : std::vector<NonWrappingTextFragment>{};
    mutable_node.nonwrap_fragment_cache_generation += disable_soft_wrap ? 1U : 0U;
    mutable_node.text_layout_cache_width_limit = width_limit;
    mutable_node.text_layout_cache_valid = true;
    if (profile_active) {
        current_text_commit_profile_.layout_paragraph_calls += 1U;
        current_text_commit_profile_.wrapped_layout_calls += disable_soft_wrap ? 0U : 1U;
        current_text_commit_profile_.nonwrap_layout_calls += disable_soft_wrap ? 1U : 0U;
        current_text_commit_profile_.layout_paragraph_total_ms +=
            ElapsedMilliseconds(paragraph_start, ProfileClock::now());
    }
    populate_from_node(mutable_node);
    return paragraph;
}

std::vector<TextClusterStop> UiRuntime::BuildTextClusterStopsImpl(
    const std::vector<GlyphPlacement>& glyphs,
    float shaped_width,
    std::size_t text_length) const {
    return BuildClusterStops(glyphs, shaped_width, text_length);
}

std::vector<std::int32_t> UiRuntime::ComputeBreakCandidatesImpl(std::string_view utf8) const {
    std::vector<std::int32_t> candidates{0};
    if (utf8.empty()) {
        return candidates;
    }

    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString unicode = icu::UnicodeString::fromUTF8(
        icu::StringPiece(utf8.data(), static_cast<int32_t>(utf8.size())));
    std::unique_ptr<icu::BreakIterator> iterator(
        icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), status));
    if (!U_FAILURE(status) && iterator != nullptr) {
        std::vector<std::int32_t> utf16_to_utf8(
            static_cast<std::size_t>(std::max(unicode.length(), 0)) + 1U,
            0);
        std::size_t byte_offset = 0U;
        std::int32_t utf16_index = 0;
        while (byte_offset < utf8.size() && utf16_index < unicode.length()) {
            std::uint32_t codepoint = 0U;
            const std::size_t next = NextUtf8Codepoint(utf8, byte_offset, &codepoint);
            const std::int32_t utf16_units = codepoint > 0xFFFFU ? 2 : 1;
            for (std::int32_t unit = 0; unit < utf16_units && utf16_index < unicode.length(); unit += 1) {
                utf16_index += 1;
                utf16_to_utf8[static_cast<std::size_t>(utf16_index)] = static_cast<std::int32_t>(next);
            }
            byte_offset = next;
        }
        while (utf16_index < unicode.length()) {
            utf16_index += 1;
            utf16_to_utf8[static_cast<std::size_t>(utf16_index)] = static_cast<std::int32_t>(utf8.size());
        }

        iterator->setText(unicode);
        for (int32_t boundary = iterator->first();
             boundary != icu::BreakIterator::DONE;
             boundary = iterator->next()) {
            const std::int32_t clamped_boundary =
                std::clamp(boundary, 0, unicode.length());
            const std::int32_t mapped_byte_offset =
                utf16_to_utf8[static_cast<std::size_t>(clamped_boundary)];
            if (mapped_byte_offset > candidates.back()) {
                candidates.push_back(mapped_byte_offset);
            }
        }
    }
    if (candidates.back() != static_cast<std::int32_t>(utf8.size())) {
        candidates.push_back(static_cast<std::int32_t>(utf8.size()));
    }
    for (std::size_t index = 0; index < utf8.size(); index += 1U) {
        const unsigned char ch = static_cast<unsigned char>(utf8[index]);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            candidates.push_back(static_cast<std::int32_t>(index + 1U));
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::vector<std::int32_t> UiRuntime::ComputeLineBreaksImpl(
    std::string_view utf8,
    float max_width,
    std::uint32_t font_id,
    float font_size,
    bool obscured) const {
    const auto compute_with_candidates = [&](std::string_view segment, const std::vector<std::int32_t>& candidates) {
        std::vector<std::int32_t> breaks{0};
        const std::vector<std::int32_t> character_candidates = BuildUtf8CharacterCandidates(segment);
        std::unordered_map<std::uint64_t, float> width_cache{};
        ShapedTextRun full_segment_shape{};
        std::vector<TextClusterStop> full_segment_stops{};
        // ASCII-heavy wrapped content is the common large-document case in the editor.
        // Shape once, then answer candidate probes from prefix X metrics instead of
        // reshaping every substring we test against the width limit.
        const bool use_ascii_prefix_widths =
            IsAsciiOnly(segment) &&
            ShapeText(segment, font_id, font_size, full_segment_shape, obscured);

        if (use_ascii_prefix_widths) {
            full_segment_stops = BuildClusterStops(
                full_segment_shape.glyphs,
                full_segment_shape.width,
                segment.size());
        }

        const auto measure_range = [&](std::int32_t start, std::int32_t end) -> float {
            if (end <= start) {
                return 0.0f;
            }
            if (use_ascii_prefix_widths) {
                const float start_x = ClusterXForIndex(
                    full_segment_stops,
                    full_segment_shape.width,
                    static_cast<std::uint32_t>(start),
                    segment.size());
                const float end_x = ClusterXForIndex(
                    full_segment_stops,
                    full_segment_shape.width,
                    static_cast<std::uint32_t>(end),
                    segment.size());
                return std::max(end_x - start_x, 0.0f);
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
                font_id,
                font_size,
                obscured);
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
                if (width <= max_width) {
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
                if (width <= max_width) {
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

    if (utf8.empty()) {
        return {0};
    }

    const auto compute_segment_breaks = [&](std::string_view segment) {
        return compute_with_candidates(segment, ComputeBreakCandidates(segment));
    };

    std::vector<std::int32_t> breaks{0};
    std::int32_t segment_start = 0;
    const auto append_segment_breaks = [&](std::string_view segment, std::int32_t offset) {
        if (segment.empty()) {
            breaks.push_back(offset);
            return;
        }
        const std::vector<std::int32_t> segment_breaks = compute_segment_breaks(segment);
        for (std::size_t index = 1; index < segment_breaks.size(); index += 1U) {
            breaks.push_back(offset + segment_breaks[index]);
        }
    };

    for (std::size_t index = 0; index < utf8.size(); index += 1U) {
        const unsigned char ch = static_cast<unsigned char>(utf8[index]);
        if (ch != '\n' && ch != '\r') {
            continue;
        }
        const std::int32_t break_offset = static_cast<std::int32_t>(index);
        append_segment_breaks(
            std::string_view(
                utf8.data() + segment_start,
                static_cast<std::size_t>(break_offset - segment_start)),
            segment_start);
        if (ch == '\r' && index + 1U < utf8.size() && utf8[index + 1U] == '\n') {
            index += 1U;
        }
        segment_start = static_cast<std::int32_t>(index + 1U);
    }

    append_segment_breaks(
        std::string_view(
            utf8.data() + segment_start,
            static_cast<std::size_t>(static_cast<std::int32_t>(utf8.size()) - segment_start)),
        segment_start);
    return breaks;
}

} // namespace effindom::v2::ui
