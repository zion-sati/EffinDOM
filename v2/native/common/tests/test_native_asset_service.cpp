#include "NativeAssetService.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace effindom::v2::native::tests {
namespace {

class TemporaryAssetRoot final {
public:
    TemporaryAssetRoot()
        : path_(std::filesystem::temp_directory_path() / "effindom-native-asset-service") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_ / "assets", error);
        std::filesystem::create_directories(path_ / "app", error);
        std::filesystem::create_directories(path_ / "fonts", error);
        std::ofstream(path_ / "assets" / "sample file.txt") << "asset";
        std::ofstream(path_ / "app" / "root-texture.png") << "texture";
        std::ofstream(path_ / "fonts" / "root-font.ttf") << "font";
    }

    ~TemporaryAssetRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("native asset locator resolves roots and encoded file sources", "[v2][native][common][assets]") {
    TemporaryAssetRoot root;
    NativeAssetEnvironment environment;
    environment.search_roots = {root.Path()};
    environment.path_from_utf8 = [](std::string_view value) {
        return std::filesystem::path(std::string(value));
    };

    CHECK(std::filesystem::equivalent(
        ResolveNativeAssetPath(environment, "assets/sample file.txt"),
        root.Path() / "assets" / "sample file.txt"));
    CHECK(std::filesystem::equivalent(
        ResolveNativeAssetPath(environment, "file://assets/sample%20file.txt"),
        root.Path() / "assets" / "sample file.txt"));
    CHECK(std::filesystem::equivalent(
        ResolveNativeAssetPath(environment, "/root-texture.png"),
        root.Path() / "app" / "root-texture.png"));
    CHECK(std::filesystem::equivalent(
        ResolveNativeAssetPath(environment, "/root-font.ttf"),
        root.Path() / "fonts" / "root-font.ttf"));
    CHECK(std::filesystem::equivalent(
        ResolveNativeAssetPath(environment, "/v2/fui-rs/fonts/root-font.ttf"),
        root.Path() / "fonts" / "root-font.ttf"));
    CHECK(ResolveNativeAssetPath(environment, "assets/missing.txt").empty());
    CHECK(ResolveNativeAssetPath(environment, "https://effindom.dev/asset").empty());
    CHECK(ResolveNativeAssetPath(environment, "data:text/plain,asset").empty());
}

TEST_CASE("native asset environment delegates system-font discovery", "[v2][native][common][assets]") {
    NativeAssetEnvironment environment;
    environment.resolve_system_font = [](std::string_view sample) {
        return NativeSystemFontSource{
            std::filesystem::path("font.ttc"),
            sample == "sample" ? "ExpectedFace" : "",
            0U};
    };

    const NativeSystemFontSource resolved = environment.resolve_system_font("sample");
    CHECK(resolved.path == std::filesystem::path("font.ttc"));
    CHECK(resolved.postscript_name == "ExpectedFace");
}

TEST_CASE("native packaged asset roots survive relocation without current-directory fallback", "[v2][native][common][assets]") {
    const auto root = std::filesystem::temp_directory_path() / "effindom-native-relocated-assets";
    const auto moved = std::filesystem::temp_directory_path() / "effindom-native-relocated-assets-moved";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(moved, error);

    const auto mac_executable = root / "Sample.app/Contents/MacOS";
    std::filesystem::create_directories(root / "Sample.app/Contents/Resources/effindom/fonts");
    std::filesystem::create_directories(root / "Sample.app/Contents/Resources/app");
    std::ofstream(root / "Sample.app/Contents/Resources/effindom/fonts/default.ttf") << "font";
    std::ofstream(root / "Sample.app/Contents/Resources/app/texture.png") << "texture";
    std::filesystem::rename(root, moved);

    NativeAssetEnvironment environment;
    environment.search_roots = BuildNativeAssetSearchRoots(
        moved / "Sample.app/Contents/MacOS", NativePackagePlatform::MacOs);
    CHECK(std::filesystem::equivalent(
        ResolveNativeAssetPath(environment, "fonts/default.ttf"),
        moved / "Sample.app/Contents/Resources/effindom/fonts/default.ttf"));
    CHECK(std::filesystem::equivalent(
        ResolveNativeAssetPath(environment, "app/texture.png"),
        moved / "Sample.app/Contents/Resources/app/texture.png"));
    CHECK(ResolveNativeAssetPath(environment, "../outside.txt").empty());

    const auto windows = BuildNativeAssetSearchRoots(
        moved / "Sample", NativePackagePlatform::Windows);
    CHECK(windows[1] == moved / "Sample/assets");
    const auto linux = BuildNativeAssetSearchRoots(
        moved / "Sample/bin", NativePackagePlatform::Linux);
    CHECK(linux[1] == moved / "Sample/share");
    std::filesystem::remove_all(moved, error);
}

} // namespace effindom::v2::native::tests
