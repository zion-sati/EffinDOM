#pragma once

#include <cstdint>
#include <string>

namespace effindom::v2::native {

inline std::string Utf8(const std::uint8_t* bytes, std::uint32_t length) {
    return bytes == nullptr || length == 0U
        ? std::string{}
        : std::string(reinterpret_cast<const char*>(bytes), length);
}

inline std::string Utf8(std::uintptr_t pointer, std::uint32_t length) {
    return Utf8(reinterpret_cast<const std::uint8_t*>(pointer), length);
}

} // namespace effindom::v2::native
