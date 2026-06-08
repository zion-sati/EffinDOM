#include "EngineInternal.h"

#include <algorithm>
#include <cmath>
#include <include/core/SkCanvas.h>
#include <include/core/SkBlurTypes.h>
#include <include/core/SkColor.h>
#include <include/core/SkFont.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkMatrix.h>
#include <include/core/SkMaskFilter.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPathEffect.h>
#include <include/core/SkSamplingOptions.h>
#include <include/core/SkSurface.h>
#include <include/effects/SkDashPathEffect.h>
#include <include/effects/SkGradientShader.h>
#ifndef EFFINDOM_V2_CORE_WASM_LIMITED_EFFECTS
#include <include/effects/SkImageFilters.h>
#endif

namespace effindom::v2 {

namespace {

bool IsCaretVisible(const detail::DisplayNode& node, double current_time_ms) {
    if (!node.has_caret) {
        return false;
    }
    if (current_time_ms <= 0.0) {
        return true;
    }

    const double elapsed_ms = current_time_ms - static_cast<double>(node.caret_last_interaction_ms);
    if (elapsed_ms < 500.0) {
        return true;
    }

    const std::uint64_t blink_phase = static_cast<std::uint64_t>(elapsed_ms / 500.0);
    return (blink_phase % 2U) == 0U;
}

SkColor ToSkColor(std::uint32_t rgba) {
    return SkColorSetARGB(
        static_cast<U8CPU>(rgba & 0xffU),
        static_cast<U8CPU>((rgba >> 24) & 0xffU),
        static_cast<U8CPU>((rgba >> 16) & 0xffU),
        static_cast<U8CPU>((rgba >> 8) & 0xffU));
}

SkBlendMode ToBlendMode(std::uint32_t blend_mode) {
    switch (blend_mode) {
    case ED_BLEND_MULTIPLY:
        return SkBlendMode::kMultiply;
    case ED_BLEND_SCREEN:
        return SkBlendMode::kScreen;
    case ED_BLEND_OVERLAY:
        return SkBlendMode::kOverlay;
    case ED_BLEND_DARKEN:
        return SkBlendMode::kDarken;
    case ED_BLEND_LIGHTEN:
        return SkBlendMode::kLighten;
    case ED_BLEND_SRC_OVER:
    default:
        return SkBlendMode::kSrcOver;
    }
}

SkRect ToSkRect(const Rect& rect) {
    return SkRect::MakeXYWH(rect.x, rect.y, rect.width, rect.height);
}

SkRRect BuildRoundedRect(const detail::DisplayNode& node, float spread = 0.0f) {
    SkRRect rounded;
    SkRect rect = ToSkRect(node.visual_bounds);
    if (spread > 0.0f) {
        rect.outset(spread, spread);
    }
    const SkVector radii[4] = {
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[0] + spread),
            detail::ClampNonNegative(node.corner_radii[0] + spread)),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[1] + spread),
            detail::ClampNonNegative(node.corner_radii[1] + spread)),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[2] + spread),
            detail::ClampNonNegative(node.corner_radii[2] + spread)),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[3] + spread),
            detail::ClampNonNegative(node.corner_radii[3] + spread)),
    };
    rounded.setRectRadii(rect, radii);
    return rounded;
}

SkRRect BuildInsetRoundedRect(const detail::DisplayNode& node, float inset) {
    SkRRect rounded;
    SkRect rect = ToSkRect(node.visual_bounds);
    const float clamped_inset = std::max(
        0.0f,
        std::min({inset, rect.width() * 0.5f, rect.height() * 0.5f}));
    rect.inset(clamped_inset, clamped_inset);
    const SkVector radii[4] = {
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[0] - clamped_inset),
            detail::ClampNonNegative(node.corner_radii[0] - clamped_inset)),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[1] - clamped_inset),
            detail::ClampNonNegative(node.corner_radii[1] - clamped_inset)),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[2] - clamped_inset),
            detail::ClampNonNegative(node.corner_radii[2] - clamped_inset)),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[3] - clamped_inset),
            detail::ClampNonNegative(node.corner_radii[3] - clamped_inset)),
    };
    rounded.setRectRadii(rect, radii);
    return rounded;
}

SkRRect BuildInsetStrokeRoundedRect(const detail::DisplayNode& node, float stroke_width) {
    const float half_stroke_width = std::max(0.0f, stroke_width * 0.5f);
    return BuildInsetRoundedRect(node, half_stroke_width);
}

