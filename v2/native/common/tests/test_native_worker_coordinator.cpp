#include "NativeWorkerCoordinator.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace effindom::v2::native {
namespace {

class TestUiQueue final {
public:
    bool Post(NativeWorkerCoordinator::UiTask task) {
        {
            std::lock_guard lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        changed_.notify_all();
        return true;
    }

    bool WaitFor(std::size_t count = 1U) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), [&] {
            return tasks_.size() >= count;
        });
    }

    std::size_t Size() {
        std::lock_guard lock(mutex_);
        return tasks_.size();
    }

    void RunAll() {
        std::deque<NativeWorkerCoordinator::UiTask> tasks;
        {
            std::lock_guard lock(mutex_);
            tasks.swap(tasks_);
        }
        for (auto& task : tasks) task();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<NativeWorkerCoordinator::UiTask> tasks_;
};

class FunctionAdapter final : public NativeWorkerLanguageAdapter {
public:
    using Function = std::function<void(const NativeWorkerStartRequest&, NativeWorkerReporter&)>;
    explicit FunctionAdapter(Function function) : function_(std::move(function)) {}
    void Invoke(
        const NativeWorkerStartRequest& request,
        NativeWorkerReporter& reporter) override {
        function_(request, reporter);
    }

private:
    Function function_;
};

struct EventLog final {
    std::mutex mutex;
    std::vector<std::string> events;

    void Add(const char* kind, std::uint32_t id, const std::string& text) {
        std::lock_guard lock(mutex);
        events.push_back(std::string(kind) + ":" + std::to_string(id) + ":" + text);
    }
};

NativeWorkerCoordinator MakeCoordinator(
    const std::shared_ptr<NativeWorkerLanguageAdapter>& adapter,
    TestUiQueue& queue,
    EventLog& log) {
    return NativeWorkerCoordinator(
        adapter,
        [&](auto task) { return queue.Post(std::move(task)); },
        [&](std::uint32_t id, const std::string& text) { log.Add("progress", id, text); },
        [&](std::uint32_t id, const std::string& text) { log.Add("complete", id, text); },
        [&](std::uint32_t id, const std::string& text) { log.Add("error", id, text); });
}

} // namespace

TEST_CASE("native worker requests own input and coalesce ordered UI delivery", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    std::string observed;
    auto adapter = std::make_shared<FunctionAdapter>(
        [&](const NativeWorkerStartRequest& request, NativeWorkerReporter& reporter) {
            observed = request.input;
            reporter.Progress("one");
            reporter.Progress("two");
            reporter.Complete("done");
            reporter.Error("late");
        });
    auto workers = MakeCoordinator(adapter, queue, log);

    std::string input = "owned input";
    workers.Start(7U, "workers.wasm", "entry", input);
    input.assign("destroyed");
    REQUIRE(queue.WaitFor());
    CHECK(queue.Size() == 1U);
    queue.RunAll();
    CHECK(observed == "owned input");
    CHECK(log.events == std::vector<std::string>{
        "progress:7:one", "progress:7:two", "complete:7:done",
    });
}

TEST_CASE("native worker cancellation and session generations suppress stale envelopes", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool release = false;
    auto adapter = std::make_shared<FunctionAdapter>(
        [&](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            std::unique_lock lock(gate_mutex);
            gate.wait(lock, [&] { return release; });
            lock.unlock();
            reporter.Progress("stale");
            if (!reporter.IsCancelled()) reporter.Complete("unexpected");
        });
    auto workers = MakeCoordinator(adapter, queue, log);
    workers.SetSessionGeneration(10U);
    workers.Start(1U, "workers.wasm", "entry", "input");
    workers.Cancel(1U);
    workers.SetSessionGeneration(11U);
    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate.notify_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.RunAll();
    CHECK(log.events.empty());
}

TEST_CASE("native worker cancel is idempotent and permits generation-safe id reuse", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool first_entered = false;
    bool release_first = false;
    std::atomic<std::uint32_t> invocation = 0U;
    auto adapter = std::make_shared<FunctionAdapter>(
        [&](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            if (++invocation == 1U) {
                std::unique_lock lock(gate_mutex);
                first_entered = true;
                gate.notify_all();
                gate.wait(lock, [&] { return release_first || reporter.IsCancelled(); });
                return;
            }
            reporter.Complete("reused");
        });
    auto workers = MakeCoordinator(adapter, queue, log);

    workers.Cancel(9U);
    workers.Start(9U, "workers.wasm", "entry", "first");
    {
        std::unique_lock lock(gate_mutex);
        REQUIRE(gate.wait_for(lock, std::chrono::seconds(1), [&] { return first_entered; }));
    }
    workers.Cancel(9U);
    workers.Cancel(9U);
    workers.Start(9U, "workers.wasm", "entry", "second");
    REQUIRE(queue.WaitFor());
    queue.RunAll();
    CHECK(log.events == std::vector<std::string>{"complete:9:reused"});
    {
        std::lock_guard lock(gate_mutex);
        release_first = true;
    }
    gate.notify_all();
}

