#include "NativeTextInputCoordinator.h"

namespace effindom::v2::native {

void NativeTextInputCoordinator::Synchronize(
    const std::optional<NativeTextInputTarget>& target, bool window_focused,
    const NativeTextInputCallbacks& callbacks) {
    if (!window_focused || !target.has_value()) {
        if (target_.has_value() && callbacks.deactivate) callbacks.deactivate();
        target_.reset();
        state_ = NativeTextInputState::Inactive;
        return;
    }

    const bool changed_target = !target_.has_value() || target_->handle != target->handle;
    if (changed_target && target_.has_value() && callbacks.deactivate) callbacks.deactivate();
    target_ = target;
    if (changed_target || state_ == NativeTextInputState::Inactive) {
        if (callbacks.activate && !callbacks.activate()) {
            state_ = NativeTextInputState::Inactive;
            return;
        }
        state_ = NativeTextInputState::Editing;
    }
    if (callbacks.update_area) callbacks.update_area(*target_);
}

void NativeTextInputCoordinator::BeginComposition() {
    if (target_.has_value()) state_ = NativeTextInputState::Composing;
}

void NativeTextInputCoordinator::BeginCommit() {
    if (target_.has_value()) state_ = NativeTextInputState::Committing;
}

void NativeTextInputCoordinator::BeginCancel() {
    if (target_.has_value()) state_ = NativeTextInputState::Cancelling;
}

void NativeTextInputCoordinator::FinishComposition() {
    state_ = target_.has_value()
        ? NativeTextInputState::Editing
        : NativeTextInputState::Inactive;
}

NativeTextInputState NativeTextInputCoordinator::State() const { return state_; }

std::uint64_t NativeTextInputCoordinator::ActiveHandle() const {
    return target_.has_value() ? target_->handle : 0U;
}

} // namespace effindom::v2::native
