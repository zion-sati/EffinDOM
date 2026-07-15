#include "UiNodeVisualEncoder.h"

#include "CommandBuilder.h"
#include "effindom.h"

namespace effindom::v2::ui {

void NodeVisualEncoder::EmitBounds(
    std::uint64_t handle,
    const UINode& node,
    float abs_x,
    float abs_y,
    float scene_x,
    float scene_y,
    float width,
    float height,
    const Rect& visible_bounds,
    bool interactive,
    CommandBuilder& builder) const {
    const Rect clip_bounds = (node.clip_to_bounds || node.is_scroll_view)
        ? visibility_.ComputeClipBounds(node, scene_x, scene_y)
        : Rect{scene_x, scene_y, width, height};
    builder.SetBounds(
        handle,
        scene_x,
        scene_y,
        width,
        height,
        visible_bounds.width > 0.0f ? visible_bounds.x : abs_x,
        visible_bounds.height > 0.0f ? visible_bounds.y : abs_y,
        visible_bounds.width,
        visible_bounds.height,
        clip_bounds.x,
        clip_bounds.y,
        clip_bounds.width,
        clip_bounds.height,
        interactive,
        node.is_scroll_view ? ED_CLIP_MODE_STRICT_CONTENT : ED_CLIP_MODE_RASTER_SAFE_VISUAL);
}

void NodeVisualEncoder::EmitStyle(
    std::uint64_t handle,
    const UINode& node,
    CommandBuilder& builder) const {
    if (node.has_box_style || node.bg_color != 0U) {
        builder.SetBoxStyle(
            handle, node.bg_color,
            node.corner_radius_tl, node.corner_radius_tr, node.corner_radius_br, node.corner_radius_bl,
            node.border_width, node.border_color, node.border_style, node.border_dash_on, node.border_dash_off);
    }
    if (node.has_layer_effect) {
        builder.SetLayerEffect(handle, node.opacity, node.blur_sigma, node.blend_mode);
    }
    if (node.has_drop_shadow) {
        builder.SetDropShadow(
            handle, node.drop_shadow_color, node.drop_shadow_offset_x, node.drop_shadow_offset_y,
            node.drop_shadow_blur_sigma, node.drop_shadow_spread);
    }
    if (node.has_background_blur) {
        builder.SetBackgroundBlur(handle, node.background_blur_sigma);
    }
    if (node.has_linear_gradient && !node.gradient_stops.empty()) {
        builder.SetLinearGradient(
            handle, node.gradient_start_x, node.gradient_start_y,
            node.gradient_end_x, node.gradient_end_y, node.gradient_stops);
    }
    if (node.has_image) {
        builder.SetImage(handle, node.texture_id, node.object_fit, node.image_sampling, node.image_max_aniso);
    }
    if (node.has_image_nine) {
        builder.SetImageNine(
            handle, node.image_nine_texture_id,
            node.image_nine_inset_left, node.image_nine_inset_top,
            node.image_nine_inset_right, node.image_nine_inset_bottom,
            node.image_nine_sampling, node.image_nine_max_aniso);
    }
    if (node.is_svg_node || node.has_svg) {
        builder.SetSvg(
            handle, node.has_svg ? node.svg_id : 0U,
            node.svg_tint_color, node.svg_sampling, node.svg_max_aniso);
    }
}

} // namespace effindom::v2::ui
