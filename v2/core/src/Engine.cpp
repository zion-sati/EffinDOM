#include "EngineInternal.h"
#include "SvgIntrinsicSize.h"

#include <algorithm>
#include <utility>
#include <cmath>

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

namespace {

constexpr double kNominalFrameMs = 1000.0 / 60.0;
constexpr double kMaxFrameDeltaMs = 100.0;
constexpr float kDefaultViewportPanFriction = 0.955f;
constexpr float kTerminalMomentumVelocityPxPerSecond = 120.0f;
constexpr float kTerminalMomentumFriction = 0.88f;
constexpr float kMomentumStopDisplacementPx = 0.32f;

bool IsValidTimestamp(double timestamp_ms) {
    return std::isfinite(timestamp_ms) && timestamp_ms >= 0.0;
}

double ClampInputDeltaMs(double delta_ms) {
    if (!std::isfinite(delta_ms) || delta_ms <= 0.0) {
        return kNominalFrameMs;
    }
    return std::min(delta_ms, kMaxFrameDeltaMs);
}

float VelocityStopThreshold(float displacement_threshold_px) {
    return displacement_threshold_px / static_cast<float>(kNominalFrameMs / 1000.0);
}

float NormalizeViewportScale(float scale) {
    if (!std::isfinite(scale)) {
        return 1.0f;
    }
    return std::clamp(scale, 1.0f, 4.0f);
}

float NormalizeViewportOffset(float value) {
    return std::isfinite(value) ? value : 0.0f;
}

} // namespace

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

void Engine::ClampViewportTransform() {
    impl_->viewport_scale = NormalizeViewportScale(impl_->viewport_scale);
    if (impl_->viewport_scale <= 1.0f) {
        impl_->viewport_scale = 1.0f;
        impl_->viewport_offset_x = 0.0f;
        impl_->viewport_offset_y = 0.0f;
        return;
    }
    const float width = std::max(1.0f, impl_->viewport_width);
    const float height = std::max(1.0f, impl_->viewport_height);
    const float min_offset_x = width * (1.0f - impl_->viewport_scale);
    const float min_offset_y = height * (1.0f - impl_->viewport_scale);
    impl_->viewport_offset_x = std::clamp(NormalizeViewportOffset(impl_->viewport_offset_x), min_offset_x, 0.0f);
    impl_->viewport_offset_y = std::clamp(NormalizeViewportOffset(impl_->viewport_offset_y), min_offset_y, 0.0f);
}

bool Engine::ApplyViewportPan(float delta_x, float delta_y) {
    const float previous_x = impl_->viewport_offset_x;
    const float previous_y = impl_->viewport_offset_y;
    impl_->viewport_offset_x -= std::isfinite(delta_x) ? delta_x : 0.0f;
    impl_->viewport_offset_y -= std::isfinite(delta_y) ? delta_y : 0.0f;
    ClampViewportTransform();
    return std::abs(previous_x - impl_->viewport_offset_x) >= 0.001f ||
        std::abs(previous_y - impl_->viewport_offset_y) >= 0.001f;
}

void Engine::SetViewportSize(float logical_width, float logical_height) {
    impl_->viewport_width = std::isfinite(logical_width) && logical_width > 0.0f ? logical_width : 1.0f;
    impl_->viewport_height = std::isfinite(logical_height) && logical_height > 0.0f ? logical_height : 1.0f;
    ClampViewportTransform();
}

void Engine::SetViewportTransform(float scale, float offset_x, float offset_y) {
    impl_->viewport_scale = NormalizeViewportScale(scale);
    impl_->viewport_offset_x = NormalizeViewportOffset(offset_x);
    impl_->viewport_offset_y = NormalizeViewportOffset(offset_y);
    ClampViewportTransform();
}

float Engine::ViewportScale() const {
    return impl_->viewport_scale;
}

float Engine::ViewportOffsetX() const {
    return impl_->viewport_offset_x;
}

float Engine::ViewportOffsetY() const {
    return impl_->viewport_offset_y;
}

void Engine::SetViewportZoomFromSceneAnchor(float scale, float anchor_scene_x, float anchor_scene_y, float screen_x, float screen_y) {
    const float next_scale = NormalizeViewportScale(scale);
    SetViewportTransform(
        next_scale,
        NormalizeViewportOffset(screen_x) - (NormalizeViewportOffset(anchor_scene_x) * next_scale),
        NormalizeViewportOffset(screen_y) - (NormalizeViewportOffset(anchor_scene_y) * next_scale));
}

