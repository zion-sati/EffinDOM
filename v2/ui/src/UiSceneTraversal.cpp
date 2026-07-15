#include "UiSceneTraversal.h"

#include "CommandBuilder.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

namespace effindom::v2::ui {

namespace {

bool IntersectRect(Rect& rect, const Rect& clip) {
    const float left = std::max(rect.x, clip.x);
    const float top = std::max(rect.y, clip.y);
    const float right = std::min(rect.x + rect.width, clip.x + clip.width);
    const float bottom = std::min(rect.y + rect.height, clip.y + clip.height);
    rect.x = left;
    rect.y = top;
    rect.width = std::max(0.0f, right - left);
    rect.height = std::max(0.0f, bottom - top);
    return rect.width > 0.0f && rect.height > 0.0f;
}

} // namespace

void SceneTraversal::ResetTextRenderWindow(UINode& node) const {
    if (!node.is_text_node) {
        return;
    }
    node.text_render_window_valid = false;
    node.text_render_line_start = 0U;
    node.text_render_line_end = 0U;
    node.nonwrap_render_fragment_window_valid = false;
    node.nonwrap_render_fragment_start = 0U;
    node.nonwrap_render_fragment_end = 0U;
}

Rect SceneTraversal::ComputeVisibleBounds(
    const UINode& node,
    float abs_x,
    float abs_y,
    float width,
    float height) const {
    Rect visible_bounds{abs_x, abs_y, width, height};
    if (width <= 0.0f || height <= 0.0f) {
        visible_bounds.width = 0.0f;
        visible_bounds.height = 0.0f;
        return visible_bounds;
    }
    for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
        const UINode* parent = nodes_.Resolve(current);
        if (parent == nullptr) {
            break;
        }
        if ((parent->clip_to_bounds || parent->is_scroll_view) &&
            !IntersectRect(visible_bounds, visibility_.ComputeClipBounds(*parent))) {
            break;
        }
        if (parent->is_portal) {
            break;
        }
        current = parent->parent_handle;
    }
    return visible_bounds;
}

void SceneTraversal::EmitSceneAndChildren(
    std::uint64_t handle,
    UINode& node,
    float abs_x,
    float abs_y,
    float scene_x,
    float scene_y,
    bool scroll_dirty,
    CommandBuilder& builder,
    std::vector<std::uint64_t>& paint_order,
    std::vector<SceneInstruction>& scene,
    std::vector<std::uint64_t>& deferred_portal_roots) {
    paint_order.push_back(handle);
    if (node.is_custom_drawable) {
        if (node.has_layer_effect) {
            scene.push_back(SceneInstruction{OP_PUSH_LAYER, handle});
        }
        scene.push_back(SceneInstruction{OP_PUSH_TRANSLATE, handle, scene_x, scene_y});
        scene.push_back(SceneInstruction{OP_DRAW_CUSTOM, handle});
        scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
        if (node.has_layer_effect) {
            scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
        }
    } else {
        scene.push_back(SceneInstruction{OP_DRAW_NODE, handle});
    }
    const bool clip_children = node.clip_to_bounds || node.is_scroll_view;
    if (node.is_portal) {
        deferred_portal_roots.push_back(handle);
        return;
    }
    if (node.is_grid) {
        if (clip_children) {
            scene.push_back(SceneInstruction{OP_PUSH_CLIP, handle});
        }
        LayoutGrid(
            handle, node, abs_x, abs_y, scene_x, scene_y, scroll_dirty,
            builder, paint_order, scene, deferred_portal_roots);
        if (clip_children) {
            scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
        }
        node.scroll_offset_dirty = false;
        return;
    }
    const float child_origin_x = abs_x - (node.is_scroll_view ? node.scroll_offset_x : 0.0f);
    const float child_origin_y = abs_y - (node.is_scroll_view ? node.scroll_offset_y : 0.0f);
    if (clip_children) {
        scene.push_back(SceneInstruction{OP_PUSH_CLIP, handle});
    }
    const bool translated = node.is_scroll_view &&
        (std::abs(node.scroll_offset_x) >= 0.001f || std::abs(node.scroll_offset_y) >= 0.001f);
    if (translated) {
        scene.push_back(SceneInstruction{
            OP_PUSH_TRANSLATE, handle, -node.scroll_offset_x, -node.scroll_offset_y});
    }
    for (const std::uint64_t child_handle : node.children) {
        Walk(
            child_handle, child_origin_x, child_origin_y, scene_x, scene_y, scroll_dirty,
            builder, paint_order, scene, deferred_portal_roots);
    }
    if (translated) {
        scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
    }
    if (clip_children) {
        scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
    }
    node.scroll_offset_dirty = false;
}

