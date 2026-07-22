#include "UiSelectionCoordinator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace effindom::v2::ui {

namespace {

bool IsHorizontalContainer(const UINode* node) {
    return node != nullptr && node->yg_node != nullptr &&
        YGNodeStyleGetFlexDirection(node->yg_node) == YGFlexDirectionRow;
}

std::pair<float, float> ClampSelectionPointToNodeRows(
    const UINode& node,
    float logical_x,
    float logical_y) {
    const float node_bottom = node.abs_y + std::max(0.0f, node.layout_height);
    const float last_row_y = node_bottom > node.abs_y
        ? std::nextafter(node_bottom, node.abs_y)
        : node.abs_y;
    return {logical_x, std::clamp(logical_y, node.abs_y, last_row_y)};
}

} // namespace

std::uint64_t SelectionCoordinator::FindAreaAncestor(
    const SelectionHost& host,
    std::uint64_t start_handle) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = host.ResolveSelectionNode(current);
        if (node == nullptr) {
            break;
        }
        if (node->is_selection_area) {
            return current;
        }
        if (node->is_selection_area_barrier) {
            break;
        }
        current = node->parent_handle;
    }
    return UI_INVALID_HANDLE;
}

void SelectionCoordinator::CollectAreaNodes(
    const SelectionHost& host,
    std::uint64_t handle,
    std::vector<std::uint64_t>& out) const {
    const UINode* node = host.ResolveSelectionNode(handle);
    if (node == nullptr || !node->is_active || node->visibility != UI_VISIBILITY_NORMAL) {
        return;
    }
    if (node->is_text_node && node->is_selectable && !node->is_obscured) {
        out.push_back(handle);
    }
    if (node->is_selection_area_barrier) {
        return;
    }
    for (const std::uint64_t child_handle : node->children) {
        CollectAreaNodes(host, child_handle, out);
    }
}

void SelectionCoordinator::EnsureAreaNodes(SelectionHost& host, std::uint64_t area_handle) {
    if (area_handle == UI_INVALID_HANDLE) {
        state_.area_nodes.clear();
        state_.area_handle = UI_INVALID_HANDLE;
        state_.area_nodes_dirty = false;
        return;
    }
    if (state_.area_handle != area_handle) {
        state_.area_nodes_dirty = true;
    }
    state_.area_handle = area_handle;
    if (!state_.area_nodes_dirty && !state_.area_nodes.empty()) {
        return;
    }
    state_.area_nodes.clear();
    CollectAreaNodes(host, area_handle, state_.area_nodes);
    state_.area_nodes_dirty = false;
}

int SelectionCoordinator::FindAreaNodeIndex(std::uint64_t handle) const {
    const auto it = std::find(state_.area_nodes.begin(), state_.area_nodes.end(), handle);
    return it == state_.area_nodes.end()
        ? -1
        : static_cast<int>(std::distance(state_.area_nodes.begin(), it));
}

void SelectionCoordinator::MarkAreaNodesDirty(SelectionHost& host) {
    for (const std::uint64_t handle : state_.area_nodes) {
        if (UINode* node = host.ResolveSelectionNodeMutable(handle); node != nullptr) {
            node->is_dirty = true;
        }
    }
    host.MarkSelectionLayoutDirty();
}

void SelectionCoordinator::ClearCrossSelection(SelectionHost& host, bool notify_callback) {
    if (!state_.area_nodes.empty()) {
        MarkAreaNodesDirty(host);
    }
    state_.hit_rects.clear();
    if (notify_callback && state_.area_handle != UI_INVALID_HANDLE) {
        host.NotifyCrossSelectionChanged(state_.area_handle, {});
    }
    state_.cross_active = false;
    state_.cross_dragged = false;
    state_.cross_horizontal_extend_active = false;
    state_.area_handle = UI_INVALID_HANDLE;
    state_.start_node_handle = UI_INVALID_HANDLE;
    state_.start_index = 0U;
    state_.end_node_handle = UI_INVALID_HANDLE;
    state_.end_index = 0U;
    state_.area_nodes.clear();
    state_.area_nodes_dirty = false;
}

