#pragma once

#include "NativeGraphicsSurface.h"

#include <memory>

struct SDL_Window;

namespace effindom::v2::native {

class LinuxVulkanSurface final : public NativeGraphicsSurface {
public:
    static std::unique_ptr<LinuxVulkanSurface> Create(SDL_Window* window);
    ~LinuxVulkanSurface() override;

    LinuxVulkanSurface(const LinuxVulkanSurface&) = delete;
    LinuxVulkanSurface& operator=(const LinuxVulkanSurface&) = delete;

    bool PrepareFrame(std::uint32_t width, std::uint32_t height, float pixel_density) override;
    bool QueryOutputSize(int& width, int& height) const override;
    bool Present() override;
    void RequestRecovery() override;
    bool HandleRecoveryEvent(const SDL_Event& event) override;
    SkCanvas* Canvas() const override;
    SkSurface* Surface() const override;
    std::uint64_t Generation() const override;
    std::uint64_t RecoveryCount() const override;
    bool IsGpuBacked() const override { return true; }

private:
    struct Impl;
    explicit LinuxVulkanSurface(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
