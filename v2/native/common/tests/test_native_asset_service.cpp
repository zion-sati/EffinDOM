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
        std::ofstream(path_ / "assets" / "sample file.txt") << "asset";
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

    CHECK(ResolveNativeAssetPath(environment, "assets/sample file.txt") ==
          root.Path() / "assets" / "sample file.txt");
    CHECK(ResolveNativeAssetPath(environment, "file://assets/sample%20file.txt") ==
          root.Path() / "assets" / "sample file.txt");
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

} // namespace effindom::v2::native::tests
