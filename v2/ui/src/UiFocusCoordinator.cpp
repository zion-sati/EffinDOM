#include "UiFocusCoordinator.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace effindom::v2::ui {

namespace {

std::int32_t FocusOrderKey(const UINode* node) {
    return node == nullptr
        ? std::numeric_limits<std::int32_t>::max()
        : (node->tab_index > 0 ? node->tab_index : std::numeric_limits<std::int32_t>::max());
}

} // namespace

void FocusCoordinator::Reset() {
    focused_handle_ = UI_INVALID_HANDLE;
    pending_node_id_.clear();
    order_.clear();
    order_dirty_ = true;
}

void FocusCoordinator::SetFocusedHandle(std::uint64_t handle) {
    focused_handle_ = handle;
    if (focused_handle_ != UI_INVALID_HANDLE) pending_node_id_.clear();
}

void FocusCoordinator::NotifyChanged(std::uint64_t handle, bool focused) const {
    if (handle != UI_INVALID_HANDLE) events_.FocusChanged(handle, focused);
}

void FocusCoordinator::AppendFocusableHandles(std::uint64_t handle) {
    const UINode* node = nodes_.Resolve(handle);
    if (node == nullptr || node->visibility != UI_VISIBILITY_NORMAL) return;
    if (node->is_focusable && node->tab_index >= 0) order_.push_back(handle);
    for (const std::uint64_t child_handle : node->children) AppendFocusableHandles(child_handle);
}

void FocusCoordinator::RebuildOrder(std::uint64_t root_handle, std::uint64_t scope_root) {
    order_.clear();
    if (root_handle == UI_INVALID_HANDLE) {
        order_dirty_ = false;
        return;
    }
    AppendFocusableHandles(scope_root != UI_INVALID_HANDLE ? scope_root : root_handle);
    std::stable_sort(order_.begin(), order_.end(), [this](std::uint64_t left, std::uint64_t right) {
        return FocusOrderKey(nodes_.Resolve(left)) < FocusOrderKey(nodes_.Resolve(right));
    });
    order_dirty_ = false;
}

std::uint64_t FocusCoordinator::GetNextFocusable(
    std::uint64_t current,
    bool forward,
    std::uint64_t root_handle,
    std::uint64_t scope_root) {
    if (order_dirty_) RebuildOrder(root_handle, scope_root);
    if (order_.empty()) return UI_INVALID_HANDLE;
    const auto it = std::find(order_.begin(), order_.end(), current);
    if (it == order_.end()) return forward ? order_.front() : order_.back();
    if (forward) {
        const auto next = std::next(it);
        return next == order_.end() ? order_.front() : *next;
    }
    return it == order_.begin() ? order_.back() : *std::prev(it);
}

void FocusCoordinator::CapturePendingNodeId(std::uint64_t subtree_root) {
    if (!nodes_.SubtreeContains(subtree_root, focused_handle_)) return;
    const UINode* focused_node = nodes_.Resolve(focused_handle_);
    if (focused_node == nullptr || focused_node->node_id.empty()) {
        pending_node_id_.clear();
        return;
    }
    pending_node_id_ = focused_node->node_id;
}

std::uint64_t FocusCoordinator::PendingRestoreCandidate(
    const std::unordered_map<std::string, std::uint64_t>& node_ids) const {
    if (pending_node_id_.empty() || focused_handle_ != UI_INVALID_HANDLE) return UI_INVALID_HANDLE;
    const auto it = node_ids.find(pending_node_id_);
    if (it == node_ids.end()) return UI_INVALID_HANDLE;
    const UINode* node = nodes_.Resolve(it->second);
    return node != nullptr && node->is_focusable
        ? it->second
        : static_cast<std::uint64_t>(UI_INVALID_HANDLE);
}

} // namespace effindom::v2::ui
