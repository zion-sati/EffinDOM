#pragma once

#include "UiTypes.h"

#include <array>
#include <cstdint>
#include <utility>

namespace effindom::v2::ui {

class NodeReader {
public:
    NodeReader(const std::array<UINode, kMaxNodes>& nodes, const std::uint64_t& root_handle)
        : nodes_(nodes), root_handle_(root_handle) {}

    const UINode* Resolve(std::uint64_t handle) const {
        const HandleParts parts = DecodeHandle(handle);
        if (parts.index == 0U || parts.index >= nodes_.size()) {
            return nullptr;
        }
        const UINode& node = nodes_[parts.index];
        return node.is_active && node.generation == parts.generation ? &node : nullptr;
    }

    std::uint64_t RootHandle() const {
        return root_handle_;
    }

    bool SubtreeContains(std::uint64_t subtree_root, std::uint64_t target_handle) const {
        if (subtree_root == UI_INVALID_HANDLE || target_handle == UI_INVALID_HANDLE) {
            return false;
        }
        if (subtree_root == target_handle) {
            return Resolve(subtree_root) != nullptr;
        }
        const UINode* node = Resolve(subtree_root);
        if (node == nullptr) {
            return false;
        }
        for (const std::uint64_t child_handle : node->children) {
            if (SubtreeContains(child_handle, target_handle)) {
                return true;
            }
        }
        return false;
    }

private:
    const std::array<UINode, kMaxNodes>& nodes_;
    const std::uint64_t& root_handle_;
};

class NodeWriter {
public:
    NodeWriter(std::array<UINode, kMaxNodes>& nodes, std::uint64_t& root_handle)
        : nodes_(nodes), root_handle_(root_handle) {}

    UINode* Resolve(std::uint64_t handle) const {
        const HandleParts parts = DecodeHandle(handle);
        if (parts.index == 0U || parts.index >= nodes_.size()) {
            return nullptr;
        }
        UINode& node = nodes_[parts.index];
        return node.is_active && node.generation == parts.generation ? &node : nullptr;
    }

    std::uint64_t RootHandle() const {
        return root_handle_;
    }

    void SetRootHandle(std::uint64_t handle) {
        root_handle_ = handle;
    }

private:
    std::array<UINode, kMaxNodes>& nodes_;
    std::uint64_t& root_handle_;
};

class NodeTraversalAccess {
public:
    explicit NodeTraversalAccess(std::array<UINode, kMaxNodes>& nodes)
        : nodes_(nodes) {}

    template <typename Callback>
    void ForEachActive(Callback&& callback) const {
        for (std::uint32_t index = 1U; index < nodes_.size(); index += 1U) {
            UINode& node = nodes_[index];
            if (node.is_active) {
                callback(PackHandle(index, node.generation), node);
            }
        }
    }

    template <typename Callback>
    void ForEachActiveScrollView(Callback&& callback) const {
        ForEachActive([&](std::uint64_t handle, UINode& node) {
            if (node.is_scroll_view) {
                callback(handle, node);
            }
        });
    }

    template <typename Predicate>
    bool AnyActive(Predicate&& predicate) const {
        for (std::uint32_t index = 1U; index < nodes_.size(); index += 1U) {
            UINode& node = nodes_[index];
            if (node.is_active && predicate(PackHandle(index, node.generation), node)) {
                return true;
            }
        }
        return false;
    }

    template <typename Predicate>
    bool AnyActiveScrollView(Predicate&& predicate) const {
        return AnyActive([&](std::uint64_t handle, UINode& node) {
            return node.is_scroll_view && predicate(handle, node);
        });
    }

private:
    std::array<UINode, kMaxNodes>& nodes_;
};

class ConstNodeTraversalAccess {
public:
    explicit ConstNodeTraversalAccess(const std::array<UINode, kMaxNodes>& nodes)
        : nodes_(nodes) {}

    template <typename Callback>
    void ForEachActive(Callback&& callback) const {
        for (std::uint32_t index = 1U; index < nodes_.size(); index += 1U) {
            const UINode& node = nodes_[index];
            if (node.is_active) {
                callback(PackHandle(index, node.generation), node);
            }
        }
    }

    template <typename Callback>
    void ForEachActiveScrollView(Callback&& callback) const {
        ForEachActive([&](std::uint64_t handle, const UINode& node) {
            if (node.is_scroll_view) {
                callback(handle, node);
            }
        });
    }

    template <typename Predicate>
    bool AnyActive(Predicate&& predicate) const {
        for (std::uint32_t index = 1U; index < nodes_.size(); index += 1U) {
            const UINode& node = nodes_[index];
            if (node.is_active && predicate(PackHandle(index, node.generation), node)) {
                return true;
            }
        }
        return false;
    }

    template <typename Predicate>
    bool AnyActiveScrollView(Predicate&& predicate) const {
        return AnyActive([&](std::uint64_t handle, const UINode& node) {
            return node.is_scroll_view && predicate(handle, node);
        });
    }

private:
    const std::array<UINode, kMaxNodes>& nodes_;
};

} // namespace effindom::v2::ui
