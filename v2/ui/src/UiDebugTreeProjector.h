#pragma once

#include "UiSemanticProjector.h"

#include <functional>
#include <vector>

namespace effindom::v2::ui {

using DebugVisibleBoundsResolver = std::function<Rect(const UINode&)>;

class DebugTreeProjector {
public:
    DebugTreeProjector(NodeReader nodes, const VisibilityResolver& visibility)
        : nodes_(nodes), visibility_(visibility) {}

    static void ClearOutput(std::vector<std::uint32_t>& output);

    void Build(
        std::uint64_t root_handle,
        const DebugVisibleBoundsResolver& visible_bounds_for,
        std::vector<std::uint32_t>& output) const;

private:
    void AppendRecord(
        std::uint64_t handle,
        std::uint64_t nearest_scroll_ancestor,
        const DebugVisibleBoundsResolver& visible_bounds_for,
        std::vector<std::uint32_t>& output) const;

    NodeReader nodes_;
    const VisibilityResolver& visibility_;
};

} // namespace effindom::v2::ui
