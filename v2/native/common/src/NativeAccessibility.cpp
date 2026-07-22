#include "NativeAccessibility.h"

#include "effindom_ui.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace effindom::v2::native {
namespace {

constexpr std::uint32_t kFixedRecordWords = 14U;
constexpr std::uint32_t kHasSelected = 1U << 0U;
constexpr std::uint32_t kIsSelected = 1U << 1U;
constexpr std::uint32_t kHasExpanded = 1U << 2U;
constexpr std::uint32_t kIsExpanded = 1U << 3U;
constexpr std::uint32_t kHasDisabled = 1U << 4U;
constexpr std::uint32_t kIsDisabled = 1U << 5U;
constexpr std::uint32_t kHasValueRange = 1U << 6U;
constexpr std::uint32_t kHasReadOnly = 1U << 7U;
constexpr std::uint32_t kIsReadOnly = 1U << 8U;
constexpr std::uint32_t kHasMultiline = 1U << 9U;
constexpr std::uint32_t kIsMultiline = 1U << 10U;

float WordToFloat(std::uint32_t word) {
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(word));
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

bool EqualNode(const NativeAccessibilityNode& left, const NativeAccessibilityNode& right) {
    return left.role == right.role && left.handle == right.handle &&
        left.bounds.x == right.bounds.x && left.bounds.y == right.bounds.y &&
        left.bounds.width == right.bounds.width && left.bounds.height == right.bounds.height &&
        left.has_selected == right.has_selected && left.selected == right.selected &&
        left.has_expanded == right.has_expanded && left.expanded == right.expanded &&
        left.has_disabled == right.has_disabled && left.disabled == right.disabled &&
        left.has_value_range == right.has_value_range &&
        left.has_read_only == right.has_read_only && left.read_only == right.read_only &&
        left.has_multiline == right.has_multiline && left.multiline == right.multiline &&
        left.checked == right.checked && left.orientation == right.orientation &&
        left.value == right.value && left.minimum == right.minimum &&
        left.maximum == right.maximum && left.label == right.label;
}

bool EqualSnapshot(const NativeAccessibilitySnapshot& left, const NativeAccessibilitySnapshot& right) {
    if (left.focused_handle != right.focused_handle || left.nodes.size() != right.nodes.size()) return false;
    for (std::size_t index = 0U; index < left.nodes.size(); ++index) {
        if (!EqualNode(left.nodes[index], right.nodes[index])) return false;
    }
    return true;
}

bool DecodeSnapshot(const std::uint32_t* words, std::uint32_t length,
    std::uint64_t focused_handle, NativeAccessibilitySnapshot& output) {
    if (words == nullptr || length == 0U) return false;
    NativeAccessibilitySnapshot decoded;
    decoded.focused_handle = focused_handle;
    const std::uint32_t record_count = words[0];
    decoded.nodes.reserve(record_count);
    std::unordered_set<std::uint64_t> handles;
    std::uint32_t offset = 1U;
    for (std::uint32_t record = 0U; record < record_count; ++record) {
        if (length - offset < kFixedRecordWords) return false;
        const std::uint32_t role = words[offset];
        if (role < static_cast<std::uint32_t>(NativeAccessibilityRole::Button) ||
            role > static_cast<std::uint32_t>(NativeAccessibilityRole::ComboBox)) return false;
        NativeAccessibilityNode node;
        node.role = static_cast<NativeAccessibilityRole>(role);
        node.handle = static_cast<std::uint64_t>(words[offset + 1U]) |
            (static_cast<std::uint64_t>(words[offset + 2U]) << 32U);
        if (node.handle == 0U || !handles.insert(node.handle).second) return false;
        node.bounds = {WordToFloat(words[offset + 3U]), WordToFloat(words[offset + 4U]),
            WordToFloat(words[offset + 5U]), WordToFloat(words[offset + 6U])};
        if (!std::isfinite(node.bounds.x) || !std::isfinite(node.bounds.y) ||
            !std::isfinite(node.bounds.width) || !std::isfinite(node.bounds.height) ||
            node.bounds.width < 0.0f || node.bounds.height < 0.0f) return false;
        const std::uint32_t flags = words[offset + 7U];
        node.has_selected = (flags & kHasSelected) != 0U;
        node.selected = (flags & kIsSelected) != 0U;
        node.has_expanded = (flags & kHasExpanded) != 0U;
        node.expanded = (flags & kIsExpanded) != 0U;
        node.has_disabled = (flags & kHasDisabled) != 0U;
        node.disabled = (flags & kIsDisabled) != 0U;
        node.has_value_range = (flags & kHasValueRange) != 0U;
        node.has_read_only = (flags & kHasReadOnly) != 0U;
        node.read_only = (flags & kIsReadOnly) != 0U;
        node.has_multiline = (flags & kHasMultiline) != 0U;
        node.multiline = (flags & kIsMultiline) != 0U;
        if (words[offset + 8U] > static_cast<std::uint32_t>(NativeAccessibilityCheckedState::Mixed) ||
            words[offset + 9U] > static_cast<std::uint32_t>(NativeAccessibilityOrientation::Vertical)) return false;
        node.checked = static_cast<NativeAccessibilityCheckedState>(words[offset + 8U]);
        node.orientation = static_cast<NativeAccessibilityOrientation>(words[offset + 9U]);
        node.value = WordToFloat(words[offset + 10U]);
        node.minimum = WordToFloat(words[offset + 11U]);
        node.maximum = WordToFloat(words[offset + 12U]);
        if (!std::isfinite(node.value) || !std::isfinite(node.minimum) || !std::isfinite(node.maximum)) return false;
        const std::uint32_t label_length = words[offset + 13U];
        const std::uint64_t label_words = (static_cast<std::uint64_t>(label_length) + 3U) / 4U;
        if (label_words > std::numeric_limits<std::uint32_t>::max() ||
            label_words > static_cast<std::uint64_t>(length - offset - kFixedRecordWords)) return false;
        const char* label = reinterpret_cast<const char*>(words + offset + kFixedRecordWords);
        node.label.assign(label, label + label_length);
        offset += kFixedRecordWords + static_cast<std::uint32_t>(label_words);
        decoded.nodes.push_back(std::move(node));
    }
    if (offset != length) return false;
    output = std::move(decoded);
    return true;
}

const char* ActivationKey(NativeAccessibilityRole role) {
    switch (role) {
        case NativeAccessibilityRole::CheckBox:
        case NativeAccessibilityRole::Radio:
        case NativeAccessibilityRole::Switch:
            return " ";
        default:
            return "Enter";
    }
}

} // namespace

