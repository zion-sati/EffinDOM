#include "UiRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/ubrk.h>
#include <unicode/unistr.h>

namespace effindom::v2::ui {

namespace {

struct ClusterStop {
    std::uint32_t index = 0;
    float x = 0.0f;
};

struct LineByteRange {
    std::uint32_t raw_start = 0U;
    std::uint32_t visible_start = 0U;
    std::uint32_t end = 0U;
};

template <typename TStop>
float ClusterTrailingXForIndex(
    const std::vector<TStop>& stops,
    float shaped_width,
    std::uint32_t local_index,
    std::size_t text_length) {
    if (local_index == 0U) {
        return 0.0f;
    }
    if (local_index >= text_length) {
        return shaped_width;
    }
    for (const TStop& stop : stops) {
        if (stop.index >= local_index) {
            return stop.x;
        }
    }
    return shaped_width;
}

std::vector<ClusterStop> BuildClusterStops(
    const std::vector<GlyphPlacement>& glyphs,
    float shaped_width,
    std::size_t line_length) {
    std::vector<ClusterStop> stops{};
    stops.reserve(glyphs.size() + 2U);
    stops.push_back(ClusterStop{0U, 0.0f});
    for (const GlyphPlacement& glyph : glyphs) {
        stops.push_back(ClusterStop{
            static_cast<std::uint32_t>(std::min<std::size_t>(glyph.cluster, line_length)),
            glyph.x,
        });
    }
    stops.push_back(ClusterStop{static_cast<std::uint32_t>(line_length), shaped_width});

    std::stable_sort(stops.begin(), stops.end(), [](const ClusterStop& lhs, const ClusterStop& rhs) {
        return lhs.index < rhs.index;
    });

    std::vector<ClusterStop> deduped{};
    deduped.reserve(stops.size());
    for (const ClusterStop& stop : stops) {
        if (!deduped.empty() && deduped.back().index == stop.index) {
            deduped.back().x = std::min(deduped.back().x, stop.x);
            continue;
        }
        deduped.push_back(stop);
    }
    return deduped;
}

std::size_t VisibleLineCount(const UINode& node) {
    const std::size_t available = node.break_offsets.size() > 1U ? node.break_offsets.size() - 1U : 0U;
    if (node.visible_line_count == 0U) return available;
    return std::min(node.visible_line_count, available);
}

float CaretWidthForNode(const UINode& node) {
    return std::max(1.0f, std::min(node.font_size * 0.125f, 2.0f));
}

bool IsUtf8ContinuationByte(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

bool IsAsciiWordByte(unsigned char byte) {
    return std::isalnum(byte) != 0 || byte == '_';
}

bool IsAsciiOnly(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char ch) {
        return static_cast<unsigned char>(ch) < 0x80U;
    });
}

bool IsLineBreakByte(char ch) {
    return ch == '\n' || ch == '\r';
}

LineByteRange GetLineByteRange(const UINode& node, std::size_t line_index) {
    if (node.break_offsets.size() < 2U) {
        return {};
    }
    const std::size_t last_line_index = node.break_offsets.size() - 2U;
    const std::size_t clamped_line_index = std::min(line_index, last_line_index);
    const std::uint32_t raw_start =
        static_cast<std::uint32_t>(std::max(node.break_offsets[clamped_line_index], 0));
    const std::uint32_t end =
        static_cast<std::uint32_t>(std::max(node.break_offsets[clamped_line_index + 1U], 0));
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    std::uint32_t visible_start = std::min(raw_start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    while (visible_start < clamped_end && IsLineBreakByte(node.text_content[visible_start])) {
        visible_start += 1U;
    }
    return LineByteRange{
        std::min(raw_start, text_length),
        visible_start,
        clamped_end,
    };
}

const CachedLogicalLineShape* TryGetNonWrapMonospaceLogicalLine(
    const UINode& node,
    std::size_t line_index) {
    if (!node.logical_line_shape_cache_valid ||
        line_index >= node.logical_line_shapes.size()) {
        return nullptr;
    }
    const CachedLogicalLineShape& shape = node.logical_line_shapes[line_index];
    if (!shape.monospace_fast_path_eligible || shape.monospace_cell_width <= 0.0f) {
        return nullptr;
    }
    return &shape;
}

std::size_t LineIndexForPosition(const UINode& node, std::uint32_t pos) {
    if (node.break_offsets.size() < 2U) {
        return 0U;
    }

    const std::uint32_t clamped = std::min<std::uint32_t>(pos, static_cast<std::uint32_t>(node.text_content.size()));
    if (clamped == static_cast<std::uint32_t>(std::max(node.break_offsets.front(), 0))) {
        return 0U;
    }
    const auto it = std::upper_bound(
        node.break_offsets.begin() + 1,
        node.break_offsets.end(),
        static_cast<std::int32_t>(clamped));
    if (it == node.break_offsets.begin()) {
        return 0U;
    }
    const std::size_t line_count = node.break_offsets.size() - 1U;
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::distance(node.break_offsets.begin(), it) - 1),
        line_count - 1U);
}

bool IsSoftWrappedLineBoundary(const UINode& node, std::uint32_t pos) {
    if (!node.text_wrap) {
        return false;
    }
    if (node.break_offsets.size() < 3U) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t clamped = std::min<std::uint32_t>(pos, text_length);
    if (clamped == 0U || clamped >= text_length) {
        return false;
    }

    const auto it = std::lower_bound(
        node.break_offsets.begin() + 1,
        node.break_offsets.end() - 1,
        static_cast<std::int32_t>(clamped));
    if (it == node.break_offsets.end() - 1 || *it != static_cast<std::int32_t>(clamped)) {
        return false;
    }

    return !IsLineBreakByte(node.text_content[clamped - 1U]);
}

