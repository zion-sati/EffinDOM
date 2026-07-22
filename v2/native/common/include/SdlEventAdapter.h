#pragma once

#include "NativeInputRouter.h"

#include <cstdint>
#include <string>
#include <utility>

union SDL_Event;

namespace effindom::v2::native {

struct SdlEventAdapterOptions {
    bool normalize_display_pixel_coordinates = false;
    bool normalize_display_content_coordinates = false;
    bool keypad_always_numeric = false;
};

class SdlEventAdapter final {
public:
    SdlEventAdapter(NativeInputRouter& router, SdlEventAdapterOptions options);
    bool HandleEvent(const SDL_Event& event, double timestamp_ms);
    std::uint32_t CurrentModifiers() const;
    std::uint32_t CurrentButtons() const;

    static std::int32_t PointerButton(std::uint8_t button);
    static std::uint32_t PointerButtons(std::uint64_t buttons);
    static std::uint32_t Modifiers(std::uint32_t modifiers);
    static std::string KeyName(std::uint32_t keycode, std::uint32_t scancode,
        std::uint32_t modifiers, bool keypad_always_numeric);
    static float LogicalCoordinate(float coordinate, float display_scale, bool normalize);
    static float DisplayContentScale(float display_scale, float pixel_density);
    static std::pair<float, float> WheelDeltas(const SDL_Event& event);
    static bool EndsInputBatch(std::uint32_t event_type);

private:
    float LogicalCoordinate(std::uint32_t window_id, float coordinate) const;

    NativeInputRouter& router_;
    SdlEventAdapterOptions options_;
};

} // namespace effindom::v2::native
