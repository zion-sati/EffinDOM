#pragma once

#include "Engine.h"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <include/core/SkImage.h>
#include <include/core/SkPath.h>
#include <include/core/SkPicture.h>
#include <include/core/SkRRect.h>
#include <include/core/SkSamplingOptions.h>
#include <include/core/SkSurface.h>
#include <include/core/SkTextBlob.h>
#include <include/core/SkTypeface.h>
#include <modules/svg/include/SkSVGDOM.h>

class SkCanvas;
class SkFont;

/* EM_JS callback — defined in Wasm.cpp for wasm builds, stubbed for native.
   Must use C linkage to match the EM_JS-generated symbol. */
#ifdef __cplusplus
extern "C" {
#endif

void effindom_v2_custom_draw(std::uint32_t handle_lo, std::uint32_t handle_hi, std::uintptr_t canvas_ptr);

#ifdef __cplusplus
}
#endif

#ifndef __EMSCRIPTEN__
inline void effindom_v2_custom_draw(std::uint32_t, std::uint32_t, std::uintptr_t) {}
#endif

namespace effindom::v2::detail {

constexpr std::uint32_t kMaxNodes = 65536;
constexpr std::uint32_t kInvalidGlyphBlobCacheIndex = kMaxNodes;
constexpr std::uint64_t kGlyphBlobMaxUnusedGenerations = 120U;
constexpr std::size_t kGlyphBlobBudgetBytes = 512U * 1024U;

struct HandleParts {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
};

struct SceneInstruction {
    std::uint32_t opcode = 0;
    std::uint64_t handle = ED_INVALID_HANDLE;
    float arg0 = 0.0f;
    float arg1 = 0.0f;
};

struct SvgRecord {
    sk_sp<SkPicture> picture;
    float intrinsic_width = 1.0f;
    float intrinsic_height = 1.0f;
    struct RasterVariant {
        std::uint32_t pixel_width = 0;
        std::uint32_t pixel_height = 0;
        std::uint32_t tint_color = 0;
        std::uint64_t last_used_generation = 0;
        sk_sp<SkImage> image;
    };
    std::vector<RasterVariant> raster_variants{};
};

struct TextureRecord {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels{};
    sk_sp<SkImage> raster_image;
};

struct DisplayNode {
    bool alive = false;
    std::uint32_t generation = 0;
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
    mutable sk_sp<SkTextBlob> cached_glyph_blob{};
    std::uint64_t glyph_blob_version = 0;
    mutable std::uint64_t cached_glyph_blob_version = 0;
    mutable std::uint64_t glyph_blob_build_count = 0;
    mutable std::uint64_t glyph_blob_last_used_generation = 0;
    std::size_t glyph_blob_estimated_bytes = 0;
    std::uint32_t glyph_blob_prev_index = kInvalidGlyphBlobCacheIndex;
    std::uint32_t glyph_blob_next_index = kInvalidGlyphBlobCacheIndex;
    bool glyph_blob_in_lru = false;

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

    void ResetForCreate(std::uint32_t next_generation);
    void ResetForDelete();
};

HandleParts DecodeHandle(std::uint64_t handle);
std::uint64_t DecodeHandleWords(std::uint32_t low, std::uint32_t high);
float ReadFloat(std::uint32_t word);
float ClampNonNegative(float value);
float ClampOpacity(float value);
std::uint32_t VerbArgCount(std::uint32_t verb);
bool IsValidImageSampling(std::uint32_t sampling);
std::uint32_t NormalizeImageMaxAniso(std::uint32_t max_aniso);
SkSamplingOptions MakeImageSamplingOptions(std::uint32_t sampling, std::uint32_t max_aniso);

} // namespace effindom::v2::detail

