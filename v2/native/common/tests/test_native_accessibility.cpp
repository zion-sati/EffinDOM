#include "NativeAccessibility.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace effindom::v2::native;

std::uint32_t FloatWord(float value) {
    std::uint32_t word = 0U;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

std::vector<std::uint32_t> SemanticBuffer(
    std::uint64_t handle, std::uint32_t role, std::uint32_t flags, std::string label) {
    std::vector<std::uint32_t> words{
        1U, role, static_cast<std::uint32_t>(handle), static_cast<std::uint32_t>(handle >> 32U),
        FloatWord(10.0f), FloatWord(20.0f), FloatWord(100.0f), FloatWord(30.0f),
        flags, 2U, 1U, FloatWord(5.0f), FloatWord(0.0f), FloatWord(10.0f),
        static_cast<std::uint32_t>(label.size()),
    };
    const std::size_t start = words.size();
    words.resize(start + (label.size() + 3U) / 4U, 0U);
    std::memcpy(words.data() + start, label.data(), label.size());
    return words;
}

struct AdapterState {
    std::uint32_t updates = 0U;
    std::uint32_t clears = 0U;
    std::vector<std::uint64_t> announcements;
    NativeAccessibilitySnapshot snapshot;
};

class RecordingAdapter final : public NativeAccessibilityAdapter {
public:
    explicit RecordingAdapter(std::shared_ptr<AdapterState> state) : state_(std::move(state)) {}
    void Update(const NativeAccessibilitySnapshot& snapshot) override {
        ++state_->updates;
        state_->snapshot = snapshot;
    }
    void Announce(const NativeAccessibilityNode& node) override {
        state_->announcements.push_back(node.handle);
    }
    void Clear() override { ++state_->clears; }

private:
    std::shared_ptr<AdapterState> state_;
};

} // namespace

TEST_CASE("native accessibility decodes semantic records and suppresses unchanged updates",
    "[v2][native][accessibility]") {
    auto state = std::make_shared<AdapterState>();
    NativeAccessibilityCoordinator coordinator([] {});
    coordinator.Attach(std::make_unique<RecordingAdapter>(state));
    const auto words = SemanticBuffer(0x100000002ULL, 11U, (1U << 4U) | (1U << 6U), "Ready");

    REQUIRE(coordinator.Sync(words.data(), static_cast<std::uint32_t>(words.size()), 0x100000002ULL));
    REQUIRE(state->snapshot.nodes.size() == 1U);
    const auto& node = state->snapshot.nodes.front();
    CHECK(node.handle == 0x100000002ULL);
    CHECK(node.role == NativeAccessibilityRole::CheckBox);
    CHECK(node.label == "Ready");
    CHECK(node.bounds.x == 10.0f);
    CHECK(node.has_disabled);
    CHECK_FALSE(node.disabled);
    CHECK(node.has_value_range);
    CHECK(node.checked == NativeAccessibilityCheckedState::True);
    CHECK(state->snapshot.focused_handle == node.handle);
    CHECK(state->updates == 2U); // Initial empty attachment and first semantic commit.

    CHECK(coordinator.Sync(words.data(), static_cast<std::uint32_t>(words.size()), node.handle));
    CHECK(state->updates == 2U);
}

TEST_CASE("native accessibility rejects malformed buffers atomically",
    "[v2][native][accessibility]") {
    auto state = std::make_shared<AdapterState>();
    NativeAccessibilityCoordinator coordinator([] {});
    coordinator.Attach(std::make_unique<RecordingAdapter>(state));
    auto words = SemanticBuffer(42U, 1U, 0U, "Action");
    REQUIRE(coordinator.Sync(words.data(), static_cast<std::uint32_t>(words.size()), 0U));
    const auto update_count = state->updates;

    words.pop_back();
    CHECK_FALSE(coordinator.Sync(words.data(), static_cast<std::uint32_t>(words.size()), 0U));
    CHECK(state->updates == update_count);
    REQUIRE(state->snapshot.nodes.size() == 1U);
    CHECK(state->snapshot.nodes.front().label == "Action");
}

TEST_CASE("native accessibility announcements resolve stable semantic handles",
    "[v2][native][accessibility]") {
    auto state = std::make_shared<AdapterState>();
    NativeAccessibilityCoordinator coordinator([] {});
    coordinator.Attach(std::make_unique<RecordingAdapter>(state));
    const auto words = SemanticBuffer(77U, 10U, 0U, "Status updated");
    REQUIRE(coordinator.Sync(words.data(), static_cast<std::uint32_t>(words.size()), 0U));

    coordinator.Announce(77U);
    coordinator.Announce(999U);
    CHECK(state->announcements == std::vector<std::uint64_t>{77U});
}

TEST_CASE("disabled and missing accessibility actions do not request a frame",
    "[v2][native][accessibility]") {
    std::uint32_t frames = 0U;
    NativeAccessibilityCoordinator coordinator([&frames] { ++frames; });
    auto words = SemanticBuffer(91U, 1U, (1U << 4U) | (1U << 5U), "Disabled");
    REQUIRE(coordinator.Sync(words.data(), static_cast<std::uint32_t>(words.size()), 0U));

    coordinator.PerformAction(NativeAccessibilityAction::Press, 91U);
    coordinator.PerformAction(NativeAccessibilityAction::Focus, 999U);
    CHECK(frames == 0U);
}
