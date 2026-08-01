#include "NativeFuiRsWorkerAdapter.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace effindom::v2::native {
namespace {

class RecordingReporter final : public NativeWorkerReporter {
public:
    bool IsCancelled() const override { return cancelled_.load(); }
    bool WaitForCancellation(std::chrono::milliseconds timeout) override {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return cancelled_.load(); });
    }
    void Progress(std::string text) override { events.push_back("progress:" + text); }
    void Complete(std::string text) override { events.push_back("complete:" + text); }
    void Error(std::string text) override { events.push_back("error:" + text); }
    void Cancel() {
        cancelled_ = true;
        changed_.notify_all();
    }

    std::vector<std::string> events;

private:
    std::atomic<bool> cancelled_ = false;
    std::mutex mutex_;
    std::condition_variable changed_;
};

thread_local std::uint32_t invocation_count = 0U;
thread_local std::string received_input;
thread_local bool clock_allowed = false;
thread_local bool ui_service_allowed = true;
std::mutex non_cooperative_mutex;
std::condition_variable non_cooperative_changed;
bool release_non_cooperative = false;
bool service_allowed_after_cancel = true;

void ProgressThenComplete(std::uintptr_t input, std::uint32_t length) {
    ++invocation_count;
    if (invocation_count == 1U) {
        received_input.assign(reinterpret_cast<const char*>(input), length);
        constexpr char progress[] = "half";
        fui_native_worker_report_progress(
            reinterpret_cast<const std::uint8_t*>(progress), sizeof(progress) - 1U);
        fui_native_worker_request_yield(0);
        return;
    }
    constexpr char complete[] = "done";
    fui_native_worker_complete_string(
        reinterpret_cast<const std::uint8_t*>(complete), sizeof(complete) - 1U);
}

void DelayedYield(std::uintptr_t, std::uint32_t) {
    fui_native_worker_request_yield(5000);
}

void NoTerminalOrYield(std::uintptr_t, std::uint32_t) {}

constexpr std::uint8_t kArtifact[] = "./workers.wasm";
constexpr std::uint8_t kEntry[] = "entry";
constexpr std::uint8_t kClockService[] = "demoWorkerClockWallClockSinceEpochMs";
const NativeFuiRsWorkerHostServiceEntry kHostServices[]{{kClockService}};

void CheckAllowlist(std::uintptr_t, std::uint32_t) {
    constexpr std::uint8_t ui_service[] = "uiSetText";
    clock_allowed = fui_native_worker_host_service_is_allowed(
        kClockService, sizeof(kClockService) - 1U);
    ui_service_allowed = fui_native_worker_host_service_is_allowed(
        ui_service, sizeof(ui_service) - 1U);
    constexpr char complete[] = "checked";
    fui_native_worker_complete_string(
        reinterpret_cast<const std::uint8_t*>(complete), sizeof(complete) - 1U);
}

void BlockThenCheckAllowlist(std::uintptr_t, std::uint32_t) {
    {
        std::unique_lock lock(non_cooperative_mutex);
        non_cooperative_changed.wait(lock, [] { return release_non_cooperative; });
    }
    service_allowed_after_cancel = fui_native_worker_host_service_is_allowed(
        kClockService, sizeof(kClockService) - 1U);
}

NativeWorkerStartRequest Request() {
    return {1U, 1U, 1U, "./workers.wasm", "entry", "hello \xF0\x9F\x8C\x8D"};
}

} // namespace

TEST_CASE("native FUI-RS adapter preserves input progress yield and completion", "[v2][native][worker]") {
    invocation_count = 0U;
    received_input.clear();
    const NativeFuiRsWorkerRegistryEntry entries[]{
        {kArtifact, kEntry, kHostServices, 1U, ProgressThenComplete},
    };
    NativeFuiRsWorkerAdapter adapter(entries, 1U);
    RecordingReporter reporter;

    adapter.Invoke(Request(), reporter);

    CHECK(received_input == "hello \xF0\x9F\x8C\x8D");
    CHECK(invocation_count == 2U);
    CHECK(reporter.events == std::vector<std::string>{"progress:half", "complete:done"});
}

TEST_CASE("native FUI-RS adapter cancellation interrupts delayed yield", "[v2][native][worker]") {
    const NativeFuiRsWorkerRegistryEntry entries[]{
        {kArtifact, kEntry, nullptr, 0U, DelayedYield},
    };
    NativeFuiRsWorkerAdapter adapter(entries, 1U);
    RecordingReporter reporter;
    std::atomic<bool> returned = false;
    const auto started = std::chrono::steady_clock::now();
    std::thread worker([&] {
        adapter.Invoke(Request(), reporter);
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    reporter.Cancel();
    worker.join();

    CHECK(returned.load());
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
    CHECK(reporter.events.empty());
}

TEST_CASE("native FUI-RS adapter rejects missing and non-yielding entries", "[v2][native][worker]") {
    const NativeFuiRsWorkerRegistryEntry entries[]{
        {kArtifact, kEntry, nullptr, 0U, NoTerminalOrYield},
    };
    NativeFuiRsWorkerAdapter adapter(entries, 1U);
    RecordingReporter no_yield;
    adapter.Invoke(Request(), no_yield);
    REQUIRE(no_yield.events.size() == 1U);
    CHECK(no_yield.events.front().find("error:Worker exited without calling") == 0U);

    RecordingReporter missing;
    auto request = Request();
    request.entry = "missing";
    adapter.Invoke(request, missing);
    CHECK(missing.events == std::vector<std::string>{
        "error:Native Worker bundle or entry is not registered.",
    });
}

TEST_CASE("native FUI-RS adapter enforces the declared host-service allowlist", "[v2][native][worker]") {
    const NativeFuiRsWorkerRegistryEntry entries[]{
        {kArtifact, kEntry, kHostServices, 1U, CheckAllowlist},
    };
    NativeFuiRsWorkerAdapter adapter(entries, 1U);
    RecordingReporter reporter;
    clock_allowed = false;
    ui_service_allowed = true;
    adapter.Invoke(Request(), reporter);
    CHECK(clock_allowed);
    CHECK_FALSE(ui_service_allowed);
    CHECK(reporter.events == std::vector<std::string>{"complete:checked"});
}

TEST_CASE("native FUI-RS adapter revokes host services from non-cooperative canceled work", "[v2][native][worker]") {
    const NativeFuiRsWorkerRegistryEntry entries[]{
        {kArtifact, kEntry, kHostServices, 1U, BlockThenCheckAllowlist},
    };
    NativeFuiRsWorkerAdapter adapter(entries, 1U);
    RecordingReporter reporter;
    release_non_cooperative = false;
    service_allowed_after_cancel = true;
    std::thread worker([&] { adapter.Invoke(Request(), reporter); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    reporter.Cancel();
    {
        std::lock_guard lock(non_cooperative_mutex);
        release_non_cooperative = true;
    }
    non_cooperative_changed.notify_all();
    worker.join();
    CHECK_FALSE(service_allowed_after_cancel);
    CHECK(reporter.events.empty());
}

} // namespace effindom::v2::native
