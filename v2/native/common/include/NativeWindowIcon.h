#pragma once

#include <filesystem>
#include <string>

struct SDL_Window;

namespace effindom::v2::native {

std::filesystem::path ResolvePackagedApplicationIcon(
    const std::filesystem::path& executable_directory);
std::filesystem::path FindPackagedApplicationIcon();
bool ApplySdlWindowIcon(
    SDL_Window* window,
    const std::filesystem::path& path,
    std::string& error);

} // namespace effindom::v2::native
