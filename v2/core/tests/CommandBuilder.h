#pragma once

#include "Engine.h"
#include "effindom.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace effindom::v2::test {

inline std::uint32_t FloatBits(float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float size mismatch");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline std::uint64_t Handle(std::uint32_t index, std::uint32_t generation = 1) {
    return (static_cast<std::uint64_t>(generation) << 32) | index;
}

class CommandBuilder {
public:
    void CreateNode(std::uint64_t handle) {
        PushHandleCommand(CMD_CREATE_NODE, handle);
    }

    void DeleteNode(std::uint64_t handle) {
        PushHandleCommand(CMD_DELETE_NODE, handle);
    }

    void SetBounds(
        std::uint64_t handle,
        float x,
        float y,
        float width,
        float height,
        bool interactive,
        std::uint32_t clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL) {
        SetBounds(handle, x, y, width, height, x, y, width, height, x, y, width, height, interactive, clip_mode);
    }

    void SetBounds(
        std::uint64_t handle,
        float vis_x,
        float vis_y,
        float vis_width,
        float vis_height,
        float hit_x,
        float hit_y,
        float hit_width,
        float hit_height,
        bool interactive,
        std::uint32_t clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL
    ) {
        SetBounds(
            handle,
            vis_x,
            vis_y,
            vis_width,
            vis_height,
            hit_x,
            hit_y,
            hit_width,
            hit_height,
            vis_x,
            vis_y,
            vis_width,
            vis_height,
            interactive,
            clip_mode);
    }

    void SetBounds(
        std::uint64_t handle,
        float vis_x,
        float vis_y,
        float vis_width,
        float vis_height,
        float hit_x,
        float hit_y,
        float hit_width,
        float hit_height,
        float clip_x,
        float clip_y,
        float clip_width,
        float clip_height,
        bool interactive,
        std::uint32_t clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL
    ) {
        words_.push_back(CMD_SET_BOUNDS);
        PushHandle(handle);
        words_.push_back(FloatBits(vis_x));
        words_.push_back(FloatBits(vis_y));
        words_.push_back(FloatBits(vis_width));
        words_.push_back(FloatBits(vis_height));
        words_.push_back(FloatBits(hit_x));
        words_.push_back(FloatBits(hit_y));
        words_.push_back(FloatBits(hit_width));
        words_.push_back(FloatBits(hit_height));
        words_.push_back(FloatBits(clip_x));
        words_.push_back(FloatBits(clip_y));
        words_.push_back(FloatBits(clip_width));
        words_.push_back(FloatBits(clip_height));
        const std::uint32_t bounds_flags =
            (interactive ? ED_BOUNDS_FLAG_INTERACTIVE : 0U) |
            ((clip_mode << ED_BOUNDS_CLIP_MODE_SHIFT) & ED_BOUNDS_CLIP_MODE_MASK);
        words_.push_back(bounds_flags);
    }

    void SetBoxStyle(
        std::uint64_t handle,
        std::uint32_t bg_color,
        float radius_tl,
        float radius_tr,
        float radius_br,
        float radius_bl,
        float border_width = 0.0f,
        std::uint32_t border_color = 0,
        std::uint32_t border_style = ED_BORDER_SOLID,
        float border_dash_on = 0.0f,
        float border_dash_off = 0.0f
    ) {
        words_.push_back(CMD_SET_BOX_STYLE);
        PushHandle(handle);
        words_.push_back(bg_color);
        words_.push_back(FloatBits(radius_tl));
        words_.push_back(FloatBits(radius_tr));
        words_.push_back(FloatBits(radius_br));
        words_.push_back(FloatBits(radius_bl));
        words_.push_back(FloatBits(border_width));
        words_.push_back(border_color);
        words_.push_back(border_style);
        words_.push_back(FloatBits(border_dash_on));
        words_.push_back(FloatBits(border_dash_off));
    }

    void SetLinearGradient(
        std::uint64_t handle,
        float sx,
        float sy,
        float ex,
        float ey,
        const std::vector<GradientStop>& stops
    ) {
        words_.push_back(CMD_SET_LINEAR_GRADIENT);
        PushHandle(handle);
        words_.push_back(FloatBits(sx));
        words_.push_back(FloatBits(sy));
        words_.push_back(FloatBits(ex));
        words_.push_back(FloatBits(ey));
        words_.push_back(static_cast<std::uint32_t>(stops.size()));
        for (const GradientStop& stop : stops) {
            words_.push_back(FloatBits(stop.offset));
            words_.push_back(stop.color);
        }
    }

