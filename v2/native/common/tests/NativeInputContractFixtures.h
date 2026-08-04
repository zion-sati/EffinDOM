#pragma once

#include "NativeInputTypes.h"
#include "effindom_ui.h"

#include <vector>

namespace effindom::v2::native::test {

inline std::vector<NativeWheelInput> WheelTrace() {
    return {
        NativeWheelInput{42U, 120.0f, 80.0f, 0.0f, 16.0f,
            NativeWheelDeltaMode::Pixel, UI_KEY_MOD_SHIFT, false, false, false, 10.0},
        NativeWheelInput{42U, 121.0f, 81.0f, 1.5f, 4.0f,
            NativeWheelDeltaMode::Pixel, 0U, true, true, false, 11.0},
        NativeWheelInput{42U, 123.0f, 84.0f, 0.5f, 2.0f,
            NativeWheelDeltaMode::Pixel, 0U, true, false, true, 12.0},
    };
}

inline std::vector<NativeFrameInput> AnimationFrameTrace() {
    return {{1000.0}, {1016.0}, {1032.0}};
}

inline std::vector<NativeTextCompositionInput> CompositionCommitTrace() {
    return {
        NativeTextCompositionInput{NativeTextCompositionPhase::Start, "", 4U, 7U, 4U, 7U, 20.0},
        NativeTextCompositionInput{NativeTextCompositionPhase::Update, "ni", 4U, 7U, 2U, 2U, 21.0},
        NativeTextCompositionInput{NativeTextCompositionPhase::Update, "\xE4\xBD\xA0", 4U, 7U, 3U, 3U, 22.0},
        NativeTextCompositionInput{NativeTextCompositionPhase::Commit, "\xE4\xBD\xA0", 4U, 7U,
            kNativeTextRangeUnspecified, kNativeTextRangeUnspecified, 23.0},
    };
}

inline std::vector<NativeTextCompositionInput> CompositionCancelTrace() {
    return {
        NativeTextCompositionInput{NativeTextCompositionPhase::Start, "", 2U, 2U, 2U, 2U, 30.0},
        NativeTextCompositionInput{NativeTextCompositionPhase::Update, "dead", 2U, 2U, 4U, 4U, 31.0},
        NativeTextCompositionInput{NativeTextCompositionPhase::Cancel, "", 2U, 2U,
            kNativeTextRangeUnspecified, kNativeTextRangeUnspecified, 32.0},
    };
}

inline std::vector<NativePointerContactInput> MultiPointerTrace() {
    return {
        NativePointerContactInput{20.0f, 30.0f, 7, UI_POINTER_TYPE_TOUCH, true,
            0, 1U, 0.5f, 0.0f, 12.0f, 14.0f, 0.0f, 0.0f, 0.0f, 0U, 40.0},
        NativePointerContactInput{80.0f, 90.0f, 9, UI_POINTER_TYPE_TOUCH, false,
            0, 1U, 0.6f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0U, 41.0},
        NativePointerContactInput{24.0f, 34.0f, 7, UI_POINTER_TYPE_TOUCH, true,
            -1, 1U, 0.55f, 0.0f, 12.0f, 14.0f, 0.0f, 0.0f, 0.0f, 0U, 42.0},
        NativePointerContactInput{84.0f, 94.0f, 9, UI_POINTER_TYPE_TOUCH, false,
            -1, 1U, 0.65f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0U, 43.0},
    };
}

inline NativePointerContactInput PenContactFixture() {
    return NativePointerContactInput{50.0f, 60.0f, 12, UI_POINTER_TYPE_PEN, true,
        0, 1U, 0.75f, -0.2f, 3.0f, 4.0f, 18.0f, -12.0f, 90.0f,
        UI_KEY_MOD_SHIFT, 50.0};
}

} // namespace effindom::v2::native::test
