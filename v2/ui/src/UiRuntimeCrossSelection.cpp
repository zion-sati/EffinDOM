#include "UiRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace effindom::v2::ui {

namespace {

bool IsHorizontalContainer(const UINode* node) {
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    return YGNodeStyleGetFlexDirection(node->yg_node) == YGFlexDirectionRow;
}

} // namespace

std::uint64_t UiRuntime::FindSelectionAreaAncestor(std::uint64_t start_handle) const {
    for (std::uint64_t current = start_handle; current != UI_INVALID_HANDLE;) {
        const UINode* node = Resolve(current);
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

void UiRuntime::CollectSelectionAreaNodes(std::uint64_t handle, std::vector<std::uint64_t>& out) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr) {
        return;
    }
    if (node->is_text_node && node->is_selectable && !node->is_obscured) {
        out.push_back(handle);
    }
    if (node->is_selection_area_barrier) {
        return;
    }
    for (const std::uint64_t child_handle : node->children) {
        CollectSelectionAreaNodes(child_handle, out);
    }
}

void UiRuntime::EnsureSelectionAreaNodes(std::uint64_t area_handle) {
    if (area_handle == UI_INVALID_HANDLE) {
        selection_area_nodes_.clear();
        selection_area_handle_ = UI_INVALID_HANDLE;
        selection_area_nodes_dirty_ = false;
        return;
    }
    if (selection_area_handle_ != area_handle) {
        selection_area_nodes_dirty_ = true;
    }
    selection_area_handle_ = area_handle;
    if (!selection_area_nodes_dirty_ && !selection_area_nodes_.empty()) {
        return;
    }
    selection_area_nodes_.clear();
    CollectSelectionAreaNodes(area_handle, selection_area_nodes_);
    selection_area_nodes_dirty_ = false;
}

int UiRuntime::FindSelectionAreaNodeIndex(std::uint64_t handle) const {
    const auto it = std::find(selection_area_nodes_.begin(), selection_area_nodes_.end(), handle);
    if (it == selection_area_nodes_.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(selection_area_nodes_.begin(), it));
}

void UiRuntime::MarkSelectionAreaNodesDirty() {
    for (const std::uint64_t handle : selection_area_nodes_) {
        if (UINode* node = ResolveMutable(handle); node != nullptr) {
            node->is_dirty = true;
        }
    }
    layout_dirty_ = true;
}

void UiRuntime::ClearCrossSelection(bool notify_callback) {
    if (!selection_area_nodes_.empty()) {
        MarkSelectionAreaNodesDirty();
    }
    current_selection_hit_rects_.clear();
    if (notify_callback && selection_area_handle_ != UI_INVALID_HANDLE) {
        as_on_cross_selection_changed(selection_area_handle_, nullptr, 0U);
    }
    cross_selection_active_ = false;
    cross_selection_dragged_ = false;
    cross_selection_horizontal_extend_active_ = false;
    selection_area_handle_ = UI_INVALID_HANDLE;
    start_node_handle_ = UI_INVALID_HANDLE;
    start_index_ = 0U;
    end_node_handle_ = UI_INVALID_HANDLE;
    end_index_ = 0U;
    selection_area_nodes_.clear();
    selection_area_nodes_dirty_ = false;
}

bool UiRuntime::UpdateCrossSelectionEndpoint(std::uint64_t handle, float logical_x, float logical_y) {
    if (!cross_selection_active_) {
        return false;
    }

    EnsureSelectionAreaNodes(selection_area_handle_);
    if (selection_area_nodes_.empty()) {
        return false;
    }

    std::uint64_t target_handle = UI_INVALID_HANDLE;
    std::uint32_t target_index = 0U;

    if (handle != UI_INVALID_HANDLE) {
        const int direct_index = FindSelectionAreaNodeIndex(handle);
        if (direct_index >= 0) {
            if (const UINode* node = Resolve(handle); node != nullptr) {
                target_handle = handle;
                target_index = GetStringIndexFromPoint(*node, logical_x - node->abs_x, logical_y - node->abs_y);
            }
        }
    }

    if (target_handle == UI_INVALID_HANDLE) {
        for (const std::uint64_t candidate_handle : selection_area_nodes_) {
            const UINode* node = Resolve(candidate_handle);
            if (node == nullptr) {
                continue;
            }
            if (logical_x >= node->abs_x &&
                logical_x <= (node->abs_x + node->layout_width) &&
                logical_y >= node->abs_y &&
                logical_y <= (node->abs_y + node->layout_height)) {
                target_handle = candidate_handle;
                target_index = GetStringIndexFromPoint(*node, logical_x - node->abs_x, logical_y - node->abs_y);
                break;
            }
        }
    }

    if (target_handle == UI_INVALID_HANDLE) {
        return false;
    }

    if (selection_handle_drag_active_ && active_selection_drag_endpoint_ == 0U) {
        if (start_node_handle_ == target_handle && start_index_ == target_index) {
            return false;
        }
        start_node_handle_ = target_handle;
        start_index_ = target_index;
    } else {
        if (end_node_handle_ == target_handle && end_index_ == target_index) {
            return false;
        }
        end_node_handle_ = target_handle;
        end_index_ = target_index;
    }
    MarkSelectionAreaNodesDirty();
    return true;
}