void DrawSolidBorder(SkCanvas* canvas, const detail::DisplayNode& node) {
    SkPath border_path;
    border_path.setFillType(SkPathFillType::kEvenOdd);
    border_path.addRRect(BuildInsetRoundedRect(node, 0.0f));

    const SkRRect inner = BuildInsetRoundedRect(node, node.border_width);
    if (!inner.getBounds().isEmpty()) {
        border_path.addRRect(inner);
    }

    SkPaint border_paint;
    border_paint.setStyle(SkPaint::kFill_Style);
    border_paint.setColor(ToSkColor(node.border_color));
    border_paint.setAntiAlias(true);
    canvas->drawPath(border_path, border_paint);
}

Rect ExpandClipMaxEdgesToDevicePixels(const detail::DisplayNode& node) {
    const Rect& clip_bounds = node.clip_bounds;
    if (node.clip_mode != ED_CLIP_MODE_RASTER_SAFE_VISUAL) {
        return clip_bounds;
    }

    float right = clip_bounds.x + clip_bounds.width + 1.0f;
    float bottom = clip_bounds.y + clip_bounds.height + 1.0f;
    return Rect{
        clip_bounds.x,
        clip_bounds.y,
        std::max(0.0f, right - clip_bounds.x),
        std::max(0.0f, bottom - clip_bounds.y),
    };
}

SkRRect BuildClipRoundedRect(const detail::DisplayNode& node) {
    SkRRect rounded;
    const Rect clip_bounds = ExpandClipMaxEdgesToDevicePixels(node);
    const SkRect rect = ToSkRect(clip_bounds);
    const float inset_left = std::max(0.0f, clip_bounds.x - node.visual_bounds.x);
    const float inset_top = std::max(0.0f, clip_bounds.y - node.visual_bounds.y);
    const float inset_right =
        std::max(0.0f, (node.visual_bounds.x + node.visual_bounds.width) - (clip_bounds.x + clip_bounds.width));
    const float inset_bottom =
        std::max(0.0f, (node.visual_bounds.y + node.visual_bounds.height) - (clip_bounds.y + clip_bounds.height));
    const SkVector radii[4] = {
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[0] - std::max(inset_left, inset_top)),
            detail::ClampNonNegative(node.corner_radii[0] - std::max(inset_left, inset_top))),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[1] - std::max(inset_right, inset_top)),
            detail::ClampNonNegative(node.corner_radii[1] - std::max(inset_right, inset_top))),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[2] - std::max(inset_right, inset_bottom)),
            detail::ClampNonNegative(node.corner_radii[2] - std::max(inset_right, inset_bottom))),
        SkVector::Make(
            detail::ClampNonNegative(node.corner_radii[3] - std::max(inset_left, inset_bottom)),
            detail::ClampNonNegative(node.corner_radii[3] - std::max(inset_left, inset_bottom))),
    };
    rounded.setRectRadii(rect, radii);
    return rounded;
}

void DrawDropShadow(SkCanvas* canvas, const detail::DisplayNode& node) {
    const std::uint8_t shadow_alpha = static_cast<std::uint8_t>(node.drop_shadow_color & 0xffU);
    if (shadow_alpha == 0U) {
        return;
    }

    SkPaint shadow_paint;
    shadow_paint.setAntiAlias(true);
    shadow_paint.setStyle(SkPaint::kFill_Style);
    shadow_paint.setColor(ToSkColor(node.drop_shadow_color));
    if (node.has_layer_effect) {
        shadow_paint.setAlphaf(detail::ClampOpacity(node.opacity) * shadow_paint.getAlphaf());
    }
    if (node.drop_shadow_blur_sigma > 0.0f) {
        shadow_paint.setMaskFilter(SkMaskFilter::MakeBlur(SkBlurStyle::kNormal_SkBlurStyle, node.drop_shadow_blur_sigma));
    }

    canvas->save();
    canvas->translate(node.drop_shadow_offset_x, node.drop_shadow_offset_y);
    canvas->drawRRect(BuildRoundedRect(node, node.drop_shadow_spread), shadow_paint);
    canvas->restore();
}

SkPath BuildPath(const detail::DisplayNode& node) {
    SkPath path;
    for (const PathVerbRecord& verb : node.path) {
        switch (verb.verb) {
        case ED_PATH_MOVE_TO:
            path.moveTo(verb.args[0], verb.args[1]);
            break;
        case ED_PATH_LINE_TO:
            path.lineTo(verb.args[0], verb.args[1]);
            break;
        case ED_PATH_QUAD_TO:
            path.quadTo(verb.args[0], verb.args[1], verb.args[2], verb.args[3]);
            break;
        case ED_PATH_CUBIC_TO:
            path.cubicTo(verb.args[0], verb.args[1], verb.args[2], verb.args[3], verb.args[4], verb.args[5]);
            break;
        case ED_PATH_CLOSE:
            path.close();
            break;
        default:
            break;
        }
    }
    return path;
}

