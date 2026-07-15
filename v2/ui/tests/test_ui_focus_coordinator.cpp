#include "TestUiSupport.h"
#include "UiFocusCoordinator.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

using effindom::v2::ui::FocusCoordinator;
using effindom::v2::ui::NodeReader;
using effindom::v2::ui::PackHandle;
using effindom::v2::ui::UINode;
using effindom::v2::ui::UiEventSink;
using effindom::v2::ui::kMaxNodes;

struct FocusFixture {
    std::unique_ptr<std::array<UINode, kMaxNodes>> nodes =
        std::make_unique<std::array<UINode, kMaxNodes>>();
    std::uint64_t root_handle = UI_INVALID_HANDLE;
    UiEventSink events{};
    FocusCoordinator focus{NodeReader(*nodes, root_handle), events};

    std::uint64_t Activate(std::uint32_t index) {
        UINode& node = (*nodes)[index];
        node = UINode{};
        node.generation = 1U;
        node.is_active = true;
        node.visibility = UI_VISIBILITY_NORMAL;
        return PackHandle(index, node.generation);
    }

    UINode& Node(std::uint64_t handle) {
        return (*nodes)[effindom::v2::ui::DecodeHandle(handle).index];
    }

    void AddChild(std::uint64_t parent, std::uint64_t child) {
        Node(parent).children.push_back(child);
        Node(child).parent_handle = parent;
    }
};

} // namespace

TEST_CASE("focus coordinator preserves retained tab ordering and wraparound", "[v2][ui][focus]") {
    FocusFixture fixture{};
    const std::uint64_t root = fixture.Activate(1U);
    const std::uint64_t natural_first = fixture.Activate(2U);
    const std::uint64_t explicit_second = fixture.Activate(3U);
    const std::uint64_t explicit_first = fixture.Activate(4U);
    const std::uint64_t natural_second = fixture.Activate(5U);
    fixture.root_handle = root;
    fixture.AddChild(root, natural_first);
    fixture.AddChild(root, explicit_second);
    fixture.AddChild(root, explicit_first);
    fixture.AddChild(root, natural_second);

    fixture.Node(natural_first).is_focusable = true;
    fixture.Node(natural_first).tab_index = 0;
    fixture.Node(explicit_second).is_focusable = true;
    fixture.Node(explicit_second).tab_index = 2;
    fixture.Node(explicit_first).is_focusable = true;
    fixture.Node(explicit_first).tab_index = 1;
    fixture.Node(natural_second).is_focusable = true;
    fixture.Node(natural_second).tab_index = 0;

    CHECK(fixture.focus.GetNextFocusable(UI_INVALID_HANDLE, true, root, UI_INVALID_HANDLE) == explicit_first);
    CHECK(fixture.focus.GetNextFocusable(explicit_first, true, root, UI_INVALID_HANDLE) == explicit_second);
    CHECK(fixture.focus.GetNextFocusable(explicit_second, true, root, UI_INVALID_HANDLE) == natural_first);
    CHECK(fixture.focus.GetNextFocusable(natural_first, true, root, UI_INVALID_HANDLE) == natural_second);
    CHECK(fixture.focus.GetNextFocusable(natural_second, true, root, UI_INVALID_HANDLE) == explicit_first);
    CHECK(fixture.focus.GetNextFocusable(explicit_first, false, root, UI_INVALID_HANDLE) == natural_second);
}

TEST_CASE("focus coordinator honors visibility scope and invalidation", "[v2][ui][focus]") {
    FocusFixture fixture{};
    const std::uint64_t root = fixture.Activate(1U);
    const std::uint64_t outside = fixture.Activate(2U);
    const std::uint64_t scope = fixture.Activate(3U);
    const std::uint64_t inside = fixture.Activate(4U);
    const std::uint64_t hidden_parent = fixture.Activate(5U);
    const std::uint64_t hidden_child = fixture.Activate(6U);
    fixture.root_handle = root;
    fixture.AddChild(root, outside);
    fixture.AddChild(root, scope);
    fixture.AddChild(scope, inside);
    fixture.AddChild(scope, hidden_parent);
    fixture.AddChild(hidden_parent, hidden_child);

    for (const std::uint64_t handle : {outside, inside, hidden_child}) {
        fixture.Node(handle).is_focusable = true;
        fixture.Node(handle).tab_index = 0;
    }
    fixture.Node(hidden_parent).visibility = UI_VISIBILITY_HIDDEN;

    CHECK(fixture.focus.GetNextFocusable(UI_INVALID_HANDLE, true, root, scope) == inside);
    CHECK(fixture.focus.GetNextFocusable(inside, true, root, scope) == inside);

    fixture.Node(inside).tab_index = -1;
    fixture.focus.InvalidateOrder();
    CHECK(fixture.focus.GetNextFocusable(UI_INVALID_HANDLE, true, root, scope) == UI_INVALID_HANDLE);

    fixture.Node(inside).tab_index = 0;
    fixture.focus.InvalidateOrder();
    CHECK(fixture.focus.GetNextFocusable(UI_INVALID_HANDLE, true, root, UI_INVALID_HANDLE) == outside);
}

TEST_CASE("focus coordinator captures and resolves pending node-id focus", "[v2][ui][focus]") {
    FocusFixture fixture{};
    const std::uint64_t root = fixture.Activate(1U);
    const std::uint64_t subtree = fixture.Activate(2U);
    const std::uint64_t focused = fixture.Activate(3U);
    const std::uint64_t replacement = fixture.Activate(4U);
    fixture.root_handle = root;
    fixture.AddChild(root, subtree);
    fixture.AddChild(subtree, focused);
    fixture.Node(focused).node_id = "editor";
    fixture.Node(focused).is_focusable = true;
    fixture.Node(replacement).node_id = "editor";
    fixture.Node(replacement).is_focusable = true;

    fixture.focus.SetFocusedHandle(focused);
    fixture.focus.CapturePendingNodeId(subtree);
    CHECK(fixture.focus.PendingNodeId() == "editor");
    CHECK(fixture.focus.PendingRestoreCandidate({{"editor", replacement}}) == UI_INVALID_HANDLE);

    fixture.focus.SetFocusedHandle(UI_INVALID_HANDLE);
    CHECK(fixture.focus.PendingRestoreCandidate({{"editor", replacement}}) == replacement);
    fixture.Node(replacement).is_focusable = false;
    CHECK(fixture.focus.PendingRestoreCandidate({{"editor", replacement}}) == UI_INVALID_HANDLE);

    fixture.focus.Reset();
    CHECK(fixture.focus.FocusedHandle() == UI_INVALID_HANDLE);
    CHECK(fixture.focus.PendingNodeId().empty());
}

TEST_CASE("focus coordinator emits neutral focus notifications", "[v2][ui][focus]") {
    using namespace test_ui_support;

    UseRecordingInteractionCallbacks();
    FocusFixture fixture{};
    const std::uint64_t handle = fixture.Activate(1U);
    fixture.focus.NotifyChanged(handle, true);
    fixture.focus.NotifyChanged(handle, false);
    fixture.focus.NotifyChanged(UI_INVALID_HANDLE, true);

    REQUIRE(g_focus_events.size() == 2U);
    CHECK(g_focus_events[0].handle == handle);
    CHECK(g_focus_events[0].is_focused);
    CHECK(g_focus_events[1].handle == handle);
    CHECK_FALSE(g_focus_events[1].is_focused);
}