TEST_CASE("native worker cancellation removes already queued progress and completion", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    auto adapter = std::make_shared<FunctionAdapter>(
        [](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            reporter.Progress("queued");
            reporter.Complete("queued terminal");
        });
    auto workers = MakeCoordinator(adapter, queue, log);
    workers.Start(13U, "workers.wasm", "entry", "");
    REQUIRE(queue.WaitFor());
    workers.Cancel(13U);
    queue.RunAll();
    CHECK(log.events.empty());
}

TEST_CASE("native worker cooperative cancellation can report one terminal error", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    auto adapter = std::make_shared<FunctionAdapter>(
        [](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            reporter.WaitForCancellation(std::chrono::seconds(5));
            reporter.Progress("late progress");
            reporter.Complete("late completion");
            reporter.Error("cancelled:25");
        });
    auto workers = MakeCoordinator(adapter, queue, log);
    workers.Start(14U, "workers.wasm", "entry", "");
    workers.Cancel(14U);
    REQUIRE(queue.WaitFor());
    queue.RunAll();
    CHECK(log.events == std::vector<std::string>{"error:14:cancelled:25"});
}

TEST_CASE("native worker clear wakes delayed cooperative work and prevents remount leakage", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    std::atomic<std::uint32_t> completed = 0U;
    auto adapter = std::make_shared<FunctionAdapter>(
        [&](const NativeWorkerStartRequest& request, NativeWorkerReporter& reporter) {
            if (request.input == "old") {
                reporter.WaitForCancellation(std::chrono::seconds(5));
                reporter.Progress("stale");
                return;
            }
            ++completed;
            reporter.Complete("new");
        });
    auto workers = MakeCoordinator(adapter, queue, log);
    workers.SetSessionGeneration(20U);
    workers.Start(1U, "workers.wasm", "entry", "old");
    const auto started = std::chrono::steady_clock::now();
    workers.SetSessionGeneration(21U);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
    workers.Start(1U, "workers.wasm", "entry", "new");
    REQUIRE(queue.WaitFor());
    queue.RunAll();
    CHECK(completed.load() == 1U);
    CHECK(log.events == std::vector<std::string>{"complete:1:new"});
}

TEST_CASE("native worker teardown is bounded and detached work has no callbacks", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    auto release = std::make_shared<std::atomic<bool>>(false);
    auto adapter = std::make_shared<FunctionAdapter>(
        [release](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            reporter.Progress("revoked");
            reporter.Complete("revoked");
        });
    const auto started = std::chrono::steady_clock::now();
    {
        auto workers = MakeCoordinator(adapter, queue, log);
        workers.Start(1U, "workers.wasm", "entry", "");
    }
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
    release->store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.RunAll();
    CHECK(log.events.empty());
}

