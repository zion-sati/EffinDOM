#pragma once

#include "UiNodeStoreAccess.h"
#include "UiGridLayoutSource.h"

namespace effindom::v2::ui {

class SceneGeometryResolver {
public:
    SceneGeometryResolver(NodeWriter nodes, const GridLayoutSource& grid_layout)
        : nodes_(nodes), grid_layout_(grid_layout) {
        deferred_portal_roots_.reserve(kMaxNodes / 8U);
    }

    void Resolve(std::uint64_t root_handle);

private:
    void ResolveNode(
        std::uint64_t handle,
        float parent_abs_x,
        float parent_abs_y,
        float parent_scene_x,
        float parent_scene_y);
    void ResolveGrid(
        std::uint64_t handle,
        UINode& node,
        float abs_x,
        float abs_y,
        float scene_x,
        float scene_y);
    void ResolveDeferredPortals();

    NodeWriter nodes_;
    const GridLayoutSource& grid_layout_;
    std::vector<std::uint64_t> deferred_portal_roots_{};
};

} // namespace effindom::v2::ui