void Engine::PanViewportBy(float delta_x, float delta_y) {
    (void)ApplyViewportPan(delta_x, delta_y);
}

void Engine::BeginViewportPan(double timestamp_ms) {
    impl_->viewport_pan_active = true;
    impl_->viewport_pan_dragged = false;
    impl_->viewport_pan_momentum_active = false;
    impl_->viewport_pan_velocity_x = 0.0f;
    impl_->viewport_pan_velocity_y = 0.0f;
    impl_->has_last_viewport_pan_timestamp = IsValidTimestamp(timestamp_ms);
    impl_->last_viewport_pan_timestamp_ms = impl_->has_last_viewport_pan_timestamp ? timestamp_ms : 0.0;
}

void Engine::UpdateViewportPan(float delta_x, float delta_y, double timestamp_ms) {
    if (!impl_->viewport_pan_active) {
        BeginViewportPan(timestamp_ms);
    }
    double delta_ms = kNominalFrameMs;
    if (IsValidTimestamp(timestamp_ms)) {
        if (impl_->has_last_viewport_pan_timestamp) {
            delta_ms = ClampInputDeltaMs(timestamp_ms - impl_->last_viewport_pan_timestamp_ms);
        }
        impl_->last_viewport_pan_timestamp_ms = timestamp_ms;
        impl_->has_last_viewport_pan_timestamp = true;
    } else if (impl_->has_last_viewport_pan_timestamp) {
        impl_->last_viewport_pan_timestamp_ms += kNominalFrameMs;
    }
    const bool changed = ApplyViewportPan(delta_x, delta_y);
    impl_->viewport_pan_dragged = impl_->viewport_pan_dragged || changed || std::abs(delta_x) > 0.0f || std::abs(delta_y) > 0.0f;
    const float seconds = static_cast<float>(delta_ms / 1000.0);
    if (seconds > 0.0f) {
        impl_->viewport_pan_velocity_x = (std::isfinite(delta_x) ? delta_x : 0.0f) / seconds;
        impl_->viewport_pan_velocity_y = (std::isfinite(delta_y) ? delta_y : 0.0f) / seconds;
    }
}

void Engine::EndViewportPan(double timestamp_ms) {
    if (IsValidTimestamp(timestamp_ms)) {
        impl_->last_viewport_pan_timestamp_ms = timestamp_ms;
        impl_->has_last_viewport_pan_timestamp = true;
    }
    impl_->viewport_pan_momentum_active = impl_->viewport_pan_dragged &&
        (std::abs(impl_->viewport_pan_velocity_x) >= VelocityStopThreshold(kMomentumStopDisplacementPx) ||
         std::abs(impl_->viewport_pan_velocity_y) >= VelocityStopThreshold(kMomentumStopDisplacementPx));
    if (!impl_->viewport_pan_momentum_active) {
        impl_->viewport_pan_velocity_x = 0.0f;
        impl_->viewport_pan_velocity_y = 0.0f;
    }
    impl_->viewport_pan_active = false;
    impl_->viewport_pan_dragged = false;
}

bool Engine::TickViewportPanMomentum(double timestamp_ms) {
    if (!impl_->viewport_pan_momentum_active || impl_->viewport_pan_active) {
        return false;
    }
    double delta_ms = kNominalFrameMs;
    if (IsValidTimestamp(timestamp_ms)) {
        if (impl_->has_last_viewport_pan_timestamp) {
            delta_ms = ClampInputDeltaMs(timestamp_ms - impl_->last_viewport_pan_timestamp_ms);
        }
        impl_->last_viewport_pan_timestamp_ms = timestamp_ms;
        impl_->has_last_viewport_pan_timestamp = true;
    } else if (impl_->has_last_viewport_pan_timestamp) {
        impl_->last_viewport_pan_timestamp_ms += kNominalFrameMs;
    }
    const float frame_factor = static_cast<float>(delta_ms / kNominalFrameMs);
    float effective_friction = kDefaultViewportPanFriction;
    const float max_velocity = std::max(std::abs(impl_->viewport_pan_velocity_x), std::abs(impl_->viewport_pan_velocity_y));
    if (max_velocity < kTerminalMomentumVelocityPxPerSecond && frame_factor > 0.0f) {
        effective_friction *= kTerminalMomentumFriction;
    }
    const float decay = frame_factor <= 0.0f ? 1.0f : std::pow(effective_friction, frame_factor);
    const float nominal_seconds = static_cast<float>(kNominalFrameMs / 1000.0);
    const float displacement_factor = effective_friction > 0.0f && effective_friction < 1.0f
        ? nominal_seconds * ((1.0f - decay) / (1.0f - effective_friction))
        : static_cast<float>(delta_ms / 1000.0);
    const bool changed = ApplyViewportPan(
        impl_->viewport_pan_velocity_x * displacement_factor,
        impl_->viewport_pan_velocity_y * displacement_factor);
    impl_->viewport_pan_velocity_x *= decay;
    impl_->viewport_pan_velocity_y *= decay;
    if (!changed ||
        (std::abs(impl_->viewport_pan_velocity_x) < VelocityStopThreshold(kMomentumStopDisplacementPx) &&
         std::abs(impl_->viewport_pan_velocity_y) < VelocityStopThreshold(kMomentumStopDisplacementPx))) {
        ClearViewportPanMomentum();
    }
    return changed;
}

