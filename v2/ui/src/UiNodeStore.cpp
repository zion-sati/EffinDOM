#include "UiNodeStore.h"

#include "CommandBuilder.h"

#include <algorithm>

namespace effindom::v2::ui {

namespace {

void ApplyDefaultBoxModel(YGNodeRef node) {
    YGNodeStyleSetPadding(node, YGEdgeLeft, 0.0f);
    YGNodeStyleSetPadding(node, YGEdgeTop, 0.0f);
    YGNodeStyleSetPadding(node, YGEdgeRight, 0.0f);
    YGNodeStyleSetPadding(node, YGEdgeBottom, 0.0f);
    YGNodeStyleSetMargin(node, YGEdgeLeft, 0.0f);
    YGNodeStyleSetMargin(node, YGEdgeTop, 0.0f);
    YGNodeStyleSetMargin(node, YGEdgeRight, 0.0f);
    YGNodeStyleSetMargin(node, YGEdgeBottom, 0.0f);
}

} // namespace

NodeStore::~NodeStore() {
    Reset();
}

std::uint64_t NodeStore::Create(std::uint32_t type, YGMeasureFunc text_measure_func) {
    for (std::uint32_t index = 1U; index < static_cast<std::uint32_t>(nodes_.size()); index += 1U) {
        UINode& node = nodes_[index];
        if (node.is_active) {
            continue;
        }

        const std::uint32_t next_generation = node.generation + 1U;
        node = UINode{};
        node.generation = next_generation;
        node.node_type = type;
        node.is_active = true;
        node.needs_creation = true;
        node.is_dirty = true;
        node.is_text_node = type == UI_NODE_TEXT;
        node.is_svg_node = type == UI_NODE_SVG;
        node.is_scroll_view = type == UI_NODE_SCROLLVIEW;
        node.is_grid = type == UI_NODE_GRID;
        node.yg_node = YGNodeNew();
        if (node.yg_node == nullptr) {
            node = UINode{};
            return UI_INVALID_HANDLE;
        }
        ApplyDefaultBoxModel(node.yg_node);
        if (node.is_text_node) {
            YGNodeSetContext(node.yg_node, &node);
            YGNodeSetMeasureFunc(node.yg_node, text_measure_func);
        }
        const std::uint64_t handle = PackHandle(index, next_generation);
        pending_creations_.push_back(handle);
        return handle;
    }
    return UI_INVALID_HANDLE;
}

bool NodeStore::Destroy(std::uint64_t handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }

    for (const std::uint64_t child_handle : node->children) {
        if (UINode* child = ResolveMutable(child_handle); child != nullptr) {
            child->parent_handle = UI_INVALID_HANDLE;
            if (node->yg_node != nullptr && child->yg_node != nullptr) {
                YGNodeRemoveChild(node->yg_node, child->yg_node);
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

    const auto creation = std::find(pending_creations_.begin(), pending_creations_.end(), handle);
    if (creation != pending_creations_.end()) {
        pending_creations_.erase(creation);
    } else {
        pending_deletions_.push_back(handle);
    }

    node->is_active = false;
    node->needs_creation = false;
    node->is_dirty = false;
    node->parent_handle = UI_INVALID_HANDLE;
    return true;
}

bool NodeStore::AddChild(std::uint64_t parent_handle, std::uint64_t child_handle) {
    UINode* parent = ResolveMutable(parent_handle);
    UINode* child = ResolveMutable(child_handle);
    if (parent == nullptr || child == nullptr || parent == child ||
        parent->yg_node == nullptr || child->yg_node == nullptr) {
        return false;
    }
    if (child->parent_handle == parent_handle) {
        return true;
    }
    if (child->parent_handle != UI_INVALID_HANDLE &&
        !RemoveChild(child->parent_handle, child_handle)) {
        return false;
    }
    YGNodeInsertChild(parent->yg_node, child->yg_node, static_cast<std::uint32_t>(parent->children.size()));
    child->parent_handle = parent_handle;
    parent->children.push_back(child_handle);
    return true;
}

bool NodeStore::RemoveChild(std::uint64_t parent_handle, std::uint64_t child_handle) {
    UINode* parent = ResolveMutable(parent_handle);
    UINode* child = ResolveMutable(child_handle);
    if (parent == nullptr || child == nullptr || parent->yg_node == nullptr || child->yg_node == nullptr) {
        return false;
    }
    const auto child_entry = std::find(parent->children.begin(), parent->children.end(), child_handle);
    if (child_entry == parent->children.end()) {
        return false;
    }
    YGNodeRemoveChild(parent->yg_node, child->yg_node);
    parent->children.erase(child_entry);
    child->parent_handle = UI_INVALID_HANDLE;
    return true;
}

bool NodeStore::SetRoot(std::uint64_t handle) {
    if (handle != UI_INVALID_HANDLE && Resolve(handle) == nullptr) {
        return false;
    }
    root_handle_ = handle;
    return true;
}

void NodeStore::EmitLifecycleCommands(CommandBuilder& builder) {
    for (const std::uint64_t handle : pending_deletions_) {
        builder.DeleteNode(handle);
    }
    pending_deletions_.clear();
    for (const std::uint64_t handle : pending_creations_) {
        if (UINode* node = ResolveMutable(handle); node != nullptr && node->needs_creation) {
            builder.CreateNode(handle);
            node->needs_creation = false;
        }
    }
    pending_creations_.clear();
}

void NodeStore::Reset() {
    for (UINode& node : nodes_) {
        if (node.yg_node != nullptr) {
            YGNodeFree(node.yg_node);
            node.yg_node = nullptr;
        }
        node.children.clear();
        node.children.shrink_to_fit();
        node = UINode{};
    }
    root_handle_ = UI_INVALID_HANDLE;
    pending_creations_.clear();
    pending_deletions_.clear();
}

} // namespace effindom::v2::ui
