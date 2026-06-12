#include "UiRuntime.h"

#include <algorithm>
#include <cmath>

namespace effindom::v2::ui {

namespace {

void ApplyEffectivePadding(YGNodeRef yg_node, float left, float top, float right, float bottom) {
    YGNodeStyleSetPadding(yg_node, YGEdgeLeft, left);
    YGNodeStyleSetPadding(yg_node, YGEdgeTop, top);
    YGNodeStyleSetPadding(yg_node, YGEdgeRight, right);
    YGNodeStyleSetPadding(yg_node, YGEdgeBottom, bottom);
}

void ApplyEffectiveMargin(YGNodeRef yg_node, float left, float top, float right, float bottom) {
    YGNodeStyleSetMargin(yg_node, YGEdgeLeft, left);
    YGNodeStyleSetMargin(yg_node, YGEdgeTop, top);
    YGNodeStyleSetMargin(yg_node, YGEdgeRight, right);
    YGNodeStyleSetMargin(yg_node, YGEdgeBottom, bottom);
}

bool ApplyYogaAlign(YGNodeRef yg_node, std::uint32_t align_enum, bool self) {
    YGAlign align = YGAlignAuto;
    switch (align_enum) {
    case UI_ALIGN_SELF_AUTO:
        align = YGAlignAuto;
        break;
    case UI_ALIGN_SELF_START:
        align = YGAlignFlexStart;
        break;
    case UI_ALIGN_SELF_CENTER:
        align = YGAlignCenter;
        break;
    case UI_ALIGN_SELF_END:
        align = YGAlignFlexEnd;
        break;
    case UI_ALIGN_SELF_STRETCH:
        align = YGAlignStretch;
        break;
    default:
        return false;
    }
    if (self) {
        YGNodeStyleSetAlignSelf(yg_node, align);
    } else {
        YGNodeStyleSetAlignItems(yg_node, align == YGAlignAuto ? YGAlignFlexStart : align);
    }
    return true;
}

}

void UiRuntime::CapturePendingFocusId(std::uint64_t subtree_root) {
    if (!SubtreeContains(subtree_root, focused_handle_)) {
        return;
    }
    const UINode* focused_node = Resolve(focused_handle_);
    if (focused_node == nullptr || focused_node->node_id.empty()) {
        pending_focus_node_id_.clear();
        return;
    }
    pending_focus_node_id_ = focused_node->node_id;
}

void UiRuntime::RemoveTextFindHighlightsInSubtree(std::uint64_t subtree_root) {
    text_find_highlights_.erase(
        std::remove_if(
            text_find_highlights_.begin(),
            text_find_highlights_.end(),
            [&](const TextFindHighlight& highlight) {
                return SubtreeContains(subtree_root, highlight.handle);
            }),
        text_find_highlights_.end());
}

void UiRuntime::RestorePendingFocusIfPossible() {
    if (pending_focus_node_id_.empty() || focused_handle_ != UI_INVALID_HANDLE) {
        return;
    }
    const auto it = node_id_map_.find(pending_focus_node_id_);
    if (it == node_id_map_.end()) {
        return;
    }
    const std::uint64_t handle = it->second;
    const UINode* node = Resolve(handle);
    if (node == nullptr || !node->is_focusable) {
        return;
    }
    pending_focus_node_id_.clear();
    SetFocus(handle);
}

std::uint64_t UiRuntime::CreateNode(std::uint32_t type) {
    for (std::uint32_t index = 1; index < static_cast<std::uint32_t>(node_pool_.size()); index += 1U) {
        UINode& node = node_pool_[index];
        if (node.is_active) {
            continue;
        }

        const std::uint32_t next_generation = node.generation + 1U;
        node = UINode{};
        node.generation = next_generation;
        node.is_active = true;
        node.needs_creation = true;
        node.is_dirty = true;
        node.is_text_node = type == UI_NODE_TEXT;
        node.is_svg_node = type == UI_NODE_SVG;
        node.is_scroll_view = type == UI_NODE_SCROLLVIEW;
        node.is_grid = type == UI_NODE_GRID;
        node.yg_node = YGNodeNew();
        if (node.yg_node == nullptr) { node = UINode{}; return UI_INVALID_HANDLE; }
        ApplyEffectivePadding(node.yg_node, 0.0f, 0.0f, 0.0f, 0.0f);
        ApplyEffectiveMargin(node.yg_node, 0.0f, 0.0f, 0.0f, 0.0f);
        if (node.is_text_node) {
            YGNodeSetContext(node.yg_node, &node);
            YGNodeSetMeasureFunc(node.yg_node, &UiRuntime::MeasureTextCallback);
        }
        const std::uint64_t handle = PackHandle(index, next_generation);
        pending_creations_.push_back(handle);
        layout_dirty_ = true;
        return handle;
    }
    return UI_INVALID_HANDLE;
}

bool UiRuntime::DeleteNode(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    grid_side_tables_.erase(handle);
    if (!node->node_id.empty()) {
        node_id_map_.erase(node->node_id);
    }
    CapturePendingFocusId(handle);
    if (SubtreeContains(handle, focused_handle_)) {
        SetFocus(UI_INVALID_HANDLE);
    }
    if (SubtreeContains(handle, last_hovered_handle_)) {
        ClearHover(last_hovered_handle_);
    }
    if (SubtreeContains(handle, active_selection_handle_)) {
        active_selection_handle_ = UI_INVALID_HANDLE;
    }
    if (SubtreeContains(handle, text_find_handle_)) {
        text_find_handle_ = UI_INVALID_HANDLE;
        text_find_start_ = 0U;
        text_find_end_ = 0U;
    }
    RemoveTextFindHighlightsInSubtree(handle);
    if (selection_area_handle_ != UI_INVALID_HANDLE) {
        selection_area_nodes_dirty_ = true;
        if (cross_selection_active_ &&
            (SubtreeContains(handle, selection_area_handle_) ||
             SubtreeContains(handle, start_node_handle_) ||
             SubtreeContains(handle, end_node_handle_))) {
            ClearCrossSelection(false);
        }
    }

    if (node->parent_handle != UI_INVALID_HANDLE) {
        (void)RemoveChild(node->parent_handle, handle);
    }

    for (const std::uint64_t child_handle : node->children) {
        if (UINode* child = ResolveMutable(child_handle); child != nullptr) {
            child->parent_handle = UI_INVALID_HANDLE;
        }
        const HandleParts child_parts = DecodeHandle(child_handle);
        if (node->yg_node != nullptr && child_parts.index < node_pool_.size()) {
            UINode& child_node = node_pool_[child_parts.index];
            if (child_node.yg_node != nullptr) {
                YGNodeRemoveChild(node->yg_node, child_node.yg_node);
            }
        }
    }
    node->children.clear();
    node->children.shrink_to_fit();

    if (node->yg_node != nullptr) {
        YGNodeFree(node->yg_node);
        node->yg_node = nullptr;
    }

    if (root_handle_ == handle) {
        root_handle_ = UI_INVALID_HANDLE;
    }

    const auto create_it = std::find(pending_creations_.begin(), pending_creations_.end(), handle);
    if (create_it != pending_creations_.end()) {
        pending_creations_.erase(create_it);
    } else {
        pending_deletions_.push_back(handle);
    }

    node->is_active = false;
    node->needs_creation = false;
    node->is_dirty = false;
    node->parent_handle = UI_INVALID_HANDLE;
    InvalidateFocusOrder();
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::AddChild(std::uint64_t parent_handle, std::uint64_t child_handle) {
    UINode* parent = ResolveMutable(parent_handle);
    UINode* child = ResolveMutable(child_handle);
    if (parent == nullptr || child == nullptr || parent == child || parent->yg_node == nullptr || child->yg_node == nullptr) {
        return false;
    }
    if (child->parent_handle == parent_handle) {
        return true;
    }
    if (child->parent_handle != UI_INVALID_HANDLE) {
        (void)RemoveChild(child->parent_handle, child_handle);
    }

    YGNodeInsertChild(parent->yg_node, child->yg_node, static_cast<std::uint32_t>(parent->children.size()));
    child->parent_handle = parent_handle;
    parent->children.push_back(child_handle);
    InvalidateFocusOrder();
    if (selection_area_handle_ != UI_INVALID_HANDLE) {
        selection_area_nodes_dirty_ = true;
    }
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::RemoveChild(std::uint64_t parent_handle, std::uint64_t child_handle) {
    UINode* parent = ResolveMutable(parent_handle);
    UINode* child = ResolveMutable(child_handle);
    if (parent == nullptr || child == nullptr || parent->yg_node == nullptr || child->yg_node == nullptr) {
        return false;
    }

    const auto it = std::find(parent->children.begin(), parent->children.end(), child_handle);
    if (it == parent->children.end()) {
        return false;
    }

    YGNodeRemoveChild(parent->yg_node, child->yg_node);
    parent->children.erase(it);
    child->parent_handle = UI_INVALID_HANDLE;
    CapturePendingFocusId(child_handle);
    if (SubtreeContains(child_handle, focused_handle_)) {
        SetFocus(UI_INVALID_HANDLE);
    }
    if (SubtreeContains(child_handle, last_hovered_handle_)) {
        ClearHover(last_hovered_handle_);
    }
    if (SubtreeContains(child_handle, active_selection_handle_)) {
        active_selection_handle_ = UI_INVALID_HANDLE;
    }
    if (SubtreeContains(child_handle, text_find_handle_)) {
        text_find_handle_ = UI_INVALID_HANDLE;
        text_find_start_ = 0U;
        text_find_end_ = 0U;
    }
    RemoveTextFindHighlightsInSubtree(child_handle);
    if (selection_area_handle_ != UI_INVALID_HANDLE) {
        selection_area_nodes_dirty_ = true;
        if (cross_selection_active_ &&
            (SubtreeContains(child_handle, selection_area_handle_) ||
             SubtreeContains(child_handle, start_node_handle_) ||
             SubtreeContains(child_handle, end_node_handle_))) {
            ClearCrossSelection(false);
        }
    }
    InvalidateFocusOrder();
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetRoot(std::uint64_t handle) {
    if (handle == UI_INVALID_HANDLE) {
        SetFocus(UI_INVALID_HANDLE);
        ClearHover(last_hovered_handle_);
        active_selection_handle_ = UI_INVALID_HANDLE;
        text_find_handle_ = UI_INVALID_HANDLE;
        text_find_start_ = 0U;
        text_find_end_ = 0U;
        text_find_highlights_.clear();
        ClearCrossSelection(false);
        selection_area_nodes_dirty_ = true;
        root_handle_ = UI_INVALID_HANDLE;
        InvalidateFocusOrder();
        layout_dirty_ = true;
        return true;
    }
    if (ResolveMutable(handle) == nullptr) {
        return false;
    }
    root_handle_ = handle;
    if (!SubtreeContains(root_handle_, focused_handle_)) {
        SetFocus(UI_INVALID_HANDLE);
    }
    if (!SubtreeContains(root_handle_, last_hovered_handle_)) {
        ClearHover(last_hovered_handle_);
    }
    if (!SubtreeContains(root_handle_, text_find_handle_)) {
        text_find_handle_ = UI_INVALID_HANDLE;
        text_find_start_ = 0U;
        text_find_end_ = 0U;
    }
    text_find_highlights_.erase(
        std::remove_if(
            text_find_highlights_.begin(),
            text_find_highlights_.end(),
            [&](const TextFindHighlight& highlight) {
                return !SubtreeContains(root_handle_, highlight.handle);
            }),
        text_find_highlights_.end());
    if (!SubtreeContains(root_handle_, active_selection_handle_)) {
        active_selection_handle_ = UI_INVALID_HANDLE;
    }
    if (selection_area_handle_ != UI_INVALID_HANDLE && !SubtreeContains(root_handle_, selection_area_handle_)) {
        ClearCrossSelection(false);
    }
    selection_area_nodes_dirty_ = true;
    InvalidateFocusOrder();
    layout_dirty_ = true;
    RestorePendingFocusIfPossible();
    return true;
}

bool UiRuntime::SetPositionType(std::uint64_t handle, std::uint32_t pos_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr ||
        (pos_enum != UI_POSITION_RELATIVE && pos_enum != UI_POSITION_ABSOLUTE)) {
        return false;
    }
    YGNodeStyleSetPositionType(
        node->yg_node,
        pos_enum == UI_POSITION_ABSOLUTE ? YGPositionTypeAbsolute : YGPositionTypeRelative);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetPosition(std::uint64_t handle, float left, float top, float right, float bottom) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }

    const auto apply_edge = [node](YGEdge edge, float value) {
        YGNodeStyleSetPosition(node->yg_node, edge, std::isfinite(value) ? value : YGUndefined);
    };
    apply_edge(YGEdgeLeft, left);
    apply_edge(YGEdgeTop, top);
    apply_edge(YGEdgeRight, right);
    apply_edge(YGEdgeBottom, bottom);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetNodeId(std::uint64_t handle, const std::uint8_t* utf8_id, std::uint32_t len) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || (utf8_id == nullptr && len > 0U)) {
        return false;
    }

    std::string new_id{};
    if (len > 0U) {
        new_id.assign(reinterpret_cast<const char*>(utf8_id), reinterpret_cast<const char*>(utf8_id) + len);
    }
    if (node->node_id == new_id) {
        return true;
    }

    if (!new_id.empty()) {
        const auto it = node_id_map_.find(new_id);
        if (it != node_id_map_.end()) {
            if (Resolve(it->second) != nullptr) {
                return false;
            }
            node_id_map_.erase(it);
        }
    }

    if (!node->node_id.empty()) {
        node_id_map_.erase(node->node_id);
    }

    node->node_id = std::move(new_id);
    if (!node->node_id.empty()) {
        node_id_map_.emplace(node->node_id, handle);
    }
    return true;
}

bool UiRuntime::SetSemanticRole(std::uint64_t handle, std::uint32_t role_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || role_enum > UI_SEMANTIC_COMBOBOX) {
        return false;
    }
    const std::string previous_text = node->text_content;
    node->semantic_role = static_cast<UiSemanticRole>(role_enum);
    if (node->is_text_node && ApplyAbsurdLineClamp(*node)) {
        NotifyTextStateChanged(handle, *node, &previous_text);
    }
    return true;
}

