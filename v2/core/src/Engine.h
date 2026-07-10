#pragma once

#include "effindom.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class SkCanvas;

namespace effindom::v2 {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool Contains(float px, float py) const;
};

struct Insets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct GradientStop {
    float offset = 0.0f;
    std::uint32_t color = 0;
};

struct PathVerbRecord {
    std::uint32_t verb = ED_PATH_MOVE_TO;
    std::array<float, 6> args{};
    std::uint32_t arg_count = 0;
};

struct GlyphPlacement {
    std::uint32_t glyph_id = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t font_id = 0;
    std::uint32_t color = 0;
    float font_size = 0.0f;
};

struct ColoredRect {
    Rect rect{};
    std::uint32_t color = 0;
};

struct NodeDebugView {
    bool alive = false;
    std::uint64_t handle = ED_INVALID_HANDLE;
    Rect visual_bounds{};
    Rect hit_bounds{};
    Rect clip_bounds{};
    bool interactive = false;
    std::uint32_t clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL;

    bool has_box_style = false;
    std::uint32_t bg_color = 0;
    std::array<float, 4> corner_radii{};

    bool has_border = false;
    float border_width = 0.0f;
    std::uint32_t border_color = 0;
    std::uint32_t border_style = ED_BORDER_SOLID;
    float border_dash_on = 0.0f;
    float border_dash_off = 0.0f;

    bool has_gradient = false;
    float gradient_start_x = 0.0f;
    float gradient_start_y = 0.0f;
    float gradient_end_x = 0.0f;
    float gradient_end_y = 0.0f;
    std::vector<GradientStop> gradient_stops{};

    bool has_layer_effect = false;
    float opacity = 1.0f;
    float blur_sigma = 0.0f;
    float background_blur_sigma = 0.0f;
    std::uint32_t drop_shadow_color = 0;
    float drop_shadow_offset_x = 0.0f;
    float drop_shadow_offset_y = 0.0f;
    float drop_shadow_blur_sigma = 0.0f;
    float drop_shadow_spread = 0.0f;
    std::uint32_t blend_mode = ED_BLEND_SRC_OVER;

    bool has_image = false;
    std::uint32_t texture_id = 0;
    std::uint32_t object_fit = ED_OBJECT_FIT_FILL;
    std::uint32_t image_sampling = ED_IMAGE_SAMPLING_LINEAR;
    std::uint32_t image_max_aniso = 0;
    bool has_image_nine = false;
    std::uint32_t image_nine_texture_id = 0;
    Insets image_nine_insets{};
    std::uint32_t image_nine_sampling = ED_IMAGE_SAMPLING_LINEAR;
    std::uint32_t image_nine_max_aniso = 0;
    bool has_svg = false;
    std::uint32_t svg_id = 0;
    std::uint32_t svg_tint_color = 0;
    std::uint32_t svg_sampling = ED_IMAGE_SAMPLING_LINEAR;
    std::uint32_t svg_max_aniso = 0;

    bool has_path = false;
    std::uint32_t path_fill_color = 0;
    std::uint32_t path_stroke_color = 0;
    float path_stroke_width = 0.0f;
    std::vector<PathVerbRecord> path{};

    bool has_glyph_run = false;
    bool glyphs_have_per_color = false;
    bool glyphs_have_per_style = false;
    std::uint32_t font_id = 0;
    float font_size = 16.0f;
    std::uint32_t glyph_color = 0;
    std::vector<GlyphPlacement> glyphs{};
    std::uint64_t glyph_blob_build_count = 0;
    bool glyph_blob_cached = false;
    std::uint64_t glyph_blob_last_used_generation = 0;
    std::size_t glyph_blob_estimated_bytes = 0;

    std::uint32_t fade_edge = ED_FADE_NONE;

    bool has_caret = false;
    float caret_x = 0.0f;
    float caret_y = 0.0f;
    float caret_height = 0.0f;
    std::uint32_t caret_color = 0;
    std::uint32_t caret_last_interaction_ms = 0;

    std::uint32_t highlight_color = 0;
    std::vector<Rect> highlights{};
    std::vector<ColoredRect> colored_highlights{};
};

struct SceneInstructionDebugView {
    std::uint32_t opcode = 0;
    std::uint64_t handle = ED_INVALID_HANDLE;
};

