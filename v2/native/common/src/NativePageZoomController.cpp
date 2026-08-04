#include "NativePageZoomController.h"

#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace effindom::v2::native {
namespace {

bool Different(float first, float second) {
    return std::abs(first - second) >= 0.001f;
}

} // namespace

NativePageZoomController::NativePageZoomController(
    Engine& engine, std::function<void()> request_frame)
    : engine_(engine), request_frame_(std::move(request_frame)) {}

void NativePageZoomController::SetEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled_) (void)Reset();
}

bool NativePageZoomController::IsEnabled() const { return enabled_; }

NativePageZoomState NativePageZoomController::State() const {
    return {engine_.ViewportScale(), engine_.ViewportOffsetX(), engine_.ViewportOffsetY()};
}

NativeScenePoint NativePageZoomController::ScreenToScene(float screen_x, float screen_y) const {
    const NativePageZoomState state = State();
    const float scale = std::max(1.0f, state.scale);
    return {(screen_x - state.offset_x) / scale, (screen_y - state.offset_y) / scale};
}

NativeScenePoint NativePageZoomController::SceneToScreen(float scene_x, float scene_y) const {
    const NativePageZoomState state = State();
    return {
        scene_x * state.scale + state.offset_x,
        scene_y * state.scale + state.offset_y,
    };
}

NativeSceneRect NativePageZoomController::SceneToScreen(const NativeSceneRect& rect) const {
    const NativeScenePoint origin = SceneToScreen(rect.x, rect.y);
    const float scale = State().scale;
    return {origin.x, origin.y, rect.width * scale, rect.height * scale};
}

float NativePageZoomController::ScreenLengthToScene(float length) const {
    return length / State().scale;
}

bool NativePageZoomController::SetScaleFromScreenAnchor(
    float scale, float screen_x, float screen_y) {
    if (!enabled_) return false;
    const NativePageZoomState before = State();
    const NativeScenePoint anchor = ScreenToScene(screen_x, screen_y);
    engine_.SetViewportZoomFromSceneAnchor(scale, anchor.x, anchor.y, screen_x, screen_y);
    return NotifyIfChanged(before);
}

bool NativePageZoomController::PanBy(float delta_x, float delta_y) {
    if (!enabled_ || State().scale <= 1.0f) return false;
    const NativePageZoomState before = State();
    engine_.PanViewportBy(delta_x, delta_y);
    return NotifyIfChanged(before);
}

void NativePageZoomController::BeginPan(double timestamp_ms) {
    if (enabled_ && State().scale > 1.0f) engine_.BeginViewportPan(timestamp_ms);
}

bool NativePageZoomController::UpdatePan(
    float delta_x, float delta_y, double timestamp_ms) {
    if (!enabled_ || State().scale <= 1.0f) return false;
    const NativePageZoomState before = State();
    engine_.UpdateViewportPan(delta_x, delta_y, timestamp_ms);
    return NotifyIfChanged(before);
}

void NativePageZoomController::EndPan(double timestamp_ms) {
    if (enabled_ && State().scale > 1.0f) engine_.EndViewportPan(timestamp_ms);
}

bool NativePageZoomController::TickMomentum(double timestamp_ms) {
    if (!enabled_ || !engine_.TickViewportPanMomentum(timestamp_ms)) return false;
    if (request_frame_) request_frame_();
    return true;
}

bool NativePageZoomController::ScaleByFactorFromScreenAnchor(
    float factor, float screen_x, float screen_y) {
    if (!enabled_ || !std::isfinite(factor) || factor <= 0.0f) return false;
    return SetScaleFromScreenAnchor(State().scale * factor, screen_x, screen_y);
}

bool NativePageZoomController::BeginPinch(float screen_x, float screen_y) {
    if (!enabled_) return false;
    engine_.ClearViewportPanMomentum();
    pinch_ = PinchState{State().scale, ScreenToScene(screen_x, screen_y)};
    return true;
}

bool NativePageZoomController::UpdatePinch(
    float relative_scale, float screen_x, float screen_y) {
    if (!enabled_ || !pinch_.has_value() || !std::isfinite(relative_scale) ||
        relative_scale <= 0.0f) return false;
    const float requested_scale = pinch_->initial_scale * relative_scale;
    engine_.SetViewportZoomFromSceneAnchor(requested_scale, pinch_->anchor.x,
        pinch_->anchor.y, screen_x, screen_y);
    const float actual_scale = State().scale;
    if (actual_scale != requested_scale) {
        pinch_ = PinchState{actual_scale, ScreenToScene(screen_x, screen_y)};
    }
    if (request_frame_) request_frame_();
    return true;
}

void NativePageZoomController::EndPinch() { pinch_.reset(); }

bool NativePageZoomController::Reset() {
    pinch_.reset();
    const NativePageZoomState before = State();
    engine_.ClearViewportPanMomentum();
    engine_.SetViewportTransform(1.0f, 0.0f, 0.0f);
    return NotifyIfChanged(before);
}

bool NativePageZoomController::NotifyIfChanged(const NativePageZoomState& before) {
    const NativePageZoomState after = State();
    const bool changed = Different(before.scale, after.scale) ||
        Different(before.offset_x, after.offset_x) ||
        Different(before.offset_y, after.offset_y);
    if (changed && request_frame_) request_frame_();
    return changed;
}

} // namespace effindom::v2::native