sk_sp<SkShader> BuildGradientShader(const detail::DisplayNode& node) {
    if (!node.has_gradient || node.gradient_stops.size() < 2) {
        return nullptr;
    }

    std::vector<SkColor> colors(node.gradient_stops.size());
    std::vector<SkScalar> positions(node.gradient_stops.size());
    for (std::size_t index = 0; index < node.gradient_stops.size(); index += 1) {
        colors[index] = ToSkColor(node.gradient_stops[index].color);
        positions[index] = std::clamp(node.gradient_stops[index].offset, 0.0f, 1.0f);
    }

    const SkPoint points[2] = {
        SkPoint::Make(node.gradient_start_x, node.gradient_start_y),
        SkPoint::Make(node.gradient_end_x, node.gradient_end_y),
    };
    return SkGradientShader::MakeLinear(
        points,
        colors.data(),
        positions.data(),
        static_cast<int>(colors.size()),
        SkTileMode::kClamp);
}

sk_sp<SkPathEffect> BuildBorderPathEffect(const detail::DisplayNode& node) {
    const float dash_on = detail::ClampNonNegative(node.border_dash_on);
    const float dash_off = detail::ClampNonNegative(node.border_dash_off);
    if (node.border_style == ED_BORDER_DOTTED) {
        const SkScalar intervals[] = {
            std::max(node.border_width, 1.0f),
            std::max(node.border_width, 1.0f),
        };
        return SkDashPathEffect::Make(intervals, 2, 0.0f);
    }
    if (node.border_style == ED_BORDER_DASHED && (dash_on > 0.0f || dash_off > 0.0f)) {
        const SkScalar intervals[] = {
            dash_on > 0.0f ? dash_on : std::max(node.border_width * 2.0f, 1.0f),
            dash_off > 0.0f ? dash_off : std::max(node.border_width, 1.0f),
        };
        return SkDashPathEffect::Make(intervals, 2, 0.0f);
    }
    return nullptr;
}

void ConfigureLayerPaint(const detail::DisplayNode& node, SkPaint& paint) {
    paint.setBlendMode(ToBlendMode(node.blend_mode));
    paint.setAlphaf(detail::ClampOpacity(node.opacity));
#ifndef EFFINDOM_V2_CORE_WASM_LIMITED_EFFECTS
    if (node.blur_sigma > 0.0f) {
        paint.setImageFilter(SkImageFilters::Blur(node.blur_sigma, node.blur_sigma, nullptr));
    }
#else
    (void)node;
#endif
}

bool NeedsLayerPaint(const detail::DisplayNode& node) {
    return node.has_layer_effect &&
        (node.opacity < 0.999f || node.blur_sigma > 0.0f || node.blend_mode != ED_BLEND_SRC_OVER);
}

bool SaveNodeLayer(SkCanvas* canvas, const detail::DisplayNode& node, const SkRect* bounds_override = nullptr) {
    const bool needs_layer_paint = NeedsLayerPaint(node);
#ifndef EFFINDOM_V2_CORE_WASM_LIMITED_EFFECTS
    const bool needs_backdrop = node.background_blur_sigma > 0.0f;
#else
    const bool needs_backdrop = false;
    (void)node;
#endif
    if (!needs_layer_paint && !needs_backdrop) {
        return false;
    }

    if (!needs_backdrop) {
        const SkPaint* layer_paint_ptr = nullptr;
        SkPaint layer_paint;
        if (needs_layer_paint) {
            ConfigureLayerPaint(node, layer_paint);
            layer_paint_ptr = &layer_paint;
        }
        canvas->saveLayer(bounds_override, layer_paint_ptr);
        return true;
    }

    SkCanvas::SaveLayerRec layer_rec;
    SkPaint layer_paint;
    SkRect layer_bounds;
    sk_sp<SkImageFilter> backdrop_filter;
    if (needs_layer_paint) {
        ConfigureLayerPaint(node, layer_paint);
        layer_rec.fPaint = &layer_paint;
    }
    if (bounds_override != nullptr) {
        layer_rec.fBounds = bounds_override;
    } else {
        layer_bounds = ToSkRect(node.visual_bounds);
        layer_rec.fBounds = &layer_bounds;
    }
#ifndef EFFINDOM_V2_CORE_WASM_LIMITED_EFFECTS
    if (needs_backdrop) {
        backdrop_filter = SkImageFilters::Blur(node.background_blur_sigma, node.background_blur_sigma, nullptr);
        layer_rec.fBackdrop = backdrop_filter.get();
    }
#endif
    canvas->saveLayer(layer_rec);
    return true;
}

