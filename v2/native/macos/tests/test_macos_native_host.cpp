#include "MacosNativeHost.h"
#include "input/MacosInputRouter.h"
#include "input/MacosScrollWheelBridge.h"
#include "effindom_ui.h"
#include "SDL3/SDL.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

using effindom::v2::native::MacosNativeHost;

extern "C" std::uint64_t __fui_native_action_handle();
extern "C" std::uint64_t __fui_native_application_root_handle();
extern "C" std::uint64_t __fui_native_scroll_handle();
extern "C" std::uint64_t __fui_native_body_text_handle();
extern "C" std::uint64_t __fui_native_click_text_handle();
extern "C" std::uint64_t __fui_native_scroll_view_handle();
extern "C" std::uint64_t __fui_native_drop_zone_handle();
extern "C" void __fui_native_schedule_ui_dispatch();
extern "C" void __fui_native_schedule_cancelled_ui_dispatch();
extern "C" std::uint32_t __fui_native_ui_dispatch_count();
extern "C" bool __fui_native_clipboard_roundtrip(const std::uint8_t* text, std::uint32_t length);
extern "C" std::uint64_t __fui_native_start_test_file_dialog();
extern "C" std::uint32_t __fui_native_file_dialog_result_length();
extern "C" std::uint32_t __fui_native_copy_file_dialog_result(std::uint8_t* destination, std::uint32_t capacity);
extern "C" void __fui_native_clear_drop_result();
extern "C" std::uint32_t __fui_native_drop_result_length();
extern "C" std::uint32_t __fui_native_copy_drop_result(std::uint8_t* destination, std::uint32_t capacity);
extern "C" void __fui_native_set_test_image_source(const std::uint8_t* source, std::uint32_t length);
extern "C" std::uint32_t __fui_native_test_image_state();
extern "C" float __fui_native_test_image_width();
extern "C" float __fui_native_test_image_height();
extern "C" void __fui_native_clear_test_image();
extern "C" void fui_load_font(std::uint32_t font_id, std::uintptr_t source, std::uint32_t length);
extern "C" void fui_load_svg(std::uint32_t svg_id, std::uintptr_t source, std::uint32_t length);
extern "C" void fui_release_svg(std::uint32_t svg_id);

namespace {

std::string NativeFileDialogResult() {
    std::string result(__fui_native_file_dialog_result_length(), '\0');
    result.resize(__fui_native_copy_file_dialog_result(
        reinterpret_cast<std::uint8_t*>(result.data()),
        static_cast<std::uint32_t>(result.size())));
    return result;
}

bool PumpUntilFileDialogCompletes(MacosNativeHost& host) {
    for (std::size_t attempt = 0U; attempt < 32U; ++attempt) {
        host.PumpEvent(false);
        if (!NativeFileDialogResult().empty()) return true;
    }
    return false;
}

std::string NativeDropResult() {
    std::string result(__fui_native_drop_result_length(), '\0');
    result.resize(__fui_native_copy_drop_result(
        reinterpret_cast<std::uint8_t*>(result.data()),
        static_cast<std::uint32_t>(result.size())));
    return result;
}

std::string TextDocument(std::uint64_t handle) {
    std::string text(ui_get_text_document_utf8_length(handle), '\0');
    if (!text.empty()) {
        REQUIRE(ui_copy_text_document_utf8(
            handle,
            reinterpret_cast<std::uint8_t*>(text.data()),
            static_cast<std::uint32_t>(text.size())));
    }
    return text;
}

} // namespace

TEST_CASE("native FUI-RS mounts remounts and disposes one application", "[v2][native][macos][n3a]") {
    MacosNativeHost host(false);
    const auto baseline = host.State();
    host.MountApplication();
    host.DrainFrames();
    const auto first = host.State();
    CHECK(first.mount_count == baseline.mount_count + 1U);
    CHECK(first.dispose_count == baseline.dispose_count);

    host.MountApplication();
    host.DrainFrames();
    const auto second = host.State();
    CHECK(second.mount_count == baseline.mount_count + 2U);
    CHECK(second.dispose_count == baseline.dispose_count + 1U);

    host.Unmount();
    const auto disposed = host.State();
    CHECK(disposed.dispose_count == baseline.dispose_count + 2U);
    CHECK(host.IsIdle());
}

