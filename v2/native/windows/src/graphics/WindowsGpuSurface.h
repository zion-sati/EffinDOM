#pragma once

#include "NativeGraphicsSurface.h"

#include <cstdint>
#include <memory>

union SDL_Event;
struct SDL_Window;
class SkCanvas;
class SkSurface;

namespace effindom::v2::native {

class WindowsGpuSurface final : public NativeGraphicsSurface {
public:
    struct Impl;

    static std::unique_ptr<WindowsGpuSurface> Create(SDL_Window* window);
    ~WindowsGpuSurface() override;

    bool PrepareFrame(std::uint32_t width, std::uint32_t height, float pixel_density) override;
    bool QueryOutputSize(int& width, int& height) const override;
    SkCanvas* Canvas() const override;
    SkSurface* Surface() const override;
    bool Present() override;
    void RequestRecovery() override;
    bool HandleRecoveryEvent(const SDL_Event& event) override;
    std::uint64_t Generation() const override;
    std::uint64_t RecoveryCount() const override;
    bool IsGpuBacked() const override { return true; }

private:
    explicit WindowsGpuSurface(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
