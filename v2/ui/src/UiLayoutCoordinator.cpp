#include "UiLayoutCoordinator.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace effindom::v2::ui {

namespace {

constexpr std::uint32_t kMaxLayoutStabilizationPasses = 4U;

bool IsHorizontalFlexDirection(YGFlexDirection direction) {
    return direction == YGFlexDirectionRow || direction == YGFlexDirectionRowReverse;
}

double ElapsedMilliseconds(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

void LayoutCoordinator::ApplyLayoutStyles(std::uint64_t handle, std::uint64_t parent_handle) {
    UINode* node = writer_.Resolve(handle);
    if (node == nullptr || node->yg_node == nullptr) return;

    if (node->has_width) {
        switch (node->width_unit) {
        case UI_SIZE_UNIT_PIXEL: YGNodeStyleSetWidth(node->yg_node, node->width); break;
        case UI_SIZE_UNIT_AUTO: YGNodeStyleSetWidthAuto(node->yg_node); break;
        case UI_SIZE_UNIT_PERCENT: YGNodeStyleSetWidthPercent(node->yg_node, node->width); break;
        default: YGNodeStyleSetWidthAuto(node->yg_node); break;
        }
    } else {
        YGNodeStyleSetWidthAuto(node->yg_node);
    }

    if (node->has_height) {
        switch (node->height_unit) {
        case UI_SIZE_UNIT_PIXEL: YGNodeStyleSetHeight(node->yg_node, node->height); break;
        case UI_SIZE_UNIT_AUTO: YGNodeStyleSetHeightAuto(node->yg_node); break;
        case UI_SIZE_UNIT_PERCENT: YGNodeStyleSetHeightPercent(node->yg_node, node->height); break;
        default: YGNodeStyleSetHeightAuto(node->yg_node); break;
        }
    } else {
        YGNodeStyleSetHeightAuto(node->yg_node);
    }

    if (node->has_flex_basis) YGNodeStyleSetFlexBasis(node->yg_node, node->flex_basis);
    else YGNodeStyleSetFlexBasisAuto(node->yg_node);
    YGNodeStyleSetFlexGrow(node->yg_node, 0.0f);

    YGNodeStyleSetAlignSelf(node->yg_node, YGAlignAuto);
    if (node->has_align_self) {
        switch (node->align_self) {
        case UI_ALIGN_SELF_AUTO: YGNodeStyleSetAlignSelf(node->yg_node, YGAlignAuto); break;
        case UI_ALIGN_SELF_START: YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexStart); break;
        case UI_ALIGN_SELF_CENTER: YGNodeStyleSetAlignSelf(node->yg_node, YGAlignCenter); break;
        case UI_ALIGN_SELF_END: YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexEnd); break;
        case UI_ALIGN_SELF_STRETCH: YGNodeStyleSetAlignSelf(node->yg_node, YGAlignStretch); break;
        default: YGNodeStyleSetAlignSelf(node->yg_node, YGAlignAuto); break;
        }
    }

