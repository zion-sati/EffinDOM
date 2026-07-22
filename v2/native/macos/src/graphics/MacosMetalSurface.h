#pragma once

#include "NativeGraphicsSurface.h"

#include <cstdint>
#include <memory>

union SDL_Event;
struct SDL_Window;
class SkCanvas;
class SkSurface;

namespace effindom::v2::native {

class MacosMetalSurface final : public NativeGraphicsSurface {
public:
    static std::unique_ptr<MacosMetalSurface> Create(SDL_Window* window);
    static void FailNextInitializationForTesting();
    static void FailNextRecoveryForTesting();

    ~MacosMetalSurface() override;

    MacosMetalSurface(const MacosMetalSurface&) = delete;
    MacosMetalSurface& operator=(const MacosMetalSurface&) = delete;

    bool PrepareFrame(std::uint32_t width, std::uint32_t height, float pixel_density) override;
    bool QueryOutputSize(int& width, int& height) const override;
    bool Present() override;
    void SetBackdropColor(std::uint32_t rgba) override;
    void RequestRecovery() override;
    bool HandleRecoveryEvent(const SDL_Event& event) override;

    SkCanvas* Canvas() const override;
    SkSurface* Surface() const override;
    std::uint64_t Generation() const override;
    std::uint64_t RecoveryCount() const override;
    bool IsGpuBacked() const override { return true; }

private:
    struct Impl;

    explicit MacosMetalSurface(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