    void SetLayerEffect(std::uint64_t handle, float opacity, float blur_sigma, std::uint32_t blend_mode) {
        words_.push_back(CMD_SET_LAYER_EFFECT);
        PushHandle(handle);
        words_.push_back(FloatBits(opacity));
        words_.push_back(FloatBits(blur_sigma));
        words_.push_back(blend_mode);
    }

    void SetBackgroundBlur(std::uint64_t handle, float blur_sigma) {
        words_.push_back(CMD_SET_BACKGROUND_BLUR);
        PushHandle(handle);
        words_.push_back(FloatBits(blur_sigma));
    }

    void SetDropShadow(
        std::uint64_t handle,
        std::uint32_t color,
        float offset_x,
        float offset_y,
        float blur_sigma,
        float spread) {
        words_.push_back(CMD_SET_DROP_SHADOW);
        PushHandle(handle);
        words_.push_back(color);
        words_.push_back(FloatBits(offset_x));
        words_.push_back(FloatBits(offset_y));
        words_.push_back(FloatBits(blur_sigma));
        words_.push_back(FloatBits(spread));
    }

    void SetImage(
        std::uint64_t handle,
        std::uint32_t texture_id,
        std::uint32_t object_fit,
        std::uint32_t sampling = ED_IMAGE_SAMPLING_LINEAR,
        std::uint32_t max_aniso = 0) {
        words_.push_back(CMD_SET_IMAGE);
        PushHandle(handle);
        words_.push_back(texture_id);
        words_.push_back(object_fit);
        words_.push_back(sampling);
        words_.push_back(max_aniso);
    }

    void SetImageNine(
        std::uint64_t handle,
        std::uint32_t texture_id,
        float inset_left,
        float inset_top,
        float inset_right,
        float inset_bottom,
        std::uint32_t sampling = ED_IMAGE_SAMPLING_LINEAR,
        std::uint32_t max_aniso = 0
    ) {
        words_.push_back(CMD_SET_IMAGE_NINE);
        PushHandle(handle);
        words_.push_back(texture_id);
        words_.push_back(FloatBits(inset_left));
        words_.push_back(FloatBits(inset_top));
        words_.push_back(FloatBits(inset_right));
        words_.push_back(FloatBits(inset_bottom));
        words_.push_back(sampling);
        words_.push_back(max_aniso);
    }

    void SetPath(
        std::uint64_t handle,
        std::uint32_t fill_color,
        std::uint32_t stroke_color,
        float stroke_width,
        const std::vector<PathVerbRecord>& verbs
    ) {
        words_.push_back(CMD_SET_PATH);
        PushHandle(handle);
        words_.push_back(fill_color);
        words_.push_back(stroke_color);
        words_.push_back(FloatBits(stroke_width));
        words_.push_back(static_cast<std::uint32_t>(verbs.size()));
        for (const PathVerbRecord& verb : verbs) {
            words_.push_back(verb.verb);
            for (std::uint32_t index = 0; index < verb.arg_count; index += 1) {
                words_.push_back(FloatBits(verb.args[index]));
            }
        }
    }

    void SetSvg(
        std::uint64_t handle,
        std::uint32_t svg_id,
        std::uint32_t tint_color,
        std::uint32_t sampling = ED_IMAGE_SAMPLING_LINEAR,
        std::uint32_t max_aniso = 0) {
        words_.push_back(CMD_SET_SVG);
        PushHandle(handle);
        words_.push_back(svg_id);
        words_.push_back(tint_color);
        words_.push_back(sampling);
        words_.push_back(max_aniso);
    }

    void SetGlyphRun(
        std::uint64_t handle,
        std::uint32_t font_id,
        float font_size,
        std::uint32_t color,
        const std::vector<GlyphPlacement>& glyphs
    ) {
        words_.push_back(CMD_SET_GLYPH_RUN);
        PushHandle(handle);
        words_.push_back(font_id);
        words_.push_back(FloatBits(font_size));
        words_.push_back(color);
        words_.push_back(static_cast<std::uint32_t>(glyphs.size()));
        for (const GlyphPlacement& glyph : glyphs) {
            words_.push_back(glyph.glyph_id);
            words_.push_back(FloatBits(glyph.x));
            words_.push_back(FloatBits(glyph.y));
            words_.push_back(glyph.font_id != 0U ? glyph.font_id : font_id);
        }
    }

