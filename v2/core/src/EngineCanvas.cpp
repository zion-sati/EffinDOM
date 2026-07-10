#include "EngineInternal.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <include/core/SkBitmap.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkImage.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPath.h>
#include <include/core/SkRRect.h>
#include <include/core/SkSurface.h>
#include <modules/svg/include/SkSVGDOM.h>

namespace effindom::v2 {

/* ── Color conversion ──────────────────────────────────────────── */

namespace {

enum CanvasBatchOp : std::uint32_t {
    CANVAS_BATCH_SAVE = 1,
    CANVAS_BATCH_RESTORE = 2,
    CANVAS_BATCH_TRANSLATE = 3,
    CANVAS_BATCH_SCALE = 4,
    CANVAS_BATCH_ROTATE = 5,
    CANVAS_BATCH_CLIP_RECT = 6,
    CANVAS_BATCH_CLIP_ROUND_RECT = 7,
    CANVAS_BATCH_DRAW_RECT = 10,
    CANVAS_BATCH_DRAW_CIRCLE = 11,
    CANVAS_BATCH_DRAW_LINE = 12,
    CANVAS_BATCH_DRAW_ROUND_RECT = 13,
    CANVAS_BATCH_DRAW_PATH = 20,
    CANVAS_BATCH_DRAW_TEXT_NODE = 30,
    CANVAS_BATCH_DRAW_IMAGE = 31,
    CANVAS_BATCH_DRAW_SVG = 32,
};

float WordToFloat(std::uint32_t word) {
    float value = 0.0f;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

std::uint64_t WordsToHandle(std::uint32_t lo, std::uint32_t hi) {
    return (static_cast<std::uint64_t>(hi) << 32U) | static_cast<std::uint64_t>(lo);
}

SkColor ToCanvasColor(std::uint32_t rgba) {
    // 0xRRGGBBAA → SkColor (ARGB)
    return SkColorSetARGB(
        static_cast<U8CPU>(rgba & 0xffU),
        static_cast<U8CPU>((rgba >> 24) & 0xffU),
        static_cast<U8CPU>((rgba >> 16) & 0xffU),
        static_cast<U8CPU>((rgba >> 8) & 0xffU));
}

bool HasFillAlpha(std::uint32_t color) {
    return (color & 0xffU) != 0;
}

bool HasStroke(std::uint32_t color, float width) {
    return width > 0.0f && (color & 0xffU) != 0;
}

} // namespace

/* ── Standalone canvas state ───────────────────────────────────── */

void EdCanvasSave(SkCanvas* canvas) {
    if (canvas) canvas->save();
}

void EdCanvasRestore(SkCanvas* canvas) {
    if (canvas) canvas->restore();
}

void EdCanvasTranslate(SkCanvas* canvas, float x, float y) {
    if (canvas) canvas->translate(x, y);
}

void EdCanvasScale(SkCanvas* canvas, float sx, float sy) {
    if (canvas) canvas->scale(sx, sy);
}

void EdCanvasRotate(SkCanvas* canvas, float degrees) {
    if (canvas) canvas->rotate(degrees);
}

void EdCanvasClipRect(SkCanvas* canvas, float x, float y, float w, float h) {
    if (canvas) canvas->clipRect(SkRect::MakeXYWH(x, y, w, h), SkClipOp::kIntersect, true);
}

void EdCanvasClipRoundRect(SkCanvas* canvas, float x, float y, float w, float h,
                           float top_left, float top_right, float bottom_right, float bottom_left) {
    if (!canvas) return;
    const SkRect rect = SkRect::MakeXYWH(x, y, w, h);
    const SkVector radii[4] = {
        SkVector::Make(std::max(0.0f, top_left), std::max(0.0f, top_left)),
        SkVector::Make(std::max(0.0f, top_right), std::max(0.0f, top_right)),
        SkVector::Make(std::max(0.0f, bottom_right), std::max(0.0f, bottom_right)),
        SkVector::Make(std::max(0.0f, bottom_left), std::max(0.0f, bottom_left)),
    };
    SkRRect rounded;
    rounded.setRectRadii(rect, radii);
    if (rounded.isRect()) {
        canvas->clipRect(rect, SkClipOp::kIntersect, true);
    } else {
        canvas->clipRRect(rounded, true);
    }
}

/* ── Standalone drawing primitives ────────────────────────────── */

namespace {

void ConfigureFillPaint(SkPaint& paint, std::uint32_t color) {
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(ToCanvasColor(color));
    paint.setAntiAlias(true);
}

void ConfigureStrokePaint(SkPaint& paint, std::uint32_t color, float width) {
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setColor(ToCanvasColor(color));
    paint.setStrokeWidth(width);
    paint.setAntiAlias(true);
}

} // namespace

void EdCanvasDrawRect(SkCanvas* canvas, float x, float y, float w, float h,
                      std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width) {
    if (!canvas) return;
    const SkRect rect = SkRect::MakeXYWH(x, y, w, h);

    if (HasFillAlpha(fill_color)) {
        SkPaint paint;
        ConfigureFillPaint(paint, fill_color);
        canvas->drawRect(rect, paint);
    }

    if (HasStroke(stroke_color, stroke_width)) {
        SkPaint paint;
        ConfigureStrokePaint(paint, stroke_color, stroke_width);
        canvas->drawRect(rect, paint);
    }
}

void EdCanvasDrawCircle(SkCanvas* canvas, float cx, float cy, float radius,
                        std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width) {
    if (!canvas) return;

    if (HasFillAlpha(fill_color)) {
        SkPaint paint;
        ConfigureFillPaint(paint, fill_color);
        canvas->drawCircle(cx, cy, radius, paint);
    }

    if (HasStroke(stroke_color, stroke_width)) {
        SkPaint paint;
        ConfigureStrokePaint(paint, stroke_color, stroke_width);
        canvas->drawCircle(cx, cy, radius, paint);
    }
}

void EdCanvasDrawLine(SkCanvas* canvas, float x1, float y1, float x2, float y2,
                      std::uint32_t color, float stroke_width) {
    if (!canvas || !HasStroke(color, stroke_width)) return;

    SkPaint paint;
    ConfigureStrokePaint(paint, color, stroke_width);
    canvas->drawLine(x1, y1, x2, y2, paint);
}

void EdCanvasDrawRoundRect(SkCanvas* canvas, float x, float y, float w, float h,
                           float rx, float ry,
                           std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width) {
    if (!canvas) return;

    SkRRect rrect;
    rrect.setRectXY(SkRect::MakeXYWH(x, y, w, h), rx, ry);

    if (HasFillAlpha(fill_color)) {
        SkPaint paint;
        ConfigureFillPaint(paint, fill_color);
        canvas->drawRRect(rrect, paint);
    }

    if (HasStroke(stroke_color, stroke_width)) {
        SkPaint paint;
        ConfigureStrokePaint(paint, stroke_color, stroke_width);
        canvas->drawRRect(rrect, paint);
    }
}

/* ── Path management ───────────────────────────────────────────── */

std::uint32_t Engine::CreatePath() {
    const std::uint32_t id = impl_->next_path_id++;
    impl_->paths.emplace(id, SkPath{});
    return id;
}

void Engine::DestroyPath(std::uint32_t path_id) {
    impl_->paths.erase(path_id);
}

void Engine::PathMoveTo(std::uint32_t path_id, float x, float y) {
    auto it = impl_->paths.find(path_id);
    if (it != impl_->paths.end()) it->second.moveTo(x, y);
}

void Engine::PathLineTo(std::uint32_t path_id, float x, float y) {
    auto it = impl_->paths.find(path_id);
    if (it != impl_->paths.end()) it->second.lineTo(x, y);
}

void Engine::PathQuadTo(std::uint32_t path_id, float cx, float cy, float x, float y) {
    auto it = impl_->paths.find(path_id);
    if (it != impl_->paths.end()) it->second.quadTo(cx, cy, x, y);
}

void Engine::PathCubicTo(std::uint32_t path_id, float cx1, float cy1, float cx2, float cy2, float x, float y) {
    auto it = impl_->paths.find(path_id);
    if (it != impl_->paths.end()) it->second.cubicTo(cx1, cy1, cx2, cy2, x, y);
}

void Engine::PathClose(std::uint32_t path_id) {
    auto it = impl_->paths.find(path_id);
    if (it != impl_->paths.end()) it->second.close();
}

void Engine::PathAddRect(std::uint32_t path_id, float x, float y, float w, float h) {
    auto it = impl_->paths.find(path_id);
    if (it != impl_->paths.end()) it->second.addRect(SkRect::MakeXYWH(x, y, w, h));
}

void Engine::PathAddCircle(std::uint32_t path_id, float cx, float cy, float r) {
    auto it = impl_->paths.find(path_id);
    if (it != impl_->paths.end()) it->second.addCircle(cx, cy, r);
}

/* ── Stateful canvas drawing ───────────────────────────────────── */

void Engine::CanvasDrawPath(SkCanvas* canvas, std::uint32_t path_id,
                            std::uint32_t fill_color, std::uint32_t stroke_color, float stroke_width) const {
    if (!canvas) return;
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return;
    const SkPath& path = it->second;

    if (HasFillAlpha(fill_color)) {
        SkPaint paint;
        ConfigureFillPaint(paint, fill_color);
        canvas->drawPath(path, paint);
    }

    if (HasStroke(stroke_color, stroke_width)) {
        SkPaint paint;
        ConfigureStrokePaint(paint, stroke_color, stroke_width);
        canvas->drawPath(path, paint);
    }
}

void Engine::CanvasDrawTextNode(SkCanvas* canvas, std::uint64_t handle, float x, float y) const {
    if (!canvas || handle == ED_INVALID_HANDLE) return;
    const detail::DisplayNode* node = impl_->Resolve(handle);
    if (!node) return;
    impl_->DrawTextContent(canvas, *node, x, y, false, false);
}

void Engine::CanvasDrawImage(SkCanvas* canvas, std::uint32_t texture_id,
                             float x, float y, float w, float h,
                             std::uint32_t sampling_kind,
                             std::uint32_t max_aniso) const {
    if (!canvas) return;

    auto texture_it = impl_->textures.find(texture_id);
    if (texture_it == impl_->textures.end() || !texture_it->second.raster_image) return;

    const SkRect dst = SkRect::MakeXYWH(x, y, w, h);
    canvas->drawImageRect(
        texture_it->second.raster_image,
        dst,
        detail::MakeImageSamplingOptions(sampling_kind, max_aniso));
}

void Engine::CanvasDrawSvg(SkCanvas* canvas, std::uint32_t svg_id,
                           float x, float y, float w, float h) const {
    if (!canvas) return;

    auto svg_it = impl_->svgs.find(svg_id);
    if (svg_it == impl_->svgs.end() || !svg_it->second.picture) return;

    const SkRect src = SkRect::MakeWH(
        svg_it->second.intrinsic_width,
        svg_it->second.intrinsic_height);
    const SkRect dst = SkRect::MakeXYWH(x, y, w, h);

    canvas->save();
    canvas->translate(dst.left(), dst.top());
    canvas->scale(dst.width() / src.width(), dst.height() / src.height());
    svg_it->second.picture->playback(canvas);
    canvas->restore();
}

void Engine::CanvasDrawBatch(SkCanvas* canvas, const std::uint32_t* words, std::uint32_t word_count) const {
    if (!canvas || words == nullptr || word_count == 0U) return;

    std::uint32_t i = 0U;
    auto has = [&](std::uint32_t count) {
        return count <= word_count && i <= word_count - count;
    };
    auto next_float = [&]() {
        return WordToFloat(words[i++]);
    };

    while (i < word_count) {
        const std::uint32_t op = words[i++];
        switch (op) {
        case CANVAS_BATCH_SAVE:
            EdCanvasSave(canvas);
            break;
        case CANVAS_BATCH_RESTORE:
            EdCanvasRestore(canvas);
            break;
        case CANVAS_BATCH_TRANSLATE:
            if (!has(2U)) return;
            {
                const float x = next_float();
                const float y = next_float();
                EdCanvasTranslate(canvas, x, y);
            }
            break;
        case CANVAS_BATCH_SCALE:
            if (!has(2U)) return;
            {
                const float sx = next_float();
                const float sy = next_float();
                EdCanvasScale(canvas, sx, sy);
            }
            break;
        case CANVAS_BATCH_ROTATE:
            if (!has(1U)) return;
            {
                const float degrees = next_float();
                EdCanvasRotate(canvas, degrees);
            }
            break;
        case CANVAS_BATCH_CLIP_RECT:
            if (!has(4U)) return;
            {
                const float x = next_float();
                const float y = next_float();
                const float w = next_float();
                const float h = next_float();
                EdCanvasClipRect(canvas, x, y, w, h);
            }
            break;
        case CANVAS_BATCH_CLIP_ROUND_RECT:
            if (!has(8U)) return;
            {
                const float x = next_float();
                const float y = next_float();
                const float w = next_float();
                const float h = next_float();
                const float top_left = next_float();
                const float top_right = next_float();
                const float bottom_right = next_float();
                const float bottom_left = next_float();
                EdCanvasClipRoundRect(canvas, x, y, w, h, top_left, top_right, bottom_right, bottom_left);
            }
            break;
        case CANVAS_BATCH_DRAW_RECT:
            if (!has(7U)) return;
            {
                const float x = next_float();
                const float y = next_float();
                const float w = next_float();
                const float h = next_float();
                const std::uint32_t fill = words[i++];
                const std::uint32_t stroke = words[i++];
                const float stroke_width = next_float();
                EdCanvasDrawRect(canvas, x, y, w, h, fill, stroke, stroke_width);
            }
            break;
        case CANVAS_BATCH_DRAW_CIRCLE:
            if (!has(6U)) return;
            {
                const float cx = next_float();
                const float cy = next_float();
                const float radius = next_float();
                const std::uint32_t fill = words[i++];
                const std::uint32_t stroke = words[i++];
                const float stroke_width = next_float();
                EdCanvasDrawCircle(canvas, cx, cy, radius, fill, stroke, stroke_width);
            }
            break;
        case CANVAS_BATCH_DRAW_LINE:
            if (!has(6U)) return;
            {
                const float x1 = next_float();
                const float y1 = next_float();
                const float x2 = next_float();
                const float y2 = next_float();
                const std::uint32_t color = words[i++];
                const float stroke_width = next_float();
                EdCanvasDrawLine(canvas, x1, y1, x2, y2, color, stroke_width);
            }
            break;
        case CANVAS_BATCH_DRAW_ROUND_RECT:
            if (!has(9U)) return;
            {
                const float x = next_float();
                const float y = next_float();
                const float w = next_float();
                const float h = next_float();
                const float rx = next_float();
                const float ry = next_float();
                const std::uint32_t fill = words[i++];
                const std::uint32_t stroke = words[i++];
                const float stroke_width = next_float();
                EdCanvasDrawRoundRect(canvas, x, y, w, h, rx, ry, fill, stroke, stroke_width);
            }
            break;
        case CANVAS_BATCH_DRAW_PATH:
            if (!has(4U)) return;
            {
                const std::uint32_t path_id = words[i++];
                const std::uint32_t fill = words[i++];
                const std::uint32_t stroke = words[i++];
                const float stroke_width = next_float();
                CanvasDrawPath(canvas, path_id, fill, stroke, stroke_width);
            }
            break;
        case CANVAS_BATCH_DRAW_TEXT_NODE:
            if (!has(4U)) return;
            {
                const std::uint64_t handle = WordsToHandle(words[i], words[i + 1U]);
                i += 2U;
                const float x = next_float();
                const float y = next_float();
                CanvasDrawTextNode(canvas, handle, x, y);
            }
            break;
        case CANVAS_BATCH_DRAW_IMAGE:
            if (!has(7U)) return;
            {
                const std::uint32_t texture_id = words[i++];
                const float x = next_float();
                const float y = next_float();
                const float w = next_float();
                const float h = next_float();
                const std::uint32_t sampling_kind = words[i++];
                const std::uint32_t max_aniso = words[i++];
                CanvasDrawImage(canvas, texture_id, x, y, w, h, sampling_kind, max_aniso);
            }
            break;
        case CANVAS_BATCH_DRAW_SVG:
            if (!has(5U)) return;
            {
                const std::uint32_t svg_id = words[i++];
                const float x = next_float();
                const float y = next_float();
                const float w = next_float();
                const float h = next_float();
                CanvasDrawSvg(canvas, svg_id, x, y, w, h);
            }
            break;
        default:
            return;
        }
    }
}

/* ── Offscreen surfaces ────────────────────────────────────────── */

std::uint32_t Engine::CreateOffscreenSurface(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) return 0;

    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(width), static_cast<int>(height),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);

    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return 0;

