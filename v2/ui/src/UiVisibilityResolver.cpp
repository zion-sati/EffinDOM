#include "UiVisibilityResolver.h"

#include <algorithm>

namespace effindom::v2::ui {

Rect VisibilityResolver::ComputeContentBounds(const UINode& node, float origin_x, float origin_y) {
    const float border_left = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetBorder(node.yg_node, YGEdgeLeft));
    const float border_top = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetBorder(node.yg_node, YGEdgeTop));
    const float border_right = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetBorder(node.yg_node, YGEdgeRight));
    const float border_bottom = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetBorder(node.yg_node, YGEdgeBottom));
    const float padding_left = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetPadding(node.yg_node, YGEdgeLeft));
    const float padding_top = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetPadding(node.yg_node, YGEdgeTop));
    const float padding_right = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetPadding(node.yg_node, YGEdgeRight));
    const float padding_bottom = node.yg_node == nullptr ? 0.0f : std::max(0.0f, YGNodeLayoutGetPadding(node.yg_node, YGEdgeBottom));
    const float left = border_left + padding_left;
    const float top = border_top + padding_top;
    const float right = border_right + padding_right;
    const float bottom = border_bottom + padding_bottom;
    return Rect{
        origin_x + left,
        origin_y + top,
        std::max(0.0f, node.layout_width - left - right),
        std::max(0.0f, node.layout_height - top - bottom),
    };
}

Rect VisibilityResolver::ComputeClipBounds(const UINode& node) const {
    return ComputeClipBounds(node, node.abs_x, node.abs_y);
}

Rect VisibilityResolver::ComputeClipBounds(const UINode& node, float origin_x, float origin_y) const {
    if (node.is_scroll_view || node.clip_to_bounds) {
        return ComputeContentBounds(node, origin_x, origin_y);
    }
    return Rect{
        origin_x,
        origin_y,
        std::max(0.0f, node.layout_width),
        std::max(0.0f, node.layout_height),
    };
}

bool VisibilityResolver::Intersect(Rect& bounds, const Rect& clip) {
    const float left = std::max(bounds.x, clip.x);
    const float top = std::max(bounds.y, clip.y);
    const float right = std::min(bounds.x + bounds.width, clip.x + clip.width);
    const float bottom = std::min(bounds.y + bounds.height, clip.y + clip.height);
    bounds.x = left;
    bounds.y = top;
    bounds.width = std::max(0.0f, right - left);
    bounds.height = std::max(0.0f, bottom - top);
    return bounds.width > 0.0f && bounds.height > 0.0f;
}

Rect VisibilityResolver::ClipToAncestors(Rect bounds, std::uint64_t first_ancestor_handle) const {
    for (std::uint64_t current = first_ancestor_handle; current != UI_INVALID_HANDLE;) {
        const UINode* ancestor = nodes_.Resolve(current);
        if (ancestor == nullptr) {
            break;
        }
        if ((ancestor->clip_to_bounds || ancestor->is_scroll_view) &&
            !Intersect(bounds, ComputeClipBounds(*ancestor))) {
            break;
        }
        if (ancestor->is_portal) {
            break;
        }
        current = ancestor->parent_handle;
    }
    return bounds;
}

bool VisibilityResolver::TryGetMultilineTextboxViewportBounds(const UINode& node, Rect& bounds) const {
    const UINode* parent = nodes_.Resolve(node.parent_handle);
    if (parent == nullptr || !parent->is_scroll_view) {
        return false;
    }
    bounds = ClipToAncestors(ComputeClipBounds(*parent), parent->parent_handle);
    return true;
}

} // namespace effindom::v2::ui
