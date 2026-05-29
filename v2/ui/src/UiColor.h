#pragma once

#include <cstdint>

#ifndef EF_RGBA
#define EF_RGBA(r, g, b, a) \
    (static_cast<std::uint32_t>( \
        ((static_cast<std::uint32_t>(r) & 0xFFU) << 24U) | \
        ((static_cast<std::uint32_t>(g) & 0xFFU) << 16U) | \
        ((static_cast<std::uint32_t>(b) & 0xFFU) << 8U) | \
        (static_cast<std::uint32_t>(a) & 0xFFU)))
#endif

#ifndef EF_RGB
#define EF_RGB(r, g, b) EF_RGBA((r), (g), (b), 0xFFU)
#endif
