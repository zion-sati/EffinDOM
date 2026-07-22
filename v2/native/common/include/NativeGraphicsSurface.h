#pragma once

#include <cstdint>

union SDL_Event;
class SkCanvas;
class SkSurface;

namespace effindom::v2::native {

class NativeGraphicsSurface {
public:
    virtual ~NativeGraphicsSurface() = default;

    virtual bool PrepareFrame(
        std::uint32_t width,
        std::uint32_t height,
        float pixel_density) = 0;
    virtual bool QueryOutputSize(int& width, int& height) const = 0;
    virtual bool Present() = 0;
    virtual void SetBackdropColor(std::uint32_t rgba) { (void)rgba; }
    virtual void RequestRecovery() = 0;
    virtual bool HandleRecoveryEvent(const SDL_Event& event) = 0;
    virtual SkCanvas* Canvas() const = 0;
    virtual SkSurface* Surface() const = 0;
    virtual std::uint64_t Generation() const = 0;
    virtual std::uint64_t RecoveryCount() const = 0;
    virtual bool IsGpuBacked() const = 0;
};

} // namespace effindom::v2::native
