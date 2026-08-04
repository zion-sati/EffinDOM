#pragma once

#include <functional>
#include <optional>

namespace effindom::v2 {
class Engine;
}

namespace effindom::v2::native {

struct NativePageZoomState {
    float scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

struct NativeScenePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct NativeSceneRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

class NativePageZoomController final {
public:
    NativePageZoomController(Engine& engine, std::function<void()> request_frame);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    NativePageZoomState State() const;
    NativeScenePoint ScreenToScene(float screen_x, float screen_y) const;
    NativeScenePoint SceneToScreen(float scene_x, float scene_y) const;
    NativeSceneRect SceneToScreen(const NativeSceneRect& rect) const;
    float ScreenLengthToScene(float length) const;
    bool SetScaleFromScreenAnchor(float scale, float screen_x, float screen_y);
    bool ScaleByFactorFromScreenAnchor(float factor, float screen_x, float screen_y);
    bool BeginPinch(float screen_x, float screen_y);
    bool UpdatePinch(float relative_scale, float screen_x, float screen_y);
    void EndPinch();
    bool PanBy(float delta_x, float delta_y);
    void BeginPan(double timestamp_ms);
    bool UpdatePan(float delta_x, float delta_y, double timestamp_ms);
    void EndPan(double timestamp_ms);
    bool TickMomentum(double timestamp_ms);
    bool Reset();

private:
    bool NotifyIfChanged(const NativePageZoomState& before);

    Engine& engine_;
    std::function<void()> request_frame_;
    struct PinchState {
        float initial_scale = 1.0f;
        NativeScenePoint anchor{};
    };
    std::optional<PinchState> pinch_;
    bool enabled_ = true;
};

} // namespace effindom::v2::native