bool SelectionCoordinator::UpdateCrossSelectionEndpoint(
    SelectionHost& host,
    std::uint64_t handle,
    float logical_x,
    float logical_y) {
    if (!state_.cross_active) {
        return false;
    }
    EnsureAreaNodes(host, state_.area_handle);
    if (state_.area_nodes.empty()) {
        return false;
    }

    std::uint64_t target_handle = UI_INVALID_HANDLE;
    std::uint32_t target_index = 0U;
    if (handle != UI_INVALID_HANDLE && FindAreaNodeIndex(handle) >= 0) {
        if (const UINode* node = host.ResolveSelectionNode(handle); node != nullptr) {
            const auto [clamped_x, clamped_y] = ClampSelectionPointToNodeRows(*node, logical_x, logical_y);
            target_handle = handle;
            target_index = host.GetSelectionIndexFromPoint(
                *node,
                clamped_x - node->abs_x,
                clamped_y - node->abs_y);
        }
    }
    if (target_handle == UI_INVALID_HANDLE) {
        float best_vertical_distance = std::numeric_limits<float>::max();
        float best_horizontal_distance = std::numeric_limits<float>::max();
        for (const std::uint64_t candidate_handle : state_.area_nodes) {
            const UINode* node = host.ResolveSelectionNode(candidate_handle);
            if (node == nullptr) {
                continue;
            }
            const float right = node->abs_x + std::max(0.0f, node->layout_width);
            const float bottom = node->abs_y + std::max(0.0f, node->layout_height);
            const float vertical_distance = logical_y < node->abs_y
                ? node->abs_y - logical_y
                : (logical_y > bottom ? logical_y - bottom : 0.0f);
            const float horizontal_distance = logical_x < node->abs_x
                ? node->abs_x - logical_x
                : (logical_x > right ? logical_x - right : 0.0f);
            if (vertical_distance > best_vertical_distance ||
                (vertical_distance == best_vertical_distance && horizontal_distance >= best_horizontal_distance)) {
                continue;
            }
            const auto [clamped_x, clamped_y] = ClampSelectionPointToNodeRows(*node, logical_x, logical_y);
            target_handle = candidate_handle;
            target_index = host.GetSelectionIndexFromPoint(
                *node,
                clamped_x - node->abs_x,
                clamped_y - node->abs_y);
            best_vertical_distance = vertical_distance;
            best_horizontal_distance = horizontal_distance;
        }
    }
    if (target_handle == UI_INVALID_HANDLE) {
        return false;
    }

    if (state_.handle_drag_active && state_.drag_endpoint == 0U) {
        if (state_.start_node_handle == target_handle && state_.start_index == target_index) {
            return false;
        }
        state_.start_node_handle = target_handle;
        state_.start_index = target_index;
    } else {
        if (state_.end_node_handle == target_handle && state_.end_index == target_index) {
            return false;
        }
        state_.end_node_handle = target_handle;
        state_.end_index = target_index;
    }
    MarkAreaNodesDirty(host);
    return true;
}

bool SelectionCoordinator::GetCrossSelectionHighlight(
    const SelectionHost& host,
    std::uint64_t handle,
    std::uint32_t& out_start,
    std::uint32_t& out_end) const {
    if (!state_.cross_active || state_.area_handle == UI_INVALID_HANDLE) {
        return false;
    }
    const UINode* node = host.ResolveSelectionNode(handle);
    if (node == nullptr || FindAreaAncestor(host, handle) != state_.area_handle) {
        return false;
    }
    const int start_position = FindAreaNodeIndex(state_.start_node_handle);
    const int end_position = FindAreaNodeIndex(state_.end_node_handle);
    const int node_index = FindAreaNodeIndex(handle);
    if (start_position < 0 || end_position < 0) {
        return false;
    }
    const bool forward = start_position < end_position ||
        (start_position == end_position && state_.start_index <= state_.end_index);
    const int first_index = forward ? start_position : end_position;
    const int last_index = forward ? end_position : start_position;
    if (node_index < first_index || node_index > last_index) {
        return false;
    }
    const std::uint32_t first_offset = forward ? state_.start_index : state_.end_index;
    const std::uint32_t last_offset = forward ? state_.end_index : state_.start_index;
    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    if (first_index == last_index) {
        out_start = std::min(first_offset, text_length);
        out_end = std::min(last_offset, text_length);
    } else if (node_index == first_index) {
        out_start = std::min(first_offset, text_length);
        out_end = text_length;
    } else if (node_index == last_index) {
        out_start = 0U;
        out_end = std::min(last_offset, text_length);
    } else {
        out_start = 0U;
        out_end = text_length;
    }
    return out_start != out_end;
}