    const std::uint32_t id = impl_->next_offscreen_id++;
    impl_->offscreen_surfaces.emplace(id, Engine::Impl::OffscreenSurface{
        std::move(surface), width, height
    });
    return id;
}

void* Engine::GetOffscreenCanvas(std::uint32_t offscreen_id) const {
    auto it = impl_->offscreen_surfaces.find(offscreen_id);
    if (it == impl_->offscreen_surfaces.end()) return nullptr;
    return static_cast<void*>(it->second.surface->getCanvas());
}

void Engine::ReadOffscreenPixels(std::uint32_t offscreen_id, std::uint8_t* out_rgba) const {
    if (!out_rgba) return;
    auto it = impl_->offscreen_surfaces.find(offscreen_id);
    if (it == impl_->offscreen_surfaces.end()) return;

    const auto& surface = it->second.surface;
    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(it->second.width), static_cast<int>(it->second.height),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);

    surface->readPixels(info, out_rgba, it->second.width * 4U, 0, 0);
}

void Engine::DestroyOffscreenSurface(std::uint32_t offscreen_id) {
    impl_->offscreen_surfaces.erase(offscreen_id);
}

std::uint32_t Engine::RenderNodeToRgba(std::uint64_t handle, std::uint32_t width, std::uint32_t height,
                                       std::uint8_t* out_pixels, std::uint32_t out_capacity,
                                       float scale, float x, float y) {
    if (width == 0 || height == 0 || out_pixels == nullptr) return 0;

    const detail::DisplayNode* node = impl_->Resolve(handle);
    if (node == nullptr || !node->alive) return 0;
    if (!node->has_glyph_run && node->glyphs.empty()) return 0;

    const std::uint32_t byte_count = width * height * 4U;
    if (out_capacity < byte_count) return 0;

    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(width), static_cast<int>(height),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return 0;

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    canvas->save();
    const float effective_scale = scale > 0.0f ? scale : 1.0f;
    canvas->scale(effective_scale, effective_scale);
    canvas->translate(x - node->visual_bounds.x, y - node->visual_bounds.y);
    impl_->DrawNode(canvas, *node, 0.0);
    canvas->restore();

    // Skia readPixels via SkBitmap (void* overload is broken in wasm64)
    SkBitmap bitmap;
    bitmap.allocPixels(info);
    surface->readPixels(bitmap, 0, 0);
    void* pixelAddr = bitmap.getPixels();
    if (pixelAddr) {
        std::memcpy(out_pixels, pixelAddr, byte_count);
    }
    return pixelAddr ? byte_count : 0;
}

} // namespace effindom::v2
