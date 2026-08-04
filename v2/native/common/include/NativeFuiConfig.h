#pragma once

#include <filesystem>
#include <optional>

namespace effindom::v2::native {

std::filesystem::path ResolvePackagedFuiConfig(
    const std::filesystem::path& executable_directory);
std::filesystem::path FindPackagedFuiConfig();
std::optional<bool> LoadPackagedPageZoomMode(const std::filesystem::path& path);

} // namespace effindom::v2::native