bool UiRuntime::SetSemanticLabel(std::uint64_t handle, const std::uint8_t* utf8_label, std::uint32_t len) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || (utf8_label == nullptr && len > 0U)) {
        return false;
    }
    if (len == 0U) {
        node->semantic_label.clear();
    } else {
        node->semantic_label.assign(
            reinterpret_cast<const char*>(utf8_label),
            reinterpret_cast<const char*>(utf8_label) + len);
    }
    return true;
}

bool UiRuntime::SetSemanticChecked(std::uint64_t handle, std::uint32_t checked_state_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || checked_state_enum > UI_SEMANTIC_CHECKED_MIXED) {
        return false;
    }
    node->semantic_checked_state = static_cast<UiSemanticCheckedState>(checked_state_enum);
    return true;
}

bool UiRuntime::SetSemanticSelected(std::uint64_t handle, bool has_selected, bool is_selected) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->has_semantic_selected = has_selected;
    node->semantic_selected = has_selected && is_selected;
    return true;
}

bool UiRuntime::SetSemanticExpanded(std::uint64_t handle, bool has_expanded, bool is_expanded) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->has_semantic_expanded = has_expanded;
    node->semantic_expanded = has_expanded && is_expanded;
    return true;
}

bool UiRuntime::SetSemanticDisabled(std::uint64_t handle, bool has_disabled, bool is_disabled) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->has_semantic_disabled = has_disabled;
    node->semantic_disabled = has_disabled && is_disabled;
    return true;
}

