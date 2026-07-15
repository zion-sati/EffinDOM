#pragma once

#include "UiRuntime.h"
#include "UiGridLayoutSource.h"
#include "UiNodeVisualEncoder.h"
#include "UiTextPaintEncoder.h"

namespace effindom::v2::ui {

class SceneTraversal {
public:
    SceneTraversal(
        NodeReader nodes,
        NodeWriter writer,
        VisibilityResolver visibility,
        bool& layout_dirty,
        const GridLayoutSource& grid_layout,
        TextPaintAccess text_host)
        : nodes_(nodes),
          writer_(writer),
          visibility_(visibility),
          layout_dirty_(layout_dirty),
          grid_layout_(grid_layout),
          visual_encoder_(visibility),
          text_encoder_(text_host) {}

    void Walk(
        std::uint64_t handle,
        float parent_abs_x,
        float parent_abs_y,
        float parent_scene_x,
        float parent_scene_y,
        bool inherited_scroll_dirty,
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order,
        std::vector<SceneInstruction>& scene,
        std::vector<std::uint64_t>& deferred_portal_roots);
    void WalkDeferredPortals(
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order,
        std::vector<SceneInstruction>& scene,
        std::vector<std::uint64_t>& deferred_portal_roots);

private:
    void ResetTextRenderWindow(UINode& node) const;
    Rect ComputeVisibleBounds(
        const UINode& node,
        float abs_x,
        float abs_y,
        float width,
        float height) const;
    void EmitSceneAndChildren(
        std::uint64_t handle,
        UINode& node,
        float abs_x,
        float abs_y,
        float scene_x,
        float scene_y,
        bool scroll_dirty,
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order,
        std::vector<SceneInstruction>& scene,
        std::vector<std::uint64_t>& deferred_portal_roots);
    void ClearCulledSubtree(
        std::uint64_t handle,
        float parent_abs_x,
        float parent_abs_y,
        float parent_scene_x,
        float parent_scene_y,
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order);
    void LayoutGrid(
        std::uint64_t handle,
        UINode& node,
        float abs_x,
        float abs_y,
        float scene_x,
        float scene_y,
        bool inherited_scroll_dirty,
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order,
        std::vector<SceneInstruction>& scene,
        std::vector<std::uint64_t>& deferred_portal_roots);

    NodeReader nodes_;
    NodeWriter writer_;
    VisibilityResolver visibility_;
    bool& layout_dirty_;
    const GridLayoutSource& grid_layout_;
    NodeVisualEncoder visual_encoder_;
    TextPaintEncoder text_encoder_;
};

} // namespace effindom::v2::ui
