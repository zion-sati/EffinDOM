#pragma once

#include <cstdint>

namespace effindom::v2::detail {

struct SvgIntrinsicSize {
    float width = 1.0f;
    float height = 1.0f;
};

SvgIntrinsicSize ParseSvgIntrinsicSize(const std::uint8_t* bytes, std::uint32_t length);

} // namespace effindom::v2::detail
