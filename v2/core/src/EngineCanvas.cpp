#include "EngineInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>
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

std::optional<std::uint32_t> RgbaByteCount(std::uint32_t width, std::uint32_t height) {
    if (width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const std::uint64_t byte_count =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (byte_count > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(byte_count);
}

std::uint32_t CanvasBatchArgumentCount(std::uint32_t op) {
    switch (op) {
    case CANVAS_BATCH_SAVE:
    case CANVAS_BATCH_RESTORE:
        return 0U;
    case CANVAS_BATCH_ROTATE:
        return 1U;
    case CANVAS_BATCH_TRANSLATE:
    case CANVAS_BATCH_SCALE:
        return 2U;
    case CANVAS_BATCH_CLIP_RECT:
    case CANVAS_BATCH_DRAW_PATH:
    case CANVAS_BATCH_DRAW_TEXT_NODE:
        return 4U;
    case CANVAS_BATCH_DRAW_SVG:
        return 5U;
    case CANVAS_BATCH_DRAW_CIRCLE:
    case CANVAS_BATCH_DRAW_LINE:
        return 6U;
    case CANVAS_BATCH_DRAW_RECT:
    case CANVAS_BATCH_DRAW_IMAGE:
        return 7U;
    case CANVAS_BATCH_CLIP_ROUND_RECT:
        return 8U;
    case CANVAS_BATCH_DRAW_ROUND_RECT:
        return 9U;
    default:
        return UINT32_MAX;
    }
}

bool ValidateCanvasBatch(const std::uint32_t* words, std::uint32_t word_count) {
    std::uint32_t index = 0U;
    while (index < word_count) {
        const std::uint32_t argument_count = CanvasBatchArgumentCount(words[index++]);
        if (argument_count == UINT32_MAX ||
            argument_count > word_count ||
            index > word_count - argument_count) {
            return false;
        }
        index += argument_count;
    }
    return true;
}

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
    if (impl_->paths.size() >= static_cast<std::size_t>(UINT32_MAX - 1U)) return 0U;
    for (std::size_t attempt = 0U; attempt <= impl_->paths.size(); ++attempt) {
        const std::uint32_t id = impl_->next_path_id++;
        if (id == 0U) continue;
        if (impl_->paths.emplace(id, SkPath{}).second) return id;
    }
    return 0U;
}

bool Engine::DestroyPath(std::uint32_t path_id) {
    return path_id != 0U && impl_->paths.erase(path_id) != 0U;
}

bool Engine::PathMoveTo(std::uint32_t path_id, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return false;
    it->second.moveTo(x, y);
    return true;
}

bool Engine::PathLineTo(std::uint32_t path_id, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return false;
    it->second.lineTo(x, y);
    return true;
}

bool Engine::PathQuadTo(std::uint32_t path_id, float cx, float cy, float x, float y) {
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(x) || !std::isfinite(y)) return false;
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return false;
    it->second.quadTo(cx, cy, x, y);
    return true;
}

bool Engine::PathCubicTo(std::uint32_t path_id, float cx1, float cy1, float cx2, float cy2, float x, float y) {
    if (!std::isfinite(cx1) || !std::isfinite(cy1) ||
        !std::isfinite(cx2) || !std::isfinite(cy2) ||
        !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return false;
    it->second.cubicTo(cx1, cy1, cx2, cy2, x, y);
    return true;
}

bool Engine::PathClose(std::uint32_t path_id) {
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return false;
    it->second.close();
    return true;
}

bool Engine::PathAddRect(std::uint32_t path_id, float x, float y, float w, float h) {
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(w) || !std::isfinite(h) || w <= 0.0f || h <= 0.0f) {
        return false;
    }
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return false;
    it->second.addRect(SkRect::MakeXYWH(x, y, w, h));
    return true;
}

bool Engine::PathAddCircle(std::uint32_t path_id, float cx, float cy, float r) {
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r) || r <= 0.0f) return false;
    auto it = impl_->paths.find(path_id);
    if (it == impl_->paths.end()) return false;
    it->second.addCircle(cx, cy, r);
    return true;
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