std::size_t LineIndexForBoundaryNavigation(const UINode& node, std::uint32_t pos) {
    std::size_t line_index = LineIndexForPosition(node, pos);
    if (line_index > 0U && IsSoftWrappedLineBoundary(node, pos)) {
        line_index -= 1U;
    }
    return line_index;
}

} // namespace

float UiRuntime::GetAlignedLineXOffset(const UINode& node, float line_width) const {
    const Rect text_bounds = ComputeTextContentBounds(node);
    if (node.text_align == UI_TEXT_ALIGN_CENTER) {
        return text_bounds.x + std::max(0.0f, (text_bounds.width - line_width) * 0.5f);
    }
    if (node.text_align == UI_TEXT_ALIGN_RIGHT) {
        return text_bounds.x + std::max(0.0f, text_bounds.width - line_width);
    }
    return text_bounds.x;
}

float UiRuntime::GetAlignedTextYOffset(const UINode& node, float content_height) const {
    const Rect text_bounds = ComputeTextContentBounds(node);
    const float clamped_content_height = std::max(content_height, 0.0f);
    const float available_height = std::max(text_bounds.height, 0.0f);
    const float remaining = available_height - clamped_content_height;
    
    if (node.text_vertical_align == UI_TEXT_VERTICAL_ALIGN_CENTER) {
        return text_bounds.y + (remaining * 0.5f);
    }
    if (node.text_vertical_align == UI_TEXT_VERTICAL_ALIGN_BOTTOM) {
        return text_bounds.y + remaining;
    }
    return text_bounds.y;
}

std::uint32_t UiRuntime::ResolveTextFadeMask(const UINode& node, const ParagraphLayout& paragraph) const {
    std::uint32_t fade_mask = ED_FADE_NONE;
    if (node.text_overflow == UI_TEXT_OVERFLOW_FADE && paragraph.clipped) {
        fade_mask |= ED_FADE_BOTTOM;
    }
    if (!node.text_overflow_fade_horizontal && !node.text_overflow_fade_vertical) {
        return fade_mask;
    }

    const Rect text_bounds = ComputeTextContentBounds(node);
    if (text_bounds.width <= 0.0f || text_bounds.height <= 0.0f) {
        return fade_mask;
    }

    if (node.text_overflow_fade_horizontal &&
        paragraph.width > (text_bounds.width + 0.001f)) {
        fade_mask |= ED_FADE_RIGHT;
    }

    if (node.text_overflow_fade_vertical) {
        const float content_offset_y = GetAlignedTextYOffset(node, paragraph.height);
        const float bounds_top = text_bounds.y;
        const float bounds_bottom = text_bounds.y + text_bounds.height;
        if (content_offset_y < bounds_top - 0.001f) {
            fade_mask |= ED_FADE_TOP;
        }
        if (paragraph.clipped ||
            (content_offset_y + paragraph.height) > (bounds_bottom + 0.001f)) {
            fade_mask |= ED_FADE_BOTTOM;
        }
    }

    return fade_mask;
}

float UiRuntime::GetLineHeightForIndex(const UINode& node, std::size_t line_index) const {
    if (line_index < node.line_heights.size()) {
        return std::max(node.line_heights[line_index], 1.0f);
    }
    if (node.is_text_node) {
        return std::max(ResolvePrimaryLineBoxMetrics(node).height, 1.0f);
    }
    return std::max(node.line_height, 1.0f);
}

float UiRuntime::GetLineAscentForIndex(const UINode& node, std::size_t line_index) const {
    if (line_index < node.line_ascents.size()) {
        return std::max(node.line_ascents[line_index], 0.0f);
    }
    if (node.is_text_node) {
        return ResolvePrimaryLineBoxMetrics(node).ascent;
    }
    return GetLineHeightForIndex(node, line_index);
}

float UiRuntime::GetLineTopForIndex(const UINode& node, std::size_t line_index) const {
    if (line_index < node.line_y_offsets.size()) {
        return std::max(node.line_y_offsets[line_index], 0.0f);
    }
    return static_cast<float>(line_index) * std::max(node.line_height, 0.0f);
}

float UiRuntime::GetTextContentHeight(const UINode& node, std::size_t visible_line_count) const {
    if (visible_line_count == 0U) {
        return 0.0f;
    }
    if (visible_line_count < node.line_y_offsets.size()) {
        return std::max(node.line_y_offsets[visible_line_count], 0.0f);
    }
    return static_cast<float>(visible_line_count) * std::max(node.line_height, 0.0f);
}

std::size_t UiRuntime::LineIndexForYOffset(const UINode& node, float local_y, std::size_t line_count) const {
    if (line_count == 0U) {
        return 0U;
    }
    if (node.line_y_offsets.size() > line_count) {
        const float clamped_local_y = std::max(local_y, 0.0f);
        const auto it = std::upper_bound(
            node.line_y_offsets.begin() + 1,
            node.line_y_offsets.begin() + static_cast<std::ptrdiff_t>(line_count + 1U),
            clamped_local_y);
        const std::size_t index = static_cast<std::size_t>(std::distance(node.line_y_offsets.begin(), it));
        return std::min(index, line_count) - 1U;
    }
    if (node.line_height <= 0.0f) {
        return 0U;
    }
    return static_cast<std::size_t>(std::clamp(
        std::floor(std::max(local_y, 0.0f) / node.line_height),
        0.0f,
        static_cast<float>(line_count - 1U)));
}



