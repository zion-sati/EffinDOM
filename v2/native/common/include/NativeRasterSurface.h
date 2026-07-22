#pragma once

#include "NativeGraphicsSurface.h"

#include <memory>

struct SDL_Window;

namespace effindom::v2::native {

struct NativeRasterSurfaceOptions {
    bool disable_logical_presentation = false;
    bool force_software_presentation = false;
};

class NativeRasterSurface final : public NativeGraphicsSurface {
public:
    static std::unique_ptr<NativeRasterSurface> Create(
        SDL_Window* window,
        NativeRasterSurfaceOptions options = {});
    ~NativeRasterSurface() override;

    NativeRasterSurface(const NativeRasterSurface&) = delete;
    NativeRasterSurface& operator=(const NativeRasterSurface&) = delete;

    bool QueryOutputSize(int& width, int& height) const override;
    bool PrepareFrame(std::uint32_t width, std::uint32_t height,
        float pixel_density) override;
    bool Present() override;
    void SetBackdropColor(std::uint32_t rgba) override;
    void RequestRecovery() override;
    bool HandleRecoveryEvent(const SDL_Event& event) override;
    SkCanvas* Canvas() const override;
    SkSurface* Surface() const override;
    std::uint64_t Generation() const override;
    std::uint64_t RecoveryCount() const override;
    bool IsGpuBacked() const override;

private:
    struct Impl;
    explicit NativeRasterSurface(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
