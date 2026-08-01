#include "EngineInternal.h"
#include "SvgIntrinsicSize.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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

std::optional<std::size_t> RgbaByteLength(std::uint32_t width, std::uint32_t height) {
    if (width == 0U || height == 0U) return std::nullopt;
    constexpr std::size_t channels = 4U;
    const std::size_t width_value = width;
    const std::size_t height_value = height;
    if (width_value > std::numeric_limits<std::size_t>::max() / height_value) return std::nullopt;
    const std::size_t pixels = width_value * height_value;
    if (pixels > std::numeric_limits<std::size_t>::max() / channels) return std::nullopt;
    return pixels * channels;
}

} // namespace

Engine::Engine()
    : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() = default;

void Engine::SetCustomDrawCallback(CustomDrawCallback callback) {
    impl_->custom_draw_callback = std::move(callback);
}

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

void Engine::RegisterFont(
    std::uint32_t font_id,
    const std::uint8_t* bytes,
    std::uint32_t length,
    std::uint32_t face_index) {
#ifdef EFFINDOM_V2_CORE_NO_FONT_LOADING
    (void)font_id;
    (void)bytes;
    (void)length;
    (void)face_index;
    return;
#else
    if (font_id == 0 || bytes == nullptr || length == 0) {
        return;
    }
    sk_sp<SkData> data = SkData::MakeWithCopy(bytes, length);
    std::array<sk_sp<SkData>, 1> font_data = {data};
    sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(font_data.data(), font_data.size()));
    sk_sp<SkTypeface> typeface = font_mgr
        ? font_mgr->makeFromData(data, static_cast<int>(face_index))
        : nullptr;
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

void Engine::UnregisterSvg(std::uint32_t svg_id) {
    if (svg_id != 0U) impl_->svgs.erase(svg_id);
}

bool Engine::RegisterTextureRgba(
    std::uint32_t texture_id,
    const std::uint8_t* rgba,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t byte_length
) {
    if (texture_id == 0 || rgba == nullptr || width == 0 || height == 0) {
        return false;
    }
    const auto expected = RgbaByteLength(width, height);
    if (!expected.has_value() || byte_length != *expected) return false;

    detail::TextureRecord texture;
    texture.width = width;
    texture.height = height;
    texture.pixels.assign(rgba, rgba + *expected);
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    texture.raster_image = SkImages::RasterFromData(
        info,
        SkData::MakeWithCopy(texture.pixels.data(), *expected),
        static_cast<size_t>(width) * 4U);
    if (!texture.raster_image) return false;
    impl_->textures[texture_id] = std::move(texture);
    return true;
}

bool Engine::RegisterTextureSubRgba(
    std::uint32_t texture_id,
    const std::uint8_t* sub_rgba,
    std::uint32_t sub_x,
    std::uint32_t sub_y,
    std::uint32_t sub_w,
    std::uint32_t sub_h,
    std::uint32_t full_w,
    std::uint32_t full_h,
    std::size_t byte_length
) {
    if (texture_id == 0 || sub_rgba == nullptr || sub_w == 0 || sub_h == 0 || full_w == 0 || full_h == 0) {
        return false;
    }
    const auto sub_bytes = RgbaByteLength(sub_w, sub_h);
    const auto full_bytes = RgbaByteLength(full_w, full_h);
    if (!sub_bytes.has_value() || !full_bytes.has_value() || byte_length != *sub_bytes) return false;
    if (sub_x >= full_w || sub_y >= full_h || sub_w > full_w - sub_x || sub_h > full_h - sub_y) return false;

    detail::TextureRecord texture;
    const auto existing = impl_->textures.find(texture_id);
    if (existing == impl_->textures.end()) {
        texture.width = full_w;
        texture.height = full_h;
        texture.pixels.assign(*full_bytes, 0U);
    } else {
        if (existing->second.width != full_w || existing->second.height != full_h) return false;
        texture = existing->second;
    }

    for (std::uint32_t row = 0; row < sub_h; ++row) {
        const std::size_t src_offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(sub_w) * 4U;
        const std::size_t dst_offset =
            (static_cast<std::size_t>(sub_y + row) * static_cast<std::size_t>(full_w) + sub_x) * 4U;
        std::memcpy(
            texture.pixels.data() + dst_offset,
            sub_rgba + src_offset,
            static_cast<std::size_t>(sub_w) * 4U);
    }

    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(full_w), static_cast<int>(full_h),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    texture.raster_image = SkImages::RasterFromData(
        info,
        SkData::MakeWithCopy(texture.pixels.data(), *full_bytes),
        static_cast<size_t>(full_w) * 4U);
    if (!texture.raster_image) return false;
    impl_->textures[texture_id] = std::move(texture);
    return true;
}

bool Engine::UnregisterTexture(std::uint32_t texture_id) {
    return texture_id != 0U && impl_->textures.erase(texture_id) != 0U;
}

bool Engine::HasFontForTesting(std::uint32_t font_id) const {
    return impl_->fonts.find(font_id) != impl_->fonts.end();
}

bool Engine::FontHasGlyphForTesting(std::uint32_t font_id, std::uint32_t codepoint) const {
    const auto found = impl_->fonts.find(font_id);
    return found != impl_->fonts.end() && found->second != nullptr &&
        found->second->unicharToGlyph(static_cast<SkUnichar>(codepoint)) != 0U;
}

std::optional<std::pair<float, float>> Engine::GetSvgSizeForTesting(std::uint32_t svg_id) const {
    const auto found = impl_->svgs.find(svg_id);
    if (found == impl_->svgs.end()) return std::nullopt;
    return std::pair<float, float>{found->second.intrinsic_width, found->second.intrinsic_height};
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> Engine::GetTextureSizeForTesting(
    std::uint32_t texture_id) const {
    const auto found = impl_->textures.find(texture_id);
    if (found == impl_->textures.end()) return std::nullopt;
    return std::pair<std::uint32_t, std::uint32_t>{found->second.width, found->second.height};
}

std::size_t Engine::TextureCountForTesting() const { return impl_->textures.size(); }

std::size_t Engine::PathCountForTesting() const { return impl_->paths.size(); }

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

GlyphRenderStats Engine::GetGlyphRenderStatsForTesting() const {
    return impl_->glyph_render_stats;
}

void Engine::ClearGlyphRenderStatsForTesting() {
    impl_->glyph_render_stats = GlyphRenderStats{};
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
