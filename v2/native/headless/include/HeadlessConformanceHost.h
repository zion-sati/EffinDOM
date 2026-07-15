#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace effindom::v2::headless {

struct ConformanceState {
    std::string editor_text{};
    std::uint32_t selection_start = 0U;
    std::uint32_t selection_end = 0U;
    float scroll_offset_y = 0.0f;
    float logical_width = 0.0f;
    float logical_height = 0.0f;
    bool pointer_route_observed = false;
    bool keyboard_route_observed = false;
};

class HeadlessConformanceHost {
public:
    HeadlessConformanceHost();
    ~HeadlessConformanceHost();

    HeadlessConformanceHost(const HeadlessConformanceHost&) = delete;
    HeadlessConformanceHost& operator=(const HeadlessConformanceHost&) = delete;

    void Mount();
    void RunInputScenario();
    void RequestFrame();
    bool RunNextFrame();
    void DrainFrames();
    void AdvanceTime(double milliseconds);
    void Dispose();

    bool IsMounted() const;
    bool IsIdle() const;
    std::uint64_t FrameCount() const;
    std::size_t HostCallCount() const;
    ConformanceState State() const;
    std::vector<std::uint8_t> SnapshotRgba() const;
    std::string SemanticSnapshot() const;
    std::string HostCallTrace() const;

    bool WriteArtifacts(const std::filesystem::path& output_dir, std::string& error) const;
    bool VerifyArtifacts(const std::filesystem::path& golden_dir, std::string& error) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::headless
