#pragma once

#include "UiVisibilityResolver.h"

#include <cstdint>

namespace effindom::v2::ui {

class CommandBuilder;

class NodeVisualEncoder {
public:
    explicit NodeVisualEncoder(VisibilityResolver visibility)
        : visibility_(visibility) {}

    void EmitBounds(
        std::uint64_t handle,
        const UINode& node,
        float abs_x,
        float abs_y,
        float scene_x,
        float scene_y,
        float width,
        float height,
        const Rect& visible_bounds,
        bool interactive,
        CommandBuilder& builder) const;
    void EmitStyle(std::uint64_t handle, const UINode& node, CommandBuilder& builder) const;

private:
    VisibilityResolver visibility_;
};

} // namespace effindom::v2::ui