bool UiRuntime::SetSemanticValueRange(
    std::uint64_t handle,
    bool has_value_range,
    float value_now,
    float value_min,
    float value_max) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->has_semantic_value_range = has_value_range;
    if (has_value_range) {
        node->semantic_value_now = value_now;
        node->semantic_value_min = value_min;
        node->semantic_value_max = value_max;
    } else {
        node->semantic_value_now = 0.0f;
        node->semantic_value_min = 0.0f;
        node->semantic_value_max = 0.0f;
    }
    return true;
}

bool UiRuntime::SetSemanticOrientation(std::uint64_t handle, std::uint32_t orientation_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || orientation_enum > UI_ORIENTATION_VERTICAL) {
        return false;
    }
    node->semantic_orientation = static_cast<UiOrientation>(orientation_enum);
    return true;
}

bool UiRuntime::RequestSemanticAnnouncement(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    as_on_request_semantic_announcement(handle);
    return true;
}

bool UiRuntime::SetPortal(std::uint64_t handle, bool is_portal) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->is_portal = is_portal;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetVisibility(std::uint64_t handle, std::uint32_t visibility_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || visibility_enum > UI_VISIBILITY_COLLAPSED) {
        return false;
    }

    const UiVisibility next_visibility = static_cast<UiVisibility>(visibility_enum);
    if (node->visibility == next_visibility) {
        return true;
    }
    const UiVisibility previous_visibility = node->visibility;
    node->visibility = next_visibility;
    node->is_dirty = true;

    if (node->yg_node != nullptr) {
        YGNodeStyleSetDisplay(
            node->yg_node,
            next_visibility == UI_VISIBILITY_COLLAPSED ? YGDisplayNone : YGDisplayFlex);
    }
    if (previous_visibility == UI_VISIBILITY_COLLAPSED || next_visibility == UI_VISIBILITY_COLLAPSED) {
        layout_dirty_ = true;
    }

    if (next_visibility != UI_VISIBILITY_NORMAL) {
        if (SubtreeContains(handle, focused_handle_)) {
            SetFocus(UI_INVALID_HANDLE);
        }
        if (SubtreeContains(handle, last_hovered_handle_)) {
            ClearHover(last_hovered_handle_);
        }
        if (SubtreeContains(handle, active_selection_handle_)) {
            active_selection_handle_ = UI_INVALID_HANDLE;
        }
        if (selection_area_handle_ != UI_INVALID_HANDLE) {
            selection_area_nodes_dirty_ = true;
            if (cross_selection_active_ &&
                (SubtreeContains(handle, selection_area_handle_) ||
                 SubtreeContains(handle, start_node_handle_) ||
                 SubtreeContains(handle, end_node_handle_))) {
                ClearCrossSelection(false);
            }
        }
    }

    return true;
}