bool SelectionCoordinator::GetCrossSelectionEndpointSceneRects(
    SelectionHost& host,
    std::uint64_t area_handle,
    Rect& out_start_rect,
    Rect& out_end_rect) {
    if (!state_.cross_active || state_.area_handle == UI_INVALID_HANDLE || area_handle != state_.area_handle) {
        return false;
    }
    EnsureAreaNodes(host, state_.area_handle);
    const int start_position = FindAreaNodeIndex(state_.start_node_handle);
    const int end_position = FindAreaNodeIndex(state_.end_node_handle);
    if (start_position < 0 || end_position < 0) {
        return false;
    }
    const bool forward = start_position < end_position ||
        (start_position == end_position && state_.start_index <= state_.end_index);
    const int first_position = forward ? start_position : end_position;
    const int last_position = forward ? end_position : start_position;
    const std::uint64_t first_handle = forward ? state_.start_node_handle : state_.end_node_handle;
    const std::uint64_t last_handle = forward ? state_.end_node_handle : state_.start_node_handle;
    const std::uint32_t first_offset = forward ? state_.start_index : state_.end_index;
    const std::uint32_t last_offset = forward ? state_.end_index : state_.start_index;
    const UINode* first_node = host.ResolveSelectionNode(first_handle);
    const UINode* last_node = host.ResolveSelectionNode(last_handle);
    if (first_node == nullptr || last_node == nullptr) {
        return false;
    }
    const std::vector<Rect> first_rects = host.GetSelectionRangeSceneRects(
        first_handle,
        first_offset,
        first_position == last_position ? last_offset : static_cast<std::uint32_t>(first_node->text_content.size()));
    const std::vector<Rect> last_rects = first_position == last_position
        ? first_rects
        : host.GetSelectionRangeSceneRects(last_handle, 0U, last_offset);
    if (first_rects.empty() || last_rects.empty()) {
        return false;
    }
    const Rect& first_rect = first_rects.front();
    const Rect& last_rect = last_rects.back();
    if (forward) {
        out_start_rect = Rect{first_rect.x, first_rect.y, 0.0f, first_rect.height};
        out_end_rect = Rect{last_rect.x + last_rect.width, last_rect.y, 0.0f, last_rect.height};
    } else {
        out_start_rect = Rect{last_rect.x + last_rect.width, last_rect.y, 0.0f, last_rect.height};
        out_end_rect = Rect{first_rect.x, first_rect.y, 0.0f, first_rect.height};
    }
    return true;
}

void SelectionCoordinator::NormalizeCrossSelectionEndpoints() {
    const int start_position = FindAreaNodeIndex(state_.start_node_handle);
    const int end_position = FindAreaNodeIndex(state_.end_node_handle);
    if (start_position < 0 || end_position < 0 ||
        (start_position < end_position || (start_position == end_position && state_.start_index <= state_.end_index))) {
        return;
    }
    std::swap(state_.start_node_handle, state_.end_node_handle);
    std::swap(state_.start_index, state_.end_index);
}

void SelectionCoordinator::BeginCrossSelection(
    SelectionHost& host,
    std::uint64_t area_handle,
    std::uint64_t start_node_handle,
    std::uint32_t start_index,
    std::uint64_t end_node_handle,
    std::uint32_t end_index,
    bool horizontal_extend_active) {
    EnsureAreaNodes(host, area_handle);
    state_.hit_rects.clear();
    state_.horizontal_extend_active = false;
    state_.cross_active = true;
    state_.cross_dragged = false;
    state_.cross_horizontal_extend_active = horizontal_extend_active;
    state_.area_handle = area_handle;
    state_.start_node_handle = start_node_handle;
    state_.start_index = start_index;
    state_.end_node_handle = end_node_handle;
    state_.end_index = end_index;
    state_.active_handle = UI_INVALID_HANDLE;
    state_.active_dragged = false;
    MarkAreaNodesDirty(host);
}

void SelectionCoordinator::BeginNodeSelection(
    UINode& node,
    std::uint64_t handle,
    std::uint32_t start,
    std::uint32_t end,
    bool horizontal_extend_active) {
    state_.hit_rects.clear();
    state_.horizontal_extend_active = horizontal_extend_active;
    state_.active_handle = handle;
    state_.active_dragged = false;
    state_.anchor_handle = handle;
    state_.anchor_index = start;
    node.selection_start = start;
    node.selection_end = end;
}

