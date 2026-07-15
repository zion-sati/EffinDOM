#include "MacosNativeHost.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    bool hidden = false;
    std::filesystem::path screenshot{};
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--hidden") hidden = true;
        if (arg == "--screenshot" && index + 1 < argc) screenshot = argv[++index];
    }
    try {
        effindom::v2::native::MacosNativeHost host(!hidden);
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
            host.RunNextFrame();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
