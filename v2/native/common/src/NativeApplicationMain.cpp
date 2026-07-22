#include "NativeHost.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

// Keep high-frequency pointer and wheel input from forcing one complete render
// per SDL event. The bound prevents a continuously busy queue from starving
// presentation altogether while preserving event ordering within each batch.
constexpr std::uint32_t kMaximumEventsPerFrame = 256U;

} // namespace

int main(int argc, char** argv) {
    bool hidden = false;
    std::filesystem::path screenshot;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--hidden") hidden = true;
        if (argument == "--screenshot" && index + 1 < argc) screenshot = argv[++index];
    }
    try {
        effindom::v2::native::NativeHost host(!hidden);
        host.MountApplication();
        if (hidden) host.DrainFrames();
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
