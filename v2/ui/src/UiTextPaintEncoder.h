#pragma once

#include "UiTextPaintAccess.h"

namespace effindom::v2::ui {

class TextPaintEncoder {
public:
    using WalkTextState = UiRuntime::WalkTextState;
    using ParagraphLayout = UiRuntime::ParagraphLayout;
    using VisualGeometryWindow = UiRuntime::VisualGeometryWindow;
    using NonWrappingFragmentWindow = UiRuntime::NonWrappingFragmentWindow;
    using ShapedTextRun = UiRuntime::ShapedTextRun;
    using FragmentGeometrySlice = UiRuntime::FragmentGeometrySlice;
    using TextFindHighlight = UiRuntime::TextFindHighlight;

    explicit TextPaintEncoder(TextPaintAccess host)
        : host_(host) {}

    WalkTextState Prepare(
        UINode& node,
        Rect& visible_bounds,
        float abs_x,
        float abs_y,
        float width,
        float height,
        bool emit_layout_updates,
        bool scroll_dirty);
    void Emit(
        std::uint64_t handle,
        UINode& node,
        const WalkTextState& text,
        const Rect& visible_bounds,
        float abs_x,
        float abs_y,
        float scene_x,
        float scene_y,
        bool selection_visuals_only_update,
        CommandBuilder& builder);

private:
    void EmitGlyphs(
        std::uint64_t handle,
        UINode& node,
        const WalkTextState& text,
        const Rect& visible_bounds,
        float abs_x,
        float text_offset_y,
        CommandBuilder& builder);
    void EmitDecorations(
        std::uint64_t handle,
        UINode& node,
        const WalkTextState& text,
        const Rect& visible_bounds,
        float abs_x,
        float abs_y,
        float scene_x,
        float scene_y,
        float text_offset_y,
        bool selection_visuals_only_update,
        CommandBuilder& builder);

    TextPaintAccess host_;
};

} // namespace effindom::v2::ui
