#pragma once

#include "NativeAccessibility.h"

#include <memory>

struct SDL_Window;

namespace effindom::v2::native {

std::unique_ptr<NativeAccessibilityAdapter> CreateMacosAccessibilityAdapter(
    SDL_Window* window,
    NativeAccessibilityActionHandler action_handler);

} // namespace effindom::v2::native
