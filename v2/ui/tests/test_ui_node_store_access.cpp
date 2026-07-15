#include "TestUiSupport.h"

#include "CommandBuilder.h"
#include "UiNodeStore.h"
#include "UiNodeStoreAccess.h"

#include <memory>

TEST_CASE("v2 ui node store access resolves generation handles and exposes the root", "[v2][ui][node-store]") {
    static std::array<effindom::v2::ui::UINode, effindom::v2::ui::kMaxNodes> nodes{};
    nodes[1] = effindom::v2::ui::UINode{};
    nodes[2] = effindom::v2::ui::UINode{};
    nodes[1].is_active = true;
    nodes[1].generation = 7U;
    nodes[2].is_active = true;
    nodes[2].generation = 3U;
    const std::uint64_t root = effindom::v2::ui::PackHandle(1U, 7U);

    const effindom::v2::ui::NodeReader reader(nodes, root);
    CHECK(reader.RootHandle() == root);
    CHECK(reader.Resolve(root) == &nodes[1]);
    CHECK(reader.Resolve(effindom::v2::ui::PackHandle(1U, 8U)) == nullptr);
    CHECK(reader.Resolve(effindom::v2::ui::PackHandle(0U, 7U)) == nullptr);
}

TEST_CASE("v2 ui node store writer and traversal expose active nodes without raw pool access", "[v2][ui][node-store]") {
    static std::array<effindom::v2::ui::UINode, effindom::v2::ui::kMaxNodes> nodes{};
    nodes[1] = effindom::v2::ui::UINode{};
    nodes[2] = effindom::v2::ui::UINode{};
    nodes[1].is_active = true;
    nodes[1].generation = 2U;
    nodes[2].is_active = true;
    nodes[2].generation = 4U;
    nodes[2].is_scroll_view = true;
    std::uint64_t root = effindom::v2::ui::PackHandle(1U, 2U);

    effindom::v2::ui::NodeWriter writer(nodes, root);
    const std::uint64_t scroll = effindom::v2::ui::PackHandle(2U, 4U);
    REQUIRE(writer.Resolve(scroll) == &nodes[2]);
    writer.SetRootHandle(scroll);
    CHECK(writer.RootHandle() == scroll);

    effindom::v2::ui::NodeTraversalAccess traversal(nodes);
    std::vector<std::uint64_t> active{};
    std::vector<std::uint64_t> scroll_views{};
    traversal.ForEachActive([&](std::uint64_t handle, effindom::v2::ui::UINode&) {
        active.push_back(handle);
    });
    traversal.ForEachActiveScrollView([&](std::uint64_t handle, effindom::v2::ui::UINode&) {
        scroll_views.push_back(handle);
    });

    CHECK(active == std::vector<std::uint64_t>{effindom::v2::ui::PackHandle(1U, 2U), scroll});
    CHECK(scroll_views == std::vector<std::uint64_t>{scroll});
    CHECK(traversal.AnyActive([&](std::uint64_t handle, const effindom::v2::ui::UINode&) {
        return handle == scroll;
    }));
    CHECK(traversal.AnyActiveScrollView([&](std::uint64_t handle, const effindom::v2::ui::UINode&) {
        return handle == scroll;
    }));
}

TEST_CASE("v2 ui node store rejects stale generations and reuses released slots", "[v2][ui][node-store]") {
    auto store = std::make_unique<effindom::v2::ui::NodeStore>();
    const std::uint64_t first = store->Create(UI_NODE_FLEX_BOX, nullptr);
    REQUIRE(first != UI_INVALID_HANDLE);
    REQUIRE(store->Resolve(first) != nullptr);
    REQUIRE(store->Destroy(first));
    CHECK(store->Resolve(first) == nullptr);

    const std::uint64_t reused = store->Create(UI_NODE_FLEX_BOX, nullptr);
    REQUIRE(reused != UI_INVALID_HANDLE);
    CHECK(test_ui_support::HandleIndex(reused) == test_ui_support::HandleIndex(first));
    CHECK(test_ui_support::HandleGeneration(reused) == test_ui_support::HandleGeneration(first) + 1U);
    CHECK(store->Resolve(first) == nullptr);
    CHECK(store->Resolve(reused) != nullptr);
}

TEST_CASE("v2 ui node store owns parent-child attachment and detachment", "[v2][ui][node-store]") {
    auto store = std::make_unique<effindom::v2::ui::NodeStore>();
    const std::uint64_t first_parent = store->Create(UI_NODE_FLEX_BOX, nullptr);
    const std::uint64_t second_parent = store->Create(UI_NODE_FLEX_BOX, nullptr);
    const std::uint64_t child = store->Create(UI_NODE_FLEX_BOX, nullptr);
    REQUIRE(store->AddChild(first_parent, child));
    CHECK(store->Resolve(child)->parent_handle == first_parent);
    CHECK(store->Resolve(first_parent)->children == std::vector<std::uint64_t>{child});

    REQUIRE(store->AddChild(second_parent, child));
    CHECK(store->Resolve(first_parent)->children.empty());
    CHECK(store->Resolve(second_parent)->children == std::vector<std::uint64_t>{child});
    CHECK(store->Resolve(child)->parent_handle == second_parent);

    REQUIRE(store->Destroy(second_parent));
    CHECK(store->Resolve(child)->parent_handle == UI_INVALID_HANDLE);
}

TEST_CASE("v2 ui node store emits deletions before creations and cancels uncommitted nodes", "[v2][ui][node-store]") {
    auto store = std::make_unique<effindom::v2::ui::NodeStore>();
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    const std::uint64_t deleted = store->Create(UI_NODE_FLEX_BOX, nullptr);
    store->EmitLifecycleCommands(builder);
    words.clear();

    REQUIRE(store->Destroy(deleted));
    const std::uint64_t created = store->Create(UI_NODE_FLEX_BOX, nullptr);
    const std::uint64_t cancelled = store->Create(UI_NODE_FLEX_BOX, nullptr);
    REQUIRE(store->Destroy(cancelled));
    store->EmitLifecycleCommands(builder);

    REQUIRE(words.size() == 6U);
    CHECK(words[0] == CMD_DELETE_NODE);
    CHECK(words[1] == static_cast<std::uint32_t>(deleted));
    CHECK(words[2] == static_cast<std::uint32_t>(deleted >> 32U));
    CHECK(words[3] == CMD_CREATE_NODE);
    CHECK(words[4] == static_cast<std::uint32_t>(created));
    CHECK(words[5] == static_cast<std::uint32_t>(created >> 32U));
    CHECK_FALSE(store->HasPendingLifecycleCommands());
}

TEST_CASE("v2 ui node store replaces and resets its root", "[v2][ui][node-store]") {
    auto store = std::make_unique<effindom::v2::ui::NodeStore>();
    const std::uint64_t first = store->Create(UI_NODE_FLEX_BOX, nullptr);
    const std::uint64_t second = store->Create(UI_NODE_FLEX_BOX, nullptr);
    REQUIRE(store->SetRoot(first));
    CHECK(store->RootHandle() == first);
    REQUIRE(store->SetRoot(second));
    CHECK(store->RootHandle() == second);
    CHECK_FALSE(store->SetRoot(effindom::v2::ui::PackHandle(3U, 99U)));
    CHECK(store->RootHandle() == second);

    store->Reset();
    CHECK(store->RootHandle() == UI_INVALID_HANDLE);
    CHECK(store->Resolve(first) == nullptr);
    CHECK(store->Resolve(second) == nullptr);
    CHECK_FALSE(store->HasPendingLifecycleCommands());
}
