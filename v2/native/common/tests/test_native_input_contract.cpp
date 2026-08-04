#include "NativeInputContractFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;
using namespace effindom::v2::native;
using namespace effindom::v2::native::test;

TEST_CASE("native wheel contract preserves browser-equivalent event data",
    "[v2][native][contract][input][wheel]") {
    const auto trace = WheelTrace();
    REQUIRE(trace.size() == 3U);
    CHECK(trace.front().handle == 42U);
    CHECK(trace.front().x == Approx(120.0f));
    CHECK(trace.front().y == Approx(80.0f));
    CHECK(trace.front().delta_y == Approx(16.0f));
    CHECK(trace.front().delta_mode == NativeWheelDeltaMode::Pixel);
    CHECK(trace.front().modifiers == UI_KEY_MOD_SHIFT);
    CHECK_FALSE(trace.front().precise);
    CHECK(trace[1].precise);
    CHECK(trace[1].begins_gesture);
    CHECK(trace.back().ends_gesture);
}

TEST_CASE("native animation frame contract uses monotonic millisecond timestamps",
    "[v2][native][contract][frame]") {
    const auto frames = AnimationFrameTrace();
    REQUIRE(frames.size() >= 2U);
    CHECK(std::adjacent_find(frames.begin(), frames.end(), [](const auto& left, const auto& right) {
        return left.timestamp_ms >= right.timestamp_ms;
    }) == frames.end());
}

TEST_CASE("native composition contract represents update commit and cancel lifecycles",
    "[v2][native][contract][input][ime]") {
    const auto commit = CompositionCommitTrace();
    REQUIRE(commit.size() == 4U);
    CHECK(commit.front().phase == NativeTextCompositionPhase::Start);
    CHECK(commit[1].phase == NativeTextCompositionPhase::Update);
    CHECK(commit[2].text == "\xE4\xBD\xA0");
    CHECK(commit.back().phase == NativeTextCompositionPhase::Commit);
    CHECK(commit.back().replacement_start == 4U);
    CHECK(commit.back().replacement_end == 7U);
    CHECK(commit.back().selection_start == kNativeTextRangeUnspecified);

    const auto cancel = CompositionCancelTrace();
    REQUIRE(cancel.size() == 3U);
    CHECK(cancel.front().phase == NativeTextCompositionPhase::Start);
    CHECK(cancel[1].phase == NativeTextCompositionPhase::Update);
    CHECK(cancel.back().phase == NativeTextCompositionPhase::Cancel);
    CHECK(cancel.back().text.empty());
}

TEST_CASE("native pointer contract preserves stable multi-pointer identity",
    "[v2][native][contract][input][pointer]") {
    const auto trace = MultiPointerTrace();
    REQUIRE(trace.size() == 4U);
    CHECK(trace[0].pointer_id == trace[2].pointer_id);
    CHECK(trace[1].pointer_id == trace[3].pointer_id);
    CHECK(trace[0].pointer_id != trace[1].pointer_id);
    CHECK(trace[0].primary);
    CHECK_FALSE(trace[1].primary);
    CHECK(trace[0].pointer_type == UI_POINTER_TYPE_TOUCH);
    CHECK(trace[2].pressure == Approx(0.55f));
}

TEST_CASE("native pen contract carries pressure tilt twist and contact bounds",
    "[v2][native][contract][input][pen]") {
    const auto pen = PenContactFixture();
    CHECK(pen.pointer_type == UI_POINTER_TYPE_PEN);
    CHECK(pen.pressure == Approx(0.75f));
    CHECK(pen.tangential_pressure == Approx(-0.2f));
    CHECK(pen.width == Approx(3.0f));
    CHECK(pen.height == Approx(4.0f));
    CHECK(pen.tilt_x == Approx(18.0f));
    CHECK(pen.tilt_y == Approx(-12.0f));
    CHECK(pen.twist == Approx(90.0f));
    CHECK(pen.modifiers == UI_KEY_MOD_SHIFT);
}
