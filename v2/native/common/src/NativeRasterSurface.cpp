#include "NativeRasterSurface.h"

#include "SDL3/SDL.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

#include <string_view>
#include <vector>

namespace effindom::v2::native {

struct NativeRasterSurface::Impl {
    Impl(SDL_Window* owner, NativeRasterSurfaceOptions value)
        : window(owner), options(value) {}

    ~Impl() {
        surface.reset();
        if (texture != nullptr) SDL_DestroyTexture(texture);
        if (renderer != nullptr) SDL_DestroyRenderer(renderer);
    }

    bool Initialize() {
        renderer = SDL_CreateRenderer(
            window, options.force_software_presentation ? "software" : nullptr);
        if (renderer == nullptr) return false;
        const char* renderer_name = SDL_GetRendererName(renderer);
        SDL_Log("EffinDOM SDL presentation renderer: %s",
            renderer_name == nullptr ? "unknown" : renderer_name);
        if (options.force_software_presentation &&
            (renderer_name == nullptr || std::string_view(renderer_name) != "software")) {
            SDL_SetError("SDL did not create the requested software renderer");
            return false;
        }
        if (options.disable_logical_presentation && !SDL_SetRenderLogicalPresentation(
                renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED)) {
            return false;
        }
        SDL_SetRenderVSync(renderer, 0);
        generation = 1U;
        return true;
    }

    bool PrepareFrame(std::uint32_t next_width, std::uint32_t next_height) {
        if (next_width == width && next_height == height &&
            surface != nullptr && texture != nullptr) {
            return true;
        }
        width = next_width;
        height = next_height;
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
        pixels.resize(row_bytes * height);
        surface = SkSurfaces::WrapPixels(
            SkImageInfo::MakeN32Premul(width, height), pixels.data(), row_bytes);
        if (surface == nullptr) return false;
        if (texture != nullptr) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
            SDL_TEXTUREACCESS_STREAMING, static_cast<int>(width),
            static_cast<int>(height));
        if (texture == nullptr) return false;
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        return true;
    }

    bool Present() {
        if (surface == nullptr || texture == nullptr) return false;
        if (!SDL_UpdateTexture(texture, nullptr, pixels.data(),
                static_cast<int>(static_cast<std::size_t>(width) * 4U))) {
            return false;
        }
        if (!SDL_SetRenderDrawColor(renderer,
                (backdrop_color >> 24U) & 0xFFU,
                (backdrop_color >> 16U) & 0xFFU,
                (backdrop_color >> 8U) & 0xFFU,
                backdrop_color & 0xFFU) ||
            !SDL_RenderClear(renderer)) {
            return false;
        }
        const SDL_FRect destination{
            0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        };
        if (!SDL_RenderTexture(renderer, texture, nullptr, &destination)) return false;
        SDL_RenderPresent(renderer);
        return true;
    }

    SDL_Window* window = nullptr;
    NativeRasterSurfaceOptions options{};
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    std::vector<std::uint8_t> pixels{};
    sk_sp<SkSurface> surface{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t generation = 0U;
    std::uint32_t backdrop_color = 0xFFFFFFFFU;
};

std::unique_ptr<NativeRasterSurface> NativeRasterSurface::Create(
    SDL_Window* window,
    NativeRasterSurfaceOptions options) {
    auto impl = std::make_unique<Impl>(window, options);
    if (!impl->Initialize()) return nullptr;
    return std::unique_ptr<NativeRasterSurface>(new NativeRasterSurface(std::move(impl)));
}

NativeRasterSurface::NativeRasterSurface(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
NativeRasterSurface::~NativeRasterSurface() = default;

bool NativeRasterSurface::QueryOutputSize(int& width, int& height) const {
    return SDL_GetRenderOutputSize(impl_->renderer, &width, &height);
}
bool NativeRasterSurface::PrepareFrame(
    std::uint32_t width,
    std::uint32_t height,
    float) {
    return impl_->PrepareFrame(width, height);
}
bool NativeRasterSurface::Present() { return impl_->Present(); }
void NativeRasterSurface::SetBackdropColor(std::uint32_t rgba) {
    impl_->backdrop_color = rgba;
}
void NativeRasterSurface::RequestRecovery() {}
bool NativeRasterSurface::HandleRecoveryEvent(const SDL_Event&) { return false; }
SkCanvas* NativeRasterSurface::Canvas() const {
    return impl_->surface == nullptr ? nullptr : impl_->surface->getCanvas();
}
SkSurface* NativeRasterSurface::Surface() const { return impl_->surface.get(); }
std::uint64_t NativeRasterSurface::Generation() const { return impl_->generation; }
std::uint64_t NativeRasterSurface::RecoveryCount() const { return 0U; }
bool NativeRasterSurface::IsGpuBacked() const { return false; }

} // namespace effindom::v2::native
