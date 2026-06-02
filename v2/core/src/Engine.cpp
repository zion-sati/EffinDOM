#include "EngineInternal.h"
#include "SvgIntrinsicSize.h"

#include <utility>

#include <include/core/SkData.h>
#include <include/core/SkFontMgr.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkPictureRecorder.h>
#include <include/core/SkStream.h>
#ifndef EFFINDOM_V2_CORE_NO_FONT_LOADING
#include <include/ports/SkFontMgr_data.h>
#endif
#include <modules/svg/include/SkSVGDOM.h>

namespace effindom::v2 {

Engine::Engine()
    : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() = default;

void Engine::Init(std::uint32_t physical_width, std::uint32_t physical_height, float dpr) {
    impl_->physical_width = physical_width;
    impl_->physical_height = physical_height;
    impl_->dpr = dpr > 0.0f ? dpr : 1.0f;
}

void Engine::Resize(std::uint32_t physical_width, std::uint32_t physical_height, float dpr) {
    Init(physical_width, physical_height, dpr);
}

void Engine::RegisterFont(std::uint32_t font_id, const std::uint8_t* bytes, std::uint32_t length) {
#ifdef EFFINDOM_V2_CORE_NO_FONT_LOADING
    (void)font_id;
    (void)bytes;
    (void)length;
    return;
#else
    if (font_id == 0 || bytes == nullptr || length == 0) {
        return;
    }
    sk_sp<SkData> data = SkData::MakeWithCopy(bytes, length);
    std::array<sk_sp<SkData>, 1> font_data = {data};
    sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(font_data.data(), font_data.size()));
    sk_sp<SkTypeface> typeface = font_mgr ? font_mgr->makeFromData(data) : nullptr;
    if (typeface) {
        impl_->fonts[font_id] = std::move(typeface);
        for (detail::DisplayNode& node : impl_->nodes) {
            if (!node.alive || !node.has_glyph_run) {
                continue;
            }
            impl_->ReleaseGlyphBlobCache(node);
            node.glyph_blob_version += 1U;
        }
    }
#endif
}

void Engine::UnregisterFont(std::uint32_t font_id) {
#ifdef EFFINDOM_V2_CORE_NO_FONT_LOADING
    (void)font_id;
    return;
#else
    if (font_id == 0U || impl_->fonts.erase(font_id) == 0U) {
        return;
    }
    for (detail::DisplayNode& node : impl_->nodes) {
        if (!node.alive || !node.has_glyph_run) {
            continue;
        }
        impl_->ReleaseGlyphBlobCache(node);
        node.glyph_blob_version += 1U;
    }
#endif
}

void Engine::RegisterSvg(std::uint32_t svg_id, const std::uint8_t* bytes, std::uint32_t length) {
    if (svg_id == 0 || bytes == nullptr || length == 0) {
        return;
    }

    SkMemoryStream stream(bytes, length, true);
    sk_sp<SkSVGDOM> dom = SkSVGDOM::MakeFromStream(stream);
    if (!dom) {
        impl_->svgs.erase(svg_id);
        return;
    }

    const detail::SvgIntrinsicSize parsed_size = detail::ParseSvgIntrinsicSize(bytes, length);
    const float intrinsic_width = std::max(parsed_size.width, 1.0f);
    const float intrinsic_height = std::max(parsed_size.height, 1.0f);
    dom->setContainerSize(SkSize::Make(intrinsic_width, intrinsic_height));

    SkPictureRecorder recorder;
    SkCanvas* picture_canvas = recorder.beginRecording(SkRect::MakeWH(intrinsic_width, intrinsic_height));
    dom->render(picture_canvas);
    sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();

    detail::SvgRecord& svg = impl_->svgs[svg_id];
    svg.picture = std::move(picture);
    svg.intrinsic_width = intrinsic_width;
    svg.intrinsic_height = intrinsic_height;
    svg.raster_variants.clear();
}

void Engine::RegisterTextureRgba(
    std::uint32_t texture_id,
    const std::uint8_t* rgba,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t byte_length
) {
    if (texture_id == 0 || rgba == nullptr || width == 0 || height == 0) {
        return;
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (byte_length < expected) {
        return;
    }

    detail::TextureRecord& texture = impl_->textures[texture_id];
    texture.width = width;
    texture.height = height;
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    texture.raster_image = SkImages::RasterFromData(
        info,
        SkData::MakeWithCopy(rgba, expected),
        static_cast<size_t>(width) * 4U);
}

void Engine::UnregisterTexture(std::uint32_t texture_id) {
    if (texture_id == 0U) {
        return;
    }
    impl_->textures.erase(texture_id);
}

std::optional<NodeDebugView> Engine::GetNodeForTesting(std::uint64_t handle) const {
    const detail::DisplayNode* node = impl_->Resolve(handle);
    if (node == nullptr) {
        return std::nullopt;
    }
    return NodeDebugView{
        node->alive,
        handle,
        node->visual_bounds,
        node->hit_bounds,
        node->clip_bounds,
        node->interactive,
        node->clip_mode,
        node->has_box_style,
        node->bg_color,
        node->corner_radii,
        node->has_border,
        node->border_width,
        node->border_color,
        node->border_style,
        node->border_dash_on,
        node->border_dash_off,
        node->has_gradient,
        node->gradient_start_x,
        node->gradient_start_y,
        node->gradient_end_x,
        node->gradient_end_y,
        node->gradient_stops,
        node->has_layer_effect,
        node->opacity,
        node->blur_sigma,
        node->background_blur_sigma,
        node->drop_shadow_color,
        node->drop_shadow_offset_x,
        node->drop_shadow_offset_y,
        node->drop_shadow_blur_sigma,
        node->drop_shadow_spread,
        node->blend_mode,
        node->has_image,
        node->texture_id,
        node->object_fit,
        node->has_image_nine,
        node->image_nine_texture_id,
        node->image_nine_insets,
        node->has_svg,
        node->svg_id,
        node->svg_tint_color,
        node->has_path,
        node->path_fill_color,
        node->path_stroke_color,
        node->path_stroke_width,
        node->path,
        node->has_glyph_run,
        node->font_id,
        node->font_size,
        node->glyph_color,
        node->glyphs,
        node->glyph_blob_build_count,
        node->cached_glyph_blob != nullptr,
        node->glyph_blob_last_used_generation,
        node->glyph_blob_estimated_bytes,
        node->fade_edge,
        node->has_caret,
        node->caret_x,
        node->caret_y,
        node->caret_height,
        node->caret_color,
        node->caret_last_interaction_ms,
        node->highlight_color,
        node->highlights,
        node->colored_highlights,
    };
}

std::vector<SceneInstructionDebugView> Engine::GetSceneInstructionsForTesting() const {
    std::vector<SceneInstructionDebugView> result;
    result.reserve(impl_->scene_instructions.size());
    for (const detail::SceneInstruction& instruction : impl_->scene_instructions) {
        result.push_back(SceneInstructionDebugView{instruction.opcode, instruction.handle});
    }
    return result;
}

std::vector<std::uint64_t> Engine::GetPaintOrderForTesting() const {
    return impl_->paint_order;
}

std::uint32_t Engine::physical_width() const {
    return impl_->physical_width;
}

std::uint32_t Engine::physical_height() const {
    return impl_->physical_height;
}

float Engine::dpr() const {
    return impl_->dpr;
}

} // namespace effindom::v2
