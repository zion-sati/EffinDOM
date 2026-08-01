#include "NativeTimerCoordinator.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

extern "C" void __fui_on_timer(std::uint32_t timer_id);

namespace effindom::v2::native {
namespace {

using Clock = std::chrono::steady_clock;

struct TimerEntry {
    Clock::time_point deadline;
    std::uint64_t generation = 0U;
    std::uint64_t sequence = 0U;
    bool queued = false;
};

struct DueTimer {
    std::uint32_t timer_id = 0U;
    Clock::time_point deadline;
    std::uint64_t generation = 0U;
    std::uint64_t sequence = 0U;
};

} // namespace

struct NativeTimerCoordinator::State final {
    State(UiPost post_to_ui, TimerDispatch dispatch_timer)
        : post(std::move(post_to_ui)), dispatch(std::move(dispatch_timer)) {}

    UiPost post;
    TimerDispatch dispatch;
    std::mutex mutex;
    std::condition_variable wake;
    std::map<std::uint32_t, TimerEntry> timers;
    std::uint64_t next_generation = 1U;
    std::uint64_t next_sequence = 1U;
    bool alive = true;
    bool stopping = false;
    std::thread worker;
};

void NativeTimerCoordinator::DeliverTimer(
    const std::weak_ptr<State>& weak_state,
    std::uint32_t timer_id,
    std::uint64_t generation) {
    const auto state = weak_state.lock();
    if (state == nullptr) return;
    NativeTimerCoordinator::TimerDispatch dispatch;
    {
        std::lock_guard lock(state->mutex);
        const auto iterator = state->timers.find(timer_id);
        if (!state->alive || iterator == state->timers.end() ||
            iterator->second.generation != generation || !iterator->second.queued) {
            return;
        }
        state->timers.erase(iterator);
        dispatch = state->dispatch;
    }
    dispatch(timer_id);
}

void NativeTimerCoordinator::RemoveFailedPost(
    const std::shared_ptr<State>& state,
    std::uint32_t timer_id,
    std::uint64_t generation) {
    std::lock_guard lock(state->mutex);
    const auto iterator = state->timers.find(timer_id);
    if (iterator != state->timers.end() && iterator->second.generation == generation) {
        state->timers.erase(iterator);
    }
}

void NativeTimerCoordinator::RunTimerThread(const std::shared_ptr<State>& state) {
    std::unique_lock lock(state->mutex);
    while (!state->stopping) {
        auto earliest = state->timers.end();
        for (auto iterator = state->timers.begin(); iterator != state->timers.end(); ++iterator) {
            if (iterator->second.queued) continue;
            if (earliest == state->timers.end() ||
                iterator->second.deadline < earliest->second.deadline ||
                (iterator->second.deadline == earliest->second.deadline &&
                    iterator->second.sequence < earliest->second.sequence)) {
                earliest = iterator;
            }
        }
        if (earliest == state->timers.end()) {
            state->wake.wait(lock);
            continue;
        }
        const Clock::time_point now = Clock::now();
        if (earliest->second.deadline > now) {
            state->wake.wait_until(lock, earliest->second.deadline);
            continue;
        }

        std::vector<DueTimer> due;
        for (auto& [timer_id, timer] : state->timers) {
            if (!timer.queued && timer.deadline <= now) {
                timer.queued = true;
                due.push_back(DueTimer{
                    timer_id, timer.deadline, timer.generation, timer.sequence,
                });
            }
        }
        std::sort(due.begin(), due.end(), [](const DueTimer& left, const DueTimer& right) {
            if (left.deadline != right.deadline) return left.deadline < right.deadline;
            return left.sequence < right.sequence;
        });

        const auto post = state->post;
        const std::weak_ptr<State> weak_state = state;
        lock.unlock();
        for (const DueTimer& timer : due) {
            const bool posted = post([weak_state, timer] {
                DeliverTimer(weak_state, timer.timer_id, timer.generation);
                return true;
            });
            if (!posted) RemoveFailedPost(state, timer.timer_id, timer.generation);
        }
        lock.lock();
    }
}

NativeTimerCoordinator::NativeTimerCoordinator(UiPost post_to_ui, TimerDispatch dispatch)
    : state_(std::make_shared<State>(
          std::move(post_to_ui),
          dispatch ? std::move(dispatch) : TimerDispatch{__fui_on_timer})) {
    state_->worker = std::thread(RunTimerThread, state_);
}

NativeTimerCoordinator::~NativeTimerCoordinator() {
    {
        std::lock_guard lock(state_->mutex);
        state_->alive = false;
        state_->stopping = true;
        state_->timers.clear();
    }
    state_->wake.notify_all();
    if (state_->worker.joinable()) state_->worker.join();
    state_.reset();
}

void NativeTimerCoordinator::Start(std::uint32_t timer_id, std::int32_t delay_ms) {
    if (timer_id == 0U) return;
    const auto delay = std::chrono::milliseconds(std::max(delay_ms, 0));
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->alive || state_->stopping) return;
        state_->timers[timer_id] = TimerEntry{
            Clock::now() + delay,
            state_->next_generation++,
            state_->next_sequence++,
            false,
        };
    }
    state_->wake.notify_all();
}

void NativeTimerCoordinator::Cancel(std::uint32_t timer_id) {
    {
        std::lock_guard lock(state_->mutex);
        state_->timers.erase(timer_id);
    }
    state_->wake.notify_all();
}

void NativeTimerCoordinator::Clear() {
    {
        std::lock_guard lock(state_->mutex);
        state_->timers.clear();
    }
    state_->wake.notify_all();
}

} // namespace effindom::v2::native
