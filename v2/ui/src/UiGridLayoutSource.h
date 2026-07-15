#pragma once

#include "UiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace effindom::v2::ui {

class GridLayoutSource {
public:
    virtual ~GridLayoutSource() = default;

    virtual Rect ComputeContentBounds(const UINode& node, float origin_x, float origin_y) const = 0;
    virtual const std::vector<std::string>& GridRowSharedSizeGroups(std::uint64_t handle) const = 0;
};

} // namespace effindom::v2::ui
