#pragma once

#include <cstdint>
#include <memory>

union SDL_Event;
struct SDL_Window;
class SkCanvas;
class SkSurface;

namespace effindom::v2::native {

class MacosMetalSurface final {
public:
    static std::unique_ptr<MacosMetalSurface> Create(SDL_Window* window);

    ~MacosMetalSurface();

    MacosMetalSurface(const MacosMetalSurface&) = delete;
    MacosMetalSurface& operator=(const MacosMetalSurface&) = delete;

    bool PrepareFrame(std::uint32_t width, std::uint32_t height, float pixel_density);
    bool Present();
    void RequestRecovery();
    bool HandleRecoveryEvent(const SDL_Event& event);

    SkCanvas* Canvas() const;
    SkSurface* Surface() const;
    std::uint64_t Generation() const;
    std::uint64_t RecoveryCount() const;

private:
    struct Impl;

    explicit MacosMetalSurface(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
