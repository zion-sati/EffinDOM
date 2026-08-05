#include "Engine.h"
#include "NativeAssetService.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace effindom::v2::native::tests {
extern std::atomic_uint32_t loaded_texture_callbacks;
extern std::atomic_uint32_t failed_texture_callbacks;
extern std::atomic_uint32_t loaded_svg_callbacks;
extern std::atomic_uint32_t failed_svg_callbacks;
void ResetNativeAssetCallbackCounts();

namespace {

constexpr std::array<std::uint8_t, 68> kOnePixelPng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x04, 0x00, 0x00, 0x00, 0xB5, 0x1C, 0x0C,
    0x02, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41,
    0x54, 0x78, 0xDA, 0x63, 0x64, 0xF8, 0x0F, 0x00,
    0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xE3, 0x66,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
    0xAE, 0x42, 0x60, 0x82,
};

NativeHttpResponse SuccessfulPngResponse() {
    NativeHttpResponse response;
    response.status = 200;
    response.content_type = "image/png";
    response.bytes.assign(kOnePixelPng.begin(), kOnePixelPng.end());
    return response;
}

void Drain(NativeAssetService& service) {
    for (std::uint32_t attempt = 0U; attempt < 1000U; ++attempt) {
        if (!service.ProcessPendingAssets()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FAIL("native remote asset request did not settle");
}

} // namespace

TEST_CASE("native remote assets share transfers and reuse decoded response bytes", "[v2][native][common][assets][remote]") {
    ResetNativeAssetCallbackCounts();
    std::atomic_uint32_t fetches{0U};
    NativeAssetEnvironment environment;
    environment.fetch_remote_asset = [&](std::string_view, const std::shared_ptr<std::atomic_bool>&) {
        ++fetches;
        return SuccessfulPngResponse();
    };
    Engine engine;
    NativeAssetService service(engine, [] {}, std::move(environment));

    REQUIRE(service.LoadTexture(7101U, "https://example.invalid/shared.png"));
    REQUIRE(service.LoadTexture(7102U, "https://example.invalid/shared.png"));
    Drain(service);
    CHECK(fetches.load() == 1U);
    CHECK(loaded_texture_callbacks.load() == 2U);
    CHECK(failed_texture_callbacks.load() == 0U);

    REQUIRE(service.LoadTexture(7103U, "https://example.invalid/shared.png"));
    CHECK(fetches.load() == 1U);
    CHECK(loaded_texture_callbacks.load() == 3U);
}

TEST_CASE("releasing the last remote asset consumer cancels completion", "[v2][native][common][assets][remote]") {
    ResetNativeAssetCallbackCounts();
    NativeAssetEnvironment environment;
    environment.fetch_remote_asset = [](std::string_view, const std::shared_ptr<std::atomic_bool>& cancelled) {
        while (!cancelled->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        NativeHttpResponse response;
        response.cancelled = true;
        return response;
    };
    Engine engine;
    NativeAssetService service(engine, [] {}, std::move(environment));

    REQUIRE(service.LoadTexture(7201U, "https://example.invalid/cancel.png"));
    service.ReleaseTexture(7201U);
    Drain(service);
    CHECK(loaded_texture_callbacks.load() == 0U);
    CHECK(failed_texture_callbacks.load() == 0U);
}

TEST_CASE("native remote SVG responses use the same asynchronous asset lifecycle", "[v2][native][common][assets][remote]") {
    ResetNativeAssetCallbackCounts();
    NativeAssetEnvironment environment;
    environment.fetch_remote_asset = [](std::string_view, const std::shared_ptr<std::atomic_bool>&) {
        constexpr std::string_view svg =
            "<svg xmlns='http://www.w3.org/2000/svg' width='2' height='3'><rect width='2' height='3'/></svg>";
        NativeHttpResponse response;
        response.status = 200;
        response.content_type = "image/svg+xml; charset=utf-8";
        response.bytes.assign(svg.begin(), svg.end());
        return response;
    };
    Engine engine;
    NativeAssetService service(engine, [] {}, std::move(environment));

    REQUIRE(service.LoadSvg(7301U, "https://example.invalid/fixture.svg"));
    Drain(service);
    CHECK(loaded_svg_callbacks.load() == 1U);
    CHECK(failed_svg_callbacks.load() == 0U);
}

} // namespace effindom::v2::native::tests
