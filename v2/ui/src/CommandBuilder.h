#pragma once

#include "../../core/include/effindom.h"

#include "UiTypes.h"

#include <cstring>
#include <vector>

namespace effindom::v2::ui {

class CommandBuilder {
public:
    explicit CommandBuilder(std::vector<std::uint32_t>& words)
        : words_(words) {}

    static std::uint32_t FloatToWord(float value) {
        std::uint32_t word = 0;
        std::memcpy(&word, &value, sizeof(word));
        return word;
    }

    static float WordToFloat(std::uint32_t word) {
        float value = 0.0f;
        std::memcpy(&value, &word, sizeof(value));
        return value;
    }

    void CreateNode(std::uint64_t handle) {
        words_.push_back(CMD_CREATE_NODE);
        PushHandle(handle);
    }

    void DeleteNode(std::uint64_t handle) {
        words_.push_back(CMD_DELETE_NODE);
        PushHandle(handle);
    }

    void SetBounds(
        std::uint64_t handle,
        float x,
        float y,
        float width,
        float height,
        bool interactive = false,
        std::uint32_t clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL) {
        SetBounds(handle, x, y, width, height, x, y, width, height, x, y, width, height, interactive, clip_mode);
    }

    void SetBounds(
        std::uint64_t handle,
        float x,
        float y,
        float width,
        float height,
        float hit_x,
        float hit_y,
        float hit_width,
        float hit_height,
        bool interactive = false,
        std::uint32_t clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL) {
        SetBounds(
            handle,
            x,
            y,
            width,
            height,
            hit_x,
            hit_y,
            hit_width,
            hit_height,
            x,
            y,
            width,
            height,
            interactive,
            clip_mode);
    }

    void SetBounds(
        std::uint64_t handle,
        float x,
        float y,
        float width,
        float height,
        float hit_x,
        float hit_y,
        float hit_width,
        float hit_height,
        float clip_x,
        float clip_y,
        float clip_width,
        float clip_height,
        bool interactive = false,
        std::uint32_t clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL) {
        words_.push_back(CMD_SET_BOUNDS);
        PushHandle(handle);
        PushFloat(x);
        PushFloat(y);
        PushFloat(width);
        PushFloat(height);
        PushFloat(hit_x);
        PushFloat(hit_y);
        PushFloat(hit_width);
        PushFloat(hit_height);
        PushFloat(clip_x);
        PushFloat(clip_y);
        PushFloat(clip_width);
        PushFloat(clip_height);
        const std::uint32_t bounds_flags =
            (interactive ? static_cast<std::uint32_t>(ED_BOUNDS_FLAG_INTERACTIVE) : 0U) |
            ((clip_mode << ED_BOUNDS_CLIP_MODE_SHIFT) & ED_BOUNDS_CLIP_MODE_MASK);
        words_.push_back(bounds_flags);
    }

    void SetBoxStyle(
        std::uint64_t handle,
        std::uint32_t bg_color,
        float radius_tl = 0.0f,
        float radius_tr = 0.0f,
        float radius_br = 0.0f,
        float radius_bl = 0.0f,
        float border_width = 0.0f,
        std::uint32_t border_color = 0,
        std::uint32_t border_style = ED_BORDER_SOLID,
        float border_dash_on = 0.0f,
        float border_dash_off = 0.0f) {
        words_.push_back(CMD_SET_BOX_STYLE);
        PushHandle(handle);
        words_.push_back(bg_color);
        PushFloat(radius_tl);
        PushFloat(radius_tr);
        PushFloat(radius_br);
        PushFloat(radius_bl);
        PushFloat(border_width);
        words_.push_back(border_color);
        words_.push_back(border_style);
        PushFloat(border_dash_on);
        PushFloat(border_dash_off);
    }

    void SetLinearGradient(
        std::uint64_t handle,
        float sx,
        float sy,
        float ex,
        float ey,
        const std::vector<GradientStop>& stops) {
        words_.push_back(CMD_SET_LINEAR_GRADIENT);
        PushHandle(handle);
        PushFloat(sx);
        PushFloat(sy);
        PushFloat(ex);
        PushFloat(ey);
        words_.push_back(static_cast<std::uint32_t>(stops.size()));
        for (const GradientStop& stop : stops) {
            PushFloat(stop.offset);
            words_.push_back(stop.color);
        }
    }

    void SetLayerEffect(std::uint64_t handle, float opacity, float blur_sigma, std::uint32_t blend_mode) {
        words_.push_back(CMD_SET_LAYER_EFFECT);
        PushHandle(handle);
        PushFloat(opacity);
        PushFloat(blur_sigma);
        words_.push_back(blend_mode);
    }

