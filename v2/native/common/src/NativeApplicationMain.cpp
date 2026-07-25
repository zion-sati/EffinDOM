#include "NativeHost.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Keep high-frequency pointer and wheel input from forcing one complete render
// per SDL event. The bound prevents a continuously busy queue from starving
// presentation altogether while preserving event ordering within each batch.
constexpr std::uint32_t kMaximumEventsPerFrame = 256U;

bool RunPackageSelfTest(effindom::v2::native::NativeHost& host, std::string& error) {
    const auto state = host.State();
    if (state.mount_count != 1U || state.frame_count == 0U ||
        state.logical_width <= 0.0f || state.logical_height <= 0.0f) {
        error = "packaged native application did not mount and render";
        return false;
    }

    const auto& semantics = host.AccessibilitySnapshotForTesting();
    const auto action = std::find_if(
        semantics.nodes.begin(), semantics.nodes.end(), [](const auto& node) {
            return node.role == effindom::v2::native::NativeAccessibilityRole::Button &&
                node.label == "Increment click count";
        });
    if (action == semantics.nodes.end()) {
        error = "packaged native application did not publish its semantic button";
        return false;
    }
    if (!host.SvgSizeForTesting(9001U).has_value() || host.TextureCountForTesting() == 0U) {
        error = "packaged native application did not load its SVG and image assets";
        return false;
    }
    const float action_x = action->bounds.x + action->bounds.width * 0.5f;
    const float action_y = action->bounds.y + action->bounds.height * 0.5f;

    host.SetSystemDarkModeForTesting(false);
    host.DrainFrames();
    const std::vector<std::uint8_t> light_frame = host.SnapshotRgba();
    host.SetSystemDarkModeForTesting(true);
    host.DrainFrames();
    const std::vector<std::uint8_t> dark_frame = host.SnapshotRgba();
    if (light_frame.empty() || light_frame == dark_frame) {
        error = "packaged native application did not react to system theme changes";
        return false;
    }

    host.DispatchPointer(action_x, action_y, true, 0, 1U, 1);
    host.DispatchPointer(action_x, action_y, false, 0, 0U, 1);
    host.DrainFrames();
    if (host.SnapshotRgba() == dark_frame) {
        error = "packaged native application did not route pointer activation";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bool hidden = false;
    bool package_self_test = false;
    std::filesystem::path screenshot;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--hidden") hidden = true;
        if (argument == "--package-self-test") package_self_test = true;
        if (argument == "--screenshot" && index + 1 < argc) screenshot = argv[++index];
    }
    try {
        effindom::v2::native::NativeHost host(!hidden);
        host.MountApplication();
        if (hidden) host.DrainFrames();
        if (package_self_test) {
            std::string error;
            if (!RunPackageSelfTest(host, error)) {
                std::cerr << error << '\n';
                return 1;
            }
        }
        if (!screenshot.empty()) {
            std::string error;
            if (!host.WriteScreenshot(screenshot, error)) {
                std::cerr << error << '\n';
                return 1;
            }
        }
        if (hidden) return 0;
        while (host.IsRunning()) {
            host.PumpEvent(true);
            for (std::uint32_t count = 1U;
                 count < kMaximumEventsPerFrame && host.IsRunning() &&
                     !host.ShouldPresentAfterLastEvent() && host.PumpEvent(false);
                 ++count) {}
            host.RunNextFrame();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