bool SelectionCoordinator::BeginCrossSelectionEndpointDrag(SelectionHost& host, std::uint32_t endpoint) {
    if (endpoint > 1U || !state_.cross_active || state_.area_handle == UI_INVALID_HANDLE) {
        return false;
    }
    EnsureAreaNodes(host, state_.area_handle);
    if (state_.area_nodes.empty() || state_.start_node_handle == UI_INVALID_HANDLE ||
        state_.end_node_handle == UI_INVALID_HANDLE ||
        (state_.start_node_handle == state_.end_node_handle && state_.start_index == state_.end_index)) {
        return false;
    }
    state_.cross_dragged = true;
    state_.cross_horizontal_extend_active = true;
    state_.handle_drag_active = true;
    state_.drag_endpoint = endpoint;
    state_.stationary_index = 0U;
    state_.active_handle = UI_INVALID_HANDLE;
    state_.active_dragged = false;
    MarkAreaNodesDirty(host);
    return true;
}

bool SelectionCoordinator::BeginNodeSelectionEndpointDrag(
    SelectionHost& host,
    UINode& node,
    std::uint64_t handle,
    std::uint32_t endpoint) {
    if (endpoint > 1U || !node.is_text_node || !node.is_selectable || node.selection_start == node.selection_end) {
        return false;
    }
    state_.handle_drag_active = true;
    state_.drag_endpoint = endpoint;
    state_.stationary_index = endpoint == 0U ? node.selection_end : node.selection_start;
    state_.active_handle = handle;
    state_.active_dragged = true;
    state_.horizontal_extend_active = true;
    state_.anchor_handle = handle;
    state_.anchor_index = node.selection_start;
    host.MarkSelectionVisualsDirty(node);
    return true;
}

void SelectionCoordinator::InvalidateSubtree(
    SelectionHost& host,
    const NodeReader& nodes,
    std::uint64_t subtree_root) {
    if (nodes.SubtreeContains(subtree_root, state_.active_handle)) {
        state_.active_handle = UI_INVALID_HANDLE;
    }
    if (state_.area_handle == UI_INVALID_HANDLE) {
        return;
    }
    state_.area_nodes_dirty = true;
    if (state_.cross_active &&
        (nodes.SubtreeContains(subtree_root, state_.area_handle) ||
         nodes.SubtreeContains(subtree_root, state_.start_node_handle) ||
         nodes.SubtreeContains(subtree_root, state_.end_node_handle))) {
        ClearCrossSelection(host, false);
    }
}

void SelectionCoordinator::ReconcileRoot(
    SelectionHost& host,
    const NodeReader& nodes,
    std::uint64_t root_handle) {
    if (root_handle == UI_INVALID_HANDLE) {
        state_.active_handle = UI_INVALID_HANDLE;
        ClearCrossSelection(host, false);
        state_.area_nodes_dirty = true;
        return;
    }
    if (!nodes.SubtreeContains(root_handle, state_.active_handle)) {
        state_.active_handle = UI_INVALID_HANDLE;
    }
    if (state_.area_handle != UI_INVALID_HANDLE && !nodes.SubtreeContains(root_handle, state_.area_handle)) {
        ClearCrossSelection(host, false);
    }
    state_.area_nodes_dirty = true;
}

void SelectionCoordinator::MarkAreaTopologyDirty() {
    if (state_.area_handle != UI_INVALID_HANDLE) {
        state_.area_nodes_dirty = true;
    }
}

void SelectionCoordinator::RemoveArea(std::uint64_t area_handle, SelectionHost& host) {
    if (state_.area_handle != area_handle) {
        return;
    }
    state_.area_nodes_dirty = true;
    ClearCrossSelection(host, false);
}

bool SelectionCoordinator::HitTests(float logical_x, float logical_y) const {
    return std::any_of(state_.hit_rects.begin(), state_.hit_rects.end(), [logical_x, logical_y](const Rect& rect) {
        return logical_x >= rect.x && logical_x <= rect.x + rect.width &&
            logical_y >= rect.y && logical_y <= rect.y + rect.height;
    });
}

