#include "MacosAssetEnvironment.h"

#include <CoreText/CoreText.h>
#include <mach-o/dyld.h>

#include <array>
#include <vector>

namespace effindom::v2::native {
namespace {

std::filesystem::path ExecutableDirectory() {
    std::uint32_t size = 0U;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0U) return {};
    std::vector<char> path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path.data(), error);
    return (error ? std::filesystem::path(path.data()) : canonical).parent_path();
}

std::string Utf8(CFStringRef value) {
    if (value == nullptr) return {};
    const CFIndex length = CFStringGetLength(value);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<std::size_t>(capacity), '\0');
    if (!CFStringGetCString(value, result.data(), capacity, kCFStringEncodingUTF8)) return {};
    result.resize(std::char_traits<char>::length(result.c_str()));
    return result;
}

NativeSystemFontSource SourceFromFont(CTFontRef font) {
    if (font == nullptr) return {};
    CFTypeRef attribute = CTFontCopyAttribute(font, kCTFontURLAttribute);
    CFStringRef selected_name = CTFontCopyPostScriptName(font);
    if (attribute == nullptr || CFGetTypeID(attribute) != CFURLGetTypeID()) {
        if (attribute != nullptr) CFRelease(attribute);
        if (selected_name != nullptr) CFRelease(selected_name);
        return {};
    }
    const std::string postscript_name = Utf8(selected_name);
    if (selected_name != nullptr) CFRelease(selected_name);
    std::array<UInt8, 4096> path{};
    const bool resolved = CFURLGetFileSystemRepresentation(
        static_cast<CFURLRef>(attribute), true, path.data(), path.size());
    CFRelease(attribute);
    return resolved
        ? NativeSystemFontSource{
              std::filesystem::path(reinterpret_cast<const char*>(path.data())),
              postscript_name,
              0U}
        : NativeSystemFontSource{};
}

NativeSystemFontSource ResolveSystemFont(std::string_view sample_text) {
    if (sample_text.empty()) return {};
    CFStringRef text = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(sample_text.data()),
        static_cast<CFIndex>(sample_text.size()),
        kCFStringEncodingUTF8,
        false);
    if (text == nullptr) return {};
    CTFontRef base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 16.0, nullptr);
    CTFontRef fallback = base == nullptr
        ? nullptr
        : CTFontCreateForString(base, text, CFRangeMake(0, CFStringGetLength(text)));
    if (base != nullptr) CFRelease(base);
    NativeSystemFontSource selected = SourceFromFont(fallback);
    if (fallback != nullptr) CFRelease(fallback);
    if (!selected.path.empty() && selected.path.string().find("/PrivateFrameworks/") == std::string::npos) {
        CFRelease(text);
        return selected;
    }

    CFCharacterSetRef characters = CFCharacterSetCreateWithCharactersInString(kCFAllocatorDefault, text);
    const void* keys[] = {kCTFontCharacterSetAttribute};
    const void* values[] = {characters};
    CFDictionaryRef attributes = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CTFontDescriptorRef query = CTFontDescriptorCreateWithAttributes(attributes);
    CFSetRef mandatory = CFSetCreate(kCFAllocatorDefault, keys, 1, &kCFTypeSetCallBacks);
    CFArrayRef matches = CTFontDescriptorCreateMatchingFontDescriptors(query, mandatory);
    if (matches != nullptr) {
        const CFIndex count = CFArrayGetCount(matches);
        for (CFIndex index = 0; index < count; ++index) {
            auto* descriptor = static_cast<CTFontDescriptorRef>(
                const_cast<void*>(CFArrayGetValueAtIndex(matches, index)));
            CTFontRef candidate_font = CTFontCreateWithFontDescriptor(descriptor, 16.0, nullptr);
            NativeSystemFontSource candidate = SourceFromFont(candidate_font);
            if (candidate_font != nullptr) CFRelease(candidate_font);
            if (!candidate.path.empty() &&
                candidate.path.string().find("/PrivateFrameworks/") == std::string::npos) {
                selected = std::move(candidate);
                break;
            }
        }
        CFRelease(matches);
    }
    if (mandatory != nullptr) CFRelease(mandatory);
    if (query != nullptr) CFRelease(query);
    if (attributes != nullptr) CFRelease(attributes);
    if (characters != nullptr) CFRelease(characters);
    CFRelease(text);
    return selected;
}

} // namespace

NativeAssetEnvironment CreateMacosAssetEnvironment() {
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
    environment.use_symbol_font_for_non_emoji_supplemental = true;
    return environment;
}

} // namespace effindom::v2::native
