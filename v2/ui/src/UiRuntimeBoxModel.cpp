#include "UiRuntime.h"

#include <algorithm>

namespace effindom::v2::ui {

namespace {

float ReadLayoutBorder(const UINode& node, YGEdge edge) {
    if (node.yg_node == nullptr) {
        return 0.0f;
    }
    return std::max(0.0f, YGNodeLayoutGetBorder(node.yg_node, edge));
}

float ReadLayoutPadding(const UINode& node, YGEdge edge) {
    if (node.yg_node == nullptr) {
        return 0.0f;
    }
    return std::max(0.0f, YGNodeLayoutGetPadding(node.yg_node, edge));
}

} // namespace

UiRuntime::EdgeInsets UiRuntime::ComputeBorderInsets(const UINode& node) const {
    return EdgeInsets{
        ReadLayoutBorder(node, YGEdgeLeft),
        ReadLayoutBorder(node, YGEdgeTop),
        ReadLayoutBorder(node, YGEdgeRight),
        ReadLayoutBorder(node, YGEdgeBottom),
    };
}

UiRuntime::EdgeInsets UiRuntime::ComputePaddingInsets(const UINode& node) const {
    return EdgeInsets{
        ReadLayoutPadding(node, YGEdgeLeft),
        ReadLayoutPadding(node, YGEdgeTop),
        ReadLayoutPadding(node, YGEdgeRight),
        ReadLayoutPadding(node, YGEdgeBottom),
    };
}

UiRuntime::EdgeInsets UiRuntime::ComputeContentInsets(const UINode& node) const {
    const EdgeInsets border = ComputeBorderInsets(node);
    const EdgeInsets padding = ComputePaddingInsets(node);
    return EdgeInsets{
        border.left + padding.left,
        border.top + padding.top,
        border.right + padding.right,
        border.bottom + padding.bottom,
    };
}

Rect UiRuntime::ComputeBorderBounds(const UINode& node, float origin_x, float origin_y) const {
    return Rect{
        origin_x,
        origin_y,
        std::max(0.0f, node.layout_width),
        std::max(0.0f, node.layout_height),
    };
}

Rect UiRuntime::ComputeContentBounds(const UINode& node, float origin_x, float origin_y) const {
    const EdgeInsets insets = ComputeContentInsets(node);
    const float width = std::max(0.0f, node.layout_width - insets.left - insets.right);
    const float height = std::max(0.0f, node.layout_height - insets.top - insets.bottom);
    return Rect{
        origin_x + insets.left,
        origin_y + insets.top,
        width,
        height,
    };
}

Rect UiRuntime::ComputeScrollViewportBounds(const UINode& node, float origin_x, float origin_y) const {
    return ComputeContentBounds(node, origin_x, origin_y);
}

Rect UiRuntime::ComputeTextContentBounds(const UINode& node) const {
    return ComputeContentBounds(node, 0.0f, 0.0f);
}

float UiRuntime::GetScrollViewportWidth(const UINode& node) const {
    return ComputeScrollViewportBounds(node, 0.0f, 0.0f).width;
}

float UiRuntime::GetScrollViewportHeight(const UINode& node) const {
    return ComputeScrollViewportBounds(node, 0.0f, 0.0f).height;
}

Rect UiRuntime::ComputeClipBounds(const UINode& node) const {
    return ComputeClipBounds(node, node.abs_x, node.abs_y);
}

Rect UiRuntime::ComputeClipBounds(const UINode& node, float origin_x, float origin_y) const {
    if (node.is_scroll_view || node.clip_to_bounds) {
        return ComputeContentBounds(node, origin_x, origin_y);
    }
    return ComputeBorderBounds(node, origin_x, origin_y);
}

std::uint32_t UiRuntime::ComputeClipMode(const UINode& node) const {
    return node.is_scroll_view ? ED_CLIP_MODE_STRICT_CONTENT : ED_CLIP_MODE_RASTER_SAFE_VISUAL;
}

} // namespace effindom::v2::ui