TEST_CASE("SDL3 raster presentation is demand driven", "[v2][native][macos][n3b]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto rendered = host.State();
    CHECK(rendered.frame_count > 0U);
    CHECK(host.IsIdle());
    const auto pixels = host.SnapshotRgba();
    REQUIRE_FALSE(pixels.empty());
    CHECK(std::any_of(pixels.begin(), pixels.end(), [](std::uint8_t value) { return value != 0U && value != 255U; }));

    const auto idle_frames = host.State().frame_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK_FALSE(host.RunNextFrame());
    CHECK(host.State().frame_count == idle_frames);
}

TEST_CASE("visible macOS presentation uses demand-driven Skia Metal", "[v2][native][macos][n4]") {
    MacosNativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(host.State().gpu_backed);
    CHECK(host.State().frame_count > 0U);
    host.DrainFrames();
    CHECK(host.IsIdle());
}

TEST_CASE("macOS Metal presentation recreates graphics state", "[v2][native][macos][n4]") {
    MacosNativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto before = host.State();
    REQUIRE(before.gpu_backed);
    REQUIRE(before.frame_count > 0U);

    host.RecreateGraphicsSurface();
    host.DrainFrames();
    const auto recovered = host.State();
    CHECK(recovered.gpu_backed);
    CHECK(recovered.graphics_generation == before.graphics_generation + 1U);
    CHECK(recovered.graphics_recovery_count == before.graphics_recovery_count + 1U);
    CHECK(recovered.frame_count > before.frame_count);
    CHECK(host.IsIdle());
}

TEST_CASE("macOS Metal lifecycle suspends and recovers through SDL window events", "[v2][native][macos][n4]") {
    MacosNativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto initial = host.State();
    REQUIRE(initial.gpu_backed);

    SDL_Event minimized{};
    minimized.type = SDL_EVENT_WINDOW_MINIMIZED;
    REQUIRE(SDL_PushEvent(&minimized));
    for (std::uint32_t attempt = 0U; attempt < 120U && !host.State().presentation_suspended; ++attempt) {
        host.PumpEvent(false);
    }
    CHECK(host.State().presentation_suspended);
    host.RequestFrame();
    CHECK_FALSE(host.RunNextFrame());
    CHECK(host.IsIdle());

    SDL_Event restored{};
    restored.type = SDL_EVENT_WINDOW_RESTORED;
    REQUIRE(SDL_PushEvent(&restored));
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().presentation_suspended; ++attempt) {
        host.PumpEvent(false);
    }
    host.DrainFrames();
    CHECK_FALSE(host.State().presentation_suspended);
    CHECK(host.State().frame_count > initial.frame_count);

}