bool UiRuntime::SetSelectionArea(std::uint64_t handle, bool is_selection_area) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->is_selection_area = is_selection_area;
    node->is_dirty = true;
    if (selection_area_handle_ == handle) {
        selection_area_nodes_dirty_ = true;
        if (!is_selection_area) {
            ClearCrossSelection(false);
        }
    }
    return true;
}

bool UiRuntime::SetSelectionAreaBarrier(std::uint64_t handle, bool is_barrier) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->is_selection_area_barrier = is_barrier;
    if (selection_area_handle_ != UI_INVALID_HANDLE) {
        selection_area_nodes_dirty_ = true;
    }
    return true;
}

bool UiRuntime::SetIsSharedSizeScope(std::uint64_t handle, bool is_scope) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    GridSideTableEntry* existing = FindMutableGridSideTableEntry(handle);
    if (existing == nullptr && !is_scope) {
        return true;
    }
    GridSideTableEntry& entry = existing == nullptr ? EnsureGridSideTableEntry(handle) : *existing;
    if (entry.is_shared_size_scope == is_scope) {
        return true;
    }
    entry.is_shared_size_scope = is_scope;
    PruneGridSideTableEntry(handle);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetCustomDrawable(std::uint64_t handle, bool is_custom_drawable) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    if (node->is_custom_drawable == is_custom_drawable) {
        return true;
    }
    node->is_custom_drawable = is_custom_drawable;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetGridColumns(
    std::uint64_t handle,
    std::uint32_t count,
    const float* values,
    const std::uint8_t* types) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_grid || ((count > 0U) && (values == nullptr || types == nullptr))) {
        return false;
    }
    std::vector<float> next_values{};
    std::vector<std::uint8_t> next_types{};
    if (count > 0U) {
        next_values.assign(values, values + count);
        next_types.assign(types, types + count);
    }
    for (std::uint8_t type : next_types) {
        if (type != UI_GRID_UNIT_PIXEL && type != UI_GRID_UNIT_AUTO && type != UI_GRID_UNIT_STAR) {
            return false;
        }
    }
    node->column_values = std::move(next_values);
    node->column_types = std::move(next_types);
    TrimGridSharedSizeGroups(handle, true, count);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetGridRows(
    std::uint64_t handle,
    std::uint32_t count,
    const float* values,
    const std::uint8_t* types) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_grid || ((count > 0U) && (values == nullptr || types == nullptr))) {
        return false;
    }
    std::vector<float> next_values{};
    std::vector<std::uint8_t> next_types{};
    if (count > 0U) {
        next_values.assign(values, values + count);
        next_types.assign(types, types + count);
    }
    for (std::uint8_t type : next_types) {
        if (type != UI_GRID_UNIT_PIXEL && type != UI_GRID_UNIT_AUTO && type != UI_GRID_UNIT_STAR) {
            return false;
        }
    }
    node->row_values = std::move(next_values);
    node->row_types = std::move(next_types);
    TrimGridSharedSizeGroups(handle, false, count);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetGridColumnSharedSizeGroup(
    std::uint64_t handle,
    std::uint32_t index,
    const std::uint8_t* utf8_group,
    std::uint32_t len) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_grid || (utf8_group == nullptr && len > 0U)) {
        return false;
    }
    std::string next_group{};
    if (len > 0U) {
        next_group.assign(reinterpret_cast<const char*>(utf8_group), reinterpret_cast<const char*>(utf8_group) + len);
    }
    GridSideTableEntry* existing = FindMutableGridSideTableEntry(handle);
    if (existing == nullptr && next_group.empty()) {
        return true;
    }
    GridSideTableEntry& entry = existing == nullptr ? EnsureGridSideTableEntry(handle) : *existing;
    if (entry.column_shared_size_groups.size() <= index) {
        if (next_group.empty()) {
            return true;
        }
        entry.column_shared_size_groups.resize(static_cast<std::size_t>(index) + 1U);
    }
    if (entry.column_shared_size_groups[index] == next_group) {
        return true;
    }
    entry.column_shared_size_groups[index] = std::move(next_group);
    PruneGridSideTableEntry(handle);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetGridRowSharedSizeGroup(
    std::uint64_t handle,
    std::uint32_t index,
    const std::uint8_t* utf8_group,
    std::uint32_t len) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_grid || (utf8_group == nullptr && len > 0U)) {
        return false;
    }
    std::string next_group{};
    if (len > 0U) {
        next_group.assign(reinterpret_cast<const char*>(utf8_group), reinterpret_cast<const char*>(utf8_group) + len);
    }
    GridSideTableEntry* existing = FindMutableGridSideTableEntry(handle);
    if (existing == nullptr && next_group.empty()) {
        return true;
    }
    GridSideTableEntry& entry = existing == nullptr ? EnsureGridSideTableEntry(handle) : *existing;
    if (entry.row_shared_size_groups.size() <= index) {
        if (next_group.empty()) {
            return true;
        }
        entry.row_shared_size_groups.resize(static_cast<std::size_t>(index) + 1U);
    }
    if (entry.row_shared_size_groups[index] == next_group) {
        return true;
    }
    entry.row_shared_size_groups[index] = std::move(next_group);
    PruneGridSideTableEntry(handle);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