constexpr std::size_t kMaxSvgRasterVariants = 8U;

std::optional<SkRect> ComputeImageSourceRect(const detail::TextureRecord& texture, const detail::DisplayNode& node) {
    const float src_w = static_cast<float>(texture.width);
    const float src_h = static_cast<float>(texture.height);
    const float dst_w = std::max(node.visual_bounds.width, 1.0f);
    const float dst_h = std::max(node.visual_bounds.height, 1.0f);

    if (node.object_fit == ED_OBJECT_FIT_FILL) {
        return SkRect::MakeWH(src_w, src_h);
    }

    if (node.object_fit == ED_OBJECT_FIT_NONE) {
        const float crop_w = std::min(src_w, dst_w);
        const float crop_h = std::min(src_h, dst_h);
        return SkRect::MakeXYWH(0.0f, 0.0f, crop_w, crop_h);
    }

    const float src_ratio = src_w / src_h;
    const float dst_ratio = dst_w / dst_h;
    const bool use_cover = node.object_fit == ED_OBJECT_FIT_COVER;
    const bool use_contain = node.object_fit == ED_OBJECT_FIT_CONTAIN || node.object_fit == ED_OBJECT_FIT_SCALE_DOWN;

    float crop_w = src_w;
    float crop_h = src_h;
    if ((use_cover && src_ratio > dst_ratio) || (use_contain && src_ratio < dst_ratio)) {
        crop_w = src_h * dst_ratio;
        crop_h = src_h;
    } else {
        crop_w = src_w;
        crop_h = src_w / dst_ratio;
    }

    if (node.object_fit == ED_OBJECT_FIT_SCALE_DOWN && src_w <= dst_w && src_h <= dst_h) {
        crop_w = src_w;
        crop_h = src_h;
    }

    const float crop_x = (src_w - crop_w) * 0.5f;
    const float crop_y = (src_h - crop_h) * 0.5f;
    return SkRect::MakeXYWH(crop_x, crop_y, crop_w, crop_h);
}

SkRect ComputeImageDestRect(const detail::TextureRecord& texture, const detail::DisplayNode& node) {
    const float src_w = static_cast<float>(texture.width);
    const float src_h = static_cast<float>(texture.height);
    const float dst_w = std::max(node.visual_bounds.width, 1.0f);
    const float dst_h = std::max(node.visual_bounds.height, 1.0f);

    if (node.object_fit == ED_OBJECT_FIT_FILL ||
        node.object_fit == ED_OBJECT_FIT_COVER) {
        return ToSkRect(node.visual_bounds);
    }

    float render_w = dst_w;
    float render_h = dst_h;
    if (src_w > 0.0f && src_h > 0.0f) {
        const float scale = std::min(dst_w / src_w, dst_h / src_h);
        if (node.object_fit == ED_OBJECT_FIT_CONTAIN) {
            render_w = src_w * scale;
            render_h = src_h * scale;
        } else if (node.object_fit == ED_OBJECT_FIT_SCALE_DOWN) {
            const float downscale = std::min(1.0f, scale);
            render_w = src_w * downscale;
            render_h = src_h * downscale;
        } else if (node.object_fit == ED_OBJECT_FIT_NONE) {
            render_w = std::min(src_w, dst_w);
            render_h = std::min(src_h, dst_h);
        }
    }

    const float offset_x = node.visual_bounds.x + ((dst_w - render_w) * 0.5f);
    const float offset_y = node.visual_bounds.y + ((dst_h - render_h) * 0.5f);
    return SkRect::MakeXYWH(offset_x, offset_y, render_w, render_h);
}

SkIRect ComputeImageNineCenter(const detail::TextureRecord& texture, const detail::DisplayNode& node) {
    const int left = std::clamp(static_cast<int>(node.image_nine_insets.left), 0, static_cast<int>(texture.width));
    const int top = std::clamp(static_cast<int>(node.image_nine_insets.top), 0, static_cast<int>(texture.height));
    const int right = std::clamp(
        static_cast<int>(texture.width) - static_cast<int>(node.image_nine_insets.right),
        left,
        static_cast<int>(texture.width));
    const int bottom = std::clamp(
        static_cast<int>(texture.height) - static_cast<int>(node.image_nine_insets.bottom),
        top,
        static_cast<int>(texture.height));
    return SkIRect::MakeLTRB(left, top, right, bottom);
}