float UiRuntime::GetTextboxViewportOffsetX(
    const UINode& node,
    const ShapedTextRun& line,
    std::uint32_t line_start,
    std::uint32_t line_end) const {
    const Rect text_bounds = ComputeTextContentBounds(node);
    const bool uses_horizontal_viewport =
        IsSingleLineEditorTextNode(node);
    if (!uses_horizontal_viewport || text_bounds.width <= 0.0f) {
        return 0.0f;
    }
    const float visible_width = std::max(text_bounds.width, 1.0f);
    const float caret_width = CaretWidthForNode(node);
    if (line.width <= text_bounds.width) {
        node.textbox_viewport_offset_x = 0.0f;
        return 0.0f;
    }

    const std::uint32_t caret_index = std::clamp(node.selection_end, line_start, line_end);
    const std::uint32_t local_index = caret_index - line_start;
    const std::string_view line_text(node.text_content.data() + line_start, static_cast<std::size_t>(line_end - line_start));
    const float caret_x = RawLineXForIndex(line_text, line, local_index);
    const float caret_right = caret_x + caret_width;
    const float margin = std::min(std::max(node.font_size * 0.5f, 4.0f), visible_width * 0.5f);
    const float max_offset = std::max((line.width + caret_width) - visible_width, 0.0f);
    const float current_offset = std::clamp(node.textbox_viewport_offset_x, 0.0f, max_offset);
    const float visible_start = current_offset;
    const float visible_end = current_offset + visible_width;

    float next_offset = current_offset;
    if (caret_x < visible_start + margin) {
        next_offset = std::max(caret_x - margin, 0.0f);
    } else if (caret_right > visible_end - margin) {
        next_offset = std::min(caret_right - (visible_width - margin), max_offset);
    }

    node.textbox_viewport_offset_x = next_offset;
    return next_offset;
}



std::uint32_t UiRuntime::GetStringIndexFromPoint(const UINode& node, float local_x, float local_y) const {
    if (!node.is_text_node || node.text_content.empty() || node.break_offsets.size() < 2U) {
        return 0U;
    }

    const std::size_t line_count = VisibleLineCount(node);
    if (line_count == 0U) return 0U;

    const float content_height = GetTextContentHeight(node, line_count);
    const float content_offset_y = GetAlignedTextYOffset(node, content_height);
    const float adjusted_local_y = local_y - content_offset_y;
    const bool multiline_textbox = IsMultilineEditorTextNode(node);
    if (multiline_textbox && adjusted_local_y >= content_height) {
        return static_cast<std::uint32_t>(node.text_content.size());
    }
    const int line_index = static_cast<int>(LineIndexForYOffset(node, adjusted_local_y, line_count));

    const LineByteRange range = GetLineByteRange(node, static_cast<std::size_t>(line_index));
    const std::uint32_t start = range.visible_start;
    const std::uint32_t end = range.end;
    const bool uses_fragment_geometry =
        !node.text_wrap &&
        !(IsSingleLineEditorTextNode(node)) &&
        node.nonwrap_fragment_cache_valid;
    const float full_line_width =
        static_cast<std::size_t>(line_index) < node.line_widths.size()
        ? node.line_widths[static_cast<std::size_t>(line_index)]
        : 0.0f;
    const float line_offset = GetAlignedLineXOffset(node, full_line_width);
    const float aligned_x = local_x - line_offset;
    if (aligned_x <= 0.0f) {
        return start;
    }
    if (aligned_x >= full_line_width) {
        return end;
    }

    FragmentGeometrySlice fragment_slice{};
    if (uses_fragment_geometry) {
        const CachedLogicalLineShape* monospace_line =
            TryGetNonWrapMonospaceLogicalLine(node, static_cast<std::size_t>(line_index));
        if (monospace_line != nullptr) {
            const std::uint32_t local_length = end - start;
            const float cell_width = monospace_line->monospace_cell_width;
            const std::uint32_t snapped_index = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                static_cast<std::int64_t>(std::floor((aligned_x + (cell_width * 0.5f)) / cell_width)),
                0,
                static_cast<std::int64_t>(local_length)));
            return start + snapped_index;
        }
        if (!TryShapeFragmentGeometrySliceForX(node, static_cast<std::size_t>(line_index), aligned_x, fragment_slice)) {
            return start;
        }
        const float local_slice_x = aligned_x - fragment_slice.slice_x;
        if (local_slice_x <= 0.0f) {
            return std::max(start, fragment_slice.slice_start);
        }
        if (local_slice_x >= fragment_slice.shaped.width) {
            return std::min(end, fragment_slice.slice_end);
        }

        std::uint32_t result = std::min(end, fragment_slice.slice_end);
        for (std::size_t index = 0; index + 1U < fragment_slice.cluster_stops.size(); index += 1U) {
            const float midpoint =
                (fragment_slice.cluster_stops[index].x + fragment_slice.cluster_stops[index + 1U].x) * 0.5f;
            if (local_slice_x < midpoint) {
                result = fragment_slice.slice_start + fragment_slice.cluster_stops[index].index;
                break;
            }
        }
        return result;
    }

    if (node.visual_line_shape_cache_valid &&
        static_cast<std::size_t>(line_index) < node.visual_line_shapes.size()) {
        const CachedVisualLineShape* cached_line =
            EnsureWrappedVisualLineShape(node, static_cast<std::size_t>(line_index));
        if (cached_line == nullptr) {
            return start;
        }
        const float aligned_x = local_x - GetAlignedLineXOffset(node, cached_line->width);
        if (aligned_x <= 0.0f) {
            return cached_line->start;
        }
        if (aligned_x >= cached_line->width) {
            return cached_line->end;
        }

        std::uint32_t result = cached_line->end;
        for (std::size_t index = 0; index + 1U < cached_line->cluster_stops.size(); index += 1U) {
            const float midpoint =
                (cached_line->cluster_stops[index].x + cached_line->cluster_stops[index + 1U].x) * 0.5f;
            if (aligned_x < midpoint) {
                result = cached_line->start + cached_line->cluster_stops[index].index;
                break;
            }
        }
        return result;
    }

    ShapedTextRun shaped{};
    const std::string_view line_text(node.text_content.data() + start, static_cast<std::size_t>(end - start));
    if (!ShapeText(line_text, node.font_id, node.font_size, shaped, node.is_obscured)) {
        return start;
    }
    const float viewport_offset_x = GetTextboxViewportOffsetX(node, shaped, start, end);
    const float aligned_viewport_x = local_x - (GetAlignedLineXOffset(node, shaped.width) - viewport_offset_x);
    if (aligned_viewport_x <= 0.0f) {
        return start;
    }
    if (aligned_viewport_x >= shaped.width) {
        return end;
    }

    const std::vector<ClusterStop> stops = BuildClusterStops(shaped.glyphs, shaped.width, line_text.size());
    std::uint32_t result = end;
    for (std::size_t index = 0; index + 1U < stops.size(); index += 1U) {
        const float midpoint = (stops[index].x + stops[index + 1U].x) * 0.5f;
        if (aligned_viewport_x < midpoint) {
            result = start + stops[index].index;
            break;
        }
    }
    return result;
}



