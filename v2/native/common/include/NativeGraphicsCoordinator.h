#pragma once

#include "NativeGraphicsSurface.h"
#include "NativeRasterSurface.h"

#include <cstdint>
#include <memory>

struct SDL_Window;

namespace effindom::v2::native {

enum class NativePixelDensitySource {
    WindowPixelDensity,
    DisplayScale,
    Fixed,
};

struct NativeGraphicsOptions {
    NativePixelDensitySource pixel_density_source =
        NativePixelDensitySource::WindowPixelDensity;
    NativeRasterSurfaceOptions raster{};
    float fixed_pixel_density = 1.0f;
};

struct NativeGraphicsGeometry {
    float logical_width = 0.0f;
    float logical_height = 0.0f;
    float pixel_density = 1.0f;
    std::uint32_t physical_width = 0U;
    std::uint32_t physical_height = 0U;
};

class NativeGraphicsCoordinator final {
public:
    static std::unique_ptr<NativeGraphicsCoordinator> Create(
        SDL_Window* window,
        NativeGraphicsOptions options,
        std::unique_ptr<NativeGraphicsSurface> preferred_surface);

    bool RefreshGeometry();
    bool PrepareFrame();
    bool Present();
    void SetBackdropColor(std::uint32_t rgba);
    void RequestRecovery();
    bool HandleRecoveryEvent(const SDL_Event& event);

    SkCanvas* Canvas() const;
    SkSurface* Surface() const;
    const NativeGraphicsGeometry& Geometry() const;
    std::uint64_t Generation() const;
    std::uint64_t RecoveryCount() const;
    bool IsGpuBacked() const;
    bool IsSuspended() const;
    void SetSuspended(bool suspended);

private:
    NativeGraphicsCoordinator(SDL_Window* window, NativeGraphicsOptions options,
        std::unique_ptr<NativeGraphicsSurface> surface);
    bool FallBackToRaster();
    float ReadPixelDensity() const;

    SDL_Window* window_ = nullptr;
    NativeGraphicsOptions options_{};
    std::unique_ptr<NativeGraphicsSurface> surface_;
    NativeGraphicsGeometry geometry_{};
    std::uint32_t backdrop_color_ = 0xFFFFFFFFU;
    bool suspended_ = false;
};

} // namespace effindom::v2::native
