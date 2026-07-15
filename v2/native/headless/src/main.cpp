#include "HeadlessConformanceHost.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct Arguments {
    std::filesystem::path output_dir{};
    std::filesystem::path golden_dir{};
};

bool ParseArguments(int argc, char** argv, Arguments& arguments) {
    for (int index = 1; index < argc; index += 1) {
        const std::string argument = argv[index];
        if (argument == "--output" && index + 1 < argc) {
            arguments.output_dir = argv[++index];
        } else if (argument == "--verify" && index + 1 < argc) {
            arguments.golden_dir = argv[++index];
        } else {
            return false;
        }
    }
    return !arguments.output_dir.empty();
}

} // namespace

int main(int argc, char** argv) {
    Arguments arguments{};
    if (!ParseArguments(argc, argv, arguments)) {
        std::cerr << "usage: effindom_v2_headless_conformance --output <directory> [--verify <golden-directory>]\n";
        return 2;
    }

    try {
        effindom::v2::headless::HeadlessConformanceHost host{};
        host.Mount();
        host.RunInputScenario();

        std::string error{};
        if (!host.WriteArtifacts(arguments.output_dir, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (!arguments.golden_dir.empty() && !host.VerifyArtifacts(arguments.golden_dir, error)) {
            std::cerr << error << '\n';
            return 1;
        }

        const std::size_t host_calls_before_dispose = host.HostCallCount();
        host.Dispose();
        host.AdvanceTime(1000.0);
        host.DrainFrames();
        if (host.HostCallCount() != host_calls_before_dispose || !host.IsIdle()) {
            std::cerr << "headless host produced work after disposal\n";
            return 1;
        }
        std::cout << "headless native conformance passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