UiRuntime::GridSideTableEntry& UiRuntime::EnsureGridSideTableEntry(std::uint64_t handle) {
    return grid_side_tables_[handle];
}

UiRuntime::GridSideTableEntry* UiRuntime::FindMutableGridSideTableEntry(std::uint64_t handle) {
    const auto it = grid_side_tables_.find(handle);
    return it == grid_side_tables_.end() ? nullptr : &it->second;
}

const UiRuntime::GridSideTableEntry* UiRuntime::FindGridSideTableEntry(std::uint64_t handle) const {
    const auto it = grid_side_tables_.find(handle);
    return it == grid_side_tables_.end() ? nullptr : &it->second;
}

bool UiRuntime::IsSharedSizeScope(std::uint64_t handle) const {
    const GridSideTableEntry* entry = FindGridSideTableEntry(handle);
    return entry != nullptr && entry->is_shared_size_scope;
}

const std::vector<std::string>& UiRuntime::GridColumnSharedSizeGroups(std::uint64_t handle) const {
    static const std::vector<std::string> kEmpty{};
    const GridSideTableEntry* entry = FindGridSideTableEntry(handle);
    return entry == nullptr ? kEmpty : entry->column_shared_size_groups;
}

const std::vector<std::string>& UiRuntime::GridRowSharedSizeGroups(std::uint64_t handle) const {
    static const std::vector<std::string> kEmpty{};
    const GridSideTableEntry* entry = FindGridSideTableEntry(handle);
    return entry == nullptr ? kEmpty : entry->row_shared_size_groups;
}

void UiRuntime::TrimGridSharedSizeGroups(std::uint64_t handle, bool columns, std::uint32_t count) {
    GridSideTableEntry* entry = FindMutableGridSideTableEntry(handle);
    if (entry == nullptr) {
        return;
    }
    std::vector<std::string>& groups = columns ? entry->column_shared_size_groups : entry->row_shared_size_groups;
    if (groups.size() > count) {
        groups.resize(count);
    }
    PruneGridSideTableEntry(handle);
}

void UiRuntime::PruneGridSideTableEntry(std::uint64_t handle) {
    GridSideTableEntry* entry = FindMutableGridSideTableEntry(handle);
    if (entry == nullptr) {
        return;
    }
    const bool has_column_group = std::any_of(
        entry->column_shared_size_groups.begin(),
        entry->column_shared_size_groups.end(),
        [](const std::string& group) { return !group.empty(); });
    const bool has_row_group = std::any_of(
        entry->row_shared_size_groups.begin(),
        entry->row_shared_size_groups.end(),
        [](const std::string& group) { return !group.empty(); });
    if (!entry->is_shared_size_scope && !has_column_group && !has_row_group) {
        grid_side_tables_.erase(handle);
    }
}

bool UiRuntime::SetGridPlacement(
    std::uint64_t handle,
    std::uint32_t row,
    std::uint32_t col,
    std::uint32_t row_span,
    std::uint32_t col_span) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->grid_row = row;
    node->grid_col = col;
    node->grid_row_span = std::max(row_span, 1U);
    node->grid_col_span = std::max(col_span, 1U);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetScrollOffset(std::uint64_t handle, float offset_x, float offset_y) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_scroll_view) {
        return false;
    }
    node->pending_scroll_offset_x = offset_x;
    node->pending_scroll_offset_y = offset_y;
    node->has_pending_scroll_offset = true;
    node->pending_scroll_offset_generation += 1U;
    node->scroll_velocity_x = 0.0f;
    node->scroll_velocity_y = 0.0f;
    return true;
}

bool UiRuntime::SetScrollContentSize(std::uint64_t handle, float content_width, float content_height) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr ||
        !node->is_scroll_view ||
        !std::isfinite(content_width) ||
        !std::isfinite(content_height)) {
        return false;
    }

    node->has_explicit_scroll_content_width = content_width >= 0.0f;
    node->has_explicit_scroll_content_height = content_height >= 0.0f;
    node->explicit_scroll_content_width = std::max(content_width, 0.0f);
    node->explicit_scroll_content_height = std::max(content_height, 0.0f);
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetScrollEnabled(std::uint64_t handle, bool enabled_x, bool enabled_y) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_scroll_view) {
        return false;
    }
    node->scroll_enabled_x = enabled_x;
    node->scroll_enabled_y = enabled_y;
    if (!enabled_x) {
        node->scroll_velocity_x = 0.0f;
    }
    if (!enabled_y) {
        node->scroll_velocity_y = 0.0f;
    }
    (void)ApplyScrollOffset(handle, *node, node->scroll_offset_x, node->scroll_offset_y, true);
    return true;
}

bool UiRuntime::SetShowScrollbars(std::uint64_t handle, bool show_scrollbars) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_scroll_view) {
        return false;
    }
    node->show_scrollbars = show_scrollbars;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetScrollFriction(std::uint64_t handle, float friction) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_scroll_view || !std::isfinite(friction) || friction < 0.0f || friction > 1.0f) {
        return false;
    }
    node->friction = friction;
    node->scroll_friction_overridden = true;
    return true;
}