std::string SelectionCoordinator::BuildCrossSelectionText(const SelectionHost& host) const {
    if (!state_.cross_active || state_.area_handle == UI_INVALID_HANDLE || state_.area_nodes.empty()) {
        return {};
    }
    const int start_position = FindAreaNodeIndex(state_.start_node_handle);
    const int end_position = FindAreaNodeIndex(state_.end_node_handle);
    if (start_position < 0 || end_position < 0) {
        return {};
    }
    const bool forward = start_position < end_position ||
        (start_position == end_position && state_.start_index <= state_.end_index);
    const int first_index = forward ? start_position : end_position;
    const int last_index = forward ? end_position : start_position;
    const std::uint32_t first_offset = forward ? state_.start_index : state_.end_index;
    const std::uint32_t last_offset = forward ? state_.end_index : state_.start_index;
    std::string stitched{};
    for (int index = first_index; index <= last_index; index += 1) {
        const UINode* node = host.ResolveSelectionNode(state_.area_nodes[static_cast<std::size_t>(index)]);
        if (node == nullptr || node->is_obscured) {
            continue;
        }
        const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
        std::uint32_t slice_start = 0U;
        std::uint32_t slice_end = text_length;
        if (first_index == last_index) {
            slice_start = std::min(first_offset, text_length);
            slice_end = std::min(last_offset, text_length);
        } else if (index == first_index) {
            slice_start = std::min(first_offset, text_length);
        } else if (index == last_index) {
            slice_end = std::min(last_offset, text_length);
        }
        if (slice_start >= slice_end) {
            continue;
        }
        if (!stitched.empty()) {
            const UINode* previous = host.ResolveSelectionNode(state_.area_nodes[static_cast<std::size_t>(index - 1)]);
            stitched.push_back(IsHorizontalContainer(previous == nullptr ? nullptr : host.ResolveSelectionNode(previous->parent_handle)) ? ' ' : '\n');
        }
        stitched.append(node->text_content.substr(slice_start, static_cast<std::size_t>(slice_end - slice_start)));
    }
    return stitched;
}

void SelectionCoordinator::NotifyCrossSelectionChanged(const SelectionHost& host) const {
    host.NotifyCrossSelectionChanged(state_.area_handle, BuildCrossSelectionText(host));
}

