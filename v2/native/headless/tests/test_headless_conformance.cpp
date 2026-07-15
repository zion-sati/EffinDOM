#include "HeadlessConformanceHost.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#ifndef EFFINDOM_SOURCE_DIR
#define EFFINDOM_SOURCE_DIR "."
#endif

namespace {

std::filesystem::path GoldenDirectory() {
    return std::filesystem::path(EFFINDOM_SOURCE_DIR) / "v2/native/headless/goldens";
}

} // namespace

TEST_CASE("headless native host renders retained state and becomes idle", "[v2][native][headless]") {
    effindom::v2::headless::HeadlessConformanceHost host{};
    host.Mount();
    REQUIRE(host.IsMounted());
    REQUIRE(host.RunNextFrame());
    CHECK(host.FrameCount() == 1U);
    CHECK(host.IsIdle());
    CHECK_FALSE(host.RunNextFrame());
    CHECK_FALSE(host.SnapshotRgba().empty());
}

TEST_CASE("headless native host routes deterministic retained input", "[v2][native][headless]") {
    effindom::v2::headless::HeadlessConformanceHost host{};
    host.Mount();
    host.RunInputScenario();

    const effindom::v2::headless::ConformanceState state = host.State();
    CHECK(state.editor_text == "Native headless");
    CHECK(state.selection_start == 15U);
    CHECK(state.selection_end == 15U);
    CHECK(state.scroll_offset_y > 0.0f);
    CHECK(state.logical_width == 320.0f);
    CHECK(state.logical_height == 240.0f);
    CHECK(state.pointer_route_observed);
    CHECK(state.keyboard_route_observed);
    CHECK(host.IsIdle());

    const std::string trace = host.HostCallTrace();
    CHECK(trace.find("clipboard-read") != std::string::npos);
    CHECK(trace.find("clipboard-write") != std::string::npos);
    CHECK(trace.find("semantic-announcement") != std::string::npos);
}

TEST_CASE("headless artifacts are deterministic and disposal is quiescent", "[v2][native][headless]") {
    effindom::v2::headless::HeadlessConformanceHost first{};
    first.Mount();
    first.RunInputScenario();
    effindom::v2::headless::HeadlessConformanceHost second{};
    second.Mount();
    second.RunInputScenario();

    CHECK(first.SnapshotRgba() == second.SnapshotRgba());
    CHECK(first.SemanticSnapshot() == second.SemanticSnapshot());
    CHECK(first.HostCallTrace() == second.HostCallTrace());

    std::string error{};
    const bool artifacts_match = first.VerifyArtifacts(GoldenDirectory(), error);
    INFO(error);
    REQUIRE(artifacts_match);

    const std::size_t host_calls = first.HostCallCount();
    const std::uint64_t frame_count = first.FrameCount();
    first.Dispose();
    first.AdvanceTime(5000.0);
    first.DrainFrames();
    CHECK(first.HostCallCount() == host_calls);
    CHECK(first.FrameCount() == frame_count);
    CHECK(first.IsIdle());
}