NativeAccessibilityCoordinator::NativeAccessibilityCoordinator(std::function<void()> request_frame)
    : request_frame_(std::move(request_frame)) {}

NativeAccessibilityCoordinator::~NativeAccessibilityCoordinator() { Clear(); }

void NativeAccessibilityCoordinator::Attach(std::unique_ptr<NativeAccessibilityAdapter> adapter) {
    if (adapter_) adapter_->Clear();
    adapter_ = std::move(adapter);
    if (adapter_) adapter_->Update(snapshot_);
}

bool NativeAccessibilityCoordinator::Sync(const std::uint32_t* words, std::uint32_t length,
    std::uint64_t focused_handle) {
    NativeAccessibilitySnapshot decoded;
    if (!DecodeSnapshot(words, length, focused_handle, decoded)) return false;
    if (EqualSnapshot(snapshot_, decoded)) return true;
    snapshot_ = std::move(decoded);
    if (adapter_) adapter_->Update(snapshot_);
    return true;
}

void NativeAccessibilityCoordinator::Announce(std::uint64_t handle) {
    if (!adapter_) return;
    for (const auto& node : snapshot_.nodes) {
        if (node.handle == handle) {
            adapter_->Announce(node);
            return;
        }
    }
}

void NativeAccessibilityCoordinator::PerformAction(
    NativeAccessibilityAction action, std::uint64_t handle) {
    const NativeAccessibilityNode* node = nullptr;
    for (const auto& candidate : snapshot_.nodes) {
        if (candidate.handle == handle) {
            node = &candidate;
            break;
        }
    }
    if (node == nullptr || node->disabled) return;
    ui_request_focus(handle);
    const char* key = nullptr;
    switch (action) {
        case NativeAccessibilityAction::Focus:
            break;
        case NativeAccessibilityAction::Press:
            key = ActivationKey(node->role);
            break;
        case NativeAccessibilityAction::Increment:
            key = node->orientation == NativeAccessibilityOrientation::Vertical ? "ArrowUp" : "ArrowRight";
            break;
        case NativeAccessibilityAction::Decrement:
            key = node->orientation == NativeAccessibilityOrientation::Vertical ? "ArrowDown" : "ArrowLeft";
            break;
    }
    if (key != nullptr) {
        const auto length = static_cast<std::uint32_t>(std::strlen(key));
        ui_on_key_event(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>(key), length, 0U);
        ui_on_key_event(UI_KEY_EVENT_UP, reinterpret_cast<const std::uint8_t*>(key), length, 0U);
    }
    if (request_frame_) request_frame_();
}

void NativeAccessibilityCoordinator::Clear() {
    if (adapter_) adapter_->Clear();
    snapshot_ = {};
}

const NativeAccessibilitySnapshot& NativeAccessibilityCoordinator::Snapshot() const { return snapshot_; }

} // namespace effindom::v2::native
