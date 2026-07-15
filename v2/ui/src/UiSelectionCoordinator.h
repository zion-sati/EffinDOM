#pragma once

#include "UiTypes.h"
#include "UiNodeStoreAccess.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace effindom::v2::ui {

struct SelectionState {
    std::uint64_t active_handle = UI_INVALID_HANDLE;
    bool active_dragged = false;
    bool handle_drag_active = false;
    std::uint32_t drag_endpoint = 1U;
    std::uint32_t stationary_index = 0U;
    std::uint64_t anchor_handle = UI_INVALID_HANDLE;
    std::uint32_t anchor_index = 0U;
    bool horizontal_extend_active = false;
    bool cross_active = false;
    bool cross_dragged = false;
    bool cross_horizontal_extend_active = false;
    std::uint64_t area_handle = UI_INVALID_HANDLE;
    std::uint64_t start_node_handle = UI_INVALID_HANDLE;
    std::uint32_t start_index = 0U;
    std::uint64_t end_node_handle = UI_INVALID_HANDLE;
    std::uint32_t end_index = 0U;
    std::vector<std::uint64_t> area_nodes{};
    std::vector<Rect> hit_rects{};
    bool area_nodes_dirty = false;
};

// Narrow runtime services required by retained selection policy. This keeps the
// coordinator independent from UiRuntime's unrelated frame, input, and ABI state.
class SelectionHost {
public:
    virtual ~SelectionHost() = default;

    virtual const UINode* ResolveSelectionNode(std::uint64_t handle) const = 0;
    virtual UINode* ResolveSelectionNodeMutable(std::uint64_t handle) = 0;
    virtual std::uint32_t GetSelectionIndexFromPoint(const UINode& node, float local_x, float local_y) const = 0;
    virtual std::vector<Rect> GetSelectionRangeSceneRects(
        std::uint64_t handle,
        std::uint32_t start,
        std::uint32_t end) const = 0;
    virtual std::pair<float, float> ClampSelectionPointToViewport(
        std::uint64_t handle,
        float logical_x,
        float logical_y) const = 0;
    virtual void MarkSelectionVisualsDirty(UINode& node) = 0;
    virtual void NotifySelectionChanged(
        std::uint64_t handle,
        std::uint32_t start,
        std::uint32_t end) const = 0;
    virtual void MarkSelectionLayoutDirty() = 0;
    virtual void NotifyCrossSelectionChanged(std::uint64_t area_handle, std::string_view utf8_text) const = 0;
    virtual bool HasSelectionWordNavigationModifier(std::uint32_t modifiers) const = 0;
    virtual std::uint32_t NextSelectionWordIndex(
        const UINode& node,
        std::uint32_t index,
        bool forward) const = 0;
    virtual std::uint32_t NextSelectionCharacterIndex(
        std::string_view utf8_text,
        std::uint32_t index,
        bool forward) const = 0;
    virtual std::pair<float, std::size_t> GetSelectionLocalPositionFromIndex(
        const UINode& node,
        std::uint32_t index) const = 0;
    virtual std::uint32_t GetSelectionIndexForVerticalMove(
        const UINode& node,
        std::uint32_t index,
        bool down) const = 0;
    virtual std::uint32_t GetSelectionIndexForPageMove(
        const UINode& node,
        std::uint32_t index,
        bool down) const = 0;
    virtual float GetSelectionAlignedLineXOffset(const UINode& node, float line_width) const = 0;
    virtual float GetSelectionAlignedTextYOffset(const UINode& node, float content_height) const = 0;
    virtual float GetSelectionTextContentHeight(const UINode& node, std::size_t visible_line_count) const = 0;
    virtual float GetSelectionLineTopForIndex(const UINode& node, std::size_t line_index) const = 0;
    virtual float GetSelectionLineHeightForIndex(const UINode& node, std::size_t line_index) const = 0;
    virtual std::uint64_t FocusedSelectionHandle() const = 0;
    virtual void SetSelectionFocus(std::uint64_t handle) = 0;
};

struct SelectionAutoScrollRequest {
    bool active = false;
    bool touch_tap_moved = false;
    std::uint64_t touch_tap_handle = UI_INVALID_HANDLE;
    bool is_editor_text_node = false;
    float logical_x = 0.0f;
    float logical_y = 0.0f;
};

