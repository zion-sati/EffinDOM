#include "MacosInputSettings.h"

#import <AppKit/AppKit.h>

#include "SDL3/SDL_hints.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace effindom::v2::native {

void ConfigureMacosInputSettings() {
    const auto interval_ms = static_cast<unsigned long long>(std::max(
        1.0,
        std::round(NSEvent.doubleClickInterval * 1000.0)));
    const std::string interval = std::to_string(interval_ms);
    SDL_SetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_TIME, interval.c_str());
}

} // namespace effindom::v2::native