std::pair<float, int> UiRuntime::GetLocalPositionFromIndex(
    const UINode& node,
    std::uint32_t byte_index,
    bool trailing_edge) const {
    if (!node.is_text_node) {
        return {0.0f, 0};
    }
    if (node.break_offsets.size() < 2U) {
        return {GetAlignedLineXOffset(node, 0.0f), 0};
    }

    const std::uint32_t clamped_index =
        std::min<std::uint32_t>(byte_index, static_cast<std::uint32_t>(node.text_content.size()));
    std::size_t line_index = LineIndexForPosition(node, clamped_index);
    if (trailing_edge && line_index > 0U && IsSoftWrappedLineBoundary(node, clamped_index)) {
        line_index -= 1U;
    }

    LineByteRange range = GetLineByteRange(node, line_index);
    if (line_index > 0U && clamped_index < range.visible_start) {
        line_index -= 1U;
        range = GetLineByteRange(node, line_index);
    }
    const std::uint32_t start = range.visible_start;
    const std::uint32_t end = range.end;
    const std::uint32_t local_index = std::clamp(clamped_index, start, end) - start;
    const bool uses_fragment_geometry =
        !node.text_wrap &&
        !(IsSingleLineEditorTextNode(node)) &&
        node.nonwrap_fragment_cache_valid;
    const float full_line_width =
        line_index < node.line_widths.size()
        ? node.line_widths[line_index]
        : 0.0f;
    if (uses_fragment_geometry) {
        const CachedLogicalLineShape* monospace_line =
            TryGetNonWrapMonospaceLogicalLine(node, line_index);
        if (monospace_line != nullptr) {
            const float x = std::min(
                static_cast<float>(local_index) * monospace_line->monospace_cell_width,
                full_line_width);
            return {GetAlignedLineXOffset(node, full_line_width) + x, static_cast<int>(line_index)};
        }
        FragmentGeometrySlice fragment_slice{};
        if (!TryShapeFragmentGeometrySliceForIndex(node, line_index, clamped_index, fragment_slice)) {
            return {GetAlignedLineXOffset(node, full_line_width), static_cast<int>(line_index)};
        }
        const std::string_view slice_text(
            node.text_content.data() + fragment_slice.slice_start,
            static_cast<std::size_t>(fragment_slice.slice_end - fragment_slice.slice_start));
        const std::uint32_t local_slice_index =
            std::clamp(clamped_index, fragment_slice.slice_start, fragment_slice.slice_end) - fragment_slice.slice_start;
        const float x = fragment_slice.slice_x + (
            trailing_edge
                ? ClusterTrailingXForIndex(
                    fragment_slice.cluster_stops,
                    fragment_slice.shaped.width,
                    local_slice_index,
                    slice_text.size())
                : ClusterXForIndex(
                    fragment_slice.cluster_stops,
                    fragment_slice.shaped.width,
                    local_slice_index,
                    slice_text.size()));
        return {GetAlignedLineXOffset(node, full_line_width) + x, static_cast<int>(line_index)};
    }

    if (node.visual_line_shape_cache_valid && line_index < node.visual_line_shapes.size()) {
        const CachedVisualLineShape* cached_line = EnsureWrappedVisualLineShape(node, line_index);
        if (cached_line == nullptr) {
            return {GetAlignedLineXOffset(node, full_line_width), static_cast<int>(line_index)};
        }
        const std::uint32_t local_cached_index =
            std::clamp(clamped_index, cached_line->start, cached_line->end) - cached_line->start;
        const float x = trailing_edge
            ? ClusterTrailingXForIndex(
                cached_line->cluster_stops,
                cached_line->width,
                local_cached_index,
                static_cast<std::size_t>(cached_line->end - cached_line->start))
            : ClusterXForIndex(
                cached_line->cluster_stops,
                cached_line->width,
                local_cached_index,
                static_cast<std::size_t>(cached_line->end - cached_line->start));
        return {GetAlignedLineXOffset(node, cached_line->width) + x, static_cast<int>(line_index)};
    }

    ShapedTextRun shaped{};
    const std::string_view line_text(node.text_content.data() + start, static_cast<std::size_t>(end - start));
    if (!ShapeText(line_text, node.font_id, node.font_size, shaped, node.is_obscured)) {
        return {0.0f, static_cast<int>(line_index)};
    }

    const float x = RawLineXForIndex(line_text, shaped, local_index, trailing_edge);
    const float viewport_offset_x = GetTextboxViewportOffsetX(node, shaped, start, end);
    return {GetAlignedLineXOffset(node, shaped.width) - viewport_offset_x + x, static_cast<int>(line_index)};
}

