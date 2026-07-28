#pragma once

#include "NativeAccessibility.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace effindom::v2::native {

namespace detail {

std::uint32_t LinuxAtSpiRole(NativeAccessibilityRole role);
std::vector<std::uint32_t> LinuxAtSpiStates(
    const NativeAccessibilityNode& node,
    bool focused);
std::vector<std::string> LinuxAtSpiInterfaces(const NativeAccessibilityNode& node);
std::string LinuxAtSpiObjectPath(std::uint64_t handle);
std::uint32_t LinuxAtSpiActionCount(const NativeAccessibilityNode& node);

} // namespace detail

std::unique_ptr<NativeAccessibilityAdapter> CreateLinuxAccessibilityAdapter(
    SDL_Window* window,
    NativeAccessibilityActionHandler action_handler);

} // namespace effindom::v2::native