namespace effindom::v2 {

struct Engine::Impl {
    std::uint32_t physical_width = 0;
    std::uint32_t physical_height = 0;
    float dpr = 1.0f;
    float viewport_width = 320.0f;
    float viewport_height = 220.0f;
    float viewport_scale = 1.0f;
    float viewport_offset_x = 0.0f;
    float viewport_offset_y = 0.0f;
    float viewport_pan_velocity_x = 0.0f;
    float viewport_pan_velocity_y = 0.0f;
    bool viewport_pan_active = false;
    bool viewport_pan_dragged = false;
    bool viewport_pan_momentum_active = false;
    double last_viewport_pan_timestamp_ms = 0.0;
    bool has_last_viewport_pan_timestamp = false;
    std::vector<detail::DisplayNode> nodes = std::vector<detail::DisplayNode>(detail::kMaxNodes);
    std::vector<detail::SceneInstruction> scene_instructions{};
    std::vector<std::uint64_t> paint_order{};
    std::uint64_t render_generation = 0;
    std::size_t cached_glyph_blob_bytes = 0;
    std::uint32_t glyph_blob_lru_head = detail::kInvalidGlyphBlobCacheIndex;
    std::uint32_t glyph_blob_lru_tail = detail::kInvalidGlyphBlobCacheIndex;
    mutable GlyphRenderStats glyph_render_stats{};
    std::unordered_map<std::uint32_t, sk_sp<SkTypeface>> fonts{};
    std::unordered_map<std::uint32_t, detail::SvgRecord> svgs{};
    std::unordered_map<std::uint32_t, detail::TextureRecord> textures{};
    std::unordered_map<std::uint32_t, SkPath> paths{};

    struct OffscreenSurface {
        sk_sp<SkSurface> surface;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    std::unordered_map<std::uint32_t, OffscreenSurface> offscreen_surfaces{};
    std::uint32_t next_path_id = 1;
    std::uint32_t next_offscreen_id = 1;

    detail::DisplayNode* ResolveMutable(std::uint64_t handle);
    const detail::DisplayNode* Resolve(std::uint64_t handle) const;
    bool CreateNode(std::uint64_t handle);
    bool DeleteNode(std::uint64_t handle);
    void ReleaseGlyphBlobCache(detail::DisplayNode& node);
    void StoreGlyphBlobCache(detail::DisplayNode& node, sk_sp<SkTextBlob> blob);
    void TouchGlyphBlobCache(detail::DisplayNode& node);
    void EvictGlyphBlobCaches();
    void DrawTextContent(
        SkCanvas* canvas,
        const detail::DisplayNode& node,
        float origin_x,
        float origin_y,
        bool use_glyph_blob_cache,
        bool apply_fade);
    void DrawNode(SkCanvas* canvas, const detail::DisplayNode& node, double current_time_ms);
};

} // namespace effindom::v2

/* Standalone canvas drawing functions (no engine state needed). */
namespace effindom::v2 {

void EdCanvasSave(SkCanvas* canvas);
void EdCanvasRestore(SkCanvas* canvas);
void EdCanvasTranslate(SkCanvas* canvas, float x, float y);
void EdCanvasScale(SkCanvas* canvas, float sx, float sy);
void EdCanvasRotate(SkCanvas* canvas, float degrees);
void EdCanvasClipRect(SkCanvas* canvas, float x, float y, float w, float h);
void EdCanvasClipRoundRect(SkCanvas* canvas, float x, float y, float w, float h,
                           float top_left, float top_right, float bottom_right, float bottom_left);

void EdCanvasDrawRect(SkCanvas* canvas, float x, float y, float w, float h,
                      std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width);
void EdCanvasDrawCircle(SkCanvas* canvas, float cx, float cy, float radius,
                        std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width);
void EdCanvasDrawLine(SkCanvas* canvas, float x1, float y1, float x2, float y2,
                      std::uint32_t color, float stroke_width);
void EdCanvasDrawRoundRect(SkCanvas* canvas, float x, float y, float w, float h,
                           float rx, float ry,
                           std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width);

} // namespace effindom::v2
