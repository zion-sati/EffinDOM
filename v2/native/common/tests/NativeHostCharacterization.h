#pragma once

#include "NativeHostContract.h"
#include "effindom_ui.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

namespace effindom::v2::native::tests {

template <typename Host>
void CharacterizeLifecycleAndFrameDemand() {
    Host host(false);
    const auto baseline = host.State();

    host.MountApplication();
    host.DrainFrames();
    const auto mounted = host.State();
    CHECK(mounted.mount_count == baseline.mount_count + 1U);
    CHECK(mounted.dispose_count == baseline.dispose_count);
    CHECK(mounted.frame_count > baseline.frame_count);
    CHECK(host.IsIdle());
    CHECK_FALSE(host.RunNextFrame());

    const auto pixels = host.SnapshotRgba();
    REQUIRE_FALSE(pixels.empty());
    CHECK(std::any_of(
        pixels.begin(),
        pixels.end(),
        [](std::uint8_t value) { return value != 0U && value != 255U; }));

    host.MountApplication();
    host.DrainFrames();
    const auto remounted = host.State();
    CHECK(remounted.mount_count == baseline.mount_count + 2U);
    CHECK(remounted.dispose_count == baseline.dispose_count + 1U);
    CHECK(host.IsIdle());

    host.Unmount();
    const auto unmounted = host.State();
    CHECK(unmounted.dispose_count == baseline.dispose_count + 2U);
    CHECK(host.IsIdle());
}

template <typename Host>
void CharacterizeViewportReconciliation() {
    Host host(false);
    host.MountApplication();
    host.DrainFrames();

    host.Resize(640U, 420U);
    host.DrainFrames();
    const auto resized = host.State();
    CHECK(resized.logical_width == 640.0f);
    CHECK(resized.logical_height == 420.0f);
    CHECK(resized.pixel_density > 0.0f);

    CHECK(host.IsIdle());
}

template <typename Host>
void CharacterizePointerActivation() {
    Host host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto baseline = host.State();

    host.DispatchPointer(24.0f, 24.0f, true, 0, 1U, 1);
    host.DispatchPointer(24.0f, 24.0f, false, 0, 0U, 1);
    host.DrainFrames();

    CHECK(host.State().frame_count > baseline.frame_count);
    CHECK(host.IsIdle());
}

template <typename Host>
void CharacterizeNativeHost() {
    static_assert(IsNativeHostV<Host>, "Host must satisfy the shared native host facade contract");
    CharacterizeLifecycleAndFrameDemand<Host>();
    CharacterizeViewportReconciliation<Host>();
    CharacterizePointerActivation<Host>();
}

} // namespace effindom::v2::native::tests
