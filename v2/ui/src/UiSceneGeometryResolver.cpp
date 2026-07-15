#include "UiSceneGeometryResolver.h"

#include <cmath>

namespace effindom::v2::ui {

void SceneGeometryResolver::Resolve(std::uint64_t root_handle) {
    deferred_portal_roots_.clear();
    ResolveNode(root_handle, 0.0f, 0.0f, 0.0f, 0.0f);
    ResolveDeferredPortals();
}

void SceneGeometryResolver::ResolveNode(
    std::uint64_t handle,
    float parent_abs_x,
    float parent_abs_y,
    float parent_scene_x,
    float parent_scene_y) {
    UINode* node = nodes_.Resolve(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return;
    }

    const float left = YGNodeLayoutGetLeft(node->yg_node);
    const float top = YGNodeLayoutGetTop(node->yg_node);
    node->abs_x = parent_abs_x + left;
    node->abs_y = parent_abs_y + top;
    node->scene_x = parent_scene_x + left;
    node->scene_y = parent_scene_y + top;
    node->layout_width = YGNodeLayoutGetWidth(node->yg_node);
    node->layout_height = YGNodeLayoutGetHeight(node->yg_node);

    if (node->visibility != UI_VISIBILITY_NORMAL) {
        return;
    }
    if (node->is_portal) {
        deferred_portal_roots_.push_back(handle);
        return;
    }
    if (node->is_grid) {
        ResolveGrid(handle, *node, node->abs_x, node->abs_y, node->scene_x, node->scene_y);
        return;
    }

    const float child_abs_x = node->abs_x - (node->is_scroll_view ? node->scroll_offset_x : 0.0f);
    const float child_abs_y = node->abs_y - (node->is_scroll_view ? node->scroll_offset_y : 0.0f);
    for (const std::uint64_t child_handle : node->children) {
        ResolveNode(child_handle, child_abs_x, child_abs_y, node->scene_x, node->scene_y);
    }
}

void SceneGeometryResolver::ResolveDeferredPortals() {
    for (std::size_t index = 0U; index < deferred_portal_roots_.size(); index += 1U) {
        UINode* portal = nodes_.Resolve(deferred_portal_roots_[index]);
        if (portal == nullptr) {
            continue;
        }
        const float child_abs_x = portal->abs_x - (portal->is_scroll_view ? portal->scroll_offset_x : 0.0f);
        const float child_abs_y = portal->abs_y - (portal->is_scroll_view ? portal->scroll_offset_y : 0.0f);
        for (const std::uint64_t child_handle : portal->children) {
            ResolveNode(child_handle, child_abs_x, child_abs_y, portal->abs_x, portal->abs_y);
        }
    }
}

} // namespace effindom::v2::ui