bool SelectionCoordinator::HandleCrossSelectionNavigation(
    SelectionHost& host,
    std::uint64_t area_handle,
    UINode& focused_node,
    std::string_view key,
    std::uint32_t modifiers) {
    EnsureAreaNodes(host, area_handle);
    if (state_.area_nodes.empty()) {
        return false;
    }
    const bool by_word = host.HasSelectionWordNavigationModifier(modifiers);
    const bool shift = (modifiers & UI_KEY_MOD_SHIFT) != 0U;
    const bool is_move_key = key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" || key == "ArrowDown" ||
        key == "PageUp" || key == "PageDown" || key == "Home" || key == "End";
    const bool is_directional_key =
        key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" || key == "ArrowDown" ||
        key == "PageUp" || key == "PageDown";
    if (!shift || !is_move_key) {
        return false;
    }
    const bool is_horizontal_move = key == "ArrowLeft" || key == "ArrowRight";
    const bool focused_has_selection = focused_node.selection_start != focused_node.selection_end;
    const bool has_existing_highlight =
        state_.cross_horizontal_extend_active ||
        (state_.cross_active && state_.area_handle == area_handle &&
         (state_.start_node_handle != state_.end_node_handle || state_.start_index != state_.end_index)) ||
        focused_has_selection;
    if (is_directional_key && !has_existing_highlight) {
        return false;
    }
    const std::uint64_t focused_handle = host.FocusedSelectionHandle();
    if (!state_.cross_active || state_.area_handle != area_handle) {
        state_.cross_active = true;
        state_.area_handle = area_handle;
        state_.start_node_handle = focused_handle;
        state_.start_index = is_directional_key ? focused_node.selection_start : focused_node.selection_end;
        state_.end_node_handle = focused_handle;
        state_.end_index = focused_node.selection_end;
    }
    int current_index = FindAreaNodeIndex(state_.end_node_handle);
    if (current_index < 0) {
        current_index = FindAreaNodeIndex(focused_handle);
        if (current_index < 0) {
            return false;
        }
        state_.end_node_handle = state_.area_nodes[static_cast<std::size_t>(current_index)];
        state_.end_index = focused_node.selection_end;
    }
    const UINode* current_node = host.ResolveSelectionNode(state_.end_node_handle);
    if (current_node == nullptr) {
        return false;
    }
    std::uint64_t next_handle = state_.end_node_handle;
    std::uint32_t next_index = state_.end_index;
    if (key == "Home") {
        next_handle = state_.area_nodes.front();
        next_index = 0U;
    } else if (key == "End") {
        next_handle = state_.area_nodes.back();
        if (const UINode* last_node = host.ResolveSelectionNode(next_handle); last_node != nullptr) {
            next_index = static_cast<std::uint32_t>(last_node->text_content.size());
        }
    } else if (key == "ArrowLeft" || key == "ArrowRight") {
        const bool forward = key == "ArrowRight";
        const std::uint32_t moved = by_word
            ? host.NextSelectionWordIndex(*current_node, next_index, forward)
            : host.NextSelectionCharacterIndex(current_node->text_content, next_index, forward);
        if (moved != next_index) {
            next_index = moved;
        } else if (forward && current_index + 1 < static_cast<int>(state_.area_nodes.size())) {
            next_handle = state_.area_nodes[static_cast<std::size_t>(current_index + 1)];
            next_index = 0U;
        } else if (!forward && current_index > 0) {
            next_handle = state_.area_nodes[static_cast<std::size_t>(current_index - 1)];
            if (const UINode* previous_node = host.ResolveSelectionNode(next_handle); previous_node != nullptr) {
                next_index = static_cast<std::uint32_t>(previous_node->text_content.size());
            }
        }
    } else {
        const bool down = key == "ArrowDown" || key == "PageDown";
        const auto [local_x, current_line] = host.GetSelectionLocalPositionFromIndex(*current_node, next_index);
        const float preferred_absolute_x = current_node->abs_x + local_x;
        const std::uint32_t moved =
            key == "PageUp" || key == "PageDown"
            ? host.GetSelectionIndexForPageMove(*current_node, next_index, down)
            : host.GetSelectionIndexForVerticalMove(*current_node, next_index, down);
        const auto [ignored_local_x, moved_line] = host.GetSelectionLocalPositionFromIndex(*current_node, moved);
        (void)ignored_local_x;
        if (moved_line != current_line) {
            next_index = moved;
        } else {
            const auto visible_line_count = [](const UINode& node) -> std::size_t {
                const std::size_t available = node.break_offsets.size() > 1U ? node.break_offsets.size() - 1U : 0U;
                return node.visible_line_count == 0U ? available : std::min(node.visible_line_count, available);
            };
            int adjacent_index = current_index + (down ? 1 : -1);
            while (adjacent_index >= 0 && adjacent_index < static_cast<int>(state_.area_nodes.size())) {
                const std::uint64_t candidate_handle = state_.area_nodes[static_cast<std::size_t>(adjacent_index)];
                const UINode* target = host.ResolveSelectionNode(candidate_handle);
                if (target != nullptr) {
                    const std::size_t line_count = visible_line_count(*target);
                    std::size_t best_line = down ? 0U : (line_count == 0U ? 0U : line_count - 1U);
                    bool best_contains = false;
                    float best_distance = std::numeric_limits<float>::max();
                    for (std::size_t line_index = 0; line_index < line_count; line_index += 1U) {
                        const float line_width = line_index < target->line_widths.size() ? target->line_widths[line_index] : 0.0f;
                        const float line_left = target->abs_x + host.GetSelectionAlignedLineXOffset(*target, line_width);
                        const float line_right = line_left + line_width;
                        const bool contains = preferred_absolute_x >= line_left && preferred_absolute_x <= line_right;
                        const float distance = contains ? 0.0f : std::abs(preferred_absolute_x - (line_left + line_width * 0.5f));
                        constexpr float kLineDistanceEpsilon = 0.001f;
                        const bool is_better =
                            (contains && !best_contains) ||
                            (contains == best_contains && distance + kLineDistanceEpsilon < best_distance) ||
                            (contains == best_contains && std::abs(distance - best_distance) <= kLineDistanceEpsilon &&
                             ((down && line_index < best_line) || (!down && line_index > best_line)));
                        if (is_better) {
                            best_line = line_index;
                            best_contains = contains;
                            best_distance = distance;
                        }
                    }
                    next_handle = candidate_handle;
                    const float target_content_offset_y = host.GetSelectionAlignedTextYOffset(
                        *target,
                        host.GetSelectionTextContentHeight(*target, visible_line_count(*target)));
                    const float target_local_y = target_content_offset_y +
                        host.GetSelectionLineTopForIndex(*target, best_line) +
                        host.GetSelectionLineHeightForIndex(*target, best_line) * 0.5f;
                    next_index = host.GetSelectionIndexFromPoint(*target, preferred_absolute_x - target->abs_x, target_local_y);
                    break;
                }
                adjacent_index += down ? 1 : -1;
            }
        }
    }
    if (next_handle == state_.end_node_handle && next_index == state_.end_index) {
        return true;
    }
    state_.end_node_handle = next_handle;
    state_.end_index = next_index;
    if (focused_handle != state_.end_node_handle) {
        host.SetSelectionFocus(state_.end_node_handle);
    }
    state_.cross_horizontal_extend_active = is_horizontal_move;
    MarkAreaNodesDirty(host);
    NotifyCrossSelectionChanged(host);
    return true;
}

