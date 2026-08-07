#include "NativeWorkerHost.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void fui_worker_start_string(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t);
void fui_worker_cancel(std::uint32_t);
}

namespace effindom::v2::native {
namespace {

class UiQueue final {
public:
    bool Post(NativeWorkerCoordinator::UiTask task) {
        {
            std::lock_guard lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        changed_.notify_all();
        return true;
    }

    bool Wait() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), [&] {
            return !tasks_.empty();
        });
    }

    std::vector<bool> RunAll() {
        std::deque<NativeWorkerCoordinator::UiTask> tasks;
        {
            std::lock_guard lock(mutex_);
            tasks.swap(tasks_);
        }
        std::vector<bool> requested_frames;
        for (auto& task : tasks) requested_frames.push_back(task());
        return requested_frames;
    }

    std::vector<bool> RunUntil(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::vector<bool> requested_frames;
        while (!done()) {
            {
                std::unique_lock lock(mutex_);
                if (!changed_.wait_until(lock, deadline, [&] { return !tasks_.empty(); })) {
                    break;
                }
            }
            auto batch = RunAll();
            requested_frames.insert(requested_frames.end(), batch.begin(), batch.end());
        }
        return requested_frames;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<NativeWorkerCoordinator::UiTask> tasks_;
};

std::string received_input;

void CompleteEntry(std::uintptr_t input, std::uint32_t length) {
    received_input.assign(reinterpret_cast<const char*>(input), length);
    constexpr char progress[] = "working";
    constexpr char complete[] = "finished";
    fui_native_worker_report_progress(
        reinterpret_cast<const std::uint8_t*>(progress), sizeof(progress) - 1U);
    fui_native_worker_complete_string(
        reinterpret_cast<const std::uint8_t*>(complete), sizeof(complete) - 1U);
}

void DelayedEntry(std::uintptr_t, std::uint32_t) {
    fui_native_worker_request_yield(5000);
}

constexpr std::uint8_t kArtifact[] = "./workers.wasm";
constexpr std::uint8_t kEntry[] = "demo";

struct CallbackLog final {
    std::thread::id ui_thread;
    std::vector<std::string> events;

    void Add(const char* kind, std::uint32_t id, const std::string& text) {
        CHECK(std::this_thread::get_id() == ui_thread);
        events.push_back(std::string(kind) + ":" + std::to_string(id) + ":" + text);
    }
};

NativeWorkerHost MakeHost(
    const NativeFuiRsWorkerRegistryEntry* entries,
    std::size_t count,
    UiQueue& queue,
    CallbackLog& log) {
    return NativeWorkerHost(
        entries,
        count,
        [&](auto task) { return queue.Post(std::move(task)); },
        NativeWorkerHostCallbacks{
            [&](std::uint32_t id, const std::string& text) { log.Add("progress", id, text); },
            [&](std::uint32_t id, const std::string& text) { log.Add("complete", id, text); },
            [&](std::uint32_t id, const std::string& text) { log.Add("error", id, text); },
        });
}

void Start(std::uint32_t id, const std::string& artifact, const std::string& entry, const std::string& input) {
    fui_worker_start_string(
        id,
        reinterpret_cast<std::uintptr_t>(artifact.data()),
        static_cast<std::uint32_t>(artifact.size()),
        reinterpret_cast<std::uintptr_t>(entry.data()),
        static_cast<std::uint32_t>(entry.size()),
        reinterpret_cast<std::uintptr_t>(input.data()),
        static_cast<std::uint32_t>(input.size()));
}

} // namespace

TEST_CASE("native Worker imports deliver registry callbacks on the UI thread", "[v2][native][worker]") {
    const NativeFuiRsWorkerRegistryEntry entries[]{{kArtifact, kEntry, nullptr, 0U, CompleteEntry}};
    UiQueue queue;
    CallbackLog log{std::this_thread::get_id(), {}};
    auto host = MakeHost(entries, 1U, queue, log);
    SetActiveNativeWorkerHost(&host);
    host.SetSessionGeneration(1U);

    Start(7U, "./workers.wasm", "demo", "hello \xF0\x9F\x8C\x8D");
    const auto requested_frames = queue.RunUntil([&] { return log.events.size() == 2U; });

    CHECK(received_input == "hello \xF0\x9F\x8C\x8D");
    CHECK(log.events == std::vector<std::string>{
        "progress:7:working", "complete:7:finished",
    });
    CHECK(requested_frames == std::vector<bool>{false});
    SetActiveNativeWorkerHost(nullptr);
}

TEST_CASE("native Worker imports asynchronously reject unknown entries", "[v2][native][worker]") {
    UiQueue queue;
    CallbackLog log{std::this_thread::get_id(), {}};
    auto host = MakeHost(nullptr, 0U, queue, log);
    SetActiveNativeWorkerHost(&host);
    host.SetSessionGeneration(2U);

    Start(8U, "./workers.wasm", "missing", "input");
    REQUIRE(queue.Wait());
    CHECK(log.events.empty());
    queue.RunAll();
    CHECK(log.events == std::vector<std::string>{
        "error:8:Native Worker bundle or entry is not registered.",
    });
    SetActiveNativeWorkerHost(nullptr);
}

TEST_CASE("native Worker cancellation and session changes interrupt delayed work", "[v2][native][worker]") {
    const NativeFuiRsWorkerRegistryEntry entries[]{{kArtifact, kEntry, nullptr, 0U, DelayedEntry}};
    UiQueue queue;
    CallbackLog log{std::this_thread::get_id(), {}};
    auto host = MakeHost(entries, 1U, queue, log);
    SetActiveNativeWorkerHost(&host);
    host.SetSessionGeneration(3U);

    const auto started = std::chrono::steady_clock::now();
    Start(9U, "./workers.wasm", "demo", "input");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    fui_worker_cancel(9U);
    host.SetSessionGeneration(4U);
    host.Clear();

    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
    CHECK(log.events.empty());
    SetActiveNativeWorkerHost(nullptr);
}

} // namespace effindom::v2::native
