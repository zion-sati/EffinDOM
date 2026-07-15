#include "UiTextPaintEncoder.h"

#include "CommandBuilder.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

namespace effindom::v2::ui {

namespace {

bool IntersectRect(Rect& rect, const Rect& clip) {
    const float left = std::max(rect.x, clip.x);
    const float top = std::max(rect.y, clip.y);
    const float right = std::min(rect.x + rect.width, clip.x + clip.width);
    const float bottom = std::min(rect.y + rect.height, clip.y + clip.height);
    rect.x = left;
    rect.y = top;
    rect.width = std::max(0.0f, right - left);
    rect.height = std::max(0.0f, bottom - top);
    return rect.width > 0.0f && rect.height > 0.0f;
}

} // namespace

TextPaintEncoder::WalkTextState TextPaintEncoder::Prepare(
    UINode& node_ref,
    Rect& visible_bounds,
    float abs_x,
    float abs_y,
    float width,
    float height,
    bool emit_layout_updates,
    bool scroll_dirty) {
    UINode* node = &node_ref;
    WalkTextState state{};
    Rect text_bounds{};
    ParagraphLayout& text_paragraph = state.paragraph;
    bool& text_render_window_visible = state.render_window_visible;
    bool& text_render_window_changed = state.render_window_changed;
    std::size_t& text_render_line_start = state.render_line_start;
    std::size_t& text_render_line_end = state.render_line_end;
    VisualGeometryWindow& text_geometry_window = state.geometry_window;
    bool& nonwrap_fragment_window_visible = state.fragment_window_visible;
    bool& nonwrap_fragment_window_changed = state.fragment_window_changed;
    NonWrappingFragmentWindow& nonwrap_fragment_window = state.fragment_window;
    if (node->is_text_node) {
        const bool uses_internal_textbox_viewport =
            IsSingleLineEditorTextNode(*node);
        if (visible_bounds.height <= 0.0f ||
            (visible_bounds.width <= 0.0f && (node->text_wrap || uses_internal_textbox_viewport))) {
            return state;
        }
        text_bounds = host_.ComputeTextContentBounds(*node);
        text_paragraph =
            host_.LayoutParagraph(*node, text_bounds.width > 0.0f ? std::optional<float>(text_bounds.width) : std::nullopt);
        if (!node->text_wrap && !uses_internal_textbox_viewport) {
            Rect nonwrap_visible_bounds{
                abs_x,
                abs_y,
                width + std::max(text_paragraph.width - text_bounds.width, 0.0f),
                height,
            };
            if (nonwrap_visible_bounds.width <= 0.0f || nonwrap_visible_bounds.height <= 0.0f) {
                nonwrap_visible_bounds.width = 0.0f;
                nonwrap_visible_bounds.height = 0.0f;
            } else {
                Rect window_clip{0.0f, 0.0f, host_.WindowWidth(), host_.WindowHeight()};
                (void)IntersectRect(nonwrap_visible_bounds, window_clip);
                for (std::uint64_t current = node->parent_handle; current != UI_INVALID_HANDLE;) {
                    const UINode* parent = host_.Resolve(current);
                    if (parent == nullptr) {
                        break;
                    }
                    if ((parent->clip_to_bounds || parent->is_scroll_view) &&
                        !IntersectRect(nonwrap_visible_bounds, host_.ComputeClipBounds(*parent))) {
                        break;
                    }
                    if (parent->is_portal) {
                        break;
                    }
                    current = parent->parent_handle;
                }
            }
            visible_bounds = nonwrap_visible_bounds;
        }
        const bool can_reuse_render_window_cache =
            !emit_layout_updates &&
            !scroll_dirty &&
            !node->text_glyphs_dirty &&
            node->text_layout_cache_valid;
        if (can_reuse_render_window_cache) {
            text_render_window_visible = node->text_render_window_valid;
            text_render_line_start = node->text_render_line_start;
            text_render_line_end = node->text_render_line_end;
            text_geometry_window.line_start = text_render_line_start;
            text_geometry_window.line_end = text_render_line_end;
            text_geometry_window.local_clip = Rect{
                visible_bounds.x - abs_x,
                visible_bounds.y - abs_y,
                std::max(visible_bounds.width, 0.0f),
                std::max(visible_bounds.height, 0.0f),
            };
            nonwrap_fragment_window_visible = node->nonwrap_render_fragment_window_valid;
            nonwrap_fragment_window.start = node->nonwrap_render_fragment_start;
            nonwrap_fragment_window.end = node->nonwrap_render_fragment_end;
        } else {
            text_geometry_window = host_.ResolveVisualGeometryWindow(
                *node,
                text_paragraph,
                visible_bounds,
                abs_x,
                abs_y);
            text_render_line_start = text_geometry_window.line_start;
            text_render_line_end = text_geometry_window.line_end;
            text_render_window_visible = text_geometry_window.visible();
            const bool single_line_nonwrap_fragment_window =
                text_render_window_visible &&
                !node->text_wrap &&
                !uses_internal_textbox_viewport &&
                text_paragraph.total_line_count == 1U &&
                node->nonwrap_fragment_cache_valid &&
                !text_paragraph.line_widths.empty();
            const bool multiline_nonwrap_fragment_window =
                text_render_window_visible &&
                !node->text_wrap &&
                !uses_internal_textbox_viewport &&
                text_paragraph.total_line_count > 1U &&
                node->nonwrap_fragment_cache_valid;
            if (single_line_nonwrap_fragment_window) {
                const float x_offset = host_.GetAlignedLineXOffset(*node, text_paragraph.line_widths.front());
                nonwrap_fragment_window = host_.ResolveNonWrappingFragmentWindow(
                    *node,
                    0U,
                    (visible_bounds.x - abs_x) - x_offset,
                    ((visible_bounds.x + visible_bounds.width) - abs_x) - x_offset);
                nonwrap_fragment_window_visible = nonwrap_fragment_window.start < nonwrap_fragment_window.end;
            }
            text_render_window_changed =
                node->text_render_window_valid != text_render_window_visible ||
                (text_render_window_visible &&
                 (node->text_render_line_start != text_render_line_start ||
                  node->text_render_line_end != text_render_line_end));
            nonwrap_fragment_window_changed =
                node->nonwrap_render_fragment_window_valid != nonwrap_fragment_window_visible ||
                (nonwrap_fragment_window_visible &&
                 (node->nonwrap_render_fragment_start != nonwrap_fragment_window.start ||
                  node->nonwrap_render_fragment_end != nonwrap_fragment_window.end));
            if (multiline_nonwrap_fragment_window && scroll_dirty) {
                nonwrap_fragment_window_changed = true;
            }
        }
    }
    return state;
}

void TextPaintEncoder::EmitGlyphs(
    std::uint64_t handle,
    UINode& node_ref,
    const WalkTextState& text,
    const Rect& visible_bounds,
    float abs_x,
    float text_offset_y,
    CommandBuilder& builder) {
    UINode* node = &node_ref;
    const ParagraphLayout& paragraph = text.paragraph;
    const std::size_t text_render_line_start = text.render_line_start;
    const std::size_t text_render_line_end = text.render_line_end;
    const bool text_render_window_visible = text.render_window_visible;
    const bool nonwrap_fragment_window_visible = text.fragment_window_visible;
    const NonWrappingFragmentWindow& nonwrap_fragment_window = text.fragment_window;
    const bool uses_internal_textbox_viewport = IsSingleLineEditorTextNode(*node);
                std::vector<GlyphPlacement> glyphs{};
                const bool use_nonwrap_fragment_culling =
                    !node->text_wrap &&
                    !uses_internal_textbox_viewport &&
                    node->nonwrap_fragment_cache_valid;
                for (std::size_t line_index = text_render_line_start; line_index < text_render_line_end; line_index += 1U) {
                    const std::int32_t start = paragraph.break_offsets[line_index];
                    const std::int32_t end = paragraph.break_offsets[line_index + 1U];
                    std::uint32_t line_start = static_cast<std::uint32_t>(start);
                    std::string_view line_text(
                        node->text_content.data() + line_start,
                        static_cast<std::size_t>(end - start));
                    while (!line_text.empty() && (line_text.front() == '\n' || line_text.front() == '\r')) {
                        line_text.remove_prefix(1U);
                        line_start += 1U;
                    }
                    if (line_text.empty()) {
                        continue;
                    }
                    const float full_line_width =
                        line_index < paragraph.line_widths.size() ? paragraph.line_widths[line_index] : 0.0f;
                    const float x_offset = host_.GetAlignedLineXOffset(*node, full_line_width);
                    const float line_top = host_.GetLineTopForIndex(*node, line_index);
                    const float line_ascent = host_.GetLineAscentForIndex(*node, line_index);
                    const float line_y = text_offset_y + line_top;
                    ShapedTextRun shaped{};
                    float fragment_x_offset = 0.0f;
                    std::optional<FragmentGeometrySlice> cached_slice{};
                    const CachedVisualLineShape* cached_visual_line =
                        !use_nonwrap_fragment_culling &&
                        node->visual_line_shape_cache_valid &&
                        line_index < node->visual_line_shapes.size()
                        ? host_.EnsureWrappedVisualLineShape(*node, line_index)
                        : nullptr;
                    if (use_nonwrap_fragment_culling) {
                        const NonWrappingFragmentWindow fragment_window = host_.ResolveNonWrappingFragmentWindow(
                            *node,
                            line_index,
                            (visible_bounds.x - abs_x) - x_offset,
                            ((visible_bounds.x + visible_bounds.width) - abs_x) - x_offset);
                        if (fragment_window.start == fragment_window.end) {
                            continue;
                        }
                        const NonWrappingTextFragment& first_fragment = node->nonwrap_fragments[fragment_window.start];
                        const NonWrappingTextFragment& last_fragment = node->nonwrap_fragments[fragment_window.end - 1U];
                        const std::uint32_t fragment_start =
                            host_.GetNonWrapFragmentAbsoluteStart(*node, line_index, first_fragment);
                        const std::uint32_t fragment_end =
                            host_.GetNonWrapFragmentAbsoluteEnd(*node, line_index, last_fragment);
                        if (fragment_end <= fragment_start || fragment_start < line_start) {
                            continue;
                        }
                        const std::size_t local_fragment_start = static_cast<std::size_t>(fragment_start - line_start);
                        const std::size_t local_fragment_end = static_cast<std::size_t>(fragment_end - line_start);
                        if (local_fragment_end > line_text.size() || local_fragment_start >= local_fragment_end) {
                            continue;
                        }
                        FragmentGeometrySlice slice{};
                        if (host_.TryBuildFragmentGeometrySliceFromLogicalLineShape(*node, line_index, fragment_start, fragment_end, slice)) {
                            shaped = slice.shaped;
                            fragment_x_offset = slice.slice_x;
                            cached_slice = std::move(slice);
                        } else {
                            if (!host_.ShapeText(
                                    line_text.substr(local_fragment_start, local_fragment_end - local_fragment_start),
                                    node->font_id,
                                    node->font_size,
                                    shaped,
                                    node->is_obscured)) {
                                continue;
                            }
                            fragment_x_offset = first_fragment.x;
                            FragmentGeometrySlice fallback_slice{};
                            fallback_slice.line_start = line_start;
                            fallback_slice.line_end = static_cast<std::uint32_t>(end);
                            fallback_slice.slice_start = fragment_start;
                            fallback_slice.slice_end = fragment_end;
                            fallback_slice.slice_x = first_fragment.x;
                            fallback_slice.full_line_width = full_line_width;
                            fallback_slice.shaped = shaped;
                            fallback_slice.cluster_stops = host_.BuildTextClusterStops(
                                shaped.glyphs,
                                shaped.width,
                                local_fragment_end - local_fragment_start);
                            cached_slice = std::move(fallback_slice);
                        }
                    } else if (cached_visual_line != nullptr) {
                        shaped.font_id = node->font_id;
                        shaped.width = cached_visual_line->width;
                        shaped.height = cached_visual_line->height;
                        shaped.baseline = cached_visual_line->baseline;
                        shaped.ascent = cached_visual_line->ascent;
                        shaped.descent = cached_visual_line->descent;
                        shaped.glyphs = cached_visual_line->glyphs;
                    } else if (!host_.ShapeTextStyledRange(
                                   *node,
                                   static_cast<std::uint32_t>(paragraph.break_offsets[line_index]),
                                   static_cast<std::uint32_t>(paragraph.break_offsets[line_index + 1U]),
                                   shaped)) {
                        continue;
                    }

                    if (cached_slice.has_value()) {
                        host_.StoreCachedNonWrapGeometrySlice(*node, line_index, *cached_slice);
                    }

                    const float viewport_offset_x =
                        use_nonwrap_fragment_culling
                        ? 0.0f
                        : (cached_visual_line != nullptr
                               ? 0.0f
                               : host_.GetTextboxViewportOffsetX(
                                     *node,
                                     shaped,
                                     static_cast<std::uint32_t>(paragraph.break_offsets[line_index]),
                                     static_cast<std::uint32_t>(paragraph.break_offsets[line_index + 1U])));
                    host_.AppendResolvedGlyphPlacements(
                        *node,
                        shaped,
                        x_offset - viewport_offset_x + fragment_x_offset,
                        line_y + line_ascent,
                        glyphs);
                }
                node->text_render_window_valid = text_render_window_visible;
                node->text_render_line_start = text_render_line_start;
                node->text_render_line_end = text_render_line_end;
                node->nonwrap_render_fragment_window_valid = nonwrap_fragment_window_visible;
                node->nonwrap_render_fragment_start = nonwrap_fragment_window.start;
                node->nonwrap_render_fragment_end = nonwrap_fragment_window.end;
                host_.EmitTextGlyphRun(builder, handle, *node, glyphs);
}

void TextPaintEncoder::EmitDecorations(
    std::uint64_t handle,
    UINode& node_ref,
    const WalkTextState& text,
    const Rect& visible_bounds,
    float abs_x,
    float abs_y,
    float scene_x,
    float scene_y,
    float text_offset_y,
    bool selection_visuals_only_update,
    CommandBuilder& builder) {
    UINode* node = &node_ref;
    const ParagraphLayout& paragraph = text.paragraph;
    const ParagraphLayout& text_paragraph = text.paragraph;
    const VisualGeometryWindow& text_geometry_window = text.geometry_window;
            builder.SetCaret(handle, scene_x, scene_y, 0.0f, 0U, 0U);
            VisualGeometryWindow paint_geometry_window = text_geometry_window;
            if (paint_geometry_window.visible()) {
                paint_geometry_window.line_start =
                    paint_geometry_window.line_start > 0U
                    ? paint_geometry_window.line_start - 1U
                    : 0U;
                paint_geometry_window.line_end = std::min(
                    paint_geometry_window.line_end + 1U,
                    text_paragraph.visible_line_count);
            }
            const std::vector<ColoredRect> background_rects = host_.BuildStyleInlineRects(*node, paint_geometry_window);
            const bool has_find_highlight =
                host_.TextFindHandle() == handle &&
                host_.TextFindStart() < host_.TextFindEnd();
            std::vector<ColoredRect> find_highlights{};
            if (has_find_highlight) {
                const std::vector<Rect> active_find_rects = host_.BuildSelectionRects(
                    *node,
                    host_.TextFindStart(),
                    host_.TextFindEnd(),
                    paint_geometry_window);
                host_.RecordFindRectangles(active_find_rects.size());
                for (const Rect& highlight : active_find_rects) {
                    find_highlights.push_back(ColoredRect{highlight, host_.TextFindColor()});
                }
            }
            for (const TextFindHighlight& highlight : host_.TextFindHighlights()) {
                if (highlight.handle != handle || highlight.start >= highlight.end) {
                    continue;
                }
                const std::vector<Rect> match_rects = host_.BuildSelectionRects(
                    *node,
                    highlight.start,
                    highlight.end,
                    paint_geometry_window);
                host_.RecordFindRectangles(match_rects.size());
                for (const Rect& rect : match_rects) {
                    find_highlights.push_back(ColoredRect{rect, highlight.color});
                }
            }
            auto emit_highlights =
                [&](const std::vector<Rect>& selection_rects) {
                    if (background_rects.empty() &&
                        selection_rects.empty() &&
                        !find_highlights.empty()) {
                        builder.SetHighlightsColored(handle, find_highlights);
                    } else if (!background_rects.empty() || !find_highlights.empty()) {
                        std::vector<ColoredRect> combined_highlights{};
                        combined_highlights.reserve(
                            background_rects.size() +
                            selection_rects.size() +
                            find_highlights.size());
                        combined_highlights.insert(combined_highlights.end(), background_rects.begin(), background_rects.end());
                        for (const Rect& highlight : selection_rects) {
                            combined_highlights.push_back(ColoredRect{highlight, node->selection_color});
                        }
                        combined_highlights.insert(combined_highlights.end(), find_highlights.begin(), find_highlights.end());
                        builder.SetHighlightsColored(handle, combined_highlights);
                    } else {
                        builder.SetHighlights(handle, node->selection_color, selection_rects);
                    }
                };
            if (node->is_selectable || node->is_editable || has_find_highlight) {
                const std::uint32_t caret_index =
                    std::min<std::uint32_t>(node->selection_end, static_cast<std::uint32_t>(node->text_content.size()));
                std::uint32_t highlight_start = node->selection_start;
                std::uint32_t highlight_end = node->selection_end;
                const bool can_emit_selection_highlights = node->is_selectable || node->is_editable;
                const bool has_cross_highlight =
                    can_emit_selection_highlights &&
                    host_.GetCrossSelectionHighlight(handle, highlight_start, highlight_end);
                const std::vector<Rect> highlights =
                    (can_emit_selection_highlights &&
                     (has_cross_highlight || node->selection_start != node->selection_end))
                    ? host_.BuildSelectionRects(*node, highlight_start, highlight_end, paint_geometry_window)
                    : std::vector<Rect>{};
                for (const Rect& highlight : highlights) {
                    Rect hit_rect{
                        abs_x + highlight.x,
                        abs_y + highlight.y,
                        highlight.width,
                        highlight.height,
                    };
                    if (IntersectRect(hit_rect, visible_bounds)) {
                        host_.RecordSelectionHitRect(hit_rect);
                    }
                }
                emit_highlights(highlights);
                if (
                    can_emit_selection_highlights &&
                    highlights.empty() &&
                    host_.IsFocused(handle) &&
                    !has_cross_highlight &&
                    (node->is_editable || IsEditorTextNode(*node))) {
                    const bool caret_trailing_edge =
                        node->selection_start == node->selection_end && node->caret_trailing_edge;
                    const auto [local_x, line_index] =
                        host_.GetLocalPositionFromIndex(*node, caret_index, caret_trailing_edge);
                    const std::size_t caret_line_index = static_cast<std::size_t>(line_index);
                    const bool uses_fragment_geometry =
                        !node->text_wrap &&
                        !(IsSingleLineEditorTextNode(*node)) &&
                        node->nonwrap_fragment_cache_valid;
                    float caret_height = host_.GetLineHeightForIndex(*node, caret_line_index);
                    if (!uses_fragment_geometry) {
                        if (node->visual_line_shape_cache_valid &&
                            caret_line_index < node->visual_line_shapes.size()) {
                            const CachedVisualLineShape* caret_visual_line =
                                host_.EnsureWrappedVisualLineShape(*node, caret_line_index);
                            caret_height = caret_visual_line != nullptr
                                ? caret_visual_line->height
                                : node->visual_line_shapes[caret_line_index].height;
                        } else {
                            ShapedTextRun caret_line{};
                            if (caret_line_index < paragraph.break_offsets.size() - 1U) {
                                const std::int32_t start = paragraph.break_offsets[caret_line_index];
                                const std::int32_t end = paragraph.break_offsets[caret_line_index + 1U];
                                std::string_view line_text(
                                    node->text_content.data() + start,
                                    static_cast<std::size_t>(end - start));
                                while (!line_text.empty() && (line_text.front() == '\n' || line_text.front() == '\r')) {
                                    line_text.remove_prefix(1U);
                                }
                                (void)host_.ShapeText(line_text, node->font_id, node->font_size, caret_line, node->is_obscured);
                            }
                            caret_height = std::max(caret_line.height, host_.GetLineHeightForIndex(*node, caret_line_index));
                        }
                    }
                    const float caret_line_height = host_.GetLineHeightForIndex(*node, caret_line_index);
                    const float caret_line_ascent = host_.GetLineAscentForIndex(*node, caret_line_index);
                    const float caret_line_top = host_.GetLineTopForIndex(*node, caret_line_index);
                    float caret_ascent = caret_line_ascent;
                    if (!uses_fragment_geometry) {
                        if (node->visual_line_shape_cache_valid &&
                            caret_line_index < node->visual_line_shapes.size()) {
                            const CachedVisualLineShape* caret_visual_line =
                                host_.EnsureWrappedVisualLineShape(*node, caret_line_index);
                            caret_ascent = caret_visual_line != nullptr
                                ? caret_visual_line->ascent
                                : node->visual_line_shapes[caret_line_index].ascent;
                        } else {
                            ShapedTextRun caret_line{};
                            if (caret_line_index < paragraph.break_offsets.size() - 1U) {
                                const std::int32_t start = paragraph.break_offsets[caret_line_index];
                                const std::int32_t end = paragraph.break_offsets[caret_line_index + 1U];
                                std::string_view line_text(
                                    node->text_content.data() + start,
                                    static_cast<std::size_t>(end - start));
                                while (!line_text.empty() && (line_text.front() == '\n' || line_text.front() == '\r')) {
                                    line_text.remove_prefix(1U);
                                }
                                (void)host_.ShapeText(line_text, node->font_id, node->font_size, caret_line, node->is_obscured);
                            }
                            if (caret_line.height >= caret_line_height) {
                                caret_ascent = caret_line.ascent;
                            }
                        }
                    }
                    builder.SetCaret(
                        handle,
                        scene_x + local_x,
                        scene_y + text_offset_y + caret_line_top + (caret_line_ascent - caret_ascent),
                        std::max(caret_height, 1.0f),
                        node->caret_color,
                        static_cast<std::uint32_t>(std::min<std::uint64_t>(node->last_interaction_time, 0xFFFFFFFFULL)));
                }
            } else {
                emit_highlights(std::vector<Rect>{});
            }
            if (!selection_visuals_only_update) {
                builder.SetTextFade(handle, host_.ResolveTextFadeMask(*node, paragraph));
            }
}

void TextPaintEncoder::Emit(
    std::uint64_t handle,
    UINode& node,
    const WalkTextState& text,
    const Rect& visible_bounds,
    float abs_x,
    float abs_y,
    float scene_x,
    float scene_y,
    bool selection_visuals_only_update,
    CommandBuilder& builder) {
    if (!node.is_text_node) {
        return;
    }
    const float text_offset_y = host_.GetAlignedTextYOffset(node, text.paragraph.height);
    if (!selection_visuals_only_update) {
        EmitGlyphs(handle, node, text, visible_bounds, abs_x, text_offset_y, builder);
    }
    EmitDecorations(
        handle, node, text, visible_bounds, abs_x, abs_y, scene_x, scene_y,
        text_offset_y, selection_visuals_only_update, builder);
    node.text_glyphs_dirty = false;
    node.text_selection_visuals_dirty = false;
}



} // namespace effindom::v2::ui