bool SelectionCoordinator::HandleCrossSelectionPointer(
    SelectionHost& host,
    const CrossSelectionPointerRequest& request) {
    if (!state_.cross_active ||
        !((request.pointer_move && request.primary_pointer_down) || request.pointer_up)) {
        return false;
    }
    const float selection_drag_logical_y = state_.handle_drag_active
        ? request.logical_y - request.handle_center_to_text_hit_offset
        : request.logical_y;
    const bool is_drag_update = request.pointer_move && request.primary_pointer_down;
    if (is_drag_update && request.allow_text_drag) {
        state_.cross_dragged = state_.cross_dragged || request.drag_threshold_exceeded;
    }
    const std::uint64_t selection_handle =
        state_.handle_drag_active && state_.drag_endpoint == 0U && state_.start_node_handle != UI_INVALID_HANDLE
        ? state_.start_node_handle
        : (state_.end_node_handle != UI_INVALID_HANDLE ? state_.end_node_handle :
            (request.handle != UI_INVALID_HANDLE ? request.handle : state_.area_handle));
    bool handled = false;
    if (state_.cross_dragged) {
        const bool target_in_area = request.handle != UI_INVALID_HANDLE &&
            FindAreaAncestor(host, request.handle) == state_.area_handle;
        if (target_in_area) {
            handled = UpdateCrossSelectionEndpoint(host, request.handle, request.logical_x, selection_drag_logical_y);
        } else {
            const auto [clamped_x, clamped_y] = host.ClampSelectionPointToViewport(
                selection_handle, request.logical_x, selection_drag_logical_y);
            const bool point_was_clamped =
                clamped_x != request.logical_x || clamped_y != selection_drag_logical_y;
            handled = UpdateCrossSelectionEndpoint(
                host,
                point_was_clamped ? static_cast<std::uint64_t>(UI_INVALID_HANDLE) : request.handle,
                clamped_x,
                clamped_y);
        }
    }
    if (request.pointer_up) {
        if (BuildCrossSelectionText(host).empty()) {
            ClearCrossSelection(host, true);
        } else {
            if (state_.handle_drag_active) {
                NormalizeCrossSelectionEndpoints();
            }
            state_.cross_horizontal_extend_active = true;
            NotifyCrossSelectionChanged(host);
        }
        state_.cross_dragged = false;
        state_.handle_drag_active = false;
        state_.drag_endpoint = 1U;
    } else if (state_.handle_drag_active && state_.cross_dragged) {
        NotifyCrossSelectionChanged(host);
    }
    return handled;
}

NodeSelectionPointerResult SelectionCoordinator::HandleNodeSelectionPointer(
    SelectionHost& host,
    const NodeSelectionPointerRequest& request) {
    NodeSelectionPointerResult result{};
    UINode* node = host.ResolveSelectionNodeMutable(state_.active_handle);
    if (node == nullptr || !node->is_text_node || !node->is_selectable) {
        if (node == nullptr) {
            state_.active_handle = UI_INVALID_HANDLE;
        }
        return result;
    }
    const bool update_drag = request.allow_text_drag && request.pointer_move && request.primary_pointer_down;
    if (update_drag) {
        state_.active_dragged = state_.active_dragged || request.drag_threshold_exceeded;
        if (state_.active_dragged) {
            const float selection_drag_logical_y = state_.handle_drag_active
                ? request.logical_y - request.handle_center_to_text_hit_offset
                : request.logical_y;
            const auto [viewport_x, viewport_y] = host.ClampSelectionPointToViewport(
                state_.active_handle, request.logical_x, selection_drag_logical_y);
            const auto [clamped_x, clamped_y] = ClampSelectionPointToNodeRows(
                *node, viewport_x, viewport_y);
            const std::uint32_t next_selection_end = host.GetSelectionIndexFromPoint(
                *node, clamped_x - node->abs_x, clamped_y - node->abs_y);
            std::uint32_t& dragged_endpoint = state_.drag_endpoint == 0U
                ? node->selection_start
                : node->selection_end;
            const std::uint32_t previous_start = node->selection_start;
            const std::uint32_t previous_end = node->selection_end;
            if (state_.handle_drag_active) {
                node->selection_start = std::min(state_.stationary_index, next_selection_end);
                node->selection_end = std::max(state_.stationary_index, next_selection_end);
            } else {
                dragged_endpoint = next_selection_end;
            }
            if (node->selection_start != previous_start || node->selection_end != previous_end) {
                host.MarkSelectionVisualsDirty(*node);
                if (state_.handle_drag_active) {
                    host.NotifySelectionChanged(state_.active_handle, node->selection_start, node->selection_end);
                }
            }
        }
        return result;
    }
    if (!request.pointer_up) {
        return result;
    }
    if (state_.active_dragged) {
        const float selection_drag_logical_y = state_.handle_drag_active
            ? request.logical_y - request.handle_center_to_text_hit_offset
            : request.logical_y;
        const auto [viewport_x, viewport_y] = host.ClampSelectionPointToViewport(
            state_.active_handle, request.logical_x, selection_drag_logical_y);
        const auto [clamped_x, clamped_y] = ClampSelectionPointToNodeRows(
            *node, viewport_x, viewport_y);
        const std::uint32_t next_selection_end = host.GetSelectionIndexFromPoint(
            *node, clamped_x - node->abs_x, clamped_y - node->abs_y);
        std::uint32_t& dragged_endpoint = state_.drag_endpoint == 0U
            ? node->selection_start
            : node->selection_end;
        if (state_.handle_drag_active) {
            node->selection_start = std::min(state_.stationary_index, next_selection_end);
            node->selection_end = std::max(state_.stationary_index, next_selection_end);
        } else {
            dragged_endpoint = next_selection_end;
        }
    }
    result.finished = true;
    result.handle = state_.active_handle;
    node->last_interaction_time = request.interaction_time_ms;
    host.MarkSelectionVisualsDirty(*node);
    state_.horizontal_extend_active = node->selection_start != node->selection_end;
    host.NotifySelectionChanged(state_.active_handle, node->selection_start, node->selection_end);
    state_.active_handle = UI_INVALID_HANDLE;
    state_.active_dragged = false;
    state_.handle_drag_active = false;
    state_.drag_endpoint = 1U;
    state_.stationary_index = 0U;
    return result;
}

