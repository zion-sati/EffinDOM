#pragma once

#include "UiRuntime.h"

namespace effindom::v2::ui {

class TextPaintAccess {
public:
    using ParagraphLayout = UiRuntime::ParagraphLayout;
    using VisualGeometryWindow = UiRuntime::VisualGeometryWindow;
    using NonWrappingFragmentWindow = UiRuntime::NonWrappingFragmentWindow;
    using FragmentGeometrySlice = UiRuntime::FragmentGeometrySlice;
    using ShapedTextRun = UiRuntime::ShapedTextRun;
    using TextFindHighlight = UiRuntime::TextFindHighlight;

    explicit TextPaintAccess(UiRuntime& owner) : owner_(owner) {}

    const UINode* Resolve(std::uint64_t handle) const { return owner_.Resolve(handle); }
    Rect ComputeTextContentBounds(const UINode& node) const { return owner_.ComputeTextContentBounds(node); }
    Rect ComputeClipBounds(const UINode& node) const { return owner_.ComputeClipBounds(node); }
    ParagraphLayout LayoutParagraph(const UINode& node, std::optional<float> width) const {
        return owner_.LayoutParagraph(node, width);
    }
    VisualGeometryWindow ResolveVisualGeometryWindow(
        const UINode& node, const ParagraphLayout& paragraph, const Rect& visible_bounds,
        float abs_x, float abs_y) const {
        return owner_.ResolveVisualGeometryWindow(node, paragraph, visible_bounds, abs_x, abs_y);
    }
    float GetAlignedLineXOffset(const UINode& node, float width) const {
        return owner_.GetAlignedLineXOffset(node, width);
    }
    NonWrappingFragmentWindow ResolveNonWrappingFragmentWindow(
        const UINode& node, std::size_t line_index, float left, float right) const {
        return owner_.ResolveNonWrappingFragmentWindow(node, line_index, left, right);
    }
    float GetLineTopForIndex(const UINode& node, std::size_t line_index) const {
        return owner_.GetLineTopForIndex(node, line_index);
    }
    float GetLineAscentForIndex(const UINode& node, std::size_t line_index) const {
        return owner_.GetLineAscentForIndex(node, line_index);
    }
    float GetLineHeightForIndex(const UINode& node, std::size_t line_index) const {
        return owner_.GetLineHeightForIndex(node, line_index);
    }
    const CachedVisualLineShape* EnsureWrappedVisualLineShape(
        const UINode& node, std::size_t line_index) const {
        return owner_.EnsureWrappedVisualLineShape(node, line_index);
    }
    std::uint32_t GetNonWrapFragmentAbsoluteStart(
        const UINode& node, std::size_t line_index, const NonWrappingTextFragment& fragment) const {
        return owner_.GetNonWrapFragmentAbsoluteStart(node, line_index, fragment);
    }
    std::uint32_t GetNonWrapFragmentAbsoluteEnd(
        const UINode& node, std::size_t line_index, const NonWrappingTextFragment& fragment) const {
        return owner_.GetNonWrapFragmentAbsoluteEnd(node, line_index, fragment);
    }
    bool TryBuildFragmentGeometrySliceFromLogicalLineShape(
        const UINode& node, std::size_t line_index, std::uint32_t start,
        std::uint32_t end, FragmentGeometrySlice& out) const {
        return owner_.TryBuildFragmentGeometrySliceFromLogicalLineShape(node, line_index, start, end, out);
    }
    bool ShapeText(
        std::string_view text, std::uint32_t font_id, float font_size,
        ShapedTextRun& out, bool obscured = false) const {
        return owner_.ShapeText(text, font_id, font_size, out, obscured);
    }
    std::vector<TextClusterStop> BuildTextClusterStops(
        const std::vector<GlyphPlacement>& glyphs, float width, std::size_t text_length) const {
        return owner_.BuildTextClusterStops(glyphs, width, text_length);
    }
    bool ShapeTextStyledRange(
        const UINode& node, std::uint32_t start, std::uint32_t end, ShapedTextRun& out) const {
        return owner_.ShapeTextStyledRange(node, start, end, out);
    }
    void StoreCachedNonWrapGeometrySlice(
        UINode& node, std::size_t line_index, const FragmentGeometrySlice& slice) const {
        owner_.StoreCachedNonWrapGeometrySlice(node, line_index, slice);
    }
    float GetTextboxViewportOffsetX(
        const UINode& node, const ShapedTextRun& line,
        std::uint32_t start, std::uint32_t end) const {
        return owner_.GetTextboxViewportOffsetX(node, line, start, end);
    }
    void AppendResolvedGlyphPlacements(
        const UINode& node, const ShapedTextRun& shaped, float x,
        float baseline_y, std::vector<GlyphPlacement>& out) const {
        owner_.AppendResolvedGlyphPlacements(node, shaped, x, baseline_y, out);
    }
    void EmitTextGlyphRun(
        CommandBuilder& builder, std::uint64_t handle, const UINode& node,
        const std::vector<GlyphPlacement>& glyphs) const {
        owner_.EmitTextGlyphRun(builder, handle, node, glyphs);
    }
    std::vector<ColoredRect> BuildStyleInlineRects(
        const UINode& node, std::optional<VisualGeometryWindow> window) const {
        return owner_.BuildStyleInlineRects(node, window);
    }
    std::vector<Rect> BuildSelectionRects(
        const UINode& node, std::uint32_t start, std::uint32_t end,
        std::optional<VisualGeometryWindow> window, bool clip = true) const {
        return owner_.BuildSelectionRects(node, start, end, window, clip);
    }
    bool GetCrossSelectionHighlight(
        std::uint64_t handle, std::uint32_t& start, std::uint32_t& end) const {
        return owner_.GetCrossSelectionHighlight(handle, start, end);
    }
    std::pair<float, int> GetLocalPositionFromIndex(
        const UINode& node, std::uint32_t index, bool trailing = false) const {
        return owner_.GetLocalPositionFromIndex(node, index, trailing);
    }
    std::uint32_t ResolveTextFadeMask(const UINode& node, const ParagraphLayout& paragraph) const {
        return owner_.ResolveTextFadeMask(node, paragraph);
    }
    float GetAlignedTextYOffset(const UINode& node, float height) const {
        return owner_.GetAlignedTextYOffset(node, height);
    }
    float WindowWidth() const { return owner_.window_width_; }
    float WindowHeight() const { return owner_.window_height_; }
    std::uint64_t TextFindHandle() const { return owner_.text_find_handle_; }
    std::uint32_t TextFindStart() const { return owner_.text_find_start_; }
    std::uint32_t TextFindEnd() const { return owner_.text_find_end_; }
    const std::vector<TextFindHighlight>& TextFindHighlights() const { return owner_.text_find_highlights_; }
    std::uint32_t TextFindColor() const { return owner_.text_find_color_; }
    void RecordFindRectangles(std::size_t count) const {
        owner_.text_geometry_profile_.find_rectangles_emitted += count;
    }
    void RecordSelectionHitRect(const Rect& rect) { owner_.Selection().RecordHitRect(rect); }
    bool IsFocused(std::uint64_t handle) const { return owner_.Focus().IsFocused(handle); }

private:
    UiRuntime& owner_;
};

} // namespace effindom::v2::ui
