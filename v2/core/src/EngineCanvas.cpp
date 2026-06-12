#include "EngineInternal.h"

#include <algorithm>
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkFont.h>
#include <include/core/SkImage.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPath.h>
#include <include/core/SkRRect.h>
#include <include/core/SkSurface.h>
#include <include/core/SkTextBlob.h>
#include <modules/svg/include/SkSVGDOM.h>

namespace effindom::v2 {

/* ── Color conversion ──────────────────────────────────────────── */

namespace {

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

void Engine::CanvasDrawText(SkCanvas* canvas, const std::uint8_t* utf8, std::uint32_t len,
                            float x, float y, std::uint32_t font_id, float font_size, std::uint32_t color) const {
    if (!canvas || !utf8 || len == 0) return;
    if (!HasFillAlpha(color)) return;

    auto font_it = impl_->fonts.find(font_id);
    if (font_it == impl_->fonts.end()) return;

    SkFont font(font_it->second, font_size);
    font.setSubpixel(true);
    font.setEdging(SkFont::Edging::kAntiAlias);

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ToCanvasColor(color));

    canvas->drawSimpleText(utf8, len, SkTextEncoding::kUTF8, x, y, font, paint);
}

void Engine::CanvasDrawImage(SkCanvas* canvas, std::uint32_t texture_id,
                             float x, float y, float w, float h) const {
    if (!canvas) return;

    auto texture_it = impl_->textures.find(texture_id);
    if (texture_it == impl_->textures.end() || !texture_it->second.raster_image) return;

    const SkRect dst = SkRect::MakeXYWH(x, y, w, h);
    canvas->drawImageRect(
        texture_it->second.raster_image,
        dst,
        SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone));
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

} // namespace effindom::v2
