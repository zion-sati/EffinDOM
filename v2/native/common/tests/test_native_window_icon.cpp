#include "NativeWindowIcon.h"
#include "NativeFuiConfig.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace effindom::v2::native::tests {

TEST_CASE("packaged application icon resolves development and bundle layouts",
    "[v2][native][common][icon]") {
    const auto root = std::filesystem::temp_directory_path() / "effindom-native-window-icon";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "resources/app", error);
    std::ofstream(root / "resources/app/application-icon.png") << "fixture";

    CHECK(std::filesystem::equivalent(
        ResolvePackagedApplicationIcon(root / "bin"),
        root / "resources/app/application-icon.png"));

    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Sample.app/Contents/Resources/app", error);
    std::ofstream(root / "Sample.app/Contents/Resources/app/application-icon.png") << "fixture";
    CHECK(std::filesystem::equivalent(
        ResolvePackagedApplicationIcon(root / "Sample.app/Contents/MacOS"),
        root / "Sample.app/Contents/Resources/app/application-icon.png"));
    std::filesystem::remove_all(root, error);
}

TEST_CASE("packaged FUI config resolves native layouts and reads page zoom policy",
    "[v2][native][common][config]") {
    const auto root = std::filesystem::temp_directory_path() / "effindom-native-fui-config";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "share/app", error);
    const auto config = root / "share/app/fui-config.json";
    std::ofstream(config) << R"({"version":1,"application":{"pageZoom":"disabled"}})";
    CHECK(std::filesystem::equivalent(
        ResolvePackagedFuiConfig(root / "bin"), config));
    CHECK(LoadPackagedPageZoomMode(config) == std::optional<bool>{false});
    std::ofstream(config) << R"({"version":1})";
    CHECK_FALSE(LoadPackagedPageZoomMode(config).has_value());
    std::filesystem::remove_all(root, error);
}

} // namespace effindom::v2::native::tests