TEST_CASE("native input, resize, density, and clipboard services are live", "[v2][native][macos][n3c]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto baseline_activations = host.State().activation_count;

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    const auto action_handle = __fui_native_action_handle();
    REQUIRE(ui_get_bounds(action_handle, &x, &y, &width, &height));
    INFO("action=" << action_handle << " bounds=" << x << ',' << y << ',' << width << ',' << height
         << " hit=" << host.HitTest(x + width * 0.5f, y + height * 0.5f)
         << " density-hit=" << host.HitTest(
                (x + width * 0.5f) * host.State().pixel_density,
                (y + height * 0.5f) * host.State().pixel_density));
    REQUIRE(width > 0.0f);
    REQUIRE(height > 0.0f);
    host.DispatchPointer(x + width * 0.5f, y + height * 0.5f, true, 2, 2U);
    host.DispatchPointer(x + width * 0.5f, y + height * 0.5f, false, 2, 0U);
    host.DrainFrames();
    CHECK(host.State().activation_count == baseline_activations);

    host.DispatchPointer(x + width * 0.5f, y + height * 0.5f, true);
    host.DispatchPointer(x + width * 0.5f, y + height * 0.5f, false);
    host.DrainFrames();
    CHECK(host.State().activation_count == baseline_activations + 1U);
    CHECK(TextDocument(__fui_native_click_text_handle()) ==
          "Button clicks: " + std::to_string(baseline_activations + 1U));

    host.DispatchKey("Tab", true);
    host.DispatchKey("Tab", false);
    host.DispatchKey("Enter", true);
    host.DispatchKey("Enter", false);
    host.DispatchPointerMove(24.0f, 24.0f, UI_KEY_MOD_SHIFT);
    host.DispatchWheel(0.0f, 24.0f);
    host.DrainFrames();
    CHECK(host.State().activation_count >= baseline_activations + 1U);

    host.Resize(640U, 420U);
    host.DrainFrames();
    CHECK(host.State().logical_width == 640.0f);
    CHECK(host.State().logical_height == 420.0f);
    CHECK(host.State().pixel_density > 0.0f);

    host.Resize(1200U, 760U);
    host.DrainFrames();
    const auto enlarged = host.State();
    CHECK(enlarged.logical_width == 1200.0f);
    CHECK(enlarged.logical_height == 760.0f);
    float root_x = 0.0f;
    float root_y = 0.0f;
    float root_width = 0.0f;
    float root_height = 0.0f;
    REQUIRE(ui_get_bounds(__fui_native_application_root_handle(), &root_x, &root_y, &root_width, &root_height));
    CHECK(root_width == enlarged.logical_width);
    CHECK(root_height == enlarged.logical_height);
    const auto enlarged_pixels = host.SnapshotRgba();
    REQUIRE_FALSE(enlarged_pixels.empty());
    const auto physical_width = static_cast<std::size_t>(std::lround(enlarged.logical_width * enlarged.pixel_density));
    const auto physical_height = static_cast<std::size_t>(std::lround(enlarged.logical_height * enlarged.pixel_density));
    REQUIRE(physical_width > 8U);
    REQUIRE(physical_height > 8U);
    const auto inset_pixel = ((physical_height - 8U) * physical_width + (physical_width - 8U)) * 4U;
    REQUIRE(inset_pixel + 3U < enlarged_pixels.size());
    const bool is_compositor_white = enlarged_pixels[inset_pixel] == 0xFFU &&
        enlarged_pixels[inset_pixel + 1U] == 0xFFU &&
        enlarged_pixels[inset_pixel + 2U] == 0xFFU;
    const bool is_uncovered_black = enlarged_pixels[inset_pixel] == 0U &&
        enlarged_pixels[inset_pixel + 1U] == 0U &&
        enlarged_pixels[inset_pixel + 2U] == 0U;
    CHECK_FALSE(is_compositor_white);
    CHECK_FALSE(is_uncovered_black);

    const auto frames_before_live_resize = host.State().frame_count;
    SDL_Event live_resize{};
    live_resize.type = SDL_EVENT_WINDOW_EXPOSED;
    live_resize.window.data1 = 1;
    REQUIRE(SDL_PushEvent(&live_resize));
    CHECK(host.State().frame_count > frames_before_live_resize);

    host.SetClipboardText("native clipboard");
    CHECK(host.ClipboardText() == "native clipboard");
}

TEST_CASE("SDL wheel units use one device-independent UI scroll step", "[v2][native][macos][input]") {
    using effindom::v2::native::detail::WheelDeltaToLogicalPixels;
    CHECK(WheelDeltaToLogicalPixels(1.0f) == Catch::Approx(96.0f));
    CHECK(WheelDeltaToLogicalPixels(-1.0f) == Catch::Approx(-96.0f));
    CHECK(WheelDeltaToLogicalPixels(0.1f) == Catch::Approx(9.6f));
    CHECK(WheelDeltaToLogicalPixels(-0.5f) == Catch::Approx(-48.0f));

    using effindom::v2::native::detail::AppKitPreciseDelta;
    CHECK(AppKitPreciseDelta(3.25f, false) == Catch::Approx(3.25f));
    CHECK(AppKitPreciseDelta(3.25f, true) == Catch::Approx(-3.25f));
}

TEST_CASE("mouse drag selection renders before pointer up", "[v2][native][macos][input]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const auto text_handle = __fui_native_body_text_handle();
    REQUIRE(text_handle != 0U);
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    REQUIRE(ui_get_bounds(text_handle, &x, &y, &width, &height));
    REQUIRE(width > 80.0f);
    REQUIRE(height > 0.0f);

    const float pointer_y = y + height * 0.5f;
    host.DispatchPointer(x + 4.0f, pointer_y, true);
    host.DrainFrames();
    const auto before_drag = host.SnapshotRgba();

    host.DispatchPointerMove(x + std::min(width - 4.0f, 240.0f), pointer_y);
    host.DrainFrames();
    const auto during_drag = host.SnapshotRgba();
    CHECK(ui_has_text_selection(text_handle));
    REQUIRE(during_drag.size() == before_drag.size());
    const auto state = host.State();
    const auto framebuffer_width = static_cast<std::size_t>(state.logical_width * state.pixel_density);
    const auto left = static_cast<std::size_t>(std::max(0.0f, x * state.pixel_density));
    const auto top = static_cast<std::size_t>(std::max(0.0f, y * state.pixel_density));
    const auto right = static_cast<std::size_t>(std::max(0.0f, (x + width) * state.pixel_density));
    const auto bottom = static_cast<std::size_t>(std::max(0.0f, (y + height) * state.pixel_density));
    std::size_t changed_selection_pixels = 0U;
    for (std::size_t pixel_y = top; pixel_y < bottom; ++pixel_y) {
        for (std::size_t pixel_x = left; pixel_x < right; ++pixel_x) {
            const std::size_t offset = (pixel_y * framebuffer_width + pixel_x) * 4U;
            if (!std::equal(
                    before_drag.begin() + static_cast<std::ptrdiff_t>(offset),
                    before_drag.begin() + static_cast<std::ptrdiff_t>(offset + 4U),
                    during_drag.begin() + static_cast<std::ptrdiff_t>(offset))) {
                changed_selection_pixels += 1U;
            }
        }
    }
    CHECK(changed_selection_pixels > 20U);

    host.DispatchPointer(x + std::min(width - 4.0f, 240.0f), pointer_y, false);
    host.DrainFrames();
}

