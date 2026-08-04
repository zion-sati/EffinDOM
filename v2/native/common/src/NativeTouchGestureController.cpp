#include "NativeTouchGestureController.h"

#include "effindom_ui.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace effindom::v2::native {
namespace {

constexpr float kTouchScrollThreshold = 8.0f;
constexpr float kTwoFingerPanThreshold = 10.0f;
constexpr float kTwoFingerPinchThreshold = 0.04f;
constexpr float kAxisDominanceRatio = 1.25f;
constexpr std::uint32_t kGestureIntentPan = 1U;
constexpr std::uint32_t kGestureIntentPinch = 2U;
constexpr std::uint32_t kGesturePhaseBegin = 1U;
constexpr std::uint32_t kGesturePhaseUpdate = 2U;
constexpr std::uint32_t kGesturePhaseEnd = 3U;
constexpr std::uint32_t kGesturePhaseCancel = 4U;
constexpr std::uint32_t kGestureKindPan = 1U;
constexpr std::uint32_t kGestureKindPinch = 2U;

} // namespace

NativeTouchGestureController::NativeTouchGestureController(
    NativeTouchGestureCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

bool NativeTouchGestureController::HandlePointer(std::uint32_t event_type,
    const NativePointerContactInput& input, std::uint64_t hit_handle, bool pointer_handled) {
    (void)Advance(input.timestamp_ms);
    switch (event_type) {
        case UI_EVENT_POINTER_DOWN:
            BeginContact(input, hit_handle);
            return contacts_.size() > 1U;
        case UI_EVENT_POINTER_MOVE:
            return MoveContact(input, pointer_handled);
        case UI_EVENT_POINTER_UP:
            return EndContact(input, false);
        case UI_EVENT_POINTER_CANCEL:
            return EndContact(input, true);
        default:
            return false;
    }
}

bool NativeTouchGestureController::ConsumesTerminal(std::int32_t pointer_id) const {
    if (scroll_.has_value() && scroll_->started && scroll_->pointer_id == pointer_id) return true;
    if (control_gesture_.has_value() && control_gesture_->started &&
        (control_gesture_->first_pointer_id == pointer_id ||
         control_gesture_->second_pointer_id == pointer_id)) return true;
    return long_press_.has_value() && long_press_->pointer_id == pointer_id &&
        long_press_->fired && long_press_->handled && !long_press_->continues_pointer_events;
}

bool NativeTouchGestureController::Advance(double timestamp_ms) {
    if (!long_press_.has_value() || long_press_->fired ||
        timestamp_ms < long_press_->deadline_ms) return false;
    return FireLongPress(timestamp_ms);
}

bool NativeTouchGestureController::Cancel(double timestamp_ms) {
    bool changed = FinishControlGesture(true);
    if (scroll_.has_value() && scroll_->started) {
        if (callbacks_.end_touch_scroll) callbacks_.end_touch_scroll(timestamp_ms);
        changed = true;
    }
    scroll_.reset();
    CancelLongPress();
    contacts_.clear();
    return changed;
}

void NativeTouchGestureController::BeginContact(const NativePointerContactInput& input,
    std::uint64_t hit_handle) {
    Contact contact{};
    contact.input = input;
    contact.start_x = input.x;
    contact.start_y = input.y;
    contact.last_x = input.x;
    contact.last_y = input.y;
    contact.hit_handle = hit_handle;
    contacts_[input.pointer_id] = contact;

    if (input.pointer_type == UI_POINTER_TYPE_TOUCH && contacts_.size() == 1U) {
        if (callbacks_.clear_scroll_momentum) callbacks_.clear_scroll_momentum();
        scroll_ = ScrollState{input.pointer_id, hit_handle, input.x, input.y,
            input.x, input.y, 0U, false};
    }

    if ((input.pointer_type == UI_POINTER_TYPE_TOUCH || input.pointer_type == UI_POINTER_TYPE_PEN) &&
        callbacks_.resolve_long_press_owner) {
        const std::uint64_t owner = callbacks_.resolve_long_press_owner(hit_handle);
        if (owner != 0U) {
            const std::int32_t duration = callbacks_.long_press_duration_ms
                ? std::max(0, callbacks_.long_press_duration_ms(owner)) : 500;
            const float tolerance = callbacks_.long_press_movement_tolerance
                ? std::max(0.0f, callbacks_.long_press_movement_tolerance(owner)) : 10.0f;
            long_press_ = LongPressState{input.pointer_id, owner, input.x, input.y,
                input.pointer_type, input.modifiers, duration, tolerance,
                input.timestamp_ms + static_cast<double>(duration), false, false, false};
        }
    }

    if (input.pointer_type == UI_POINTER_TYPE_TOUCH && contacts_.size() >= 2U) {
        CancelLongPress();
        ResolveControlGesture();
    }
}

bool NativeTouchGestureController::MoveContact(const NativePointerContactInput& input,
    bool pointer_handled) {
    auto found = contacts_.find(input.pointer_id);
    if (found == contacts_.end()) return false;
    Contact& contact = found->second;
    contact.input = input;
    contact.moved = true;

    if (long_press_.has_value() && long_press_->pointer_id == input.pointer_id &&
        !long_press_->fired) {
        const float travel = Distance(input.x, input.y,
            long_press_->start_x, long_press_->start_y);
        if (travel >= long_press_->movement_tolerance) CancelLongPress(input.pointer_id);
    }

    if (control_gesture_.has_value()) {
        contact.last_x = input.x;
        contact.last_y = input.y;
        return UpdateControlGesture(input.pointer_id);
    }

    if (!scroll_.has_value() || scroll_->pointer_id != input.pointer_id ||
        input.pointer_type != UI_POINTER_TYPE_TOUCH) {
        contact.last_x = input.x;
        contact.last_y = input.y;
        return false;
    }

    ScrollState& scroll = *scroll_;
    if (!scroll.started) {
        if (pointer_handled) {
            scroll.last_x = input.x;
            scroll.last_y = input.y;
            contact.last_x = input.x;
            contact.last_y = input.y;
            return false;
        }
        const float travel_x = input.x - scroll.start_x;
        const float travel_y = input.y - scroll.start_y;
        if (Distance(input.x, input.y, scroll.start_x, scroll.start_y) < kTouchScrollThreshold) {
            return true;
        }
        const float abs_x = std::abs(travel_x);
        const float abs_y = std::abs(travel_y);
        scroll.axis = abs_x >= abs_y * kAxisDominanceRatio ? 1U
            : abs_y >= abs_x * kAxisDominanceRatio ? 2U
            : abs_x >= abs_y ? 1U : 2U;
        scroll.started = true;
        CancelLongPress(input.pointer_id);
        if (callbacks_.cancel_pointer) callbacks_.cancel_pointer(input);
        if (callbacks_.begin_touch_scroll) callbacks_.begin_touch_scroll(
            scroll.start_handle, scroll.start_x, scroll.start_y, input.timestamp_ms);
    }

    const float delta_x = scroll.axis == 1U ? scroll.last_x - input.x : 0.0f;
    const float delta_y = scroll.axis == 2U ? scroll.last_y - input.y : 0.0f;
    if ((!callbacks_.touch_scroll_can_consume || callbacks_.touch_scroll_can_consume(delta_x, delta_y)) &&
        callbacks_.update_touch_scroll) {
        callbacks_.update_touch_scroll(delta_x, delta_y, input.timestamp_ms);
    }
    scroll.last_x = input.x;
    scroll.last_y = input.y;
    contact.last_x = input.x;
    contact.last_y = input.y;
    return true;
}

bool NativeTouchGestureController::EndContact(const NativePointerContactInput& input,
    bool cancelled) {
    const bool consumed = ConsumesTerminal(input.pointer_id);
    if (control_gesture_.has_value() &&
        (control_gesture_->first_pointer_id == input.pointer_id ||
         control_gesture_->second_pointer_id == input.pointer_id)) {
        (void)FinishControlGesture(cancelled);
    }
    if (scroll_.has_value() && scroll_->pointer_id == input.pointer_id) {
        if (scroll_->started && callbacks_.end_touch_scroll) {
            callbacks_.end_touch_scroll(input.timestamp_ms);
        }
        scroll_.reset();
    }
    CancelLongPress(input.pointer_id);
    contacts_.erase(input.pointer_id);
    if (contacts_.size() < 2U) control_gesture_.reset();
    return consumed;
}

void NativeTouchGestureController::ResolveControlGesture() {
    if (control_gesture_.has_value() || contacts_.size() < 2U ||
        !callbacks_.hit_test) return;
    auto first = contacts_.begin();
    auto second = std::next(first);
    if (first->second.input.pointer_type != UI_POINTER_TYPE_TOUCH ||
        second->second.input.pointer_type != UI_POINTER_TYPE_TOUCH) return;
    const float midpoint_x = (first->second.last_x + second->second.last_x) * 0.5f;
    const float midpoint_y = (first->second.last_y + second->second.last_y) * 0.5f;
    const std::uint64_t hit = callbacks_.hit_test(midpoint_x, midpoint_y);
    const std::uint64_t owner = callbacks_.resolve_gesture_owner
        ? callbacks_.resolve_gesture_owner(hit) : 0U;
    const std::uint32_t intent = callbacks_.gesture_intent ? callbacks_.gesture_intent(owner) : 0U;
    const bool can_page_zoom = callbacks_.page_zoom_enabled && callbacks_.page_zoom_enabled();
    if (intent == 0U && !can_page_zoom) return;
    control_gesture_ = ControlGestureState{
        first->first, second->first, owner, intent, 0U,
        std::max(1.0f, Distance(first->second.last_x, first->second.last_y,
            second->second.last_x, second->second.last_y)),
        midpoint_x, midpoint_y, false, false, false, intent == 0U, false,
    };
    if (scroll_.has_value() && scroll_->started && callbacks_.end_touch_scroll) {
        callbacks_.end_touch_scroll(second->second.input.timestamp_ms);
    }
    scroll_.reset();
}

bool NativeTouchGestureController::UpdateControlGesture(std::int32_t moved_pointer_id) {
    if (!control_gesture_.has_value()) return false;
    ControlGestureState& gesture = *control_gesture_;
    const auto first = contacts_.find(gesture.first_pointer_id);
    const auto second = contacts_.find(gesture.second_pointer_id);
    if (first == contacts_.end() || second == contacts_.end()) return FinishControlGesture(false);
    if (moved_pointer_id == gesture.first_pointer_id) gesture.first_moved = true;
    if (moved_pointer_id == gesture.second_pointer_id) gesture.second_moved = true;
    const float midpoint_x = (first->second.last_x + second->second.last_x) * 0.5f;
    const float midpoint_y = (first->second.last_y + second->second.last_y) * 0.5f;
    const float distance = std::max(1.0f, Distance(first->second.last_x, first->second.last_y,
        second->second.last_x, second->second.last_y));
    const float scale = distance / gesture.initial_distance;
    const float delta_x = midpoint_x - gesture.last_midpoint_x;
    const float delta_y = midpoint_y - gesture.last_midpoint_y;
    if (!gesture.started) {
        const bool pinch_ready = std::abs(scale - 1.0f) >= kTwoFingerPinchThreshold;
        const bool pan_ready = Distance(midpoint_x, midpoint_y,
            gesture.last_midpoint_x, gesture.last_midpoint_y) >= kTwoFingerPanThreshold;
        const bool both_moved = gesture.first_moved && gesture.second_moved;
        if (!gesture.page_zoom_only && pinch_ready &&
            (gesture.intent & kGestureIntentPinch) != 0U) {
            gesture.kind = kGestureKindPinch;
        } else if (!gesture.page_zoom_only && pan_ready && both_moved &&
            (gesture.intent & kGestureIntentPan) != 0U) {
            gesture.kind = kGestureKindPan;
        } else if ((pinch_ready || (pan_ready && both_moved)) &&
            callbacks_.page_zoom_enabled && callbacks_.page_zoom_enabled()) {
            gesture.kind = kGestureKindPinch;
            gesture.page_zoom_only = true;
        } else {
            return true;
        }
        gesture.started = true;
        if (callbacks_.cancel_pointer) {
            callbacks_.cancel_pointer(first->second.input);
            callbacks_.cancel_pointer(second->second.input);
        }
        if (!gesture.page_zoom_only && callbacks_.dispatch_gesture) {
            (void)callbacks_.dispatch_gesture(gesture.owner_handle, kGesturePhaseBegin,
                gesture.kind, midpoint_x, midpoint_y, 0.0f, 0.0f, 1.0f, 2);
        }
    }
    bool handled = false;
    if (!gesture.page_zoom_only && callbacks_.dispatch_gesture) {
        handled = callbacks_.dispatch_gesture(gesture.owner_handle, kGesturePhaseUpdate,
            gesture.kind, midpoint_x, midpoint_y, delta_x, delta_y, scale, 2);
    }
    if (handled && gesture.page_zoom_started) {
        if (callbacks_.end_page_zoom) callbacks_.end_page_zoom();
        gesture.page_zoom_started = false;
    } else if (!handled && callbacks_.page_zoom_enabled && callbacks_.page_zoom_enabled()) {
        if (!gesture.page_zoom_started && callbacks_.begin_page_zoom) {
            gesture.page_zoom_started = callbacks_.begin_page_zoom(
                gesture.last_midpoint_x, gesture.last_midpoint_y);
        }
        if (gesture.page_zoom_started && callbacks_.update_page_zoom) {
            (void)callbacks_.update_page_zoom(midpoint_x, midpoint_y, scale);
        }
    }
    gesture.last_midpoint_x = midpoint_x;
    gesture.last_midpoint_y = midpoint_y;
    if (handled && callbacks_.flush_retained_changes) callbacks_.flush_retained_changes();
    return true;
}

bool NativeTouchGestureController::FinishControlGesture(bool cancelled) {
    if (!control_gesture_.has_value()) return false;
    const ControlGestureState gesture = *control_gesture_;
    control_gesture_.reset();
    if (gesture.page_zoom_started && callbacks_.end_page_zoom) callbacks_.end_page_zoom();
    if (!gesture.started || gesture.page_zoom_only || !callbacks_.dispatch_gesture) {
        return gesture.started;
    }
    const bool handled = callbacks_.dispatch_gesture(gesture.owner_handle,
        cancelled ? kGesturePhaseCancel : kGesturePhaseEnd, gesture.kind,
        gesture.last_midpoint_x, gesture.last_midpoint_y, 0.0f, 0.0f, 1.0f, 2);
    if (handled && callbacks_.flush_retained_changes) callbacks_.flush_retained_changes();
    return true;
}

void NativeTouchGestureController::CancelLongPress(
    std::optional<std::int32_t> pointer_id) {
    if (!long_press_.has_value() ||
        (pointer_id.has_value() && long_press_->pointer_id != *pointer_id)) return;
    long_press_.reset();
}

bool NativeTouchGestureController::FireLongPress(double timestamp_ms) {
    (void)timestamp_ms;
    if (!long_press_.has_value() || long_press_->fired) return false;
    LongPressState& gesture = *long_press_;
    gesture.fired = true;
    gesture.handled = callbacks_.dispatch_long_press && callbacks_.dispatch_long_press(
        gesture.owner_handle, gesture.start_x, gesture.start_y, gesture.pointer_id,
        gesture.pointer_type, gesture.modifiers, gesture.duration_ms);
    gesture.continues_pointer_events = callbacks_.long_press_continues_pointer_events &&
        callbacks_.long_press_continues_pointer_events(gesture.owner_handle);
    if (gesture.handled && callbacks_.flush_retained_changes) callbacks_.flush_retained_changes();
    return gesture.handled;
}

float NativeTouchGestureController::Distance(float first_x, float first_y,
    float second_x, float second_y) {
    return std::hypot(second_x - first_x, second_y - first_y);
}

} // namespace effindom::v2::native
