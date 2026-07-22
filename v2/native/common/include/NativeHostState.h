#pragma once

#include <cstdint>

namespace effindom::v2::native {

struct NativeHostState {
    std::uint32_t activation_count = 0;
    std::uint32_t mount_count = 0;
    std::uint32_t dispose_count = 0;
    std::uint64_t frame_count = 0;
    float logical_width = 0.0f;
    float logical_height = 0.0f;
    float pixel_density = 1.0f;
    bool frame_pending = false;
    bool gpu_backed = false;
    std::uint64_t graphics_generation = 0U;
    std::uint64_t graphics_recovery_count = 0U;
    bool presentation_suspended = false;
};

} // namespace effindom::v2::native