struct CrossSelectionPointerRequest {
    bool pointer_move = false;
    bool pointer_up = false;
    bool primary_pointer_down = false;
    bool allow_text_drag = false;
    bool drag_threshold_exceeded = false;
    std::uint64_t handle = UI_INVALID_HANDLE;
    float logical_x = 0.0f;
    float logical_y = 0.0f;
    float handle_center_to_text_hit_offset = 0.0f;
};

struct NodeSelectionPointerRequest {
    bool pointer_move = false;
    bool pointer_up = false;
    bool primary_pointer_down = false;
    bool allow_text_drag = false;
    bool drag_threshold_exceeded = false;
    float logical_x = 0.0f;
    float logical_y = 0.0f;
    float handle_center_to_text_hit_offset = 0.0f;
    std::uint64_t interaction_time_ms = 0U;
};

struct NodeSelectionPointerResult {
    bool finished = false;
    std::uint64_t handle = UI_INVALID_HANDLE;
};

class SelectionCoordinator {
public:
    SelectionState& state() { return state_; }
    const SelectionState& state() const { return state_; }
    void Reset() { state_ = SelectionState{}; }

    std::uint64_t FindAreaAncestor(const SelectionHost& host, std::uint64_t start_handle) const;
    void EnsureAreaNodes(SelectionHost& host, std::uint64_t area_handle);
    int FindAreaNodeIndex(std::uint64_t handle) const;
    void MarkAreaNodesDirty(SelectionHost& host);
    void ClearCrossSelection(SelectionHost& host, bool notify_callback);
    bool UpdateCrossSelectionEndpoint(SelectionHost& host, std::uint64_t handle, float logical_x, float logical_y);
    bool GetCrossSelectionHighlight(
        const SelectionHost& host,
        std::uint64_t handle,
        std::uint32_t& out_start,
        std::uint32_t& out_end) const;
    bool GetCrossSelectionEndpointSceneRects(
        SelectionHost& host,
        std::uint64_t area_handle,
        Rect& out_start_rect,
        Rect& out_end_rect);
    void NormalizeCrossSelectionEndpoints();
    void BeginCrossSelection(
        SelectionHost& host,
        std::uint64_t area_handle,
        std::uint64_t start_node_handle,
        std::uint32_t start_index,
        std::uint64_t end_node_handle,
        std::uint32_t end_index,
        bool horizontal_extend_active);
    void BeginNodeSelection(
        UINode& node,
        std::uint64_t handle,
        std::uint32_t start,
        std::uint32_t end,
        bool horizontal_extend_active);
    bool BeginCrossSelectionEndpointDrag(SelectionHost& host, std::uint32_t endpoint);
    bool BeginNodeSelectionEndpointDrag(
        SelectionHost& host,
        UINode& node,
        std::uint64_t handle,
        std::uint32_t endpoint);
    void InvalidateSubtree(SelectionHost& host, const NodeReader& nodes, std::uint64_t subtree_root);
    void ReconcileRoot(SelectionHost& host, const NodeReader& nodes, std::uint64_t root_handle);
    void MarkAreaTopologyDirty();
    void RemoveArea(std::uint64_t area_handle, SelectionHost& host);
    void ClearHitRects() { state_.hit_rects.clear(); }
    void RecordHitRect(const Rect& rect) { state_.hit_rects.push_back(rect); }
    bool HitTests(float logical_x, float logical_y) const;
    std::string BuildCrossSelectionText(const SelectionHost& host) const;
    void NotifyCrossSelectionChanged(const SelectionHost& host) const;
    bool HandleCrossSelectionNavigation(
        SelectionHost& host,
        std::uint64_t area_handle,
        UINode& focused_node,
        std::string_view key,
        std::uint32_t modifiers);
    bool HandleCrossSelectionPointer(
        SelectionHost& host,
        const CrossSelectionPointerRequest& request);
    NodeSelectionPointerResult HandleNodeSelectionPointer(
        SelectionHost& host,
        const NodeSelectionPointerRequest& request);
    void UpdateAutoScrollSelection(
        SelectionHost& host,
        const SelectionAutoScrollRequest& request,
        std::uint64_t handle,
        UINode& node,
        float abs_x,
        float abs_y,
        float width,
        float height);

private:
    void CollectAreaNodes(const SelectionHost& host, std::uint64_t handle, std::vector<std::uint64_t>& out) const;
    SelectionState state_{};
};

} // namespace effindom::v2::ui