void UiRuntime::ResizeWindow(float logical_width, float logical_height) {
    window_width_ = logical_width;
    window_height_ = logical_height;
    layout_dirty_ = true;
}

bool UiRuntime::SetWidth(std::uint64_t handle, float value, std::uint32_t unit_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    switch (unit_enum) {
    case UI_SIZE_UNIT_PIXEL:
        YGNodeStyleSetWidth(node->yg_node, value);
        break;
    case UI_SIZE_UNIT_AUTO:
        YGNodeStyleSetWidthAuto(node->yg_node);
        break;
    case UI_SIZE_UNIT_PERCENT:
        YGNodeStyleSetWidthPercent(node->yg_node, value);
        break;
    default:
        return false;
    }
    node->has_width = true;
    node->width = value;
    node->width_unit = unit_enum;
    node->fill_width = false;
    node->has_fill_width_percent = false;
    node->has_resolved_fill_width = false;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetHeight(std::uint64_t handle, float value, std::uint32_t unit_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    switch (unit_enum) {
    case UI_SIZE_UNIT_PIXEL:
        YGNodeStyleSetHeight(node->yg_node, value);
        break;
    case UI_SIZE_UNIT_AUTO:
        YGNodeStyleSetHeightAuto(node->yg_node);
        break;
    case UI_SIZE_UNIT_PERCENT:
        YGNodeStyleSetHeightPercent(node->yg_node, value);
        break;
    default:
        return false;
    }
    node->has_height = true;
    node->height = value;
    node->height_unit = unit_enum;
    node->fill_height = false;
    node->has_fill_height_percent = false;
    node->has_resolved_fill_height = false;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFillWidth(std::uint64_t handle, bool fill) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    if (node->fill_width == fill) {
        return true;
    }
    node->fill_width = fill;
    if (fill) {
        node->has_width = false;
        node->has_fill_width_percent = false;
        node->has_resolved_fill_width = false;
        YGNodeStyleSetWidthAuto(node->yg_node);
    }
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFillHeight(std::uint64_t handle, bool fill) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    if (node->fill_height == fill) {
        return true;
    }
    node->fill_height = fill;
    if (fill) {
        node->has_height = false;
        node->has_fill_height_percent = false;
        node->has_resolved_fill_height = false;
        YGNodeStyleSetHeightAuto(node->yg_node);
    }
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFillWidthPercent(std::uint64_t handle, float percent) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || !std::isfinite(percent) || percent < 0.0f) {
        return false;
    }
    node->has_width = false;
    node->fill_width = false;
    node->has_fill_width_percent = true;
    node->fill_width_percent = percent;
    node->has_resolved_fill_width = false;
    YGNodeStyleSetWidthAuto(node->yg_node);
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFillHeightPercent(std::uint64_t handle, float percent) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || !std::isfinite(percent) || percent < 0.0f) {
        return false;
    }
    node->has_height = false;
    node->fill_height = false;
    node->has_fill_height_percent = true;
    node->fill_height_percent = percent;
    node->has_resolved_fill_height = false;
    YGNodeStyleSetHeightAuto(node->yg_node);
    layout_dirty_ = true;
    return true;
}

namespace {

bool ApplyMinMaxAxis(
    YGNodeRef yg_node,
    float value,
    std::uint32_t unit_enum,
    bool min_axis,
    bool width_axis) {
    switch (unit_enum) {
    case UI_SIZE_UNIT_PIXEL:
        if (width_axis) {
            if (min_axis) {
                YGNodeStyleSetMinWidth(yg_node, value);
            } else {
                YGNodeStyleSetMaxWidth(yg_node, value);
            }
        } else if (min_axis) {
            YGNodeStyleSetMinHeight(yg_node, value);
        } else {
            YGNodeStyleSetMaxHeight(yg_node, value);
        }
        return true;
    case UI_SIZE_UNIT_PERCENT:
        if (width_axis) {
            if (min_axis) {
                YGNodeStyleSetMinWidthPercent(yg_node, value);
            } else {
                YGNodeStyleSetMaxWidthPercent(yg_node, value);
            }
        } else if (min_axis) {
            YGNodeStyleSetMinHeightPercent(yg_node, value);
        } else {
            YGNodeStyleSetMaxHeightPercent(yg_node, value);
        }
        return true;
    case UI_SIZE_UNIT_AUTO:
        if (width_axis) {
            if (min_axis) {
                YGNodeStyleSetMinWidth(yg_node, YGUndefined);
            } else {
                YGNodeStyleSetMaxWidth(yg_node, YGUndefined);
            }
        } else if (min_axis) {
            YGNodeStyleSetMinHeight(yg_node, YGUndefined);
        } else {
            YGNodeStyleSetMaxHeight(yg_node, YGUndefined);
        }
        return true;
    default:
        return false;
    }
}

}

