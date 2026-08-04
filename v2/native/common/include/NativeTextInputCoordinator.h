#pragma once

#include "NativeInputTypes.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace effindom::v2::native {

enum class NativeTextInputState : std::uint32_t {
    Inactive = 0U,
    Editing = 1U,
    Composing = 2U,
    Committing = 3U,
    Cancelling = 4U,
};

struct NativeTextInputCallbacks {
    std::function<bool()> activate;
    std::function<void()> deactivate;
    std::function<void(const NativeTextInputTarget&)> update_area;
};

class NativeTextInputCoordinator final {
public:
    void Synchronize(const std::optional<NativeTextInputTarget>& target,
        bool window_focused, const NativeTextInputCallbacks& callbacks);
    void BeginComposition();
    void BeginCommit();
    void BeginCancel();
    void FinishComposition();

    NativeTextInputState State() const;
    std::uint64_t ActiveHandle() const;

private:
    NativeTextInputState state_ = NativeTextInputState::Inactive;
    std::optional<NativeTextInputTarget> target_;
};

} // namespace effindom::v2::native