std::pair<std::uint32_t, std::uint32_t> UiRuntime::GetWordBoundaries(const UINode& node, std::uint32_t byte_index) const {
    if (!node.is_text_node || node.text_content.empty()) return {0U, 0U};

    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t clamped_index = std::min(byte_index, text_length);
    const auto is_word_byte = [&](std::uint32_t offset) {
        const unsigned char ch = static_cast<unsigned char>(node.text_content[offset]);
        return std::isalnum(ch) != 0 || ch == '_';
    };
    if (text_length > 0U) {
        std::uint32_t probe = clamped_index;
        if (probe == text_length) probe -= 1U;
        if (probe < text_length && is_word_byte(probe)) {
            std::uint32_t start = probe;
            while (start > 0U && is_word_byte(start - 1U)) {
                start -= 1U;
            }
            std::uint32_t end = probe + 1U;
            while (end < text_length && is_word_byte(end)) {
                end += 1U;
            }
            return {start, end};
        }
    }

    auto normalize_boundary = [&](std::uint32_t index) {
        std::uint32_t boundary = std::min(index, text_length);
        while (boundary > 0U && boundary < text_length &&
               IsUtf8ContinuationByte(static_cast<unsigned char>(node.text_content[boundary]))) {
            boundary -= 1U;
        }
        return boundary;
    };
    auto is_word_boundary = [&](std::uint32_t index) {
        if (index >= text_length) return false;
        const unsigned char byte = static_cast<unsigned char>(node.text_content[index]);
        return byte >= 0x80U || IsAsciiWordByte(byte);
    };

    std::uint32_t probe = clamped_index;
    if (probe == text_length && text_length > 0U) {
        probe = NextCharacterIndex(node.text_content, text_length, false);
    }
    probe = normalize_boundary(probe);
    if (!is_word_boundary(probe)) return {clamped_index, clamped_index};

    std::uint32_t start = probe;
    while (start > 0U) {
        const std::uint32_t previous = NextCharacterIndex(node.text_content, start, false);
        if (!is_word_boundary(previous)) break;
        start = previous;
    }

    std::uint32_t end = NextCharacterIndex(node.text_content, probe, true);
    while (end < text_length && is_word_boundary(normalize_boundary(end))) {
        end = NextCharacterIndex(node.text_content, end, true);
    }
    return {start, end};
}