void Engine::ClearViewportPanMomentum() {
    impl_->viewport_pan_momentum_active = false;
    impl_->viewport_pan_active = false;
    impl_->viewport_pan_dragged = false;
    impl_->viewport_pan_velocity_x = 0.0f;
    impl_->viewport_pan_velocity_y = 0.0f;
    impl_->has_last_viewport_pan_timestamp = false;
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
    texture.pixels.assign(rgba, rgba + expected);
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    texture.raster_image = SkImages::RasterFromData(
        info,
        SkData::MakeWithCopy(texture.pixels.data(), expected),
        static_cast<size_t>(width) * 4U);
}

void Engine::RegisterTextureSubRgba(
    std::uint32_t texture_id,
    const std::uint8_t* sub_rgba,
    std::uint32_t sub_x,
    std::uint32_t sub_y,
    std::uint32_t sub_w,
    std::uint32_t sub_h,
    std::uint32_t full_w,
    std::uint32_t full_h
) {
    if (texture_id == 0 || sub_rgba == nullptr || sub_w == 0 || sub_h == 0 || full_w == 0 || full_h == 0) {
        return;
    }

    detail::TextureRecord& texture = impl_->textures[texture_id];

    // Create full-size buffer if this is a new texture
    if (texture.width == 0) {
        texture.width = full_w;
        texture.height = full_h;
        const std::size_t total_bytes = static_cast<std::size_t>(full_w) * static_cast<std::size_t>(full_h) * 4U;
        texture.pixels.assign(total_bytes, 0U);
    }

    // Clamp sub-rect to texture bounds
    const std::uint32_t clamped_x = std::min(sub_x, texture.width);
    const std::uint32_t clamped_y = std::min(sub_y, texture.height);
    const std::uint32_t clamped_w = std::min(sub_w, texture.width - clamped_x);
    const std::uint32_t clamped_h = std::min(sub_h, texture.height - clamped_y);

    if (clamped_w == 0 || clamped_h == 0) return;

    // Copy sub-row into the pixel buffer (row by row for correct stride)
    for (std::uint32_t row = 0; row < clamped_h; ++row) {
        const std::size_t src_offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(clamped_w) * 4U;
        const std::size_t dst_offset = (static_cast<std::size_t>(clamped_y + row) * static_cast<std::size_t>(texture.width) + static_cast<std::size_t>(clamped_x)) * 4U;
        std::memcpy(texture.pixels.data() + dst_offset, sub_rgba + src_offset, static_cast<std::size_t>(clamped_w) * 4U);
    }

    // Rebuild the SkImage from the updated pixel buffer
    const std::size_t total_bytes = static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height) * 4U;
    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(texture.width), static_cast<int>(texture.height),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    texture.raster_image = SkImages::RasterFromData(
        info,
        SkData::MakeWithCopy(texture.pixels.data(), total_bytes),
        static_cast<size_t>(texture.width) * 4U);
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
        node->image_sampling,
        node->image_max_aniso,
        node->has_image_nine,
        node->image_nine_texture_id,
        node->image_nine_insets,
        node->image_nine_sampling,
        node->image_nine_max_aniso,
        node->has_svg,
        node->svg_id,
        node->svg_tint_color,
        node->svg_sampling,
        node->svg_max_aniso,
        node->has_path,
        node->path_fill_color,
        node->path_stroke_color,
        node->path_stroke_width,
        node->path,
        node->has_glyph_run,
        node->glyphs_have_per_color,
        node->glyphs_have_per_style,
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
