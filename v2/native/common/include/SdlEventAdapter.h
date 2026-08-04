#pragma once

#include "NativeInputRouter.h"
#include "NativeTextInputCoordinator.h"
#include "NativeTouchGestureController.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

class NativePageZoomController;

struct SdlEventAdapterOptions {
    SdlEventAdapterOptions() = default;
    SdlEventAdapterOptions(bool normalize_pixels, bool normalize_content,
        bool keypad_numeric) :
        normalize_display_pixel_coordinates(normalize_pixels),
        normalize_display_content_coordinates(normalize_content),
        keypad_always_numeric(keypad_numeric) {}

    bool normalize_display_pixel_coordinates = false;
    bool normalize_display_content_coordinates = false;
    bool keypad_always_numeric = false;
    std::function<bool()> coarse_pointer_query;
};

class SdlEventAdapter final {
public:
    using TrackpadPinchHandler = std::function<bool(float, float, float, float)>;
    SdlEventAdapter(NativeInputRouter& router, SdlEventAdapterOptions options,
        NativePageZoomController* page_zoom = nullptr,
        TrackpadPinchHandler trackpad_pinch = {});
    bool HandleEvent(const SDL_Event& event, double timestamp_ms);
    std::uint32_t CurrentModifiers() const;
    std::uint32_t CurrentButtons() const;
    bool IsCoarsePointer() const;
    bool AdvanceGestureClock(double timestamp_ms);
    void SyncTextInput(SDL_Window* window);
    NativeTextInputState TextInputState() const;

    static std::int32_t PointerButton(std::uint8_t button);
    static std::uint32_t PointerButtons(std::uint64_t buttons);
    static std::uint32_t UpdatePointerButtons(
        std::uint32_t current, std::uint8_t button, bool down);
    static std::uint32_t Modifiers(std::uint32_t modifiers);
    static std::string KeyName(std::uint32_t keycode, std::uint32_t scancode,
        std::uint32_t modifiers, bool keypad_always_numeric);
    static float LogicalCoordinate(float coordinate, float display_scale, bool normalize);
    static float DisplayContentScale(float display_scale, float pixel_density);
    static std::pair<float, float> WheelDeltas(const SDL_Event& event);
    static bool EndsInputBatch(std::uint32_t event_type);

private:
    float LogicalCoordinate(std::uint32_t window_id, float coordinate) const;
    float TouchCoordinate(std::uint32_t window_id, float normalized, bool horizontal) const;
    std::int32_t ContactPointerId(std::uint64_t device_id, bool pen);
    void ReleaseContactPointerId(std::uint64_t device_id, bool pen);
    NativePointerContactInput PenContact(std::uint32_t window_id, std::uint64_t pen_id,
        std::uint32_t pen_state, float x, float y, double timestamp_ms);
    void RememberContact(std::uint64_t device_id, bool pen,
        const NativePointerContactInput& contact);
    void ForgetContact(std::uint64_t device_id, bool pen);
    void CancelActiveContacts(double timestamp_ms);

    struct PenAxes {
        float pressure = 0.0f;
        float tangential_pressure = 0.0f;
        float tilt_x = 0.0f;
        float tilt_y = 0.0f;
        float twist = 0.0f;
    };

    NativeInputRouter& router_;
    SdlEventAdapterOptions options_;
    NativeTouchGestureController touch_gestures_;
    TrackpadPinchHandler trackpad_pinch_;
    std::unordered_map<std::uint64_t, std::int32_t> touch_pointer_ids_;
    std::unordered_map<std::uint64_t, std::int32_t> pen_pointer_ids_;
    std::unordered_map<std::uint64_t, PenAxes> pen_axes_;
    std::unordered_map<std::uint64_t, NativePointerContactInput> active_touch_contacts_;
    std::unordered_map<std::uint64_t, NativePointerContactInput> active_pen_contacts_;
    std::uint64_t primary_touch_id_ = 0U;
    std::int32_t next_contact_pointer_id_ = 2;
    NativeTextInputCoordinator text_input_;
    bool window_focused_ = true;
    std::uint32_t mouse_buttons_ = 0U;
};

} // namespace effindom::v2::native
