#include "LinuxAssetEnvironment.h"

#include <fontconfig/fontconfig.h>
#include <unistd.h>

#include <array>
#include <limits.h>
#include <string>

namespace effindom::v2::native {
namespace {

std::filesystem::path ExecutableDirectory() {
    std::array<char, PATH_MAX> path{};
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1U);
    if (length <= 0) return {};
    path[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(path.data()).parent_path();
}

std::uint32_t FirstScalar(std::string_view text) {
    if (text.empty()) return 0U;
    const auto first = static_cast<std::uint8_t>(text[0]);
    if ((first & 0xF8U) == 0xF0U && text.size() >= 4U) {
        return ((first & 0x07U) << 18U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 12U) |
            ((static_cast<std::uint8_t>(text[2]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[3]) & 0x3FU);
    }
    if ((first & 0xF0U) == 0xE0U && text.size() >= 3U) {
        return ((first & 0x0FU) << 12U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[2]) & 0x3FU);
    }
    if ((first & 0xE0U) == 0xC0U && text.size() >= 2U) {
        return ((first & 0x1FU) << 6U) | (static_cast<std::uint8_t>(text[1]) & 0x3FU);
    }
    return first;
}

NativeSystemFontSource ResolveSystemFont(std::string_view sample_text) {
    const std::uint32_t scalar = FirstScalar(sample_text);
    if (scalar == 0U || !FcInit()) return {};

    FcPattern* pattern = FcPatternCreate();
    FcCharSet* charset = FcCharSetCreate();
    if (pattern == nullptr || charset == nullptr) {
        if (charset != nullptr) FcCharSetDestroy(charset);
        if (pattern != nullptr) FcPatternDestroy(pattern);
        return {};
    }
    FcCharSetAddChar(charset, scalar);
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    FcCharSetDestroy(charset);
    FcPatternDestroy(pattern);
    if (match == nullptr) return {};

    FcChar8* file = nullptr;
    int face_index = 0;
    const bool has_file = FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch;
    (void)FcPatternGetInteger(match, FC_INDEX, 0, &face_index);
    NativeSystemFontSource source;
    if (has_file && file != nullptr) {
        source.path = std::filesystem::path(reinterpret_cast<const char*>(file));
        source.required_scalar = scalar;
        source.face_index = face_index > 0 ? static_cast<std::uint32_t>(face_index) : 0U;
    }
    FcPatternDestroy(match);
    return source;
}

} // namespace

NativeAssetEnvironment CreateLinuxAssetEnvironment() {
    NativeAssetEnvironment environment;
    const std::filesystem::path executable = ExecutableDirectory();
    if (!executable.empty()) {
        environment.search_roots.push_back(executable);
        environment.search_roots.push_back(executable.parent_path() / "resources");
        environment.search_roots.push_back(executable.parent_path() / "resources" / "effindom");
    }
    std::error_code error;
    environment.search_roots.push_back(std::filesystem::current_path(error));
    environment.path_from_utf8 = [](std::string_view value) {
        return std::filesystem::path(std::string(value));
    };
    environment.resolve_system_font = ResolveSystemFont;
    environment.use_symbol_font_for_non_emoji_supplemental = false;
    return environment;
}

} // namespace effindom::v2::native