TEST_CASE("native UI dispatch wakes the UI thread and cancellation is safe", "[v2][native][macos][n5a]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    while (host.PumpEvent(false)) host.DrainFrames();
    const auto initial_dispatches = __fui_native_ui_dispatch_count();
    const auto initial_frames = host.State().frame_count;

    __fui_native_schedule_ui_dispatch();
    REQUIRE(host.PumpEvent(false));
    host.DrainFrames();
    CHECK(__fui_native_ui_dispatch_count() == initial_dispatches + 1U);
    CHECK(host.State().frame_count == initial_frames + 1U);
    CHECK(host.IsIdle());

    __fui_native_schedule_cancelled_ui_dispatch();
    for (std::uint32_t attempt = 0U; attempt < 8U; ++attempt) host.PumpEvent(false);
    CHECK(__fui_native_ui_dispatch_count() == initial_dispatches + 1U);
    CHECK(host.IsIdle());
}

TEST_CASE("native clipboard and external targets use OS platform services", "[v2][native][macos][n5b]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const std::string clipboard = "Native clipboard: \xE4\xBD\xA0\xE5\xA5\xBD";
    CHECK(__fui_native_clipboard_roundtrip(
        reinterpret_cast<const std::uint8_t*>(clipboard.data()),
        static_cast<std::uint32_t>(clipboard.size())));
    CHECK(host.ClipboardText() == clipboard);

    CHECK(host.OpenExternalUrl("https://effindom.dev/native"));
    CHECK(host.OpenExternalUrl("HTTP://example.com"));
    CHECK_FALSE(host.OpenExternalUrl("file:///tmp/unsafe"));
    CHECK_FALSE(host.OpenExternalUrl("javascript:alert(1)"));
    CHECK_FALSE(host.OpenExternalUrl("not a URL"));

    const auto file = std::filesystem::temp_directory_path() / "effindom-native-open-test.txt";
    {
        std::ofstream output(file);
        output << "native";
    }
    CHECK(host.OpenFile(file));
    CHECK(host.RevealFile(file));
    std::filesystem::remove(file);
    CHECK_FALSE(host.OpenFile(file));
    CHECK_FALSE(host.RevealFile(file));
}

TEST_CASE("native file dialogs marshal selection cancellation and errors to FUI-RS", "[v2][native][macos][n5c]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const auto selected_request = __fui_native_start_test_file_dialog();
    host.CompleteFileDialogForTesting(
        selected_request,
        0U,
        {"/tmp/first.txt", "/tmp/second.md"},
        {},
        0);
    REQUIRE(PumpUntilFileDialogCompletes(host));
    host.DrainFrames();
    CHECK(NativeFileDialogResult() == "selected:2:Some(0)");

    const auto cancelled_request = __fui_native_start_test_file_dialog();
    host.CompleteFileDialogForTesting(cancelled_request, 1U);
    REQUIRE(PumpUntilFileDialogCompletes(host));
    host.DrainFrames();
    CHECK(NativeFileDialogResult() == "cancelled");

    const auto error_request = __fui_native_start_test_file_dialog();
    host.CompleteFileDialogForTesting(error_request, 2U, {}, "dialog failed");
    REQUIRE(PumpUntilFileDialogCompletes(host));
    host.DrainFrames();
    CHECK(NativeFileDialogResult() == "error:dialog failed");
    CHECK(host.IsIdle());
}