sk_sp<SkImage> ResolveSvgRasterVariant(
    detail::SvgRecord& svg,
    const detail::DisplayNode& node,
    float dpr,
    std::uint64_t render_generation) {
    const SkRect target = ToSkRect(node.visual_bounds);
    if (target.isEmpty()) {
        return nullptr;
    }

    const float intrinsic_width = std::max(svg.intrinsic_width, 1.0f);
    const float intrinsic_height = std::max(svg.intrinsic_height, 1.0f);
    const std::uint32_t pixel_width = static_cast<std::uint32_t>(std::max(1.0f, std::round(target.width() * std::max(dpr, 1.0f))));
    const std::uint32_t pixel_height = static_cast<std::uint32_t>(std::max(1.0f, std::round(target.height() * std::max(dpr, 1.0f))));

    auto variant_it = std::find_if(
        svg.raster_variants.begin(),
        svg.raster_variants.end(),
        [&](const detail::SvgRecord::RasterVariant& variant) {
            return variant.pixel_width == pixel_width &&
                variant.pixel_height == pixel_height &&
                variant.tint_color == node.svg_tint_color &&
                variant.image != nullptr;
        });
    if (variant_it != svg.raster_variants.end()) {
        variant_it->last_used_generation = render_generation;
        return variant_it->image;
    }

    if (!svg.picture) {
        return nullptr;
    }

    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(
        static_cast<int>(pixel_width),
        static_cast<int>(pixel_height)));
    if (!surface) {
        return nullptr;
    }

    SkCanvas* raster_canvas = surface->getCanvas();
    const SkRect source = SkRect::MakeWH(intrinsic_width, intrinsic_height);
    raster_canvas->clear(SK_ColorTRANSPARENT);
    raster_canvas->scale(
        static_cast<float>(pixel_width) / intrinsic_width,
        static_cast<float>(pixel_height) / intrinsic_height);

    if (node.svg_tint_color != 0U) {
        raster_canvas->saveLayer(&source, nullptr);
        svg.picture->playback(raster_canvas);

        SkPaint tint_paint;
        tint_paint.setBlendMode(SkBlendMode::kSrcIn);
        tint_paint.setColor(ToSkColor(node.svg_tint_color));
        raster_canvas->drawRect(source, tint_paint);
        raster_canvas->restore();
    } else {
        svg.picture->playback(raster_canvas);
    }

    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        return nullptr;
    }

    if (svg.raster_variants.size() >= kMaxSvgRasterVariants) {
        const auto evict_it = std::min_element(
            svg.raster_variants.begin(),
            svg.raster_variants.end(),
            [](const detail::SvgRecord::RasterVariant& lhs, const detail::SvgRecord::RasterVariant& rhs) {
                return lhs.last_used_generation < rhs.last_used_generation;
            });
        if (evict_it != svg.raster_variants.end()) {
            *evict_it = detail::SvgRecord::RasterVariant{
                pixel_width,
                pixel_height,
                node.svg_tint_color,
                render_generation,
                image,
            };
            return image;
        }
    }

    svg.raster_variants.push_back(detail::SvgRecord::RasterVariant{
        pixel_width,
        pixel_height,
        node.svg_tint_color,
        render_generation,
        image,
    });
    return image;
}

void DrawSvg(detail::SvgRecord& svg, const detail::DisplayNode& node, SkCanvas* canvas, float dpr, std::uint64_t render_generation) {
    const SkRect target = ToSkRect(node.visual_bounds);
    if (target.isEmpty()) {
        return;
    }

    const sk_sp<SkImage> image = ResolveSvgRasterVariant(svg, node, dpr, render_generation);
    if (!image) {
        return;
    }
    canvas->drawImageRect(image, target, SkSamplingOptions(), nullptr);
}

sk_sp<SkTextBlob> BuildGlyphBlob(
    const detail::DisplayNode& node,
    const std::unordered_map<std::uint32_t, sk_sp<SkTypeface>>& fonts) {
    if (node.glyphs.empty()) {
        return nullptr;
    }

    SkTextBlobBuilder builder;
    std::size_t run_start = 0U;
    while (run_start < node.glyphs.size()) {
        const std::uint32_t run_font_id =
            node.glyphs[run_start].font_id != 0U ? node.glyphs[run_start].font_id : node.font_id;
        std::size_t run_end = run_start + 1U;
        while (run_end < node.glyphs.size()) {
            const std::uint32_t glyph_font_id =
                node.glyphs[run_end].font_id != 0U ? node.glyphs[run_end].font_id : node.font_id;
            if (glyph_font_id != run_font_id) {
                break;
            }
            run_end += 1U;
        }

        const auto font_it = fonts.find(run_font_id);
        SkFont font(font_it != fonts.end() ? font_it->second : sk_sp<SkTypeface>(), node.font_size);
        font.setSubpixel(true);
        font.setHinting(SkFontHinting::kSlight);
        auto& run = builder.allocRunPos(font, static_cast<int>(run_end - run_start));
        for (std::size_t index = run_start; index < run_end; index += 1U) {
            const std::size_t run_index = index - run_start;
            run.glyphs[run_index] = static_cast<SkGlyphID>(node.glyphs[index].glyph_id);
            run.points()[run_index] = SkPoint::Make(node.glyphs[index].x, node.glyphs[index].y);
        }
        run_start = run_end;
    }
    return builder.make();
}

