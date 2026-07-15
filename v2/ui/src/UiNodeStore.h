#pragma once

#include "UiNodeStoreAccess.h"

#include <array>
#include <cstdint>
#include <vector>

namespace effindom::v2::ui {

class CommandBuilder;

class NodeStore {
public:
    NodeStore() = default;
    ~NodeStore();

    NodeStore(const NodeStore&) = delete;
    NodeStore& operator=(const NodeStore&) = delete;

    std::uint64_t Create(std::uint32_t type, YGMeasureFunc text_measure_func);
    bool Destroy(std::uint64_t handle);
    bool AddChild(std::uint64_t parent_handle, std::uint64_t child_handle);
    bool RemoveChild(std::uint64_t parent_handle, std::uint64_t child_handle);
    bool SetRoot(std::uint64_t handle);
    void Reset();

    const UINode* Resolve(std::uint64_t handle) const { return Reader().Resolve(handle); }
    UINode* ResolveMutable(std::uint64_t handle) { return Writer().Resolve(handle); }
    std::uint64_t RootHandle() const { return root_handle_; }
    const std::uint64_t& RootHandleRef() const { return root_handle_; }
    bool HasPendingLifecycleCommands() const {
        return !pending_creations_.empty() || !pending_deletions_.empty();
    }
    void EmitLifecycleCommands(CommandBuilder& builder);

    NodeReader Reader() const { return NodeReader(nodes_, root_handle_); }
    NodeWriter Writer() { return NodeWriter(nodes_, root_handle_); }
    NodeTraversalAccess Traversal() { return NodeTraversalAccess(nodes_); }
    ConstNodeTraversalAccess Traversal() const { return ConstNodeTraversalAccess(nodes_); }

private:
    std::array<UINode, kMaxNodes> nodes_{};
    std::uint64_t root_handle_ = UI_INVALID_HANDLE;
    std::vector<std::uint64_t> pending_creations_{};
    std::vector<std::uint64_t> pending_deletions_{};
};

} // namespace effindom::v2::ui