TEST_CASE("native worker lifecycle stress returns cooperative work to a clean baseline", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    constexpr std::uint32_t worker_count = 96U;
    std::atomic<std::uint32_t> entered = 0U;
    std::atomic<std::uint32_t> exited = 0U;
    std::atomic<std::uint32_t> cancellation_observed = 0U;
    std::atomic<bool> release = false;
    std::mutex lifecycle_mutex;
    std::condition_variable lifecycle_changed;
    auto adapter = std::make_shared<FunctionAdapter>(
        [&](const NativeWorkerStartRequest& request, NativeWorkerReporter& reporter) {
            ++entered;
            lifecycle_changed.notify_all();
            if (request.input == "reuse") {
                reporter.Complete("done");
            } else {
                reporter.Progress("ready");
                if (request.worker_id % 2U == 0U) {
                    reporter.WaitForCancellation(std::chrono::seconds(5));
                    if (reporter.IsCancelled()) {
                        ++cancellation_observed;
                        lifecycle_changed.notify_all();
                    }
                } else {
                    while (!release.load() && !reporter.IsCancelled()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                if (!reporter.IsCancelled()) reporter.Complete("done");
            }
            ++exited;
            lifecycle_changed.notify_all();
        });
    auto workers = MakeCoordinator(adapter, queue, log);

    for (std::uint32_t id = 1U; id <= worker_count; ++id) {
        workers.Start(id, "workers.wasm", "entry", "stress");
    }
    {
        std::unique_lock lock(lifecycle_mutex);
        REQUIRE(lifecycle_changed.wait_for(lock, std::chrono::seconds(2), [&] {
            return entered.load() == worker_count;
        }));
    }
    REQUIRE(queue.WaitFor());
    const auto queued_message_batches = queue.Size();
    CHECK(queued_message_batches == 1U);

    const auto cancellation_started = std::chrono::steady_clock::now();
    for (std::uint32_t id = 2U; id <= worker_count; id += 2U) {
        workers.Cancel(id);
    }
    {
        std::unique_lock lock(lifecycle_mutex);
        REQUIRE(lifecycle_changed.wait_for(lock, std::chrono::seconds(1), [&] {
            return cancellation_observed.load() == worker_count / 2U;
        }));
    }
    const auto cancellation_latency = std::chrono::steady_clock::now() - cancellation_started;
    // wait_for above enforces one-second cancellation observation. This
    // measurement also includes scheduler delay while reacquiring the mutex,
    // which is material when the 96-thread stress case runs under emulation.
    CHECK(cancellation_latency < std::chrono::seconds(2));

    release.store(true);
    {
        std::unique_lock lock(lifecycle_mutex);
        REQUIRE(lifecycle_changed.wait_for(lock, std::chrono::seconds(5), [&] {
            return exited.load() == worker_count;
        }));
    }

    queue.RunAll();
    std::size_t delivered_progress = 0U;
    std::size_t delivered_complete = 0U;
    for (const auto& event : log.events) {
        if (event.rfind("progress:", 0U) == 0U) ++delivered_progress;
        if (event.rfind("complete:", 0U) == 0U) ++delivered_complete;
    }
    CHECK(delivered_progress == worker_count / 2U);
    CHECK(delivered_complete == worker_count / 2U);
    const auto dropped_stale = worker_count - delivered_progress;

    workers.Start(2U, "workers.wasm", "entry", "reuse");
    REQUIRE(queue.WaitFor());
    queue.RunAll();
    CHECK(log.events.back() == "complete:2:done");
    CHECK(entered.load() == worker_count + 1U);
    CHECK(exited.load() == worker_count + 1U);

    const auto teardown_started = std::chrono::steady_clock::now();
    workers.Clear();
    const auto teardown_latency = std::chrono::steady_clock::now() - teardown_started;
    CHECK(teardown_latency < std::chrono::seconds(1));

    std::cout << "Native Worker acceptance: stress_worker_threads=" << worker_count
              << " total_invocations=" << entered.load()
              << " active_workers=0"
              << " queued_message_batches=" << queued_message_batches
              << " delivered_progress=" << delivered_progress
              << " dropped_stale=" << dropped_stale
              << " cancellation_observed=" << cancellation_observed.load()
              << " cancellation_latency_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(cancellation_latency).count()
              << " teardown_latency_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(teardown_latency).count()
              << '\n';
}

TEST_CASE("native workers normalize oversized input and callback payloads", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    auto adapter = std::make_shared<FunctionAdapter>(
        [](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            reporter.Progress(std::string(kNativeWorkerMaxPayloadBytes + 1U, 'x'));
            reporter.Complete("late");
        });
    auto workers = MakeCoordinator(adapter, queue, log);

    workers.Start(
        1U, "workers.wasm", "entry",
        std::string(kNativeWorkerMaxPayloadBytes + 1U, 'x'));
    REQUIRE(queue.WaitFor());
    queue.RunAll();
    workers.Start(2U, "workers.wasm", "entry", "small");
    REQUIRE(queue.WaitFor());
    queue.RunAll();
    CHECK(log.events.size() == 2U);
    CHECK(log.events[0].find("error:1:Worker payload exceeds") == 0U);
    CHECK(log.events[1].find("error:2:Worker payload exceeds") == 0U);
}

TEST_CASE("native worker duplicate ids reject with one terminal error", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool release = false;
    auto adapter = std::make_shared<FunctionAdapter>(
        [&](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            std::unique_lock lock(gate_mutex);
            gate.wait(lock, [&] { return release; });
            lock.unlock();
            reporter.Complete("late");
        });
    auto workers = MakeCoordinator(adapter, queue, log);
    workers.Start(3U, "workers.wasm", "entry", "first");
    workers.Start(3U, "workers.wasm", "entry", "duplicate");
    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate.notify_all();
    REQUIRE(queue.WaitFor());
    queue.RunAll();
    REQUIRE_FALSE(log.events.empty());
    CHECK(log.events == std::vector<std::string>{"error:3:Worker already started."});
}

TEST_CASE("native worker mailbox overflow becomes one terminal error", "[v2][native][worker]") {
    TestUiQueue queue;
    EventLog log;
    std::mutex done_mutex;
    std::condition_variable done_changed;
    bool done = false;
    auto adapter = std::make_shared<FunctionAdapter>(
        [&](const NativeWorkerStartRequest&, NativeWorkerReporter& reporter) {
            for (std::size_t index = 0; index <= kNativeWorkerMailboxCapacity; ++index) {
                reporter.Progress(std::to_string(index));
            }
            {
                std::lock_guard lock(done_mutex);
                done = true;
            }
            done_changed.notify_all();
        });
    auto workers = MakeCoordinator(adapter, queue, log);
    workers.Start(4U, "workers.wasm", "entry", "input");
    REQUIRE(queue.WaitFor());
    {
        std::unique_lock lock(done_mutex);
        REQUIRE(done_changed.wait_for(lock, std::chrono::seconds(2), [&] { return done; }));
    }
    queue.RunAll();
    REQUIRE(log.events.size() == kNativeWorkerMailboxCapacity);
    CHECK(log.events.back() == "error:4:Worker message queue capacity exceeded.");
}

} // namespace effindom::v2::native
