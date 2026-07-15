#pragma once

#include "UiVisibilityResolver.h"

#include <functional>
#include <string>
#include <vector>

namespace effindom::v2::ui {

using SemanticVisibleBoundsResolver = std::function<Rect(const UINode&)>;

std::string BuildSemanticLabel(const UINode& node);

class SemanticProjector {
public:
    SemanticProjector(NodeReader nodes, const VisibilityResolver& visibility)
        : nodes_(nodes), visibility_(visibility) {}

    static void ClearOutput(std::vector<std::uint32_t>& output);

    void Build(
        const std::vector<std::uint64_t>& paint_order,
        std::uint64_t semantic_scope_root,
        const SemanticVisibleBoundsResolver& visible_bounds_for,
        std::vector<std::uint32_t>& output) const;

private:
    bool HasSemanticAncestor(const UINode& node) const;

    NodeReader nodes_;
    const VisibilityResolver& visibility_;
};

} // namespace effindom::v2::ui
