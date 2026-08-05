#include <atomic>
#include <cstdint>

namespace effindom::v2::native::tests {
std::atomic_uint32_t loaded_texture_callbacks{0U};
std::atomic_uint32_t failed_texture_callbacks{0U};
std::atomic_uint32_t loaded_svg_callbacks{0U};
std::atomic_uint32_t failed_svg_callbacks{0U};

void ResetNativeAssetCallbackCounts() {
    loaded_texture_callbacks.store(0U);
    failed_texture_callbacks.store(0U);
    loaded_svg_callbacks.store(0U);
    failed_svg_callbacks.store(0U);
}
} // namespace effindom::v2::native::tests

extern "C" {
void __fui_on_font_loaded(std::uint32_t) {}
void __fui_on_svg_loaded(std::uint32_t, float, float) {
    ++effindom::v2::native::tests::loaded_svg_callbacks;
}
void __fui_on_svg_failed(std::uint32_t, const std::uint8_t*, std::uint32_t) {
    ++effindom::v2::native::tests::failed_svg_callbacks;
}
void __fui_on_texture_loaded(std::uint32_t, float, float) {
    ++effindom::v2::native::tests::loaded_texture_callbacks;
}
void __fui_on_texture_failed(std::uint32_t, const std::uint8_t*, std::uint32_t) {
    ++effindom::v2::native::tests::failed_texture_callbacks;
}
}