void ApplySingleFadeMask(SkCanvas* canvas, const detail::DisplayNode& node, std::uint32_t fade_edge) {
    const SkRect bounds = ToSkRect(node.visual_bounds);
    if (bounds.isEmpty()) {
        return;
    }

    SkPoint points[2];
    SkColor colors[2];
    switch (fade_edge) {
    case ED_FADE_LEFT:
        points[0] = SkPoint::Make(bounds.left(), bounds.top());
        points[1] = SkPoint::Make(bounds.left() + std::min(bounds.width(), 24.0f), bounds.top());
        colors[0] = SK_ColorTRANSPARENT;
        colors[1] = SK_ColorBLACK;
        break;
    case ED_FADE_TOP:
        points[0] = SkPoint::Make(bounds.left(), bounds.top());
        points[1] = SkPoint::Make(bounds.left(), bounds.top() + std::min(bounds.height(), 24.0f));
        colors[0] = SK_ColorTRANSPARENT;
        colors[1] = SK_ColorBLACK;
        break;
    case ED_FADE_RIGHT:
        points[0] = SkPoint::Make(bounds.right() - std::min(bounds.width(), 24.0f), bounds.top());
        points[1] = SkPoint::Make(bounds.right(), bounds.top());
        colors[0] = SK_ColorBLACK;
        colors[1] = SK_ColorTRANSPARENT;
        break;
    case ED_FADE_BOTTOM:
        points[0] = SkPoint::Make(bounds.left(), bounds.bottom() - std::min(bounds.height(), 24.0f));
        points[1] = SkPoint::Make(bounds.left(), bounds.bottom());
        colors[0] = SK_ColorBLACK;
        colors[1] = SK_ColorTRANSPARENT;
        break;
    default:
        return;
    }

    SkPaint mask_paint;
    mask_paint.setBlendMode(SkBlendMode::kDstIn);
    mask_paint.setShader(SkGradientShader::MakeLinear(points, colors, nullptr, 2, SkTileMode::kClamp));
    canvas->drawRect(bounds, mask_paint);
}

void ApplyFadeMask(SkCanvas* canvas, const detail::DisplayNode& node) {
    const std::uint32_t fade_mask = node.fade_edge;
    if ((fade_mask & ~ED_FADE_ALL_MASK) != 0U) {
        return;
    }
    if ((fade_mask & ED_FADE_LEFT) != 0U) {
        ApplySingleFadeMask(canvas, node, ED_FADE_LEFT);
    }
    if ((fade_mask & ED_FADE_TOP) != 0U) {
        ApplySingleFadeMask(canvas, node, ED_FADE_TOP);
    }
    if ((fade_mask & ED_FADE_RIGHT) != 0U) {
        ApplySingleFadeMask(canvas, node, ED_FADE_RIGHT);
    }
    if ((fade_mask & ED_FADE_BOTTOM) != 0U) {
        ApplySingleFadeMask(canvas, node, ED_FADE_BOTTOM);
    }
}

} // namespace

