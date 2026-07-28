#pragma once

#include "NativeAccessibility.h"

#include <memory>
#include <string_view>

struct SDL_Window;

namespace effindom::v2::native {

namespace detail {

std::uint32_t MacosAccessibilityUtf16Length(std::string_view utf8);
bool MacosAccessibilityUtf16RangeToCharacterRange(std::string_view utf8,
    std::uint32_t utf16_location, std::uint32_t utf16_length,
    std::uint32_t& character_start, std::uint32_t& character_end);
bool MacosAccessibilityCharacterRangeToUtf16Range(std::string_view utf8,
    std::uint32_t character_start, std::uint32_t character_end,
    std::uint32_t& utf16_location, std::uint32_t& utf16_length);
void* MacosAccessibilityElementForTesting(std::uint64_t handle);

} // namespace detail

std::unique_ptr<NativeAccessibilityAdapter> CreateMacosAccessibilityAdapter(
    SDL_Window* window,
    NativeAccessibilityActionHandler action_handler);

} // namespace effindom::v2::native
