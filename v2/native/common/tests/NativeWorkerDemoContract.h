#pragma once

#include "SDL3/SDL.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>

extern "C" std::uint64_t __fui_native_worker_panel_handle();
extern "C" std::uint32_t __fui_native_worker_status();
extern "C" float __fui_native_worker_progress();
extern "C" bool __fui_native_worker_detail_has_prime_and_clock();
extern "C" bool __fui_native_worker_detail_has_failure_and_clock();
extern "C" bool __fui_native_worker_threads_are_split();
extern "C" void __fui_native_worker_start_prime();
extern "C" void __fui_native_worker_start_fail();
extern "C" void __fui_native_worker_cancel();

namespace effindom::v2::native::tests {

template <typename Host>
bool PumpUntilWorkerStatus(
    Host& host,
    std::uint32_t expected,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        host.PumpEvent(false);
        host.RunNextFrame();
        if (__fui_native_worker_status() == expected) return true;
        SDL_Delay(2U);
    }
    return false;
}

template <typename Host>
void CharacterizeNativeWorkerDemo() {
    constexpr std::uint32_t kIdle = 0U;
    constexpr std::uint32_t kComplete = 3U;
    constexpr std::uint32_t kCancelled = 4U;
    constexpr std::uint32_t kError = 5U;

    Host host(false);
    host.MountApplication();
    host.DrainFrames();
    CHECK(__fui_native_worker_panel_handle() != 0U);
    CHECK(__fui_native_worker_status() == kIdle);

    __fui_native_worker_start_prime();
    REQUIRE(PumpUntilWorkerStatus(host, kComplete, std::chrono::seconds(4)));
    CHECK(__fui_native_worker_progress() == Catch::Approx(100.0f));
    CHECK(__fui_native_worker_detail_has_prime_and_clock());
    CHECK(__fui_native_worker_threads_are_split());

    __fui_native_worker_start_fail();
    REQUIRE(PumpUntilWorkerStatus(host, kError, std::chrono::seconds(1)));
    CHECK(__fui_native_worker_detail_has_failure_and_clock());

    __fui_native_worker_start_prime();
    const auto progress_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (__fui_native_worker_progress() <= 0.0f
        && std::chrono::steady_clock::now() < progress_deadline) {
        host.PumpEvent(false);
        host.RunNextFrame();
        SDL_Delay(2U);
    }
    REQUIRE(__fui_native_worker_progress() > 0.0f);
    __fui_native_worker_cancel();
    INFO("worker status after cancellation=" << __fui_native_worker_status()
         << " progress=" << __fui_native_worker_progress());
    REQUIRE(PumpUntilWorkerStatus(host, kCancelled, std::chrono::seconds(1)));
    SDL_Delay(300U);
    host.DrainFrames();
    CHECK(__fui_native_worker_status() == kCancelled);

    __fui_native_worker_start_prime();
    host.MountApplication();
    host.DrainFrames();
    CHECK(__fui_native_worker_status() == kIdle);
    SDL_Delay(300U);
    host.DrainFrames();
    CHECK(__fui_native_worker_status() == kIdle);
    CHECK(host.IsIdle());
}

} // namespace effindom::v2::native::tests