void Engine::Impl::DrawNode(SkCanvas* canvas, const detail::DisplayNode& node, double current_time_ms) {
    DrawDropShadow(canvas, node);
    bool saved_background_blur_clip = false;
#ifndef EFFINDOM_V2_CORE_WASM_LIMITED_EFFECTS
    if (node.background_blur_sigma > 0.0f) {
        canvas->save();
        const SkRRect blur_clip = BuildRoundedRect(node);
        if (blur_clip.isRect()) {
            canvas->clipRect(blur_clip.getBounds(), SkClipOp::kIntersect, true);
        } else {
            canvas->clipRRect(blur_clip, true);
        }
        saved_background_blur_clip = true;
    }
#endif
    const bool saved_layer = SaveNodeLayer(canvas, node);

    if (node.has_box_style || node.has_gradient) {
        SkPaint fill_paint;
        fill_paint.setStyle(SkPaint::kFill_Style);
        fill_paint.setColor(ToSkColor(node.bg_color));
        if (const sk_sp<SkShader> shader = BuildGradientShader(node)) {
            fill_paint.setShader(shader);
        }
        canvas->drawRRect(BuildRoundedRect(node), fill_paint);
    }

    if (node.has_image) {
        const auto texture_it = textures.find(node.texture_id);
        if (texture_it != textures.end() && texture_it->second.raster_image) {
            const std::optional<SkRect> src = ComputeImageSourceRect(texture_it->second, node);
            const SkRect dst = ComputeImageDestRect(texture_it->second, node);
            if (src.has_value()) {
                canvas->drawImageRect(
                    texture_it->second.raster_image,
                    src.value(),
                    dst,
                    SkSamplingOptions(),
                    nullptr,
                    SkCanvas::kStrict_SrcRectConstraint);
            }
        }
    }

    if (node.has_image_nine) {
        const auto texture_it = textures.find(node.image_nine_texture_id);
        if (texture_it != textures.end() && texture_it->second.raster_image) {
            canvas->drawImageNine(
                texture_it->second.raster_image.get(),
                ComputeImageNineCenter(texture_it->second, node),
                ToSkRect(node.visual_bounds),
                SkFilterMode::kLinear,
                nullptr);
        }
    }

    if (node.has_svg) {
        auto svg_it = svgs.find(node.svg_id);
        if (svg_it != svgs.end()) {
            DrawSvg(svg_it->second, node, canvas, dpr, render_generation);
        }
    }

    if (node.has_path && !node.path.empty()) {
        const SkPath path = BuildPath(node);
        if ((node.path_fill_color & 0xffU) != 0) {
            SkPaint fill_paint;
            fill_paint.setStyle(SkPaint::kFill_Style);
            fill_paint.setColor(ToSkColor(node.path_fill_color));
            canvas->drawPath(path, fill_paint);
        }
        if (node.path_stroke_width > 0.0f && (node.path_stroke_color & 0xffU) != 0) {
            SkPaint stroke_paint;
            stroke_paint.setStyle(SkPaint::kStroke_Style);
            stroke_paint.setColor(ToSkColor(node.path_stroke_color));
            stroke_paint.setStrokeWidth(node.path_stroke_width);
            canvas->drawPath(path, stroke_paint);
        }
    }

    for (const ColoredRect& colored_rect : node.colored_highlights) {
        SkPaint highlight_paint;
        highlight_paint.setStyle(SkPaint::kFill_Style);
        highlight_paint.setColor(ToSkColor(colored_rect.color));
        canvas->drawRect(
            SkRect::MakeXYWH(
                node.visual_bounds.x + colored_rect.rect.x,
                node.visual_bounds.y + colored_rect.rect.y,
                colored_rect.rect.width,
                colored_rect.rect.height),
            highlight_paint);
    }

    for (const Rect& rect : node.highlights) {
        SkPaint highlight_paint;
        highlight_paint.setStyle(SkPaint::kFill_Style);
        highlight_paint.setColor(ToSkColor(node.highlight_color));
        canvas->drawRect(
            SkRect::MakeXYWH(
                node.visual_bounds.x + rect.x,
                node.visual_bounds.y + rect.y,
                rect.width,
                rect.height),
            highlight_paint);
    }

    if (node.has_glyph_run && !node.glyphs.empty()) {
        if (node.glyphs_have_per_color) {
            std::size_t color_run_start = 0U;
            while (color_run_start < node.glyphs.size()) {
                const std::uint32_t run_color = node.glyphs[color_run_start].color;
                std::size_t color_run_end = color_run_start + 1U;
                while (color_run_end < node.glyphs.size() && node.glyphs[color_run_end].color == run_color) {
                    color_run_end += 1U;
                }
                std::vector<GlyphPlacement> run_glyphs{};
                run_glyphs.reserve(color_run_end - color_run_start);
                for (std::size_t index = color_run_start; index < color_run_end; index += 1U) {
                    run_glyphs.push_back(node.glyphs[index]);
                }
                const detail::DisplayNode run_node = [&]() {
                    detail::DisplayNode copy = node;
                    copy.glyphs = std::move(run_glyphs);
                    return copy;
                }();
                const sk_sp<SkTextBlob> blob = BuildGlyphBlob(run_node, fonts);
                if (blob) {
                    SkPaint glyph_paint;
                    glyph_paint.setAntiAlias(true);
                    glyph_paint.setColor(ToSkColor(run_color));
                    canvas->drawTextBlob(blob, node.visual_bounds.x, node.visual_bounds.y, glyph_paint);
                }
                color_run_start = color_run_end;
            }
        } else {
        detail::DisplayNode& mutable_node = const_cast<detail::DisplayNode&>(node);
        sk_sp<SkTextBlob> blob{};
        if (mutable_node.glyphs.empty()) {
            ReleaseGlyphBlobCache(mutable_node);
        } else if (
            mutable_node.cached_glyph_blob != nullptr &&
            mutable_node.cached_glyph_blob_version == mutable_node.glyph_blob_version) {
            TouchGlyphBlobCache(mutable_node);
            blob = mutable_node.cached_glyph_blob;
        } else {
            StoreGlyphBlobCache(mutable_node, BuildGlyphBlob(mutable_node, fonts));
            blob = mutable_node.cached_glyph_blob;
        }
        if (blob) {
            if (node.fade_edge != ED_FADE_NONE) {
                canvas->saveLayer(nullptr, nullptr);
            }
            SkPaint glyph_paint;
            glyph_paint.setAntiAlias(true);
            glyph_paint.setColor(ToSkColor(node.glyph_color));
            canvas->drawTextBlob(blob, node.visual_bounds.x, node.visual_bounds.y, glyph_paint);
            if (node.fade_edge != ED_FADE_NONE) {
                ApplyFadeMask(canvas, node);
                canvas->restore();
            }
        }
        }
    }

    if (IsCaretVisible(node, current_time_ms)) {
        SkPaint caret_paint;
        caret_paint.setStyle(SkPaint::kFill_Style);
        caret_paint.setColor(ToSkColor(node.caret_color));
        canvas->drawRect(
            SkRect::MakeXYWH(
                node.caret_x,
                node.caret_y,
                1.0f,
                node.caret_height),
            caret_paint);
    }

    if (node.has_border && node.border_width > 0.0f && (node.border_color & 0xffU) != 0) {
        if (node.border_style == ED_BORDER_SOLID) {
            DrawSolidBorder(canvas, node);
        } else {
            SkPaint border_paint;
            border_paint.setStyle(SkPaint::kStroke_Style);
            border_paint.setStrokeWidth(node.border_width);
            border_paint.setColor(ToSkColor(node.border_color));
            border_paint.setAntiAlias(true);
            if (const sk_sp<SkPathEffect> effect = BuildBorderPathEffect(node)) {
                border_paint.setPathEffect(effect);
            }
            canvas->drawRRect(BuildInsetStrokeRoundedRect(node, node.border_width), border_paint);
        }
    }

    if (saved_layer) {
        canvas->restore();
    }
    if (saved_background_blur_clip) {
        canvas->restore();
    }
}