bool Engine::CanvasDrawBatch(SkCanvas* canvas, const std::uint32_t* words, std::uint32_t word_count) const {
    if (word_count == 0U) return true;
    if (!canvas || words == nullptr || !ValidateCanvasBatch(words, word_count)) return false;

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
            if (!has(2U)) return false;
            {
                const float x = next_float();
                const float y = next_float();
                EdCanvasTranslate(canvas, x, y);
            }
            break;
        case CANVAS_BATCH_SCALE:
            if (!has(2U)) return false;
            {
                const float sx = next_float();
                const float sy = next_float();
                EdCanvasScale(canvas, sx, sy);
            }
            break;
        case CANVAS_BATCH_ROTATE:
            if (!has(1U)) return false;
            {
                const float degrees = next_float();
                EdCanvasRotate(canvas, degrees);
            }
            break;
        case CANVAS_BATCH_CLIP_RECT:
            if (!has(4U)) return false;
            {
                const float x = next_float();
                const float y = next_float();
                const float w = next_float();
                const float h = next_float();
                EdCanvasClipRect(canvas, x, y, w, h);
            }
            break;
        case CANVAS_BATCH_CLIP_ROUND_RECT:
            if (!has(8U)) return false;
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
            if (!has(7U)) return false;
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
            if (!has(6U)) return false;
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
            if (!has(6U)) return false;
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
            if (!has(9U)) return false;
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
            if (!has(4U)) return false;
            {
                const std::uint32_t path_id = words[i++];
                const std::uint32_t fill = words[i++];
                const std::uint32_t stroke = words[i++];
                const float stroke_width = next_float();
                CanvasDrawPath(canvas, path_id, fill, stroke, stroke_width);
            }
            break;
        case CANVAS_BATCH_DRAW_TEXT_NODE:
            if (!has(4U)) return false;
            {
                const std::uint64_t handle = WordsToHandle(words[i], words[i + 1U]);
                i += 2U;
                const float x = next_float();
                const float y = next_float();
                CanvasDrawTextNode(canvas, handle, x, y);
            }
            break;
        case CANVAS_BATCH_DRAW_IMAGE:
            if (!has(7U)) return false;
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
            if (!has(5U)) return false;
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
            return false;
        }
    }
    return true;
}

/* ── Offscreen surfaces ────────────────────────────────────────── */

std::uint32_t Engine::CreateOffscreenSurface(std::uint32_t width, std::uint32_t height) {
    if (!RgbaByteCount(width, height).has_value()) return 0U;

    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(width), static_cast<int>(height),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);

    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return 0;

    std::uint32_t id = impl_->next_offscreen_id == 0U ? 1U : impl_->next_offscreen_id;
    const std::uint32_t first_id = id;
    while (impl_->offscreen_surfaces.find(id) != impl_->offscreen_surfaces.end()) {
        id += 1U;
        if (id == 0U) id = 1U;
        if (id == first_id) return 0U;
    }
    impl_->next_offscreen_id = id + 1U;
    if (impl_->next_offscreen_id == 0U) impl_->next_offscreen_id = 1U;
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

std::optional<std::pair<std::uint32_t, std::uint32_t>> Engine::GetOffscreenDimensions(
    std::uint32_t offscreen_id) const {
    const auto it = impl_->offscreen_surfaces.find(offscreen_id);
    if (it == impl_->offscreen_surfaces.end()) return std::nullopt;
    return std::pair{it->second.width, it->second.height};
}

bool Engine::ReadOffscreenPixels(
    std::uint32_t offscreen_id,
    std::uint8_t* out_rgba,
    std::uint32_t width,
    std::uint32_t height) const {
    if (out_rgba == nullptr) return false;
    auto it = impl_->offscreen_surfaces.find(offscreen_id);
    if (it == impl_->offscreen_surfaces.end() || it->second.width != width || it->second.height != height) {
        return false;
    }
    const auto byte_count = RgbaByteCount(width, height);
    if (!byte_count.has_value()) return false;

    const auto& surface = it->second.surface;
    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(width), static_cast<int>(height),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(*byte_count);
    if (!surface->readPixels(info, pixels.data(), static_cast<std::size_t>(width) * 4U, 0, 0)) {
        return false;
    }
    std::memcpy(out_rgba, pixels.data(), pixels.size());
    return true;
}

bool Engine::DestroyOffscreenSurface(std::uint32_t offscreen_id) {
    return offscreen_id != 0U && impl_->offscreen_surfaces.erase(offscreen_id) != 0U;
}

std::size_t Engine::OffscreenSurfaceCountForTesting() const {
    return impl_->offscreen_surfaces.size();
}

std::uint32_t Engine::RenderNodeToRgba(std::uint64_t handle, std::uint32_t width, std::uint32_t height,
                                       std::uint8_t* out_pixels, std::uint32_t out_capacity,
                                       float scale, float x, float y) {
    if (out_pixels == nullptr || !std::isfinite(scale) || !std::isfinite(x) || !std::isfinite(y)) return 0U;
    const auto byte_count = RgbaByteCount(width, height);
    if (!byte_count.has_value() || out_capacity < *byte_count) return 0U;

    const detail::DisplayNode* node = impl_->Resolve(handle);
    if (node == nullptr || !node->alive) return 0;

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
        std::memcpy(out_pixels, pixelAddr, *byte_count);
    }
    return pixelAddr ? *byte_count : 0U;
}

} // namespace effindom::v2
