#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace effindom::v2::native {

enum class NativeAccessibilityRole : std::uint32_t {
    Button = 1U,
    TextBox = 2U,
    Link = 3U,
    Heading = 4U,
    Form = 5U,
    List = 6U,
    ListItem = 7U,
    Image = 8U,
    Dialog = 9U,
    StaticText = 10U,
    CheckBox = 11U,
    Radio = 12U,
    RadioGroup = 13U,
    Switch = 14U,
    Slider = 15U,
    ComboBox = 16U,
};

enum class NativeAccessibilityCheckedState : std::uint32_t {
    None = 0U,
    False = 1U,
    True = 2U,
    Mixed = 3U,
};

enum class NativeAccessibilityOrientation : std::uint32_t {
    None = 0U,
    Horizontal = 1U,
    Vertical = 2U,
};

enum class NativeAccessibilityAction : std::uint32_t {
    Focus,
    Press,
    Increment,
    Decrement,
};

struct NativeAccessibilityBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct NativeAccessibilityNode {
    NativeAccessibilityRole role = NativeAccessibilityRole::StaticText;
    std::uint64_t handle = 0U;
    NativeAccessibilityBounds bounds{};
    bool has_selected = false;
    bool selected = false;
    bool has_expanded = false;
    bool expanded = false;
    bool has_disabled = false;
    bool disabled = false;
    bool has_value_range = false;
    bool has_read_only = false;
    bool read_only = false;
    bool has_multiline = false;
    bool multiline = false;
    NativeAccessibilityCheckedState checked = NativeAccessibilityCheckedState::None;
    NativeAccessibilityOrientation orientation = NativeAccessibilityOrientation::None;
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    std::string label;
};

struct NativeAccessibilitySnapshot {
    std::vector<NativeAccessibilityNode> nodes;
    std::uint64_t focused_handle = 0U;
};

using NativeAccessibilityActionHandler =
    std::function<void(NativeAccessibilityAction, std::uint64_t)>;

class NativeAccessibilityAdapter {
public:
    virtual ~NativeAccessibilityAdapter() = default;
    virtual void Update(const NativeAccessibilitySnapshot& snapshot) = 0;
    virtual void Announce(const NativeAccessibilityNode& node) = 0;
    virtual void Clear() = 0;
};

class NativeAccessibilityCoordinator final {
public:
    explicit NativeAccessibilityCoordinator(std::function<void()> request_frame);
    ~NativeAccessibilityCoordinator();

    void Attach(std::unique_ptr<NativeAccessibilityAdapter> adapter);
    bool Sync(const std::uint32_t* words, std::uint32_t length, std::uint64_t focused_handle);
    void Announce(std::uint64_t handle);
    void PerformAction(NativeAccessibilityAction action, std::uint64_t handle);
    void Clear();

    const NativeAccessibilitySnapshot& Snapshot() const;

private:
    std::function<void()> request_frame_;
    std::unique_ptr<NativeAccessibilityAdapter> adapter_;
    NativeAccessibilitySnapshot snapshot_{};
};

} // namespace effindom::v2::native