std::uint64_t Engine::HitTest(float logical_x, float logical_y) const {
    for (auto it = impl_->paint_order.rbegin(); it != impl_->paint_order.rend(); ++it) {
        const detail::DisplayNode* node = impl_->Resolve(*it);
        if (node == nullptr || !node->interactive) {
            continue;
        }
        if (node->hit_bounds.Contains(logical_x, logical_y)) {
            return *it;
        }
    }
    return ED_INVALID_HANDLE;
}

void Engine::RenderToCanvas(SkCanvas* canvas, double current_time_ms) const {
    if (canvas == nullptr) {
        return;
    }

    canvas->save();
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->scale(impl_->dpr, impl_->dpr);
    impl_->render_generation += 1U;

    int save_depth = 1;
    for (const detail::SceneInstruction& instruction : impl_->scene_instructions) {
        switch (instruction.opcode) {
        case OP_DRAW_NODE:
            if (const detail::DisplayNode* draw_node = impl_->Resolve(instruction.handle); draw_node != nullptr) {
                impl_->DrawNode(canvas, *draw_node, current_time_ms);
            }
            break;
        case OP_PUSH_CLIP: {
            const detail::DisplayNode* node = impl_->Resolve(instruction.handle);
            if (node == nullptr) {
                break;
            }
            canvas->save();
            const SkRRect clip = BuildClipRoundedRect(*node);
            if (clip.isRect()) {
                canvas->clipRect(clip.getBounds(), SkClipOp::kIntersect, false);
            } else {
                canvas->clipRRect(clip, true);
            }
            save_depth += 1;
            break;
        }
        case OP_PUSH_LAYER: {
            const detail::DisplayNode* node = impl_->Resolve(instruction.handle);
            SkRect layer_bounds;
            if (node != nullptr) {
                layer_bounds = ToSkRect(node->visual_bounds);
                SaveNodeLayer(canvas, *node, &layer_bounds);
            } else {
                canvas->saveLayer(nullptr, nullptr);
            }
            save_depth += 1;
            break;
        }
        case OP_PUSH_TRANSLATE:
            canvas->save();
            canvas->translate(instruction.arg0, instruction.arg1);
            save_depth += 1;
            break;
        case OP_POP:
            if (save_depth > 1) {
                canvas->restore();
                save_depth -= 1;
            }
            break;
        default:
            break;
        }
    }

    while (save_depth > 1) {
        canvas->restore();
        save_depth -= 1;
    }
    impl_->EvictGlyphBlobCaches();
    canvas->restore();
}

} // namespace effindom::v2