void SceneTraversal::Walk(
    std::uint64_t handle,
    float parent_abs_x,
    float parent_abs_y,
    float parent_scene_x,
    float parent_scene_y,
    bool inherited_scroll_dirty,
    CommandBuilder& builder,
    std::vector<std::uint64_t>& paint_order,
    std::vector<SceneInstruction>& scene,
    std::vector<std::uint64_t>& deferred_portal_roots) {
    UINode* node = writer_.Resolve(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return;
    }

    const float abs_x = parent_abs_x + YGNodeLayoutGetLeft(node->yg_node);
    const float abs_y = parent_abs_y + YGNodeLayoutGetTop(node->yg_node);
    const float scene_x = parent_scene_x + YGNodeLayoutGetLeft(node->yg_node);
    const float scene_y = parent_scene_y + YGNodeLayoutGetTop(node->yg_node);
    const float width = YGNodeLayoutGetWidth(node->yg_node);
    const float height = YGNodeLayoutGetHeight(node->yg_node);
    node->abs_x = abs_x;
    node->abs_y = abs_y;
    node->scene_x = scene_x;
    node->scene_y = scene_y;
    node->layout_width = width;
    node->layout_height = height;
    if (node->visibility != UI_VISIBILITY_NORMAL) {
        ResetTextRenderWindow(*node);
        node->scroll_offset_dirty = false;
        return;
    }
    Rect visible_bounds = ComputeVisibleBounds(*node, abs_x, abs_y, width, height);
    const bool emit_layout_updates = layout_dirty_;
    const bool scroll_dirty = inherited_scroll_dirty || node->scroll_offset_dirty;
    TextPaintEncoder::WalkTextState walk_text = text_encoder_.Prepare(
        *node, visible_bounds, abs_x, abs_y, width, height, emit_layout_updates, scroll_dirty);
    const bool needs_content_update =
        node->is_text_node
        ? (emit_layout_updates ||
           node->is_dirty ||
           (scroll_dirty && (walk_text.render_window_changed || walk_text.fragment_window_changed)))
        : (emit_layout_updates || node->is_dirty);
    const bool selection_visuals_only_update =
        node->is_text_node &&
        node->text_selection_visuals_dirty &&
        !node->text_glyphs_dirty &&
        !emit_layout_updates &&
        !walk_text.render_window_changed &&
        !walk_text.fragment_window_changed;
    const bool needs_bounds_update =
        (needs_content_update && !selection_visuals_only_update) ||
        (scroll_dirty && node->is_interactive);

    if (visible_bounds.width <= 0.0f || visible_bounds.height <= 0.0f) {
        ResetTextRenderWindow(*node);
        paint_order.push_back(handle);
        if (node->is_interactive) {
            visual_encoder_.EmitBounds(
                handle, *node, abs_x, abs_y, scene_x, scene_y, width, height,
                Rect{abs_x, abs_y, 0.0f, 0.0f}, true, builder);
        }
        if (node->is_portal) {
            deferred_portal_roots.push_back(handle);
            node->scroll_offset_dirty = false;
            return;
        }
        if (!node->is_portal) {
            const float child_origin_x = abs_x - (node->is_scroll_view ? node->scroll_offset_x : 0.0f);
            const float child_origin_y = abs_y - (node->is_scroll_view ? node->scroll_offset_y : 0.0f);
            for (const std::uint64_t child_handle : node->children) {
                ClearCulledSubtree(child_handle, child_origin_x, child_origin_y, scene_x, scene_y, builder, paint_order);
            }
        }
        node->scroll_offset_dirty = false;
        return;
    }

    if (needs_bounds_update) {
        visual_encoder_.EmitBounds(
            handle, *node, abs_x, abs_y, scene_x, scene_y, width, height,
            visible_bounds, node->is_interactive, builder);
    }
    if (needs_content_update) {
        if (!selection_visuals_only_update) {
            visual_encoder_.EmitStyle(handle, *node, builder);
        }

        text_encoder_.Emit(
            handle, *node, walk_text, visible_bounds, abs_x, abs_y, scene_x, scene_y,
            selection_visuals_only_update, builder);
        node->is_dirty = false;
    }

    EmitSceneAndChildren(handle, *node, abs_x, abs_y, scene_x, scene_y, scroll_dirty, builder, paint_order, scene, deferred_portal_roots);
}


