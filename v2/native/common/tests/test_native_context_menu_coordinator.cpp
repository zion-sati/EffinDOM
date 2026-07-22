#include "NativeContextMenuCoordinator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

using effindom::v2::native::NativeContextMenuCoordinator;
using effindom::v2::native::NativeContextMenuGateway;
using effindom::v2::native::NativeSecondaryPointerEvent;
using effindom::v2::native::NativePointerPhase;

class FakeGateway final : public NativeContextMenuGateway {
public:
    bool DispatchRawSecondaryPointer(const NativeSecondaryPointerEvent&) override {
        calls.emplace_back("dispatch");
        return dispatch_handled;
    }

    void HideActiveContextMenu() override { calls.emplace_back("hide"); }
    void FlushRetainedChanges() override { calls.emplace_back("flush"); }

    std::uint64_t HitTest(float, float) const override {
        calls.emplace_back("hit-test");
        return hit_handle;
    }

    bool CanShowContextMenu(std::uint64_t) const override {
        calls.emplace_back("can-show");
        return can_show;
    }

    void ShowContextMenu(std::uint64_t, float, float) override {
        calls.emplace_back("show");
        ++show_count;
    }

    void RequestFrame() override {
        calls.emplace_back("frame");
        ++frame_count;
    }

    bool dispatch_handled = false;
    bool can_show = true;
    std::uint64_t hit_handle = 42U;
    mutable std::vector<std::string> calls;
    std::uint32_t show_count = 0U;
    std::uint32_t frame_count = 0U;
};

NativeSecondaryPointerEvent Secondary(NativePointerPhase phase) {
    NativeSecondaryPointerEvent event{};
    event.phase = phase;
    event.x = 12.0f;
    event.y = 34.0f;
    return event;
}

} // namespace

TEST_CASE("native context menu dispatches raw input before fallback", "[v2][native][context-menu]") {
    FakeGateway gateway;
    NativeContextMenuCoordinator coordinator(gateway);

    coordinator.Dispatch(Secondary(NativePointerPhase::Down));
    const auto result = coordinator.Dispatch(Secondary(NativePointerPhase::Up));

    CHECK_FALSE(result.raw_event_handled);
    CHECK(result.fallback_shown);
    CHECK(gateway.calls == std::vector<std::string>{
        "dispatch", "dispatch", "hide", "flush", "hit-test", "can-show", "show", "frame"});
    CHECK(gateway.show_count == 1U);
    CHECK(gateway.frame_count == 1U);
}

TEST_CASE("handled secondary down suppresses native fallback", "[v2][native][context-menu]") {
    FakeGateway gateway;
    NativeContextMenuCoordinator coordinator(gateway);

    gateway.dispatch_handled = true;
    coordinator.Dispatch(Secondary(NativePointerPhase::Down));
    gateway.dispatch_handled = false;
    const auto result = coordinator.Dispatch(Secondary(NativePointerPhase::Up));

    CHECK_FALSE(result.fallback_shown);
    CHECK(gateway.calls == std::vector<std::string>{"dispatch", "dispatch"});
}

TEST_CASE("handled secondary up suppresses native fallback", "[v2][native][context-menu]") {
    FakeGateway gateway;
    NativeContextMenuCoordinator coordinator(gateway);

    coordinator.Dispatch(Secondary(NativePointerPhase::Down));
    gateway.dispatch_handled = true;
    const auto result = coordinator.Dispatch(Secondary(NativePointerPhase::Up));

    CHECK(result.raw_event_handled);
    CHECK_FALSE(result.fallback_shown);
    CHECK(gateway.calls == std::vector<std::string>{"dispatch", "dispatch"});
}

TEST_CASE("cancelled secondary gesture cannot open fallback", "[v2][native][context-menu]") {
    FakeGateway gateway;
    NativeContextMenuCoordinator coordinator(gateway);

    coordinator.Dispatch(Secondary(NativePointerPhase::Down));
    coordinator.Dispatch(Secondary(NativePointerPhase::Cancel));

    CHECK(gateway.calls == std::vector<std::string>{"dispatch", "dispatch"});
}

TEST_CASE("fresh menu request hides and re-hit-tests before showing", "[v2][native][context-menu]") {
    FakeGateway gateway;
    NativeContextMenuCoordinator coordinator(gateway);

    CHECK(coordinator.ShowAt(20.0f, 30.0f));
    CHECK(gateway.calls == std::vector<std::string>{
        "hide", "flush", "hit-test", "can-show", "show", "frame"});
}

TEST_CASE("blank or disabled target only dismisses the old popup", "[v2][native][context-menu]") {
    FakeGateway gateway;
    NativeContextMenuCoordinator coordinator(gateway);

    gateway.hit_handle = 0U;
    CHECK_FALSE(coordinator.ShowAt(20.0f, 30.0f));
    CHECK(gateway.calls == std::vector<std::string>{"hide", "flush", "hit-test", "frame"});
    CHECK(gateway.show_count == 0U);
    CHECK(gateway.frame_count == 1U);

    gateway.calls.clear();
    gateway.hit_handle = 42U;
    gateway.can_show = false;
    CHECK_FALSE(coordinator.ShowAt(20.0f, 30.0f));
    CHECK(gateway.calls == std::vector<std::string>{
        "hide", "flush", "hit-test", "can-show", "frame"});
    CHECK(gateway.show_count == 0U);
    CHECK(gateway.frame_count == 2U);
}
