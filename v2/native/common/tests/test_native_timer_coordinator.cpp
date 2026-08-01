#include "NativeTimerCoordinator.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace effindom::v2::native {
namespace {

class TestUiQueue final {
public:
    bool Post(NativeTimerCoordinator::UiTask task) {
        {
            std::lock_guard lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        changed_.notify_all();
        return true;
    }

    bool WaitFor(std::size_t count) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), [&] {
            return tasks_.size() >= count;
        });
    }

    void RunAll() {
        std::deque<NativeTimerCoordinator::UiTask> tasks;
        {
            std::lock_guard lock(mutex_);
            tasks.swap(tasks_);
        }
        for (auto& task : tasks) task();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<NativeTimerCoordinator::UiTask> tasks_;
};

} // namespace

TEST_CASE("native timers fire once on the UI queue thread", "[v2][native][timer]") {
    TestUiQueue queue;
    std::uint32_t fired = 0U;
    std::thread::id callback_thread;
    NativeTimerCoordinator timers(
        [&](auto task) { return queue.Post(std::move(task)); },
        [&](std::uint32_t timer_id) {
            fired = timer_id;
            callback_thread = std::this_thread::get_id();
        });

    timers.Start(7U, 0);
    REQUIRE(queue.WaitFor(1U));
    CHECK(fired == 0U);
    const std::thread::id ui_thread = std::this_thread::get_id();
    queue.RunAll();
    CHECK(fired == 7U);
    CHECK(callback_thread == ui_thread);
    queue.RunAll();
    CHECK(fired == 7U);
}

TEST_CASE("native timer cancellation and replacement suppress stale queued generations", "[v2][native][timer]") {
    TestUiQueue queue;
    std::vector<std::uint32_t> fired;
    NativeTimerCoordinator timers(
        [&](auto task) { return queue.Post(std::move(task)); },
        [&](std::uint32_t timer_id) { fired.push_back(timer_id); });

    timers.Start(1U, 0);
    REQUIRE(queue.WaitFor(1U));
    timers.Cancel(1U);
    queue.RunAll();
    CHECK(fired.empty());

    timers.Start(2U, 0);
    REQUIRE(queue.WaitFor(1U));
    timers.Start(2U, 0);
    REQUIRE(queue.WaitFor(2U));
    queue.RunAll();
    CHECK(fired == std::vector<std::uint32_t>{2U});
}

TEST_CASE("native timers define non-positive delays and preserve deadline order", "[v2][native][timer]") {
    TestUiQueue queue;
    std::vector<std::uint32_t> fired;
    NativeTimerCoordinator timers(
        [&](auto task) { return queue.Post(std::move(task)); },
        [&](std::uint32_t timer_id) { fired.push_back(timer_id); });

    timers.Start(3U, 35);
    timers.Start(1U, -10);
    timers.Start(2U, 15);
    REQUIRE(queue.WaitFor(3U));
    queue.RunAll();
    CHECK(fired == std::vector<std::uint32_t>{1U, 2U, 3U});
}

TEST_CASE("native timer clear and teardown invalidate already queued callbacks", "[v2][native][timer]") {
    TestUiQueue queue;
    std::vector<std::uint32_t> fired;
    auto timers = std::make_unique<NativeTimerCoordinator>(
        [&](auto task) { return queue.Post(std::move(task)); },
        [&](std::uint32_t timer_id) { fired.push_back(timer_id); });
    timers->Start(1U, 0);
    REQUIRE(queue.WaitFor(1U));
    timers->Clear();
    queue.RunAll();
    CHECK(fired.empty());

    timers->Start(2U, 0);
    REQUIRE(queue.WaitFor(1U));
    timers.reset();
    queue.RunAll();
    CHECK(fired.empty());
}

} // namespace effindom::v2::native