struct CommandBufferStats {
    std::uint32_t parsed_commands = 0;
    std::uint32_t ignored_commands = 0;
    std::uint32_t truncated_buffers = 0;
    std::uint32_t unknown_commands = 0;
};

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void Init(std::uint32_t physical_width, std::uint32_t physical_height, float dpr);
    void Resize(std::uint32_t physical_width, std::uint32_t physical_height, float dpr);
    void SetViewportSize(float logical_width, float logical_height);
    void SetViewportTransform(float scale, float offset_x, float offset_y);
    float ViewportScale() const;
    float ViewportOffsetX() const;
    float ViewportOffsetY() const;
    void SetViewportZoomFromSceneAnchor(float scale, float anchor_scene_x, float anchor_scene_y, float screen_x, float screen_y);
    void PanViewportBy(float delta_x, float delta_y);
    void BeginViewportPan(double timestamp_ms);
    void UpdateViewportPan(float delta_x, float delta_y, double timestamp_ms);
    void EndViewportPan(double timestamp_ms);
    bool TickViewportPanMomentum(double timestamp_ms);
    void ClearViewportPanMomentum();
    void RegisterFont(std::uint32_t font_id, const std::uint8_t* bytes, std::uint32_t length);
    void UnregisterFont(std::uint32_t font_id);
    void RegisterSvg(std::uint32_t svg_id, const std::uint8_t* bytes, std::uint32_t length);
    void RegisterTextureRgba(
        std::uint32_t texture_id,
        const std::uint8_t* rgba,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t byte_length);
    void RegisterTextureSubRgba(
        std::uint32_t texture_id,
        const std::uint8_t* sub_rgba,
        std::uint32_t sub_x,
        std::uint32_t sub_y,
        std::uint32_t sub_w,
        std::uint32_t sub_h,
        std::uint32_t full_w,
        std::uint32_t full_h);
    void UnregisterTexture(std::uint32_t texture_id);
    CommandBufferStats ExecuteCommandBuffer(const std::uint32_t* buffer, std::uint32_t length);
    std::uint64_t HitTest(float logical_x, float logical_y) const;
    void RenderToCanvas(SkCanvas* canvas, double current_time_ms = 0.0) const;

    /* Immediate-mode path management. */
    std::uint32_t CreatePath();
    void DestroyPath(std::uint32_t path_id);
    void PathMoveTo(std::uint32_t path_id, float x, float y);
    void PathLineTo(std::uint32_t path_id, float x, float y);
    void PathQuadTo(std::uint32_t path_id, float cx, float cy, float x, float y);
    void PathCubicTo(std::uint32_t path_id, float cx1, float cy1, float cx2, float cy2, float x, float y);
    void PathClose(std::uint32_t path_id);
    void PathAddRect(std::uint32_t path_id, float x, float y, float w, float h);
    void PathAddCircle(std::uint32_t path_id, float cx, float cy, float r);

    /* Stateful canvas drawing (accesses engine resources like textures, fonts, paths). */
    void CanvasDrawPath(SkCanvas* canvas, std::uint32_t path_id,
                        std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width) const;
    void CanvasDrawTextNode(SkCanvas* canvas, std::uint64_t handle, float x, float y) const;
    void CanvasDrawImage(SkCanvas* canvas, std::uint32_t texture_id,
                         float x, float y, float w, float h,
                         std::uint32_t sampling_kind = ED_IMAGE_SAMPLING_LINEAR,
                         std::uint32_t max_aniso = 0) const;
    void CanvasDrawSvg(SkCanvas* canvas, std::uint32_t svg_id,
                       float x, float y, float w, float h) const;
    void CanvasDrawBatch(SkCanvas* canvas, const std::uint32_t* words, std::uint32_t word_count) const;

    /* Offscreen raster surfaces. */
    std::uint32_t CreateOffscreenSurface(std::uint32_t width, std::uint32_t height);
    void* GetOffscreenCanvas(std::uint32_t offscreen_id) const;
    void ReadOffscreenPixels(std::uint32_t offscreen_id, std::uint8_t* out_rgba) const;
    void DestroyOffscreenSurface(std::uint32_t offscreen_id);

    /* Render a retained DisplayNode into an RGBA pixel buffer. */
    std::uint32_t RenderNodeToRgba(std::uint64_t handle, std::uint32_t width, std::uint32_t height,
                                   std::uint8_t* out_pixels, std::uint32_t out_capacity,
                                   float scale, float x, float y);

    std::optional<NodeDebugView> GetNodeForTesting(std::uint64_t handle) const;
    std::vector<SceneInstructionDebugView> GetSceneInstructionsForTesting() const;
    std::vector<std::uint64_t> GetPaintOrderForTesting() const;

    std::uint32_t physical_width() const;
    std::uint32_t physical_height() const;
    float dpr() const;

private:
    struct Impl;
    void ClampViewportTransform();
    bool ApplyViewportPan(float delta_x, float delta_y);
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2
