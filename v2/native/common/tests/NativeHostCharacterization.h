#pragma once

#include "NativeHostContract.h"
#include "effindom_ui.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

extern "C" std::uint64_t __fui_native_action_handle();
extern "C" std::uint64_t __fui_native_application_root_handle();

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

    float root_x = 0.0f;
    float root_y = 0.0f;
    float root_width = 0.0f;
    float root_height = 0.0f;
    REQUIRE(ui_get_bounds(
        __fui_native_application_root_handle(),
        &root_x,
        &root_y,
        &root_width,
        &root_height));
    CHECK(root_width == resized.logical_width);
    CHECK(root_height == resized.logical_height);
    CHECK(host.IsIdle());
}

template <typename Host>
void CharacterizePointerActivation() {
    Host host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto baseline_activations = host.State().activation_count;

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    REQUIRE(ui_get_bounds(__fui_native_action_handle(), &x, &y, &width, &height));
    REQUIRE(width > 0.0f);
    REQUIRE(height > 0.0f);

    const float pointer_x = x + width * 0.5f;
    const float pointer_y = y + height * 0.5f;
    host.DispatchPointer(pointer_x, pointer_y, true, 0, 1U, 1);
    host.DispatchPointer(pointer_x, pointer_y, false, 0, 0U, 1);
    host.DrainFrames();

    CHECK(host.State().activation_count == baseline_activations + 1U);
    CHECK(host.IsIdle());
}

template <typename Host>
void CharacterizeNativeHost() {
    static_assert(IsNativeHostV<Host>, "Host must satisfy the shared native host facade contract");
    // Exercise routed input before lifecycle churn so synthetic platform
    // implementations do not make assumptions about process-global desktop
    // services surviving across separately constructed hosts.
    CharacterizePointerActivation<Host>();
    CharacterizeLifecycleAndFrameDemand<Host>();
    CharacterizeViewportReconciliation<Host>();
}

} // namespace effindom::v2::native::tests