TEST_CASE("native SDL drops preserve routing sequence and multi-item payloads", "[v2][native][macos][n5d]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    __fui_native_clear_drop_result();

    host.DispatchDropEventForTesting(SDL_EVENT_DROP_BEGIN, 20.0f, 20.0f);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_POSITION, 20.0f, 20.0f);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_COMPLETE, 20.0f, 20.0f);
    host.DrainFrames();
    CHECK(NativeDropResult().empty());

    const auto drop_zone = __fui_native_drop_zone_handle();
    float drop_x = 0.0f;
    float drop_y = 0.0f;
    float drop_width = 0.0f;
    float drop_height = 0.0f;
    REQUIRE(ui_get_bounds(drop_zone, &drop_x, &drop_y, &drop_width, &drop_height));
    ui_set_scroll_offset(
        __fui_native_scroll_view_handle(),
        0.0f,
        std::max(0.0f, drop_y - 120.0f));
    host.RequestFrame();
    host.DrainFrames();
    REQUIRE(ui_get_visible_bounds(drop_zone, &drop_x, &drop_y, &drop_width, &drop_height));
    REQUIRE(drop_width > 0.0f);
    REQUIRE(drop_height > 0.0f);
    const float target_x = drop_x + drop_width * 0.5f;
    const float target_y = drop_y + drop_height * 0.5f;
    __fui_native_clear_drop_result();

    const auto file = std::filesystem::temp_directory_path() / "effindom-native-drop.txt";
    {
        std::ofstream output(file);
        output << "native drop";
    }

    host.DispatchDropEventForTesting(SDL_EVENT_DROP_BEGIN, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_POSITION, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_FILE, target_x, target_y, file.string());
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_TEXT, target_x, target_y, "https://effindom.dev/drop");
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_COMPLETE, target_x, target_y);
    host.DrainFrames();

    CHECK(NativeDropResult() ==
          "enter,over,over,over,drop:2:file=" + file.string() + ":uri=https://effindom.dev/drop,leave");
    std::filesystem::remove(file);

    __fui_native_clear_drop_result();
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_BEGIN, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_POSITION, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_COMPLETE, target_x, target_y);
    host.DrainFrames();
    CHECK(NativeDropResult() == "enter,over,over,leave");
    CHECK(host.IsIdle());
}

