#pragma once

#include "effindom.h"

#include <cstdint>

namespace effindom::v2::detail {

struct SvgIntrinsicSize {
    float width = 1.0f;
    float height = 1.0f;
};

EFFINDOM_CORE_API SvgIntrinsicSize ParseSvgIntrinsicSize(
    const std::uint8_t* bytes, std::uint32_t length);

} // namespace effindom::v2::detail
