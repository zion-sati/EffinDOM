#include "NativeWindowIcon.h"

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

} // namespace effindom::v2::native::tests
