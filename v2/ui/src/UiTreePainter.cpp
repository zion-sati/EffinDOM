#include "UiTreePainter.h"

#include "CommandBuilder.h"

namespace effindom::v2::ui {

TreePainter::TreePainter(
    NodeReader nodes,
    NodeWriter writer,
    VisibilityResolver visibility,
    bool& layout_dirty,
    const GridLayoutSource& grid_layout,
    TextPaintAccess text_host)
    : traversal_(nodes, writer, visibility, layout_dirty, grid_layout, text_host) {
    paint_order_.reserve(kMaxNodes / 4U);
    scene_.reserve(kMaxNodes / 4U);
    deferred_portal_roots_.reserve(kMaxNodes / 8U);
}

PaintFrameOutput TreePainter::Paint(std::uint64_t root_handle, CommandBuilder& commands) {
    paint_order_.clear();
    scene_.clear();
    deferred_portal_roots_.clear();

    traversal_.Walk(
        root_handle,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        false,
        commands,
        paint_order_,
        scene_,
        deferred_portal_roots_);
    traversal_.WalkDeferredPortals(commands, paint_order_, scene_, deferred_portal_roots_);
    return PaintFrameOutput{paint_order_, scene_};
}

} // namespace effindom::v2::ui