    void SetGlyphRunColored(
        std::uint64_t handle,
        std::uint32_t font_id,
        float font_size,
        const std::vector<GlyphPlacement>& glyphs
    ) {
        words_.push_back(CMD_SET_GLYPH_RUN_COLORED);
        PushHandle(handle);
        words_.push_back(font_id);
        words_.push_back(FloatBits(font_size));
        words_.push_back(static_cast<std::uint32_t>(glyphs.size()));
        for (const GlyphPlacement& glyph : glyphs) {
            words_.push_back(glyph.glyph_id);
            words_.push_back(FloatBits(glyph.x));
            words_.push_back(FloatBits(glyph.y));
            words_.push_back(glyph.font_id != 0U ? glyph.font_id : font_id);
            words_.push_back(glyph.color);
        }
    }

    void SetGlyphRunStyled(
        std::uint64_t handle,
        std::uint32_t font_id,
        float font_size,
        const std::vector<GlyphPlacement>& glyphs
    ) {
        words_.push_back(CMD_SET_GLYPH_RUN_STYLED);
        PushHandle(handle);
        words_.push_back(font_id);
        words_.push_back(FloatBits(font_size));
        words_.push_back(static_cast<std::uint32_t>(glyphs.size()));
        for (const GlyphPlacement& glyph : glyphs) {
            words_.push_back(glyph.glyph_id);
            words_.push_back(FloatBits(glyph.x));
            words_.push_back(FloatBits(glyph.y));
            words_.push_back(glyph.font_id != 0U ? glyph.font_id : font_id);
            words_.push_back(glyph.color);
            words_.push_back(FloatBits(glyph.font_size > 0.0f ? glyph.font_size : font_size));
        }
    }

    void SetTextFade(std::uint64_t handle, std::uint32_t fade_edge) {
        words_.push_back(CMD_SET_TEXT_FADE);
        PushHandle(handle);
        words_.push_back(fade_edge);
    }

    void SetCaret(
        std::uint64_t handle,
        float x,
        float y,
        float height,
        std::uint32_t color,
        std::uint32_t last_interaction_ms
    ) {
        words_.push_back(CMD_SET_CARET);
        PushHandle(handle);
        words_.push_back(FloatBits(x));
        words_.push_back(FloatBits(y));
        words_.push_back(FloatBits(height));
        words_.push_back(color);
        words_.push_back(last_interaction_ms);
    }

    void SetHighlights(std::uint64_t handle, std::uint32_t color, const std::vector<Rect>& rects) {
        words_.push_back(CMD_SET_HIGHLIGHTS);
        PushHandle(handle);
        words_.push_back(color);
        words_.push_back(static_cast<std::uint32_t>(rects.size()));
        for (const Rect& rect : rects) {
            words_.push_back(FloatBits(rect.x));
            words_.push_back(FloatBits(rect.y));
            words_.push_back(FloatBits(rect.width));
            words_.push_back(FloatBits(rect.height));
        }
    }

    void SetHighlightsColored(std::uint64_t handle, const std::vector<ColoredRect>& rects) {
        words_.push_back(CMD_SET_HIGHLIGHTS_COLORED);
        PushHandle(handle);
        words_.push_back(static_cast<std::uint32_t>(rects.size()));
        for (const ColoredRect& colored_rect : rects) {
            words_.push_back(FloatBits(colored_rect.rect.x));
            words_.push_back(FloatBits(colored_rect.rect.y));
            words_.push_back(FloatBits(colored_rect.rect.width));
            words_.push_back(FloatBits(colored_rect.rect.height));
            words_.push_back(colored_rect.color);
        }
    }

    void CommitScene(const std::vector<SceneInstructionDebugView>& instructions) {
        words_.push_back(CMD_COMMIT_SCENE);
        words_.push_back(static_cast<std::uint32_t>(instructions.size()));
        for (const SceneInstructionDebugView& instruction : instructions) {
            words_.push_back(instruction.opcode);
            PushHandle(instruction.handle);
            words_.push_back(0U);
            words_.push_back(0U);
        }
    }

    void CommitPaintOrder(const std::vector<std::uint64_t>& handles) {
        words_.push_back(CMD_COMMIT_PAINT_ORDER);
        words_.push_back(static_cast<std::uint32_t>(handles.size()));
        for (std::uint64_t handle : handles) {
            PushHandle(handle);
        }
    }

    void PushRaw(std::uint32_t word) {
        words_.push_back(word);
    }

    const std::vector<std::uint32_t>& words() const {
        return words_;
    }

private:
    void PushHandleCommand(std::uint32_t opcode, std::uint64_t handle) {
        words_.push_back(opcode);
        PushHandle(handle);
    }

    void PushHandle(std::uint64_t handle) {
        words_.push_back(static_cast<std::uint32_t>(handle & 0xffffffffULL));
        words_.push_back(static_cast<std::uint32_t>(handle >> 32));
    }

    std::vector<std::uint32_t> words_{};
};

} // namespace effindom::v2::test
