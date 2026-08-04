#include "NativeFuiConfig.h"

#include "SDL3/SDL.h"

#include <array>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>

namespace effindom::v2::native {

std::filesystem::path ResolvePackagedFuiConfig(
    const std::filesystem::path& executable_directory) {
    const std::array candidates = {
        executable_directory / "../Resources/app/fui-config.json",
        executable_directory / "assets/app/fui-config.json",
        executable_directory / "../share/app/fui-config.json",
        executable_directory / "resources/app/fui-config.json",
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

std::filesystem::path FindPackagedFuiConfig() {
    const char* base_path = SDL_GetBasePath();
    return base_path == nullptr ? std::filesystem::path{} : ResolvePackagedFuiConfig(base_path);
}

std::optional<bool> LoadPackagedPageZoomMode(const std::filesystem::path& path) {
    if (path.empty()) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) throw std::runtime_error("could not read " + path.string());
    const std::string source((std::istreambuf_iterator<char>(input)), {});
    const std::regex version(R"("version"\s*:\s*1(?:\s*[,}]))");
    if (!std::regex_search(source, version)) {
        throw std::runtime_error("packaged fui-config.json does not use version 1");
    }
    const std::regex application(R"("application"\s*:\s*\{([^{}]*)\})");
    std::smatch application_match;
    if (!std::regex_search(source, application_match, application)) return std::nullopt;
    const std::string body = application_match[1].str();
    const std::regex page_zoom(R"fui("pageZoom"\s*:\s*"(enabled|disabled)")fui");
    std::smatch page_zoom_match;
    if (!std::regex_search(body, page_zoom_match, page_zoom)) return std::nullopt;
    return page_zoom_match[1].str() == "enabled";
}

} // namespace effindom::v2::native
