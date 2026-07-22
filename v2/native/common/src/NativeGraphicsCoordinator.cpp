#include "NativeGraphicsCoordinator.h"

#include "SDL3/SDL.h"

#include <algorithm>
#include <cmath>

namespace effindom::v2::native {

std::unique_ptr<NativeGraphicsCoordinator> NativeGraphicsCoordinator::Create(
    SDL_Window* window,
    NativeGraphicsOptions options,
    std::unique_ptr<NativeGraphicsSurface> preferred_surface) {
    auto coordinator = std::unique_ptr<NativeGraphicsCoordinator>(
        new NativeGraphicsCoordinator(window, options, std::move(preferred_surface)));
    if (coordinator->surface_ == nullptr && !coordinator->FallBackToRaster()) return nullptr;
    if (!coordinator->RefreshGeometry()) return nullptr;
    return coordinator;
}

NativeGraphicsCoordinator::NativeGraphicsCoordinator(
    SDL_Window* window,
    NativeGraphicsOptions options,
    std::unique_ptr<NativeGraphicsSurface> surface)
    : window_(window), options_(options), surface_(std::move(surface)) {}

bool NativeGraphicsCoordinator::RefreshGeometry() {
    geometry_.pixel_density = ReadPixelDensity();
    int width = 0;
    int height = 0;
    if (!surface_->QueryOutputSize(width, height)) return false;
    geometry_.physical_width = static_cast<std::uint32_t>(std::max(width, 1));
    geometry_.physical_height = static_cast<std::uint32_t>(std::max(height, 1));
    geometry_.logical_width = static_cast<float>(geometry_.physical_width) /
        geometry_.pixel_density;
    geometry_.logical_height = static_cast<float>(geometry_.physical_height) /
        geometry_.pixel_density;
    return true;
}

bool NativeGraphicsCoordinator::PrepareFrame() {
    return surface_->PrepareFrame(geometry_.physical_width, geometry_.physical_height,
        geometry_.pixel_density);
}

bool NativeGraphicsCoordinator::Present() { return surface_->Present(); }
void NativeGraphicsCoordinator::SetBackdropColor(std::uint32_t rgba) {
    backdrop_color_ = rgba;
    surface_->SetBackdropColor(rgba);
}
void NativeGraphicsCoordinator::RequestRecovery() { surface_->RequestRecovery(); }
bool NativeGraphicsCoordinator::HandleRecoveryEvent(const SDL_Event& event) {
    return surface_->HandleRecoveryEvent(event);
}
SkCanvas* NativeGraphicsCoordinator::Canvas() const { return surface_->Canvas(); }
SkSurface* NativeGraphicsCoordinator::Surface() const { return surface_->Surface(); }
const NativeGraphicsGeometry& NativeGraphicsCoordinator::Geometry() const { return geometry_; }
std::uint64_t NativeGraphicsCoordinator::Generation() const {
    return surface_->Generation();
}
std::uint64_t NativeGraphicsCoordinator::RecoveryCount() const {
    return surface_->RecoveryCount();
}
bool NativeGraphicsCoordinator::IsGpuBacked() const { return surface_->IsGpuBacked(); }
bool NativeGraphicsCoordinator::IsSuspended() const { return suspended_; }
void NativeGraphicsCoordinator::SetSuspended(bool suspended) { suspended_ = suspended; }

bool NativeGraphicsCoordinator::FallBackToRaster() {
    // Backend selection is immutable after startup. Runtime device loss is
    // recovered by the selected strategy rather than hidden by switching to
    // a renderer with different capabilities and presentation semantics.
    if (surface_ != nullptr) return false;
    surface_ = NativeRasterSurface::Create(window_, options_.raster);
    if (surface_ != nullptr) surface_->SetBackdropColor(backdrop_color_);
    return surface_ != nullptr;
}

float NativeGraphicsCoordinator::ReadPixelDensity() const {
    float density = options_.fixed_pixel_density;
    if (options_.pixel_density_source == NativePixelDensitySource::DisplayScale) {
        density = SDL_GetWindowDisplayScale(window_);
    } else if (options_.pixel_density_source == NativePixelDensitySource::WindowPixelDensity) {
        density = SDL_GetWindowPixelDensity(window_);
    }
    if (!std::isfinite(density) || density <= 0.0f) density = 1.0f;
    return density;
}

} // namespace effindom::v2::native