bool UiRuntime::SetMinWidth(std::uint64_t handle, float value, std::uint32_t unit_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || (unit_enum != UI_SIZE_UNIT_AUTO && (!std::isfinite(value) || value < 0.0f))) {
        return false;
    }
    if (!ApplyMinMaxAxis(node->yg_node, value, unit_enum, true, true)) {
        return false;
    }
    node->has_min_width = unit_enum != UI_SIZE_UNIT_AUTO;
    node->min_width = value;
    node->min_width_unit = unit_enum;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetMaxWidth(std::uint64_t handle, float value, std::uint32_t unit_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || (unit_enum != UI_SIZE_UNIT_AUTO && (!std::isfinite(value) || value < 0.0f))) {
        return false;
    }
    if (!ApplyMinMaxAxis(node->yg_node, value, unit_enum, false, true)) {
        return false;
    }
    node->has_max_width = unit_enum != UI_SIZE_UNIT_AUTO;
    node->max_width = value;
    node->max_width_unit = unit_enum;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetMinHeight(std::uint64_t handle, float value, std::uint32_t unit_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || (unit_enum != UI_SIZE_UNIT_AUTO && (!std::isfinite(value) || value < 0.0f))) {
        return false;
    }
    if (!ApplyMinMaxAxis(node->yg_node, value, unit_enum, true, false)) {
        return false;
    }
    node->has_min_height = unit_enum != UI_SIZE_UNIT_AUTO;
    node->min_height = value;
    node->min_height_unit = unit_enum;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetMaxHeight(std::uint64_t handle, float value, std::uint32_t unit_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || (unit_enum != UI_SIZE_UNIT_AUTO && (!std::isfinite(value) || value < 0.0f))) {
        return false;
    }
    if (!ApplyMinMaxAxis(node->yg_node, value, unit_enum, false, false)) {
        return false;
    }
    node->has_max_height = unit_enum != UI_SIZE_UNIT_AUTO;
    node->max_height = value;
    node->max_height_unit = unit_enum;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFlexDirection(std::uint64_t handle, std::uint32_t dir_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    switch (dir_enum) {
    case 0U:
        YGNodeStyleSetFlexDirection(node->yg_node, YGFlexDirectionColumn);
        break;
    case 1U:
        YGNodeStyleSetFlexDirection(node->yg_node, YGFlexDirectionRow);
        break;
    default:
        return false;
    }
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFlexWrap(std::uint64_t handle, std::uint32_t wrap_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    switch (wrap_enum) {
    case 0U: YGNodeStyleSetFlexWrap(node->yg_node, YGWrapNoWrap); break;
    case 1U: YGNodeStyleSetFlexWrap(node->yg_node, YGWrapWrap); break;
    case 2U: YGNodeStyleSetFlexWrap(node->yg_node, YGWrapWrapReverse); break;
    default: return false;
    }
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFlexBasis(std::uint64_t handle, float basis) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || !std::isfinite(basis) || basis < 0.0f) {
        return false;
    }
    YGNodeStyleSetFlexBasis(node->yg_node, basis);
    node->has_flex_basis = true;
    node->flex_basis = basis;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetJustifyContent(std::uint64_t handle, std::uint32_t justify_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    switch (justify_enum) {
    case 0U:
    case 1U:
        YGNodeStyleSetJustifyContent(node->yg_node, YGJustifyFlexStart);
        break;
    case 2U:
        YGNodeStyleSetJustifyContent(node->yg_node, YGJustifyCenter);
        break;
    case 3U:
        YGNodeStyleSetJustifyContent(node->yg_node, YGJustifyFlexEnd);
        break;
    default:
        return false;
    }
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetAlignItems(std::uint64_t handle, std::uint32_t align_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    if (align_enum == UI_ALIGN_ITEMS_NONE) {
        YGNodeStyleSetAlignItems(node->yg_node, YGAlignStretch);
        layout_dirty_ = true;
        return true;
    }
    if (!ApplyYogaAlign(node->yg_node, align_enum == 0U ? UI_ALIGN_SELF_START : align_enum, false)) {
        return false;
    }
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetAlignSelf(std::uint64_t handle, std::uint32_t align_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr || !ApplyYogaAlign(node->yg_node, align_enum, true)) {
        return false;
    }
    node->has_align_self = true;
    node->align_self = align_enum;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetPadding(std::uint64_t handle, float left, float top, float right, float bottom) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr ||
        node->yg_node == nullptr ||
        !std::isfinite(left) ||
        !std::isfinite(top) ||
        !std::isfinite(right) ||
        !std::isfinite(bottom) ||
        left < 0.0f ||
        top < 0.0f ||
        right < 0.0f ||
        bottom < 0.0f) {
        return false;
    }
    ApplyEffectivePadding(node->yg_node, left, top, right, bottom);
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetMargin(std::uint64_t handle, float left, float top, float right, float bottom) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr ||
        node->yg_node == nullptr ||
        !std::isfinite(left) ||
        !std::isfinite(top) ||
        !std::isfinite(right) ||
        !std::isfinite(bottom) ||
        left < 0.0f ||
        top < 0.0f ||
        right < 0.0f ||
        bottom < 0.0f) {
        return false;
    }
    ApplyEffectiveMargin(node->yg_node, left, top, right, bottom);
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetClipToBounds(std::uint64_t handle, bool clip) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->clip_to_bounds = clip;
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetBoxStyle(
    std::uint64_t handle,
    std::uint32_t bg_color,
    float radius_tl,
    float radius_tr,
    float radius_br,
    float radius_bl,
    float border_width,
    std::uint32_t border_color,
    std::uint32_t border_style,
    float border_dash_on,
    float border_dash_off) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr ||
        !std::isfinite(radius_tl) ||
        !std::isfinite(radius_tr) ||
        !std::isfinite(radius_br) ||
        !std::isfinite(radius_bl) ||
        !std::isfinite(border_width) ||
        !std::isfinite(border_dash_on) ||
        !std::isfinite(border_dash_off) ||
        radius_tl < 0.0f ||
        radius_tr < 0.0f ||
        radius_br < 0.0f ||
        radius_bl < 0.0f ||
        border_width < 0.0f ||
        border_dash_on < 0.0f ||
        border_dash_off < 0.0f ||
        border_style > ED_BORDER_DOTTED) {
        return false;
    }
    const bool border_width_changed = std::abs(node->border_width - border_width) >= 0.001f;
    node->bg_color = bg_color;
    node->has_box_style = true;
    node->corner_radius_tl = radius_tl;
    node->corner_radius_tr = radius_tr;
    node->corner_radius_br = radius_br;
    node->corner_radius_bl = radius_bl;
    node->border_width = border_width;
    node->border_color = border_color;
    node->border_style = border_style;
    node->border_dash_on = border_dash_on;
    node->border_dash_off = border_dash_off;
    if (node->yg_node != nullptr) {
        YGNodeStyleSetBorder(node->yg_node, YGEdgeLeft, border_width);
        YGNodeStyleSetBorder(node->yg_node, YGEdgeTop, border_width);
        YGNodeStyleSetBorder(node->yg_node, YGEdgeRight, border_width);
        YGNodeStyleSetBorder(node->yg_node, YGEdgeBottom, border_width);
    }
    node->is_dirty = true;
    if (border_width_changed) {
        layout_dirty_ = true;
    }
    return true;
}