bool UiRuntime::GetCrossSelectionHighlight(std::uint64_t handle, std::uint32_t& out_start, std::uint32_t& out_end) const {
    if (!cross_selection_active_ || selection_area_handle_ == UI_INVALID_HANDLE) {
        return false;
    }

    const UINode* node = Resolve(handle);
    if (node == nullptr) {
        return false;
    }
    if (FindSelectionAreaAncestor(handle) != selection_area_handle_) {
        return false;
    }

    const int start_position = FindSelectionAreaNodeIndex(start_node_handle_);
    const int end_position = FindSelectionAreaNodeIndex(end_node_handle_);
    if (start_position < 0 || end_position < 0) {
        return false;
    }
    const bool forward = start_position < end_position || (start_position == end_position && start_index_ <= end_index_);
    const int first_index = forward ? start_position : end_position;
    const int last_index = forward ? end_position : start_position;
    const std::uint32_t first_offset = forward ? start_index_ : end_index_;
    const std::uint32_t last_offset = forward ? end_index_ : start_index_;

    const int node_index = FindSelectionAreaNodeIndex(handle);
    if (node_index < first_index || node_index > last_index) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    if (first_index == last_index) {
        out_start = std::min(first_offset, text_length);
        out_end = std::min(last_offset, text_length);
        return out_start != out_end;
    }

    if (node_index == first_index) {
        out_start = std::min(first_offset, text_length);
        out_end = text_length;
        return out_start != out_end;
    }
    if (node_index == last_index) {
        out_start = 0U;
        out_end = std::min(last_offset, text_length);
        return out_start != out_end;
    }

    out_start = 0U;
    out_end = text_length;
    return out_start != out_end;
}

