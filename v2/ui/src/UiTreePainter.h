#pragma once

#include "UiSceneTraversal.h"

namespace effindom::v2::ui {

struct PaintFrameOutput {
    const std::vector<std::uint64_t>& paint_order;
    const std::vector<SceneInstruction>& scene;
};

class TreePainter {
public:
    TreePainter(
        NodeReader nodes,
        NodeWriter writer,
        VisibilityResolver visibility,
        bool& layout_dirty,
        const GridLayoutSource& grid_layout,
        TextPaintAccess text_host);

    PaintFrameOutput Paint(std::uint64_t root_handle, CommandBuilder& commands);

private:
    SceneTraversal traversal_;
    std::vector<std::uint64_t> paint_order_{};
    std::vector<SceneInstruction> scene_{};
    std::vector<std::uint64_t> deferred_portal_roots_{};
};

} // namespace effindom::v2::ui