    if (parent_handle == UI_INVALID_HANDLE) {
        if (node->fill_width) YGNodeStyleSetWidthPercent(node->yg_node, 100.0f);
        else if (node->has_fill_width_percent) YGNodeStyleSetWidthPercent(node->yg_node, node->fill_width_percent);
        if (node->fill_height) YGNodeStyleSetHeightPercent(node->yg_node, 100.0f);
        else if (node->has_fill_height_percent) YGNodeStyleSetHeightPercent(node->yg_node, node->fill_height_percent);
    } else {
        const UINode* parent = nodes_.Resolve(parent_handle);
        if (parent != nullptr && parent->yg_node != nullptr) {
            const bool parent_is_horizontal = IsHorizontalFlexDirection(YGNodeStyleGetFlexDirection(parent->yg_node));
            const bool auto_sized_width = !node->fill_width && !node->has_fill_width_percent &&
                (!node->has_width || node->width_unit == UI_SIZE_UNIT_AUTO);
            const bool auto_sized_height = !node->fill_height && !node->has_fill_height_percent &&
                (!node->has_height || node->height_unit == UI_SIZE_UNIT_AUTO);
            if (parent->is_scroll_view && !node->has_align_self) {
                const bool cross_axis_auto_sized = parent_is_horizontal ? auto_sized_height : auto_sized_width;
                const bool child_is_horizontal = IsHorizontalFlexDirection(YGNodeStyleGetFlexDirection(node->yg_node));
                if (cross_axis_auto_sized && child_is_horizontal != parent_is_horizontal) {
                    YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexStart);
                }
            }
            if (!parent->is_scroll_view && !node->has_align_self && !node->children.empty()) {
                const bool cross_axis_explicit_auto = parent_is_horizontal
                    ? (node->has_height && node->height_unit == UI_SIZE_UNIT_AUTO)
                    : (node->has_width && node->width_unit == UI_SIZE_UNIT_AUTO);
                if (cross_axis_explicit_auto) YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexStart);
            }
            if (node->has_fill_width_percent || (node->fill_width && parent_is_horizontal)) {
                if (node->has_resolved_fill_width) YGNodeStyleSetWidth(node->yg_node, node->resolved_fill_width);
                else YGNodeStyleSetWidthAuto(node->yg_node);
            }
            if (node->has_fill_height_percent || (node->fill_height && !parent_is_horizontal)) {
                if (node->has_resolved_fill_height) YGNodeStyleSetHeight(node->yg_node, node->resolved_fill_height);
                else YGNodeStyleSetHeightAuto(node->yg_node);
            }
            if (node->fill_width && !parent_is_horizontal) YGNodeStyleSetAlignSelf(node->yg_node, YGAlignStretch);
            if (node->fill_height && parent_is_horizontal) YGNodeStyleSetAlignSelf(node->yg_node, YGAlignStretch);
        }
    }

    for (const std::uint64_t child_handle : node->children) ApplyLayoutStyles(child_handle, handle);
}

float LayoutCoordinator::ComputeFillAxisAvailableSpace(
    const UINode& node,
    const UINode* parent,
    bool width_axis,
    bool parent_is_horizontal,
    float window_width,
    float window_height) const {
    if (parent == nullptr || parent->yg_node == nullptr) return width_axis ? window_width : window_height;

    const float parent_content_width = std::max(0.0f, YGNodeLayoutGetWidth(parent->yg_node) -
        YGNodeLayoutGetBorder(parent->yg_node, YGEdgeLeft) - YGNodeLayoutGetBorder(parent->yg_node, YGEdgeRight) -
        YGNodeLayoutGetPadding(parent->yg_node, YGEdgeLeft) - YGNodeLayoutGetPadding(parent->yg_node, YGEdgeRight));
    const float parent_content_height = std::max(0.0f, YGNodeLayoutGetHeight(parent->yg_node) -
        YGNodeLayoutGetBorder(parent->yg_node, YGEdgeTop) - YGNodeLayoutGetBorder(parent->yg_node, YGEdgeBottom) -
        YGNodeLayoutGetPadding(parent->yg_node, YGEdgeTop) - YGNodeLayoutGetPadding(parent->yg_node, YGEdgeBottom));
    float available = width_axis ? parent_content_width : parent_content_height;
    const bool main_axis = width_axis == parent_is_horizontal;

    if (main_axis) {
        for (const std::uint64_t sibling_handle : parent->children) {
            const UINode* sibling = nodes_.Resolve(sibling_handle);
            if (sibling == nullptr || sibling->yg_node == nullptr || sibling == &node ||
                YGNodeStyleGetPositionType(sibling->yg_node) == YGPositionTypeAbsolute) continue;
            const bool sibling_uses_main_axis_available_space = width_axis
                ? (sibling->fill_width || sibling->has_fill_width_percent || (sibling->has_width && sibling->width_unit == UI_SIZE_UNIT_PERCENT))
                : (sibling->fill_height || sibling->has_fill_height_percent || (sibling->has_height && sibling->height_unit == UI_SIZE_UNIT_PERCENT));
            if (sibling_uses_main_axis_available_space) continue;
            if (width_axis) available -= YGNodeLayoutGetWidth(sibling->yg_node) +
                YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeLeft) + YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeRight);
            else available -= YGNodeLayoutGetHeight(sibling->yg_node) +
                YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeTop) + YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeBottom);
        }
    }

    if (width_axis) available -= YGNodeLayoutGetMargin(node.yg_node, YGEdgeLeft) + YGNodeLayoutGetMargin(node.yg_node, YGEdgeRight);
    else available -= YGNodeLayoutGetMargin(node.yg_node, YGEdgeTop) + YGNodeLayoutGetMargin(node.yg_node, YGEdgeBottom);
    return std::max(0.0f, available);
}

