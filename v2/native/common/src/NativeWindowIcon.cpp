#include "NativeWindowIcon.h"

#include "SDL3/SDL.h"
#include "include/codec/SkCodec.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"

#include <array>
#include <vector>

namespace effindom::v2::native {

std::filesystem::path ResolvePackagedApplicationIcon(
    const std::filesystem::path& executable_directory) {
    const std::array candidates = {
        executable_directory / "../Resources/app/application-icon.png",
        executable_directory / "../resources/app/application-icon.png",
        executable_directory / "resources/app/application-icon.png",
    };
    for (const auto& candidate : candidates) {
        std::error_code error;
        const auto normalized = std::filesystem::weakly_canonical(candidate, error);
        const auto& path = error ? candidate : normalized;
        error.clear();
        if (std::filesystem::is_regular_file(path, error)) return path;
    }
    return {};
}

std::filesystem::path FindPackagedApplicationIcon() {
    const char* base_path = SDL_GetBasePath();
    return base_path == nullptr
        ? std::filesystem::path{}
        : ResolvePackagedApplicationIcon(base_path);
}

bool ApplySdlWindowIcon(
    SDL_Window* window,
    const std::filesystem::path& path,
    std::string& error) {
    if (window == nullptr) {
        error = "native window is unavailable";
        return false;
    }
    sk_sp<SkData> data = SkData::MakeFromFileName(path.string().c_str());
    if (data == nullptr) {
        error = "could not read application icon " + path.string();
        return false;
    }
    std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(data);
    if (codec == nullptr) {
        error = "could not decode application icon " + path.string();
        return false;
    }
    const SkImageInfo info = SkImageInfo::Make(
        codec->dimensions().width(), codec->dimensions().height(),
        kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(info.computeMinByteSize());
    if (codec->getPixels(info, pixels.data(), info.minRowBytes()) != SkCodec::kSuccess) {
        error = "could not decode application icon pixels " + path.string();
        return false;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        info.width(), info.height(), SDL_PIXELFORMAT_RGBA32,
        pixels.data(), static_cast<int>(info.minRowBytes()));
    if (surface == nullptr) {
        error = std::string("could not create application icon surface: ") + SDL_GetError();
        return false;
    }
    const bool applied = SDL_SetWindowIcon(window, surface);
    if (!applied) error = std::string("could not set application icon: ") + SDL_GetError();
    SDL_DestroySurface(surface);
    return applied;
}

} // namespace effindom::v2::native