TEST_CASE("native assets load offline report failures release and resolve fallback fonts", "[v2][native][macos][n5e]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const auto baseline_textures = host.TextureCountForTesting();
    const std::string texture_source = "app/demo-texture.png";
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(texture_source.data()),
        static_cast<std::uint32_t>(texture_source.size()));
    host.DrainFrames();
    CHECK(__fui_native_test_image_state() == 2U);
    CHECK(__fui_native_test_image_width() > 0.0f);
    CHECK(__fui_native_test_image_height() > 0.0f);
    CHECK(host.TextureCountForTesting() == baseline_textures);
    __fui_native_clear_test_image();
    CHECK(host.TextureCountForTesting() == baseline_textures);

    const auto repository_root = std::filesystem::path(__FILE__)
                                     .parent_path()
                                     .parent_path()
                                     .parent_path()
                                     .parent_path()
                                     .parent_path();
    const auto unique_texture_path = std::filesystem::temp_directory_path() / "effindom-native-image.png";
    std::filesystem::copy_file(
        repository_root / "public" / "v2" / "fui-rs" / "demo-texture.png",
        unique_texture_path,
        std::filesystem::copy_options::overwrite_existing);
    const std::string unique_texture_source = unique_texture_path.string();
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(unique_texture_source.data()),
        static_cast<std::uint32_t>(unique_texture_source.size()));
    CHECK(__fui_native_test_image_state() == 2U);
    CHECK(host.TextureCountForTesting() == baseline_textures + 1U);
    __fui_native_clear_test_image();
    CHECK(host.TextureCountForTesting() == baseline_textures);
    std::filesystem::remove(unique_texture_path);

    const auto malformed_texture_path = std::filesystem::temp_directory_path() / "effindom-invalid-image.png";
    {
        std::ofstream output(malformed_texture_path, std::ios::binary);
        output << "not an image";
    }
    const std::string malformed_texture_source = malformed_texture_path.string();
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(malformed_texture_source.data()),
        static_cast<std::uint32_t>(malformed_texture_source.size()));
    CHECK(__fui_native_test_image_state() == 3U);
    CHECK(host.TextureCountForTesting() == baseline_textures);
    __fui_native_clear_test_image();
    std::filesystem::remove(malformed_texture_path);

    const std::string missing_source = "https://effindom.dev/not-fetched.png";
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(missing_source.data()),
        static_cast<std::uint32_t>(missing_source.size()));
    CHECK(__fui_native_test_image_state() == 3U);
    __fui_native_clear_test_image();

    const auto svg_path = std::filesystem::temp_directory_path() / "effindom-native-asset.svg";
    {
        std::ofstream output(svg_path);
        output << "<svg xmlns='http://www.w3.org/2000/svg' width='36' height='18'>"
                  "<rect width='36' height='18' fill='#0A84FF'/></svg>";
    }
    const std::string svg_source = svg_path.string();
    fui_load_svg(
        9301U,
        reinterpret_cast<std::uintptr_t>(svg_source.data()),
        static_cast<std::uint32_t>(svg_source.size()));
    const auto svg_size = host.SvgSizeForTesting(9301U);
    REQUIRE(svg_size.has_value());
    CHECK(svg_size->first == Catch::Approx(36.0f));
    CHECK(svg_size->second == Catch::Approx(18.0f));
    fui_release_svg(9301U);
    CHECK_FALSE(host.SvgSizeForTesting(9301U).has_value());
    std::filesystem::remove(svg_path);

    const auto malformed_svg_path = std::filesystem::temp_directory_path() / "effindom-invalid-asset.svg";
    {
        std::ofstream output(malformed_svg_path);
        output << "<svg><broken>";
    }
    const std::string malformed_svg_source = malformed_svg_path.string();
    fui_load_svg(
        9303U,
        reinterpret_cast<std::uintptr_t>(malformed_svg_source.data()),
        static_cast<std::uint32_t>(malformed_svg_source.size()));
    CHECK_FALSE(host.SvgSizeForTesting(9303U).has_value());
    std::filesystem::remove(malformed_svg_path);

    const std::string font_source = "fonts/NotoSansThai-Regular.ttf";
    fui_load_font(
        9302U,
        reinterpret_cast<std::uintptr_t>(font_source.data()),
        static_cast<std::uint32_t>(font_source.size()));
    CHECK(host.HasFontForTesting(9302U));

    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_THAI, "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2");
    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_CJK, "\xE4\xBD\xA0\xE5\xA5\xBD");
    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_SUPPLEMENTAL, "\xF0\x9F\x98\x80");

    bool has_cjk_glyph = false;
    bool has_emoji_glyph = false;
    for (std::uint32_t attempt = 0U; attempt < 100U && (!has_cjk_glyph || !has_emoji_glyph); ++attempt) {
        host.DrainFrames();
        std::uint32_t live_fallback_count = 0U;
        const std::uint32_t* live_fallbacks = ui_get_live_fallback_font_buffer(&live_fallback_count);
        for (std::uint32_t index = 0U; index < live_fallback_count; ++index) {
            has_cjk_glyph = has_cjk_glyph || host.FontHasGlyphForTesting(live_fallbacks[index], 0x4F60U);
            has_emoji_glyph = has_emoji_glyph || host.FontHasGlyphForTesting(live_fallbacks[index], 0x1F600U);
        }
        if (!has_cjk_glyph || !has_emoji_glyph) SDL_Delay(5U);
    }
    CHECK(has_cjk_glyph);
    CHECK(has_emoji_glyph);
    const auto fallback_count = host.FallbackFontCountForTesting();
    REQUIRE(fallback_count >= 3U);

    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_THAI, "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2");
    host.DrainFrames();
    CHECK(host.FallbackFontCountForTesting() == fallback_count);

    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_CJK, "\xE4\xBD\xA0\xE5\xA5\xBD");
    host.DrainFrames();
    CHECK(host.FallbackFontCountForTesting() == fallback_count);

    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_SUPPLEMENTAL, "\xF0\x9F\x98\x80");
    host.DrainFrames();
    CHECK(host.FallbackFontCountForTesting() == fallback_count);
    CHECK(host.IsIdle());
}

TEST_CASE("native application remount is deterministic and lifecycle remains idle", "[v2][native][macos][n3d]") {
    MacosNativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto first = host.SnapshotRgba();
    host.MountApplication();
    host.DrainFrames();
    const auto remounted = host.SnapshotRgba();
    REQUIRE(first.size() == remounted.size());
    CHECK(first == remounted);
    CHECK(host.IsIdle());
}