bool UiRuntime::GetCrossSelectionEndpointSceneRects(
    std::uint64_t area_handle,
    Rect& out_start_rect,
    Rect& out_end_rect) {
    if (!cross_selection_active_ || selection_area_handle_ == UI_INVALID_HANDLE || area_handle != selection_area_handle_) {
        return false;
    }

    EnsureSelectionAreaNodes(selection_area_handle_);
    const int start_position = FindSelectionAreaNodeIndex(start_node_handle_);
    const int end_position = FindSelectionAreaNodeIndex(end_node_handle_);
    if (start_position < 0 || end_position < 0) {
        return false;
    }

    const bool forward = start_position < end_position || (start_position == end_position && start_index_ <= end_index_);
    const int first_position = forward ? start_position : end_position;
    const int last_position = forward ? end_position : start_position;
    const std::uint64_t first_handle = forward ? start_node_handle_ : end_node_handle_;
    const std::uint64_t last_handle = forward ? end_node_handle_ : start_node_handle_;
    const std::uint32_t first_offset = forward ? start_index_ : end_index_;
    const std::uint32_t last_offset = forward ? end_index_ : start_index_;

    const UINode* first_node = Resolve(first_handle);
    const UINode* last_node = Resolve(last_handle);
    if (first_node == nullptr || last_node == nullptr) {
        return false;
    }

    if (first_position == last_position) {
        const std::vector<Rect> rects = GetTextRangeSceneRects(first_handle, first_offset, last_offset);
        if (rects.empty()) {
            return false;
        }
        const Rect& first_rect = rects.front();
        const Rect& last_rect = rects.back();
        if (forward) {
            out_start_rect = Rect{first_rect.x, first_rect.y, 0.0f, first_rect.height};
            out_end_rect = Rect{last_rect.x + last_rect.width, last_rect.y, 0.0f, last_rect.height};
        } else {
            out_start_rect = Rect{last_rect.x + last_rect.width, last_rect.y, 0.0f, last_rect.height};
            out_end_rect = Rect{first_rect.x, first_rect.y, 0.0f, first_rect.height};
        }
        return true;
    }

    const std::uint32_t first_text_length = static_cast<std::uint32_t>(first_node->text_content.size());
    const std::vector<Rect> first_rects = GetTextRangeSceneRects(first_handle, first_offset, first_text_length);
    const std::vector<Rect> last_rects = GetTextRangeSceneRects(last_handle, 0U, last_offset);
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

void UiRuntime::NormalizeCrossSelectionEndpoints() {
    const int start_position = FindSelectionAreaNodeIndex(start_node_handle_);
    const int end_position = FindSelectionAreaNodeIndex(end_node_handle_);
    if (start_position < 0 || end_position < 0) {
        return;
    }
    const bool reversed =
        start_position > end_position ||
        (start_position == end_position && start_index_ > end_index_);
    if (!reversed) {
        return;
    }
    std::swap(start_node_handle_, end_node_handle_);
    std::swap(start_index_, end_index_);
}

std::string UiRuntime::BuildCrossSelectionText() const {
    if (!cross_selection_active_ || selection_area_handle_ == UI_INVALID_HANDLE || selection_area_nodes_.empty()) {
        return {};
    }

    const int start_position = FindSelectionAreaNodeIndex(start_node_handle_);
    const int end_position = FindSelectionAreaNodeIndex(end_node_handle_);
    if (start_position < 0 || end_position < 0) {
        return {};
    }
    const bool forward = start_position < end_position || (start_position == end_position && start_index_ <= end_index_);
    const int first_index = forward ? start_position : end_position;
    const int last_index = forward ? end_position : start_position;
    const std::uint32_t first_offset = forward ? start_index_ : end_index_;
    const std::uint32_t last_offset = forward ? end_index_ : start_index_;

    std::string stitched{};
    for (int index = first_index; index <= last_index; index += 1) {
        const UINode* node = Resolve(selection_area_nodes_[static_cast<std::size_t>(index)]);
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

        if (slice_start < slice_end) {
            if (!stitched.empty()) {
                const UINode* previous = Resolve(selection_area_nodes_[static_cast<std::size_t>(index - 1)]);
                const char separator = IsHorizontalContainer(previous == nullptr ? nullptr : Resolve(previous->parent_handle))
                    ? ' '
                    : '\n';
                stitched.push_back(separator);
            }
            stitched.append(node->text_content.substr(slice_start, static_cast<std::size_t>(slice_end - slice_start)));
        }
    }
    return stitched;
}

void UiRuntime::NotifyCrossSelectionChanged() const {
    const std::string stitched = BuildCrossSelectionText();
    as_on_cross_selection_changed(
        selection_area_handle_,
        stitched.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(stitched.data()),
        static_cast<std::uint32_t>(stitched.size()));
}

bool UiRuntime::HandleCrossSelectionNavigation(
    std::uint64_t area_handle,
    UINode& focused_node,
    std::string_view key,
    std::uint32_t modifiers) {
    EnsureSelectionAreaNodes(area_handle);
    if (selection_area_nodes_.empty()) {
        return false;
    }

    const bool by_word = HasWordNavigationModifier(modifiers);
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
        cross_selection_horizontal_extend_active_ ||
        (cross_selection_active_ &&
         selection_area_handle_ == area_handle &&
         (start_node_handle_ != end_node_handle_ || start_index_ != end_index_)) ||
        focused_has_selection;
    if (is_directional_key && !has_existing_highlight) {
        return false;
    }

    if (!cross_selection_active_ || selection_area_handle_ != area_handle) {
        cross_selection_active_ = true;
        selection_area_handle_ = area_handle;
        start_node_handle_ = focused_handle_;
        start_index_ = is_directional_key ? focused_node.selection_start : focused_node.selection_end;
        end_node_handle_ = focused_handle_;
        end_index_ = focused_node.selection_end;
    }

    int current_index = FindSelectionAreaNodeIndex(end_node_handle_);
    if (current_index < 0) {
        current_index = FindSelectionAreaNodeIndex(focused_handle_);
        if (current_index < 0) {
            return false;
        }
        end_node_handle_ = selection_area_nodes_[static_cast<std::size_t>(current_index)];
        end_index_ = focused_node.selection_end;
    }

    const UINode* current_node = Resolve(end_node_handle_);
    if (current_node == nullptr) {
        return false;
    }

    std::uint64_t next_handle = end_node_handle_;
    std::uint32_t next_index = end_index_;

    if (key == "Home") {
        next_handle = selection_area_nodes_.front();
        next_index = 0U;
    } else if (key == "End") {
        next_handle = selection_area_nodes_.back();
        if (const UINode* last_node = Resolve(next_handle); last_node != nullptr) {
            next_index = static_cast<std::uint32_t>(last_node->text_content.size());
        }
    } else if (key == "ArrowLeft" || key == "ArrowRight") {
        const bool forward = key == "ArrowRight";
        const std::uint32_t moved = by_word
            ? NextWordIndex(*current_node, next_index, forward)
            : NextCharacterIndex(current_node->text_content, next_index, forward);
        if (moved != next_index) {
            next_index = moved;
        } else if (forward && current_index + 1 < static_cast<int>(selection_area_nodes_.size())) {
            next_handle = selection_area_nodes_[static_cast<std::size_t>(current_index + 1)];
            next_index = 0U;
        } else if (!forward && current_index > 0) {
            next_handle = selection_area_nodes_[static_cast<std::size_t>(current_index - 1)];
            if (const UINode* previous_node = Resolve(next_handle); previous_node != nullptr) {
                next_index = static_cast<std::uint32_t>(previous_node->text_content.size());
            }
        }
    } else {
        const bool down = key == "ArrowDown" || key == "PageDown";
        const auto [local_x, current_line] = GetLocalPositionFromIndex(*current_node, next_index);
        const float preferred_absolute_x = current_node->abs_x + local_x;
        const std::uint32_t moved =
            key == "PageUp" || key == "PageDown"
            ? IndexForPageMove(*current_node, next_index, down)
            : IndexForVerticalMove(*current_node, next_index, down);
        const auto [_, moved_line] = GetLocalPositionFromIndex(*current_node, moved);
        if (moved_line != current_line) {
            next_index = moved;
        } else {
            const auto visible_line_count = [](const UINode& node) -> std::size_t {
                const std::size_t available = node.break_offsets.size() > 1U ? node.break_offsets.size() - 1U : 0U;
                if (node.visible_line_count == 0U) {
                    return available;
                }
                return std::min(node.visible_line_count, available);
            };
            const auto find_best_line_for_absolute_x =
                [this, &visible_line_count](const UINode& node, float absolute_x, bool move_down) -> std::size_t {
                const std::size_t line_count = visible_line_count(node);
                if (line_count == 0U) {
                    return 0U;
                }

                constexpr float kLineDistanceEpsilon = 0.001f;
                std::size_t best_line = move_down ? 0U : line_count - 1U;
                bool best_contains = false;
                float best_distance = std::numeric_limits<float>::max();
                for (std::size_t line_index = 0; line_index < line_count; line_index += 1U) {
                    const float line_width = line_index < node.line_widths.size() ? node.line_widths[line_index] : 0.0f;
                    const float line_left = node.abs_x + GetAlignedLineXOffset(node, line_width);
                    const float line_right = line_left + line_width;
                    const bool contains = absolute_x >= line_left && absolute_x <= line_right;
                    const float line_center = line_left + (line_width * 0.5f);
                    const float distance = contains ? 0.0f : std::abs(absolute_x - line_center);
                    const bool is_better =
                        (contains && !best_contains) ||
                        (contains == best_contains && distance + kLineDistanceEpsilon < best_distance) ||
                        (contains == best_contains && std::abs(distance - best_distance) <= kLineDistanceEpsilon &&
                         ((move_down && line_index < best_line) || (!move_down && line_index > best_line)));
                    if (!is_better) {
                        continue;
                    }
                    best_line = line_index;
                    best_contains = contains;
                    best_distance = distance;
                }
                return best_line;
            };

            int adjacent_index = current_index + (down ? 1 : -1);
            while (adjacent_index >= 0 && adjacent_index < static_cast<int>(selection_area_nodes_.size())) {
                const std::uint64_t candidate_handle = selection_area_nodes_[static_cast<std::size_t>(adjacent_index)];
                const UINode* target = Resolve(candidate_handle);
                if (target != nullptr) {
                    next_handle = candidate_handle;
                    const std::size_t target_line =
                        find_best_line_for_absolute_x(*target, preferred_absolute_x, down);
                    const float target_content_offset_y =
                        GetAlignedTextYOffset(
                            *target,
                            GetTextContentHeight(*target, visible_line_count(*target)));
                    const float target_local_y =
                        target_content_offset_y +
                        GetLineTopForIndex(*target, target_line) +
                        (GetLineHeightForIndex(*target, target_line) * 0.5f);
                    next_index =
                        GetStringIndexFromPoint(*target, preferred_absolute_x - target->abs_x, target_local_y);
                    break;
                }
                adjacent_index += down ? 1 : -1;
            }
        }
    }

    if (next_handle == end_node_handle_ && next_index == end_index_) {
        return true;
    }

    end_node_handle_ = next_handle;
    end_index_ = next_index;
    if (focused_handle_ != end_node_handle_) {
        SetFocus(end_node_handle_);
    }
    cross_selection_horizontal_extend_active_ = is_horizontal_move;
    MarkSelectionAreaNodesDirty();
    NotifyCrossSelectionChanged();
    return true;
}

} // namespace effindom::v2::ui