bool UiRuntime::SetLayerEffect(std::uint64_t handle, float opacity, float blur_sigma, std::uint32_t blend_mode) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !std::isfinite(opacity) || !std::isfinite(blur_sigma) || blur_sigma < 0.0f) {
        return false;
    }
    node->has_layer_effect = true;
    node->opacity = opacity;
    node->blur_sigma = blur_sigma;
    node->blend_mode = blend_mode;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetDropShadow(
    std::uint64_t handle,
    std::uint32_t color,
    float offset_x,
    float offset_y,
    float blur_sigma,
    float spread) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr ||
        !std::isfinite(offset_x) ||
        !std::isfinite(offset_y) ||
        !std::isfinite(blur_sigma) ||
        !std::isfinite(spread) ||
        blur_sigma < 0.0f ||
        spread < 0.0f) {
        return false;
    }
    node->has_drop_shadow = true;
    node->drop_shadow_color = color;
    node->drop_shadow_offset_x = offset_x;
    node->drop_shadow_offset_y = offset_y;
    node->drop_shadow_blur_sigma = blur_sigma;
    node->drop_shadow_spread = spread;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetBackgroundBlur(std::uint64_t handle, float blur_sigma) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !std::isfinite(blur_sigma) || blur_sigma < 0.0f) {
        return false;
    }
    node->has_background_blur = true;
    node->background_blur_sigma = blur_sigma;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetImage(std::uint64_t handle, std::uint32_t texture_id, std::uint32_t object_fit_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || object_fit_enum > ED_OBJECT_FIT_SCALE_DOWN) {
        return false;
    }
    node->has_image = texture_id != 0U;
    node->texture_id = texture_id;
    node->object_fit = object_fit_enum;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetImageNine(
    std::uint64_t handle,
    std::uint32_t texture_id,
    float inset_left,
    float inset_top,
    float inset_right,
    float inset_bottom) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr ||
        !std::isfinite(inset_left) ||
        !std::isfinite(inset_top) ||
        !std::isfinite(inset_right) ||
        !std::isfinite(inset_bottom) ||
        inset_left < 0.0f ||
        inset_top < 0.0f ||
        inset_right < 0.0f ||
        inset_bottom < 0.0f) {
        return false;
    }
    node->has_image_nine = texture_id != 0U;
    node->image_nine_texture_id = texture_id;
    node->image_nine_inset_left = inset_left;
    node->image_nine_inset_top = inset_top;
    node->image_nine_inset_right = inset_right;
    node->image_nine_inset_bottom = inset_bottom;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetSvg(std::uint64_t handle, std::uint32_t svg_id, std::uint32_t tint_color) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->has_svg = svg_id != 0U;
    node->svg_id = svg_id;
    node->svg_tint_color = tint_color;
    node->is_dirty = true;
    return true;
}

std::uint32_t UiRuntime::PushSemanticScope(std::uint64_t handle) {
    if (Resolve(handle) == nullptr) {
        return 0U;
    }
    if (next_semantic_scope_token_ == 0U) {
        next_semantic_scope_token_ = 1U;
    }
    const std::uint32_t token = next_semantic_scope_token_;
    next_semantic_scope_token_ += 1U;
    semantic_scope_stack_.push_back(SemanticScopeEntry{token, handle});
    focus_order_dirty_ = true;
    return token;
}

bool UiRuntime::RemoveSemanticScope(std::uint32_t token) {
    if (token == 0U) {
        return false;
    }
    const auto it = std::find_if(
        semantic_scope_stack_.begin(),
        semantic_scope_stack_.end(),
        [token](const SemanticScopeEntry& entry) {
            return entry.token == token;
        });
    if (it == semantic_scope_stack_.end()) {
        return false;
    }
    semantic_scope_stack_.erase(it);
    focus_order_dirty_ = true;
    return true;
}

bool UiRuntime::SetLinearGradient(
    std::uint64_t handle,
    float start_x,
    float start_y,
    float end_x,
    float end_y,
    std::uint32_t stop_count,
    const float* offsets,
    const std::uint32_t* colors) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr ||
        !std::isfinite(start_x) ||
        !std::isfinite(start_y) ||
        !std::isfinite(end_x) ||
        !std::isfinite(end_y) ||
        (stop_count > 0U && (offsets == nullptr || colors == nullptr))) {
        return false;
    }
    node->gradient_start_x = start_x;
    node->gradient_start_y = start_y;
    node->gradient_end_x = end_x;
    node->gradient_end_y = end_y;
    node->gradient_stops.clear();
    node->gradient_stops.reserve(stop_count);
    for (std::uint32_t index = 0; index < stop_count; index += 1U) {
        node->gradient_stops.push_back(GradientStop{offsets[index], colors[index]});
    }
    node->has_linear_gradient = stop_count > 0U;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetNodeColor(std::uint64_t handle, std::uint32_t color) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    node->bg_color = color;
    node->has_box_style = true;
    node->is_dirty = true;
    return true;
}

const UINode* UiRuntime::Resolve(std::uint64_t handle) const {
    const HandleParts parts = DecodeHandle(handle);
    if (parts.index == 0 || parts.index >= node_pool_.size()) {
        return nullptr;
    }
    const UINode& node = node_pool_[parts.index];
    return (!node.is_active || node.generation != parts.generation) ? nullptr : &node;
}

} // namespace effindom::v2::ui