void SceneTraversal::WalkDeferredPortals(
    CommandBuilder& builder,
    std::vector<std::uint64_t>& paint_order,
    std::vector<SceneInstruction>& scene,
    std::vector<std::uint64_t>& deferred_portal_roots) {
    for (std::size_t index = 0; index < deferred_portal_roots.size(); index += 1U) {
        UINode* portal = writer_.Resolve(deferred_portal_roots[index]);
        if (portal != nullptr) {
            const float child_origin_x = portal->abs_x - (portal->is_scroll_view ? portal->scroll_offset_x : 0.0f);
            const float child_origin_y = portal->abs_y - (portal->is_scroll_view ? portal->scroll_offset_y : 0.0f);
            const float child_scene_x = portal->abs_x;
            const float child_scene_y = portal->abs_y;
            const bool inherited_scroll_dirty =
                portal->scroll_offset_dirty ||
                std::abs(portal->abs_x - portal->scene_x) >= 0.001f ||
                std::abs(portal->abs_y - portal->scene_y) >= 0.001f;
            if (portal->clip_to_bounds || portal->is_scroll_view) {
                scene.push_back(SceneInstruction{OP_PUSH_CLIP, deferred_portal_roots[index]});
            }
            if (portal->is_scroll_view &&
                (std::abs(portal->scroll_offset_x) >= 0.001f || std::abs(portal->scroll_offset_y) >= 0.001f)) {
                scene.push_back(SceneInstruction{
                    OP_PUSH_TRANSLATE,
                    deferred_portal_roots[index],
                    -portal->scroll_offset_x,
                    -portal->scroll_offset_y,
                });
            }
            for (const std::uint64_t child_handle : portal->children) {
                Walk(
                    child_handle,
                    child_origin_x,
                    child_origin_y,
                    child_scene_x,
                    child_scene_y,
                    inherited_scroll_dirty,
                    builder,
                    paint_order,
                    scene,
                    deferred_portal_roots);
            }
            if (portal->is_scroll_view &&
                (std::abs(portal->scroll_offset_x) >= 0.001f || std::abs(portal->scroll_offset_y) >= 0.001f)) {
                scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
            }
            if (portal->clip_to_bounds || portal->is_scroll_view) {
                scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
            }
            portal->scroll_offset_dirty = false;
        }
    }
}

void SceneTraversal::ClearCulledSubtree(
    std::uint64_t handle,
    float parent_abs_x,
    float parent_abs_y,
    float parent_scene_x,
    float parent_scene_y,
    CommandBuilder& builder,
    std::vector<std::uint64_t>& paint_order) {
    UINode* node = writer_.Resolve(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return;
    }

    const float abs_x = parent_abs_x + YGNodeLayoutGetLeft(node->yg_node);
    const float abs_y = parent_abs_y + YGNodeLayoutGetTop(node->yg_node);
    const float scene_x = parent_scene_x + YGNodeLayoutGetLeft(node->yg_node);
    const float scene_y = parent_scene_y + YGNodeLayoutGetTop(node->yg_node);
    const float width = YGNodeLayoutGetWidth(node->yg_node);
    const float height = YGNodeLayoutGetHeight(node->yg_node);
    node->abs_x = abs_x;
    node->abs_y = abs_y;
    node->scene_x = scene_x;
    node->scene_y = scene_y;
    node->layout_width = width;
    node->layout_height = height;
    if (node->visibility != UI_VISIBILITY_NORMAL) {
        node->scroll_offset_dirty = false;
        return;
    }
    paint_order.push_back(handle);

    if (node->is_interactive) {
        const Rect clip_bounds = (node->clip_to_bounds || node->is_scroll_view)
            ? visibility_.ComputeClipBounds(*node, scene_x, scene_y)
            : Rect{scene_x, scene_y, width, height};
        builder.SetBounds(
            handle,
            scene_x,
            scene_y,
            width,
            height,
            abs_x,
            abs_y,
            0.0f,
            0.0f,
            clip_bounds.x,
            clip_bounds.y,
            clip_bounds.width,
            clip_bounds.height,
            true);
    }

    if (node->is_portal) {
        node->scroll_offset_dirty = false;
        return;
    }

    const float child_origin_x = abs_x - (node->is_scroll_view ? node->scroll_offset_x : 0.0f);
    const float child_origin_y = abs_y - (node->is_scroll_view ? node->scroll_offset_y : 0.0f);
    for (const std::uint64_t child_handle : node->children) {
        ClearCulledSubtree(child_handle, child_origin_x, child_origin_y, scene_x, scene_y, builder, paint_order);
    }
    node->scroll_offset_dirty = false;
}


} // namespace effindom::v2::ui