std::pair<std::uint32_t, std::uint32_t> UiRuntime::GetParagraphBoundaries(
    const UINode& node,
    std::uint32_t byte_index) const {
    if (!node.is_text_node || node.text_content.empty() || node.text_line_starts.empty()) {
        return {0U, 0U};
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    std::uint32_t probe = std::min(byte_index, text_length);
    if (probe == text_length && probe > 0U) {
        probe -= 1U;
    }
    if (probe < text_length && node.text_content[probe] == '\n' && probe > 0U) {
        probe -= 1U;
    }
    const std::size_t line_index = LineIndexForTextLineStarts(node, probe);
    return {GetTextLineStart(node, line_index), GetTextLineEnd(node, line_index)};
}



std::uint32_t UiRuntime::NextCharacterIndex(std::string_view text, std::uint32_t pos, bool forward) const {
    const std::uint32_t clamped = std::min<std::uint32_t>(pos, static_cast<std::uint32_t>(text.size()));
    if (forward) {
        if (clamped >= text.size()) return static_cast<std::uint32_t>(text.size());
        std::uint32_t next = clamped + 1U;
        while (next < text.size() && IsUtf8ContinuationByte(static_cast<unsigned char>(text[next]))) {
            next += 1U;
        }
        return next;
    }

    if (clamped == 0U) return 0U;
    std::uint32_t next = clamped - 1U;
    while (next > 0U && IsUtf8ContinuationByte(static_cast<unsigned char>(text[next]))) {
        next -= 1U;
    }
    return next;
}



std::uint32_t UiRuntime::NextWordIndex(const UINode& node, std::uint32_t pos, bool forward) const {
    const std::string_view text = node.text_content;
    const std::uint32_t text_length = static_cast<std::uint32_t>(text.size());
    const std::uint32_t clamped = std::min(pos, text_length);

    if (IsAsciiOnly(text)) {
        if (forward) {
            std::uint32_t cursor = clamped;
            if (cursor < text_length && IsAsciiWordByte(static_cast<unsigned char>(text[cursor]))) {
                while (cursor < text_length && IsAsciiWordByte(static_cast<unsigned char>(text[cursor]))) {
                    cursor += 1U;
                }
                return cursor;
            }
            while (cursor < text_length && !IsAsciiWordByte(static_cast<unsigned char>(text[cursor]))) cursor += 1U;
            return cursor;
        }

        std::uint32_t cursor = clamped;
        if (cursor > 0U && IsAsciiWordByte(static_cast<unsigned char>(text[cursor - 1U]))) {
            cursor -= 1U;
            while (cursor > 0U && IsAsciiWordByte(static_cast<unsigned char>(text[cursor - 1U]))) cursor -= 1U;
            return cursor;
        }
        while (cursor > 0U && !IsAsciiWordByte(static_cast<unsigned char>(text[cursor - 1U]))) {
            cursor -= 1U;
        }
        if (cursor == 0U) return 0U;
        cursor -= 1U;
        while (cursor > 0U && IsAsciiWordByte(static_cast<unsigned char>(text[cursor - 1U]))) {
            cursor -= 1U;
        }
        return cursor;
    }

    auto normalize_boundary = [&](std::uint32_t index) {
        std::uint32_t boundary = std::min(index, text_length);
        while (boundary > 0U && boundary < text_length &&
               IsUtf8ContinuationByte(static_cast<unsigned char>(text[boundary]))) {
            boundary -= 1U;
        }
        return boundary;
    };
    auto is_word_boundary = [&](std::uint32_t index) {
        if (index >= text_length) return false;
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        return byte >= 0x80U || IsAsciiWordByte(byte);
    };

    if (forward) {
        std::uint32_t cursor = normalize_boundary(clamped);
        if (cursor < text_length && is_word_boundary(cursor)) {
            do {
                cursor = NextCharacterIndex(text, cursor, true);
            } while (cursor < text_length && is_word_boundary(normalize_boundary(cursor)));
            return cursor;
        }
        while (cursor < text_length && !is_word_boundary(cursor)) {
            cursor = NextCharacterIndex(text, cursor, true);
        }
        return cursor;
    }

    if (clamped == 0U) return 0U;
    std::uint32_t cursor = normalize_boundary(NextCharacterIndex(text, clamped, false));
    if (is_word_boundary(cursor)) {
        while (cursor > 0U) {
            const std::uint32_t previous = normalize_boundary(NextCharacterIndex(text, cursor, false));
            if (!is_word_boundary(previous)) break;
            cursor = previous;
        }
        return cursor;
    }
    while (cursor > 0U && !is_word_boundary(cursor)) {
        cursor = normalize_boundary(NextCharacterIndex(text, cursor, false));
    }
    if (!is_word_boundary(cursor)) return 0U;
    while (cursor > 0U) {
        const std::uint32_t previous = normalize_boundary(NextCharacterIndex(text, cursor, false));
        if (!is_word_boundary(previous)) break;
        cursor = previous;
    }
    return cursor;
}



std::uint32_t UiRuntime::IndexForLineBegin(const UINode& node, std::uint32_t pos) const {
    if (node.break_offsets.size() < 2U) {
        return 0U;
    }
    const std::size_t line_index = LineIndexForBoundaryNavigation(node, pos);
    return GetLineByteRange(node, line_index).visible_start;
}



std::uint32_t UiRuntime::IndexForLineEnd(const UINode& node, std::uint32_t pos) const {
    if (node.break_offsets.size() < 2U) {
        return 0U;
    }
    const std::size_t line_index = LineIndexForBoundaryNavigation(node, pos);
    return GetLineByteRange(node, line_index).end;
}



std::uint32_t UiRuntime::IndexForVerticalMove(const UINode& node, std::uint32_t pos, bool down) const {
    if (node.break_offsets.size() < 2U) return 0U;

    const auto [local_x, line_index] = GetLocalPositionFromIndex(node, pos);
    const int target_line = line_index + (down ? 1 : -1);
    const std::size_t line_count = VisibleLineCount(node);
    if (target_line < 0 || target_line >= static_cast<int>(line_count)) return std::min<std::uint32_t>(pos, static_cast<std::uint32_t>(node.text_content.size()));

    const float content_offset_y = GetAlignedTextYOffset(node, GetTextContentHeight(node, line_count));
    const float target_y =
        content_offset_y +
        GetLineTopForIndex(node, static_cast<std::size_t>(target_line)) +
        (GetLineHeightForIndex(node, static_cast<std::size_t>(target_line)) * 0.5f);
    return GetStringIndexFromPoint(node, local_x, target_y);
}



std::uint32_t UiRuntime::IndexForPageMove(const UINode& node, std::uint32_t pos, bool down) const {
    if (node.break_offsets.size() < 2U) {
        return 0U;
    }

    const auto [local_x, line_index] = GetLocalPositionFromIndex(node, pos);
    const std::size_t line_count = VisibleLineCount(node);
    if (line_count == 0U) {
        return 0U;
    }

    const float default_line_height = std::max(node.line_height, std::max(node.font_size, 1.0f));
    float viewport_height = std::max(ComputeContentBounds(node, 0.0f, 0.0f).height, default_line_height);
    if (IsMultilineEditorTextNode(node)) {
        const UINode* parent = Resolve(node.parent_handle);
        if (parent != nullptr && parent->is_scroll_view) {
            viewport_height = std::max(GetScrollViewportHeight(*parent), default_line_height);
        }
    }
    const auto compute_page_delta = [&](int start_line, bool moving_down) {
        float consumed_height = 0.0f;
        int visible_lines = 0;
        int line = start_line;
        while (line >= 0 &&
               line < static_cast<int>(line_count) &&
               consumed_height < viewport_height) {
            consumed_height += GetLineHeightForIndex(node, static_cast<std::size_t>(line));
            visible_lines += 1;
            line += moving_down ? 1 : -1;
        }
        return std::max(1, visible_lines - 1);
    };
    const int page_delta = compute_page_delta(line_index, down);
    const int unclamped_target_line = line_index + (down ? page_delta : -page_delta);
    if (!down && unclamped_target_line < 0) {
        return 0U;
    }
    if (down && unclamped_target_line >= static_cast<int>(line_count - 1U)) {
        return static_cast<std::uint32_t>(node.text_content.size());
    }
    const int target_line =
        std::clamp(unclamped_target_line, 0, static_cast<int>(line_count - 1U));
    const float content_offset_y = GetAlignedTextYOffset(node, GetTextContentHeight(node, line_count));
    const float target_y =
        content_offset_y +
        GetLineTopForIndex(node, static_cast<std::size_t>(target_line)) +
        (GetLineHeightForIndex(node, static_cast<std::size_t>(target_line)) * 0.5f);
    return GetStringIndexFromPoint(node, local_x, target_y);
}



void UiRuntime::EnsureTextCaretVisible(std::uint64_t handle, UINode& node) {
    if (!node.is_text_node || !node.is_selectable || node.yg_node == nullptr) {
        return;
    }

    const bool stop_after_nearest_scroll_ancestor = node.is_editable;
    const std::uint32_t caret_index =
        std::min<std::uint32_t>(node.selection_end, static_cast<std::uint32_t>(node.text_content.size()));
    const auto [caret_x, line_index] = GetLocalPositionFromIndex(node, caret_index);
    const float line_height = GetLineHeightForIndex(node, static_cast<std::size_t>(std::max(line_index, 0)));
    const std::size_t line_count = std::max<std::size_t>(VisibleLineCount(node), 1U);
    const float content_offset_y = GetAlignedTextYOffset(node, GetTextContentHeight(node, line_count));
    const float line_top =
        content_offset_y + GetLineTopForIndex(node, static_cast<std::size_t>(std::max(line_index, 0)));
    const float margin_x = std::max(4.0f, std::min(node.font_size * 0.5f, 12.0f));
    const float margin_y = std::max(2.0f, std::min(line_height * 0.25f, 8.0f));
    const float caret_width = CaretWidthForNode(node);

    float target_left = std::max(0.0f, caret_x - margin_x);
    float target_top = std::max(0.0f, line_top - margin_y);
    float target_right = caret_x + caret_width + margin_x;
    float target_bottom = line_top + line_height + margin_y;

    for (std::uint64_t current_handle = handle; current_handle != UI_INVALID_HANDLE;) {
        const UINode* current = Resolve(current_handle);
        if (current == nullptr || current->yg_node == nullptr) {
            break;
        }
        const std::uint64_t parent_handle = current->parent_handle;
        if (parent_handle == UI_INVALID_HANDLE) {
            break;
        }

        UINode* parent = ResolveMutable(parent_handle);
        if (parent == nullptr || parent->yg_node == nullptr) {
            break;
        }

        const float current_left = YGNodeLayoutGetLeft(current->yg_node);
        const float current_top = YGNodeLayoutGetTop(current->yg_node);
        target_left += current_left;
        target_right += current_left;
        target_top += current_top;
        target_bottom += current_top;

        if (parent->is_scroll_view) {
            EnsureRectVisibleWithinScrollAncestor(
                parent_handle,
                target_left - parent->scroll_offset_x,
                target_top - parent->scroll_offset_y,
                target_right - parent->scroll_offset_x,
                target_bottom - parent->scroll_offset_y,
                &target_left,
                &target_top,
                &target_right,
                &target_bottom);
            if (stop_after_nearest_scroll_ancestor) {
                break;
            }
        }
        current_handle = parent_handle;
    }
}



std::vector<Rect> UiRuntime::BuildSelectionRects(const UINode& node, std::uint32_t start, std::uint32_t end) const {
    std::vector<Rect> rects{};
    if (!node.is_text_node || node.break_offsets.size() < 2U) {
        return rects;
    }

    const std::size_t line_count = VisibleLineCount(node);
    if (line_count == 0U) return rects;
    const float content_offset_y = GetAlignedTextYOffset(node, GetTextContentHeight(node, line_count));
    if (start == end) {
        const auto [caret_x, caret_line] = GetLocalPositionFromIndex(node, start, false);
        if (caret_line < 0 || static_cast<std::size_t>(caret_line) >= line_count) {
            return rects;
        }
        const std::size_t line_index = static_cast<std::size_t>(caret_line);
        rects.push_back(Rect{
            caret_x,
            content_offset_y + GetLineTopForIndex(node, line_index),
            0.5f,
            GetLineHeightForIndex(node, line_index),
        });
        return rects;
    }

    const std::uint32_t selection_start = std::min(start, end);
    const std::uint32_t selection_end = std::max(start, end);

    rects.reserve(line_count);
    for (std::size_t line_index = 0; line_index < line_count; line_index += 1U) {
        const LineByteRange range = GetLineByteRange(node, line_index);
        const std::uint32_t line_start = range.visible_start;
        const std::uint32_t line_end = range.end;
        if (selection_end <= line_start || selection_start >= line_end) {
            continue;
        }

        const std::uint32_t rect_start = std::max(selection_start, line_start);
        const std::uint32_t rect_end = std::min(selection_end, line_end);
        const float line_width = line_index < node.line_widths.size() ? node.line_widths[line_index] : 0.0f;
        const std::string_view line_text(node.text_content.data() + line_start, static_cast<std::size_t>(line_end - line_start));
        const bool uses_fragment_geometry =
            !node.text_wrap &&
            !(IsSingleLineEditorTextNode(node)) &&
            node.nonwrap_fragment_cache_valid;
        float viewport_offset_x = 0.0f;
        if (!uses_fragment_geometry &&
            !(node.visual_line_shape_cache_valid && line_index < node.visual_line_shapes.size())) {
            ShapedTextRun shaped{};
            viewport_offset_x =
                ShapeText(line_text, node.font_id, node.font_size, shaped, node.is_obscured)
                ? GetTextboxViewportOffsetX(node, shaped, line_start, line_end)
                : 0.0f;
        }
        const float line_offset = GetAlignedLineXOffset(node, line_width) - viewport_offset_x;
        const auto [left_x, left_line] =
            rect_start == line_start ? std::pair<float, int>{line_offset, static_cast<int>(line_index)}
                                     : GetLocalPositionFromIndex(node, rect_start, false);
        const auto [right_x, right_line] =
            rect_end == line_end ? std::pair<float, int>{line_offset + line_width, static_cast<int>(line_index)}
                                 : GetLocalPositionFromIndex(node, rect_end, true);
        if (left_line != static_cast<int>(line_index) || right_line != static_cast<int>(line_index)) continue;

        rects.push_back(Rect{
            left_x,
            content_offset_y + GetLineTopForIndex(node, line_index),
            std::max(0.5f, right_x - left_x),
            GetLineHeightForIndex(node, line_index),
        });
    }
    return rects;
}

std::vector<Rect> UiRuntime::GetTextRangeSceneRects(std::uint64_t handle, std::uint32_t start, std::uint32_t end) const {
    std::vector<Rect> scene_rects{};
    const UINode* node = ResolveTextGeometryNode(handle);
    if (node == nullptr) {
        return scene_rects;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    const std::vector<Rect> local_rects = BuildSelectionRects(*node, clamped_start, clamped_end);
    scene_rects.reserve(local_rects.size());
    for (const Rect& rect : local_rects) {
        scene_rects.push_back(Rect{
            node->abs_x + rect.x,
            node->abs_y + rect.y,
            rect.width,
            rect.height,
        });
    }
    return scene_rects;
}

std::optional<Rect> UiRuntime::GetTextVisibleBounds(std::uint64_t handle) const {
    const UINode* node = ResolveTextGeometryNode(handle);
    if (node == nullptr) {
        return std::nullopt;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    if (text_length == 0U) {
        return std::nullopt;
    }

    const std::vector<Rect> scene_rects = GetTextRangeSceneRects(handle, 0U, text_length);
    if (scene_rects.empty()) {
        return std::nullopt;
    }

    float left = scene_rects.front().x;
    float top = scene_rects.front().y;
    float right = scene_rects.front().x + scene_rects.front().width;
    float bottom = scene_rects.front().y + scene_rects.front().height;
    for (std::size_t index = 1; index < scene_rects.size(); index += 1U) {
        const Rect& rect = scene_rects[index];
        left = std::min(left, rect.x);
        top = std::min(top, rect.y);
        right = std::max(right, rect.x + rect.width);
        bottom = std::max(bottom, rect.y + rect.height);
    }

    return Rect{
        left,
        top,
        std::max(0.0f, right - left),
        std::max(0.0f, bottom - top),
    };
}

std::vector<ColoredRect> UiRuntime::BuildStyleInlineRects(const UINode& node) const {
    std::vector<ColoredRect> rects{};
    if (!node.is_text_node || !node.has_text_style_runs || node.text_style_runs.empty()) {
        return rects;
    }
    for (const TextStyleRun& run : node.text_style_runs) {
        if (run.start >= run.end) {
            continue;
        }
        const std::vector<Rect> run_rects = BuildSelectionRects(node, run.start, run.end);
        rects.reserve(rects.size() + run_rects.size());
        for (const Rect& rect : run_rects) {
            if (run.bg_color != 0U) {
                rects.push_back(ColoredRect{rect, run.bg_color});
            }
            if ((run.decoration_flags & 1U) != 0U) {
                const float thickness = std::max(1.0f, rect.height * 0.08f);
                rects.push_back(ColoredRect{
                    Rect{rect.x, rect.y + rect.height - thickness, rect.width, thickness},
                    run.color,
                });
            }
            if ((run.decoration_flags & 2U) != 0U) {
                const float thickness = std::max(1.0f, rect.height * 0.08f);
                const float y = rect.y + (rect.height * 0.55f) - (thickness * 0.5f);
                rects.push_back(ColoredRect{
                    Rect{rect.x, y, rect.width, thickness},
                    run.color,
                });
            }
        }
    }
    return rects;
}



} // namespace effindom::v2::ui
