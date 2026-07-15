#pragma once

#include "UiEventSink.h"
#include "UiNodeStoreAccess.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace effindom::v2::ui {

class FocusCoordinator {
public:
    FocusCoordinator(NodeReader nodes, const UiEventSink& events)
        : nodes_(nodes), events_(events) {}

    void Reset();
    std::uint64_t FocusedHandle() const { return focused_handle_; }
    bool IsFocused(std::uint64_t handle) const { return focused_handle_ == handle; }
    void SetFocusedHandle(std::uint64_t handle);
    void NotifyChanged(std::uint64_t handle, bool focused) const;

    void InvalidateOrder() { order_dirty_ = true; }
    std::uint64_t GetNextFocusable(
        std::uint64_t current,
        bool forward,
        std::uint64_t root_handle,
        std::uint64_t scope_root);

    void CapturePendingNodeId(std::uint64_t subtree_root);
    std::uint64_t PendingRestoreCandidate(
        const std::unordered_map<std::string, std::uint64_t>& node_ids) const;
    const std::string& PendingNodeId() const { return pending_node_id_; }
    void ClearPendingNodeId() { pending_node_id_.clear(); }

private:
    void RebuildOrder(std::uint64_t root_handle, std::uint64_t scope_root);
    void AppendFocusableHandles(std::uint64_t handle);

    NodeReader nodes_;
    const UiEventSink& events_;
    std::uint64_t focused_handle_ = UI_INVALID_HANDLE;
    std::string pending_node_id_{};
    std::vector<std::uint64_t> order_{};
    bool order_dirty_ = true;
};

} // namespace effindom::v2::ui