    void SetBackgroundBlur(std::uint64_t handle, float blur_sigma) {
        words_.push_back(CMD_SET_BACKGROUND_BLUR);
        PushHandle(handle);
        PushFloat(blur_sigma);
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
        PushFloat(offset_x);
        PushFloat(offset_y);
        PushFloat(blur_sigma);
        PushFloat(spread);
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
        std::uint32_t max_aniso = 0) {
        words_.push_back(CMD_SET_IMAGE_NINE);
        PushHandle(handle);
        words_.push_back(texture_id);
        PushFloat(inset_left);
        PushFloat(inset_top);
        PushFloat(inset_right);
        PushFloat(inset_bottom);
        words_.push_back(sampling);
        words_.push_back(max_aniso);
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
        const std::vector<GlyphPlacement>& glyphs) {
        words_.push_back(CMD_SET_GLYPH_RUN);
        PushHandle(handle);
        words_.push_back(font_id);
        PushFloat(font_size);
        words_.push_back(color);
        words_.push_back(static_cast<std::uint32_t>(glyphs.size()));
        for (const GlyphPlacement& glyph : glyphs) {
            words_.push_back(glyph.glyph_id);
            PushFloat(glyph.x);
            PushFloat(glyph.y);
            words_.push_back(glyph.font_id != 0U ? glyph.font_id : font_id);
        }
    }

    void SetGlyphRunColored(
        std::uint64_t handle,
        std::uint32_t font_id,
        float font_size,
        const std::vector<GlyphPlacement>& glyphs) {
        words_.push_back(CMD_SET_GLYPH_RUN_COLORED);
        PushHandle(handle);
        words_.push_back(font_id);
        PushFloat(font_size);
        words_.push_back(static_cast<std::uint32_t>(glyphs.size()));
        for (const GlyphPlacement& glyph : glyphs) {
            words_.push_back(glyph.glyph_id);
            PushFloat(glyph.x);
            PushFloat(glyph.y);
            words_.push_back(glyph.font_id != 0U ? glyph.font_id : font_id);
            words_.push_back(glyph.color);
        }
    }

    void SetGlyphRunStyled(
        std::uint64_t handle,
        std::uint32_t font_id,
        float font_size,
        const std::vector<GlyphPlacement>& glyphs) {
        words_.push_back(CMD_SET_GLYPH_RUN_STYLED);
        PushHandle(handle);
        words_.push_back(font_id);
        PushFloat(font_size);
        words_.push_back(static_cast<std::uint32_t>(glyphs.size()));
        for (const GlyphPlacement& glyph : glyphs) {
            words_.push_back(glyph.glyph_id);
            PushFloat(glyph.x);
            PushFloat(glyph.y);
            words_.push_back(glyph.font_id != 0U ? glyph.font_id : font_id);
            words_.push_back(glyph.color);
            PushFloat(glyph.font_size > 0.0f ? glyph.font_size : font_size);
        }
    }

    void SetTextFade(std::uint64_t handle, std::uint32_t edge) {
        words_.push_back(CMD_SET_TEXT_FADE);
        PushHandle(handle);
        words_.push_back(edge);
    }

    void SetCaret(
        std::uint64_t handle,
        float x,
        float y,
        float height,
        std::uint32_t color,
        std::uint32_t last_interaction_ms) {
        words_.push_back(CMD_SET_CARET);
        PushHandle(handle);
        PushFloat(x);
        PushFloat(y);
        PushFloat(height);
        words_.push_back(color);
        words_.push_back(last_interaction_ms);
    }

    void SetHighlights(std::uint64_t handle, std::uint32_t color, const std::vector<Rect>& rects) {
        words_.push_back(CMD_SET_HIGHLIGHTS);
        PushHandle(handle);
        words_.push_back(color);
        words_.push_back(static_cast<std::uint32_t>(rects.size()));
        for (const Rect& rect : rects) {
            PushFloat(rect.x);
            PushFloat(rect.y);
            PushFloat(rect.width);
            PushFloat(rect.height);
        }
    }

    void SetHighlightsColored(std::uint64_t handle, const std::vector<ColoredRect>& rects) {
        words_.push_back(CMD_SET_HIGHLIGHTS_COLORED);
        PushHandle(handle);
        words_.push_back(static_cast<std::uint32_t>(rects.size()));
        for (const ColoredRect& colored_rect : rects) {
            PushFloat(colored_rect.rect.x);
            PushFloat(colored_rect.rect.y);
            PushFloat(colored_rect.rect.width);
            PushFloat(colored_rect.rect.height);
            words_.push_back(colored_rect.color);
        }
    }

    void CommitPaintOrder(const std::vector<std::uint64_t>& paint_order) {
        words_.push_back(CMD_COMMIT_PAINT_ORDER);
        words_.push_back(static_cast<std::uint32_t>(paint_order.size()));
        for (const std::uint64_t handle : paint_order) {
            PushHandle(handle);
        }
    }

    void CommitScene(const std::vector<SceneInstruction>& scene) {
        words_.push_back(CMD_COMMIT_SCENE);
        words_.push_back(static_cast<std::uint32_t>(scene.size()));
        for (const SceneInstruction& instruction : scene) {
            words_.push_back(instruction.opcode);
            PushHandle(instruction.handle);
            PushFloat(instruction.arg0);
            PushFloat(instruction.arg1);
        }
    }

private:
    void PushHandle(std::uint64_t handle) {
        words_.push_back(static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL));
        words_.push_back(static_cast<std::uint32_t>(handle >> 32U));
    }

    void PushFloat(float value) {
        words_.push_back(FloatToWord(value));
    }

    std::vector<std::uint32_t>& words_;
};

} // namespace effindom::v2::ui
