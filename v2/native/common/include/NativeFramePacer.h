#pragma once

#include <functional>

namespace effindom::v2::native {

class NativeFramePacer final {
public:
    using WaitForRefresh = std::function<void()>;
    using SetRefreshActive = std::function<void(bool)>;

    explicit NativeFramePacer(
        WaitForRefresh wait_for_refresh = {},
        SetRefreshActive set_refresh_active = {})
        : wait_for_refresh_(std::move(wait_for_refresh)),
          set_refresh_active_(std::move(set_refresh_active)) {}

    void WaitForFrame() {
        if (!frame_scheduled_) return;
        if (wait_for_refresh_) wait_for_refresh_();
        frame_scheduled_ = false;
    }

    void FrameCompleted(bool follow_up_pending) {
        frame_scheduled_ = follow_up_pending;
        if (set_refresh_active_) set_refresh_active_(follow_up_pending);
    }

    bool HasScheduledFrame() const { return frame_scheduled_; }
    bool ShouldBlockForEvent() const { return !HasScheduledFrame(); }

private:
    WaitForRefresh wait_for_refresh_;
    SetRefreshActive set_refresh_active_;
    bool frame_scheduled_ = false;
};

} // namespace effindom::v2::native