void SelectionCoordinator::UpdateAutoScrollSelection(
    SelectionHost& host,
    const SelectionAutoScrollRequest& request,
    std::uint64_t handle,
    UINode& node,
    float abs_x,
    float abs_y,
    float width,
    float height) {
    if (!request.active || !node.is_text_node || !node.is_selectable) {
        return;
    }
    if (request.touch_tap_moved && request.touch_tap_handle == handle &&
        (node.is_editable || request.is_editor_text_node)) {
        const auto [clamped_x, clamped_y] = host.ClampSelectionPointToViewport(
            handle, request.logical_x, request.logical_y);
        const std::uint32_t next_selection = host.GetSelectionIndexFromPoint(
            node, clamped_x - abs_x, clamped_y - abs_y);
        if (node.selection_start != next_selection || node.selection_end != next_selection) {
            node.selection_start = next_selection;
            node.selection_end = next_selection;
            node.caret_trailing_edge = false;
            host.MarkSelectionVisualsDirty(node);
            host.NotifySelectionChanged(handle, node.selection_start, node.selection_end);
        }
        return;
    }
    if (state_.active_handle == handle) {
        const auto [clamped_x, clamped_y] = host.ClampSelectionPointToViewport(
            handle, request.logical_x, request.logical_y);
        const std::uint32_t next_selection = host.GetSelectionIndexFromPoint(
            node, clamped_x - abs_x, clamped_y - abs_y);
        std::uint32_t& dragged_endpoint = state_.drag_endpoint == 0U
            ? node.selection_start
            : node.selection_end;
        const std::uint32_t previous_start = node.selection_start;
        const std::uint32_t previous_end = node.selection_end;
        if (state_.handle_drag_active) {
            node.selection_start = std::min(state_.stationary_index, next_selection);
            node.selection_end = std::max(state_.stationary_index, next_selection);
        } else {
            dragged_endpoint = next_selection;
        }
        if (node.selection_start != previous_start || node.selection_end != previous_end) {
            host.NotifySelectionChanged(handle, node.selection_start, node.selection_end);
        }
        return;
    }
    if (!state_.cross_active || FindAreaAncestor(host, handle) != state_.area_handle) {
        return;
    }
    const auto [clamped_x, clamped_y] = host.ClampSelectionPointToViewport(
        state_.end_node_handle != UI_INVALID_HANDLE ? state_.end_node_handle : handle,
        request.logical_x,
        request.logical_y);
    if (clamped_x >= abs_x && clamped_x <= abs_x + width &&
        clamped_y >= abs_y && clamped_y <= abs_y + height) {
        (void)UpdateCrossSelectionEndpoint(host, handle, clamped_x, clamped_y);
    }
}

} // namespace effindom::v2::ui
