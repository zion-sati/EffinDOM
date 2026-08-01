#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace effindom::v2::native {

class NativeTimerCoordinator final {
public:
    using UiTask = std::function<bool()>;
    using UiPost = std::function<bool(UiTask)>;
    using TimerDispatch = std::function<void(std::uint32_t)>;

    explicit NativeTimerCoordinator(UiPost post_to_ui, TimerDispatch dispatch = {});
    ~NativeTimerCoordinator();

    NativeTimerCoordinator(const NativeTimerCoordinator&) = delete;
    NativeTimerCoordinator& operator=(const NativeTimerCoordinator&) = delete;

    void Start(std::uint32_t timer_id, std::int32_t delay_ms);
    void Cancel(std::uint32_t timer_id);
    void Clear();

private:
    struct State;
    static void DeliverTimer(
        const std::weak_ptr<State>& weak_state,
        std::uint32_t timer_id,
        std::uint64_t generation);
    static void RemoveFailedPost(
        const std::shared_ptr<State>& state,
        std::uint32_t timer_id,
        std::uint64_t generation);
    static void RunTimerThread(const std::shared_ptr<State>& state);
    std::shared_ptr<State> state_;
};

} // namespace effindom::v2::native
