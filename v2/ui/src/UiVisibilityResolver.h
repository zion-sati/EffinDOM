#pragma once

#include "UiNodeStoreAccess.h"

namespace effindom::v2::ui {

// Resolves retained-node visibility without depending on UiRuntime orchestration.
// Portal nodes apply their own clip, then terminate ancestor clipping.
class VisibilityResolver {
public:
    explicit VisibilityResolver(NodeReader nodes)
        : nodes_(nodes) {}

    Rect ComputeClipBounds(const UINode& node) const;
    Rect ComputeClipBounds(const UINode& node, float origin_x, float origin_y) const;
    Rect ClipToAncestors(Rect bounds, std::uint64_t first_ancestor_handle) const;
    bool TryGetMultilineTextboxViewportBounds(const UINode& node, Rect& bounds) const;

private:
    static bool Intersect(Rect& bounds, const Rect& clip);
    static Rect ComputeContentBounds(const UINode& node, float origin_x, float origin_y);

    NodeReader nodes_;
};

} // namespace effindom::v2::ui