bool LayoutCoordinator::ResolveFillPercentLayout(
    std::uint64_t handle,
    std::uint64_t parent_handle,
    float window_width,
    float window_height) {
    UINode* node = writer_.Resolve(handle);
    if (node == nullptr || node->yg_node == nullptr) return false;

    bool changed = false;
    const UINode* parent = parent_handle == UI_INVALID_HANDLE ? nullptr : nodes_.Resolve(parent_handle);
    const bool parent_is_horizontal = parent != nullptr && parent->yg_node != nullptr &&
        IsHorizontalFlexDirection(YGNodeStyleGetFlexDirection(parent->yg_node));
    if (parent_handle != UI_INVALID_HANDLE && (node->has_fill_width_percent || (node->fill_width && parent_is_horizontal))) {
        const float available = ComputeFillAxisAvailableSpace(*node, parent, true, parent_is_horizontal, window_width, window_height);
        const float resolved = available * ((node->fill_width ? 100.0f : node->fill_width_percent) / 100.0f);
        if (!node->has_resolved_fill_width || std::abs(node->resolved_fill_width - resolved) > 0.01f) {
            YGNodeStyleSetWidth(node->yg_node, resolved);
            node->resolved_fill_width = resolved;
            node->has_resolved_fill_width = true;
            changed = true;
        }
    }
    if (parent_handle != UI_INVALID_HANDLE && (node->has_fill_height_percent || (node->fill_height && !parent_is_horizontal))) {
        const float available = ComputeFillAxisAvailableSpace(*node, parent, false, parent_is_horizontal, window_width, window_height);
        const float resolved = available * ((node->fill_height ? 100.0f : node->fill_height_percent) / 100.0f);
        if (!node->has_resolved_fill_height || std::abs(node->resolved_fill_height - resolved) > 0.01f) {
            YGNodeStyleSetHeight(node->yg_node, resolved);
            node->resolved_fill_height = resolved;
            node->has_resolved_fill_height = true;
            changed = true;
        }
    }

    for (const std::uint64_t child_handle : node->children) {
        changed = ResolveFillPercentLayout(child_handle, handle, window_width, window_height) || changed;
    }
    return changed;
}

LayoutResult LayoutCoordinator::Update(
    std::uint64_t root_handle,
    float window_width,
    float window_height,
    bool& layout_dirty) {
    LayoutResult result{};
    if (!layout_dirty) {
        const auto scroll_start = std::chrono::steady_clock::now();
        scrolling_.ApplyPendingOffsets();
        result.scroll_metrics_ms = ElapsedMilliseconds(scroll_start, std::chrono::steady_clock::now());
        return result;
    }

    UINode* root = writer_.Resolve(root_handle);
    if (root == nullptr || root->yg_node == nullptr) return result;

    result.emitted_layout_updates = layout_dirty;
    do {
        layout_dirty = false;
        ApplyLayoutStyles(root_handle, UI_INVALID_HANDLE);
        const auto layout_start = std::chrono::steady_clock::now();
        YGNodeCalculateLayout(root->yg_node, window_width, window_height, YGDirectionLTR);
        result.yoga_layout_ms += ElapsedMilliseconds(layout_start, std::chrono::steady_clock::now());

        const auto scroll_start = std::chrono::steady_clock::now();
        scrolling_.UpdateMetricsAfterLayout();
        result.scroll_metrics_ms += ElapsedMilliseconds(scroll_start, std::chrono::steady_clock::now());
        if (ResolveFillPercentLayout(root_handle, UI_INVALID_HANDLE, window_width, window_height)) {
            layout_dirty = true;
        }
        result.emitted_layout_updates = result.emitted_layout_updates || layout_dirty;
        result.stabilization_passes += 1U;
    } while (layout_dirty && result.stabilization_passes < kMaxLayoutStabilizationPasses);
    result.needs_follow_up_layout = layout_dirty;
    return result;
}

} // namespace effindom::v2::ui
