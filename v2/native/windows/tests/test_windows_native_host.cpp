#include "NativeHost.h"
#include "NativeHostCharacterization.h"
#include "NativeInputTypes.h"
#include "UiRuntime.h"
#include "input/WindowsScrollWheelBridge.h"
#include "platform/WindowsSystemThemeBridge.h"
#include "effindom_ui.h"
#include "SDL3/SDL.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using effindom::v2::native::NativeHost;

TEST_CASE("Windows native host satisfies the shared characterization contract", "[v2][native][windows][characterization]") {
    effindom::v2::native::tests::CharacterizeNativeHost<NativeHost>();
}

extern "C" std::uint64_t __fui_native_action_handle();
extern "C" std::uint64_t __fui_native_application_root_handle();
extern "C" std::uint64_t __fui_native_body_text_handle();
extern "C" std::uint64_t __fui_native_click_text_handle();
extern "C" std::uint64_t __fui_native_context_editor_handle();
extern "C" bool __fui_native_context_menu_visible();
extern "C" std::uint32_t fui_get_platform_family();
extern "C" std::uint32_t fui_get_accent_color();
extern "C" std::uint64_t __fui_native_scroll_view_handle();
extern "C" std::uint64_t __fui_native_drop_zone_handle();
extern "C" void __fui_native_schedule_ui_dispatch();
extern "C" void __fui_native_schedule_cancelled_ui_dispatch();
extern "C" std::uint32_t __fui_native_ui_dispatch_count();
extern "C" bool __fui_native_clipboard_roundtrip(const std::uint8_t*, std::uint32_t);
extern "C" std::uint64_t __fui_native_start_test_file_dialog();
extern "C" std::uint32_t __fui_native_file_dialog_result_length();
extern "C" std::uint32_t __fui_native_copy_file_dialog_result(std::uint8_t*, std::uint32_t);
extern "C" void __fui_native_clear_drop_result();
extern "C" std::uint32_t __fui_native_drop_result_length();
extern "C" std::uint32_t __fui_native_copy_drop_result(std::uint8_t*, std::uint32_t);
extern "C" void __fui_native_set_test_image_source(const std::uint8_t*, std::uint32_t);
extern "C" std::uint32_t __fui_native_test_image_state();
extern "C" float __fui_native_test_image_width();
extern "C" float __fui_native_test_image_height();
extern "C" void __fui_native_clear_test_image();
extern "C" void fui_load_font(std::uint32_t, std::uintptr_t, std::uint32_t);
extern "C" void fui_load_svg(std::uint32_t, std::uintptr_t, std::uint32_t);
extern "C" void fui_release_svg(std::uint32_t);

namespace {

struct LiveResizePaintObserver {
    NativeHost* host = nullptr;
    bool sizing = false;
    std::uint32_t live_paint_count = 0U;
    float maximum_live_width = 0.0f;

    static LRESULT CALLBACK Procedure(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        UINT_PTR subclass_id,
        DWORD_PTR reference) {
        auto& observer = *reinterpret_cast<LiveResizePaintObserver*>(reference);
        if (message == WM_ENTERSIZEMOVE) observer.sizing = true;
        if (message == WM_PAINT) {
            const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
            if (observer.sizing) {
                ++observer.live_paint_count;
                observer.maximum_live_width = std::max(
                    observer.maximum_live_width,
                    observer.host->State().logical_width);
            }
            return result;
        }
        if (message == WM_EXITSIZEMOVE) {
            const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
            observer.sizing = false;
            return result;
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, &Procedure, subclass_id);
        }
        return DefSubclassProc(window, message, wparam, lparam);
    }
};

std::string NativeFileDialogResult() {
    std::string result(__fui_native_file_dialog_result_length(), '\0');
    result.resize(__fui_native_copy_file_dialog_result(
        reinterpret_cast<std::uint8_t*>(result.data()),
        static_cast<std::uint32_t>(result.size())));
    return result;
}

TEST_CASE("Windows colorization colors are packed as opaque EffinDOM RGBA",
          "[v2][native][windows][theme]") {
    using effindom::v2::native::detail::PackWindowsColorizationColor;
    CHECK(PackWindowsColorizationColor(0xAA112233U) == 0x112233FFU);
    CHECK(PackWindowsColorizationColor(0x00123456U) == 0x123456FFU);
    CHECK(PackWindowsColorizationColor(0xFFFFFFFFU) == 0xFFFFFFFFU);
}

TEST_CASE("Windows accent state falls back and suppresses duplicate changes",
          "[v2][native][windows][theme]") {
    using effindom::v2::native::detail::kWindowsAccentColorFallback;
    using effindom::v2::native::detail::WindowsAccentColorState;

    std::optional<std::uint32_t> source;
    std::vector<std::uint32_t> changes;
    WindowsAccentColorState state(
        [&source] { return source; },
        [&changes](std::uint32_t color) { changes.push_back(color); });

    CHECK(state.Current() == kWindowsAccentColorFallback);
    CHECK_FALSE(state.Refresh());
    source = kWindowsAccentColorFallback;
    CHECK_FALSE(state.Refresh());
    source = 0x123456FFU;
    CHECK(state.Refresh());
    CHECK(state.Current() == 0x123456FFU);
    REQUIRE(changes.size() == 1U);
    CHECK(changes.front() == 0x123456FFU);
    CHECK_FALSE(state.Refresh());
    CHECK(changes.size() == 1U);
    source.reset();
    CHECK_FALSE(state.Refresh());
    CHECK(state.Current() == 0x123456FFU);
}

TEST_CASE("Windows native accent host service reflects the DWM source",
          "[v2][native][windows][theme]") {
    using effindom::v2::native::detail::kWindowsAccentColorFallback;
    using effindom::v2::native::detail::ReadWindowsAccentColor;

    NativeHost host(false);
    const std::uint32_t expected = ReadWindowsAccentColor().value_or(kWindowsAccentColorFallback);
    CHECK(fui_get_accent_color() == expected);
    CHECK((fui_get_accent_color() & 0xFFU) == 0xFFU);
}

bool PumpUntilFileDialogCompletes(NativeHost& host) {
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

std::uint64_t FindEditableTextDescendant(std::uint64_t handle) {
    const auto* node = effindom::v2::ui::GetRuntime().Resolve(handle);
    if (node == nullptr) return UI_INVALID_HANDLE;
    if (node->is_text_node && node->is_editable) return handle;
    for (const auto child : node->children) {
        const auto result = FindEditableTextDescendant(child);
        if (result != UI_INVALID_HANDLE) return result;
    }
    return UI_INVALID_HANDLE;
}

std::uint64_t FindSemanticDescendant(std::uint64_t handle, const std::string& label) {
    const auto* node = effindom::v2::ui::GetRuntime().Resolve(handle);
    if (node == nullptr) return UI_INVALID_HANDLE;
    if (node->semantic_label == label) return handle;
    for (const auto child : node->children) {
        const auto result = FindSemanticDescendant(child, label);
        if (result != UI_INVALID_HANDLE) return result;
    }
    return UI_INVALID_HANDLE;
}

TEST_CASE("Windows UI dispatch wakes the UI thread and cancellation is safe", "[v2][native][windows][w8a]") {
    NativeHost host(false);
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

TEST_CASE("Windows clipboard and external targets use OS platform services", "[v2][native][windows][w8b]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const std::string clipboard = "Native clipboard: \xE4\xBD\xA0\xE5\xA5\xBD";
    CHECK(__fui_native_clipboard_roundtrip(
        reinterpret_cast<const std::uint8_t*>(clipboard.data()),
        static_cast<std::uint32_t>(clipboard.size())));
    CHECK(host.ClipboardText() == clipboard);
    CHECK(host.OpenExternalUrl("https://effindom.dev/native"));
    CHECK(host.OpenExternalUrl("HTTP://example.com"));
    CHECK_FALSE(host.OpenExternalUrl("file:///C:/unsafe"));
    CHECK_FALSE(host.OpenExternalUrl("javascript:alert(1)"));
    CHECK_FALSE(host.OpenExternalUrl("not a URL"));

    const auto file = std::filesystem::temp_directory_path() / L"effindom-native-open-\x4F60\x597D.txt";
    { std::ofstream output(file); output << "native"; }
    CHECK(host.OpenFile(file));
    CHECK(host.RevealFile(file));
    std::filesystem::remove(file);
    CHECK_FALSE(host.OpenFile(file));
    CHECK_FALSE(host.RevealFile(file));
}

TEST_CASE("Windows file dialogs marshal selection cancellation and errors", "[v2][native][windows][w8c]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const auto selected = __fui_native_start_test_file_dialog();
    host.CompleteFileDialogForTesting(selected, 0U, {"C:/first.txt", "C:/second.md"}, {}, 0);
    REQUIRE(PumpUntilFileDialogCompletes(host));
    host.DrainFrames();
    CHECK(NativeFileDialogResult() == "selected:2:Some(0)");

    const auto cancelled = __fui_native_start_test_file_dialog();
    host.CompleteFileDialogForTesting(cancelled, 1U);
    REQUIRE(PumpUntilFileDialogCompletes(host));
    host.DrainFrames();
    CHECK(NativeFileDialogResult() == "cancelled");

    const auto failed = __fui_native_start_test_file_dialog();
    host.CompleteFileDialogForTesting(failed, 2U, {}, "dialog failed");
    REQUIRE(PumpUntilFileDialogCompletes(host));
    host.DrainFrames();
    CHECK(NativeFileDialogResult() == "error:dialog failed");
    CHECK(host.IsIdle());
}

TEST_CASE("Windows SDL drops preserve routing and multi-item payloads", "[v2][native][windows][w8d]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    __fui_native_clear_drop_result();

    host.DispatchDropEventForTesting(SDL_EVENT_DROP_BEGIN, 20.0f, 20.0f);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_POSITION, 20.0f, 20.0f);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_COMPLETE, 20.0f, 20.0f);
    host.DrainFrames();
    CHECK(NativeDropResult().empty());

    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    REQUIRE(ui_get_bounds(__fui_native_drop_zone_handle(), &x, &y, &width, &height));
    ui_set_scroll_offset(__fui_native_scroll_view_handle(), 0.0f, std::max(0.0f, y - 120.0f));
    host.RequestFrame();
    host.DrainFrames();
    REQUIRE(ui_get_visible_bounds(__fui_native_drop_zone_handle(), &x, &y, &width, &height));
    REQUIRE(width > 0.0f);
    REQUIRE(height > 0.0f);
    const float target_x = x + width * 0.5f;
    const float target_y = y + height * 0.5f;

    const auto file = std::filesystem::temp_directory_path() / L"effindom-drop-\x4F60\x597D.txt";
    { std::ofstream output(file); output << "drop"; }
    const std::string file_utf8 = file.u8string();
    __fui_native_clear_drop_result();
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_BEGIN, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_POSITION, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_FILE, target_x, target_y, file_utf8);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_TEXT, target_x, target_y, "https://effindom.dev/drop");
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_COMPLETE, target_x, target_y);
    host.DrainFrames();
    CHECK(NativeDropResult() ==
        "enter,over,over,over,drop:2:file=" + file_utf8 + ":uri=https://effindom.dev/drop,leave");
    std::filesystem::remove(file);

    __fui_native_clear_drop_result();
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_BEGIN, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_POSITION, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_COMPLETE, target_x, target_y);
    host.DrainFrames();
    CHECK(NativeDropResult() == "enter,over,over,leave");
    CHECK(host.IsIdle());
}

TEST_CASE("Windows assets load offline report failures release and resolve fallback fonts", "[v2][native][windows][w8e]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const auto baseline_textures = host.TextureCountForTesting();
    const std::string packaged_texture = "app/demo-texture.png";
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(packaged_texture.data()),
        static_cast<std::uint32_t>(packaged_texture.size()));
    host.DrainFrames();
    CHECK(__fui_native_test_image_state() == 2U);
    CHECK(__fui_native_test_image_width() > 0.0f);
    CHECK(__fui_native_test_image_height() > 0.0f);
    CHECK(host.TextureCountForTesting() == baseline_textures);
    __fui_native_clear_test_image();

    const auto source_texture = std::filesystem::path(__FILE__).parent_path().parent_path()
        .parent_path().parent_path() / "fui-rs" / "native-demo" / "resources" / "app" / "demo-texture.png";
    const auto unicode_texture = std::filesystem::temp_directory_path() / L"effindom-image-\x4F60\x597D.png";
    std::filesystem::copy_file(source_texture, unicode_texture, std::filesystem::copy_options::overwrite_existing);
    const std::string unicode_source = unicode_texture.u8string();
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(unicode_source.data()),
        static_cast<std::uint32_t>(unicode_source.size()));
    CHECK(__fui_native_test_image_state() == 2U);
    CHECK(host.TextureCountForTesting() == baseline_textures + 1U);
    __fui_native_clear_test_image();
    CHECK(host.TextureCountForTesting() == baseline_textures);
    std::filesystem::remove(unicode_texture);

    const auto malformed_image = std::filesystem::temp_directory_path() / "effindom-invalid-image.png";
    { std::ofstream output(malformed_image, std::ios::binary); output << "not an image"; }
    const std::string malformed_image_source = malformed_image.string();
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(malformed_image_source.data()),
        static_cast<std::uint32_t>(malformed_image_source.size()));
    CHECK(__fui_native_test_image_state() == 3U);
    CHECK(host.TextureCountForTesting() == baseline_textures);
    __fui_native_clear_test_image();
    std::filesystem::remove(malformed_image);

    const std::string remote_source = "https://effindom.dev/not-fetched.png";
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(remote_source.data()),
        static_cast<std::uint32_t>(remote_source.size()));
    CHECK(__fui_native_test_image_state() == 3U);
    __fui_native_clear_test_image();

    const auto svg_path = std::filesystem::temp_directory_path() / L"effindom-asset-\x4F60\x597D.svg";
    { std::ofstream output(svg_path); output << "<svg xmlns='http://www.w3.org/2000/svg' width='36' height='18'><rect width='36' height='18'/></svg>"; }
    const std::string svg_source = svg_path.u8string();
    REQUIRE_FALSE(svg_source.empty());
    fui_load_svg(9301U, reinterpret_cast<std::uintptr_t>(svg_source.data()), static_cast<std::uint32_t>(svg_source.size()));
    const auto svg_size = host.SvgSizeForTesting(9301U);
    REQUIRE(svg_size.has_value());
    CHECK(svg_size->first == Catch::Approx(36.0f));
    CHECK(svg_size->second == Catch::Approx(18.0f));
    fui_release_svg(9301U);
    CHECK_FALSE(host.SvgSizeForTesting(9301U).has_value());
    std::filesystem::remove(svg_path);

    const auto malformed_svg = std::filesystem::temp_directory_path() / "effindom-invalid.svg";
    { std::ofstream output(malformed_svg); output << "<svg><broken>"; }
    const std::string malformed_svg_source = malformed_svg.string();
    fui_load_svg(9303U, reinterpret_cast<std::uintptr_t>(malformed_svg_source.data()), static_cast<std::uint32_t>(malformed_svg_source.size()));
    CHECK_FALSE(host.SvgSizeForTesting(9303U).has_value());
    std::filesystem::remove(malformed_svg);

    const std::string font_source = "fonts/NotoSansThai-Regular.ttf";
    fui_load_font(9302U, reinterpret_cast<std::uintptr_t>(font_source.data()), static_cast<std::uint32_t>(font_source.size()));
    CHECK(host.HasFontForTesting(9302U));

    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_THAI, "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2");
    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_CJK, "\xE4\xBD\xA0\xE5\xA5\xBD");
    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_SUPPLEMENTAL, "\xF0\x9F\x98\x80");
    bool has_cjk = false;
    bool has_emoji = false;
    for (std::uint32_t attempt = 0U; attempt < 200U && (!has_cjk || !has_emoji); ++attempt) {
        host.DrainFrames();
        std::uint32_t count = 0U;
        const std::uint32_t* fallbacks = ui_get_live_fallback_font_buffer(&count);
        for (std::uint32_t index = 0U; index < count; ++index) {
            has_cjk = has_cjk || host.FontHasGlyphForTesting(fallbacks[index], 0x4F60U);
            has_emoji = has_emoji || host.FontHasGlyphForTesting(fallbacks[index], 0x1F600U);
        }
        if (!has_cjk || !has_emoji) SDL_Delay(5U);
    }
    CHECK(has_cjk);
    CHECK(has_emoji);
    const auto fallback_count = host.FallbackFontCountForTesting();
    REQUIRE(fallback_count >= 3U);

    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_THAI, "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2");
    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_CJK, "\xE4\xBD\xA0\xE5\xA5\xBD");
    host.RequestMissingFontCoverageForTesting(1U, UI_MISSING_FONT_COVERAGE_SUPPLEMENTAL, "\xF0\x9F\x98\x80");
    for (std::uint32_t attempt = 0U; attempt < 200U && !host.IsIdle(); ++attempt) {
        host.DrainFrames();
        SDL_Delay(2U);
    }
    CHECK(host.FallbackFontCountForTesting() == fallback_count);
    CHECK(host.IsIdle());
}

} // namespace

TEST_CASE("Windows native FUI-RS mounts remounts and disposes one application", "[v2][native][windows][w5]") {
    NativeHost host(false);
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
    CHECK(host.State().dispose_count == baseline.dispose_count + 2U);
    CHECK(host.IsIdle());
}

TEST_CASE("Windows SDL3 raster presentation is demand driven", "[v2][native][windows][w5]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto rendered = host.State();
    CHECK_FALSE(rendered.gpu_backed);
    CHECK(rendered.frame_count > 0U);
    CHECK(host.IsIdle());
    const auto pixels = host.SnapshotRgba();
    REQUIRE_FALSE(pixels.empty());
    CHECK(std::any_of(pixels.begin(), pixels.end(), [](std::uint8_t value) {
        return value != 0U && value != 255U;
    }));

    const auto idle_frames = host.State().frame_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK_FALSE(host.RunNextFrame());
    CHECK(host.State().frame_count == idle_frames);
}

TEST_CASE("Windows input resize density and clipboard services are live", "[v2][native][windows][w6]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    CHECK(fui_get_platform_family() == 2U);
    const auto baseline_activations = host.State().activation_count;

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    const auto action_handle = __fui_native_action_handle();
    REQUIRE(ui_get_bounds(action_handle, &x, &y, &width, &height));
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

    host.Resize(640U, 420U);
    host.DrainFrames();
    CHECK(host.State().logical_width == 640.0f);
    CHECK(host.State().logical_height == 420.0f);
    CHECK(host.State().pixel_density > 0.0f);

    float root_x = 0.0f;
    float root_y = 0.0f;
    float root_width = 0.0f;
    float root_height = 0.0f;
    REQUIRE(ui_get_bounds(__fui_native_application_root_handle(), &root_x, &root_y, &root_width, &root_height));
    CHECK(root_width == host.State().logical_width);
    CHECK(root_height == host.State().logical_height);

    host.SetClipboardText("native Windows clipboard");
    CHECK(host.ClipboardText() == "native Windows clipboard");
}

TEST_CASE("Windows wheel units use one device-independent UI scroll step", "[v2][native][windows][w6]") {
    using effindom::v2::native::detail::WheelDeltaToLogicalPixels;
    using effindom::v2::native::detail::IsWindowsPreciseWheelDelta;
    using effindom::v2::native::detail::WindowsHorizontalWheelDeltaToLogicalPixels;
    using effindom::v2::native::detail::WindowsVerticalWheelDeltaToLogicalPixels;
    CHECK(WheelDeltaToLogicalPixels(1.0f) == Catch::Approx(96.0f));
    CHECK(WheelDeltaToLogicalPixels(-1.0f) == Catch::Approx(-96.0f));
    CHECK(WheelDeltaToLogicalPixels(0.1f) == Catch::Approx(9.6f));
    CHECK(WheelDeltaToLogicalPixels(-0.5f) == Catch::Approx(-48.0f));
    CHECK(WindowsHorizontalWheelDeltaToLogicalPixels(120) == Catch::Approx(96.0f));
    CHECK(WindowsHorizontalWheelDeltaToLogicalPixels(-120) == Catch::Approx(-96.0f));
    CHECK(WindowsVerticalWheelDeltaToLogicalPixels(120) == Catch::Approx(-96.0f));
    CHECK(WindowsVerticalWheelDeltaToLogicalPixels(-120) == Catch::Approx(96.0f));
    CHECK(WindowsVerticalWheelDeltaToLogicalPixels(30) == Catch::Approx(-24.0f));
    CHECK_FALSE(IsWindowsPreciseWheelDelta(120));
    CHECK_FALSE(IsWindowsPreciseWheelDelta(-240));
    CHECK(IsWindowsPreciseWheelDelta(30));
}

TEST_CASE("Win32 mouse clicks are normalized from display pixels to logical UI coordinates",
          "[v2][native][windows][w6][win32][dpi]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    REQUIRE(ui_get_bounds(__fui_native_action_handle(), &x, &y, &width, &height));
    const float display_scale = host.State().pixel_density;
    const int pointer_x = static_cast<int>((x + width * 0.5f) * display_scale);
    const int pointer_y = static_cast<int>((y + height * 0.5f) * display_scale);
    const HWND window = FindWindowW(nullptr, L"EffinDOM Native FUI-RS");
    REQUIRE(window != nullptr);

    const auto baseline_activations = host.State().activation_count;
    REQUIRE(SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pointer_x, pointer_y)) == 0);
    REQUIRE(SendMessageW(window, WM_LBUTTONUP, 0U, MAKELPARAM(pointer_x, pointer_y)) == 0);
    host.DrainFrames();
    CHECK(host.State().activation_count == baseline_activations + 1U);
}

TEST_CASE("Win32 double and triple clicks select a word then paragraph",
          "[v2][native][windows][selection][win32]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const auto editor = __fui_native_body_text_handle();
    REQUIRE(editor != 0U);
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    REQUIRE(ui_get_bounds(editor, &x, &y, &width, &height));
    const float display_scale = host.State().pixel_density;
    const int pointer_x = static_cast<int>((x + 24.0f) * display_scale);
    const int pointer_y = static_cast<int>((y + height * 0.5f) * display_scale);
    const LPARAM point = MAKELPARAM(pointer_x, pointer_y);
    const HWND window = FindWindowW(nullptr, L"EffinDOM Native FUI-RS");
    REQUIRE(window != nullptr);

    REQUIRE(SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point) == 0);
    REQUIRE(SendMessageW(window, WM_LBUTTONUP, 0U, point) == 0);
    REQUIRE(SendMessageW(window, WM_LBUTTONDBLCLK, MK_LBUTTON, point) == 0);
    REQUIRE(SendMessageW(window, WM_LBUTTONUP, 0U, point) == 0);
    CHECK(ui_has_text_selection(editor));
    ui_copy_text_selection(editor);
    const std::string word = host.ClipboardText();
    CHECK_FALSE(word.empty());
    const std::string full_text = TextDocument(editor);
    CHECK(word != full_text);

    REQUIRE(SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point) == 0);
    REQUIRE(SendMessageW(window, WM_LBUTTONUP, 0U, point) == 0);
    ui_copy_text_selection(editor);
    CHECK(host.ClipboardText() == full_text);
}

TEST_CASE("native text-input context menu Copy and Cut invoke the selected menu items",
          "[v2][native][windows][selection][context-menu]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const auto editor = __fui_native_context_editor_handle();
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    REQUIRE(ui_get_bounds(editor, &x, &y, &width, &height));
    ui_set_scroll_offset(
        __fui_native_scroll_view_handle(), 0.0f, std::max(0.0f, y - 120.0f));
    host.RequestFrame();
    host.DrainFrames();
    REQUIRE(ui_get_visible_bounds(editor, &x, &y, &width, &height));
    const float menu_x = x + 20.0f;
    const float menu_y = y + height * 0.5f;
    const auto text = FindEditableTextDescendant(editor);
    REQUIRE(text != UI_INVALID_HANDLE);
    const auto* text_node = effindom::v2::ui::GetRuntime().Resolve(text);
    REQUIRE(text_node != nullptr);
    const std::string document = text_node->text_content;
    REQUIRE(document.size() > 6U);
    ui_set_text_selection_range(text, 0U, 6U);
    REQUIRE(ui_has_text_selection(text));

    host.DispatchPointer(menu_x, menu_y, true, 2, 2U);
    host.DispatchPointer(menu_x, menu_y, false, 2, 0U);
    host.DrainFrames();
    REQUIRE(__fui_native_context_menu_visible());

    const auto copy_item = FindSemanticDescendant(
        effindom::v2::ui::GetRuntime().root_handle(), "Copy");
    REQUIRE(copy_item != UI_INVALID_HANDLE);
    float copy_x = 0.0f;
    float copy_y = 0.0f;
    float copy_width = 0.0f;
    float copy_height = 0.0f;
    REQUIRE(ui_get_bounds(copy_item, &copy_x, &copy_y, &copy_width, &copy_height));
    copy_x += copy_width * 0.5f;
    copy_y += copy_height * 0.5f;
    host.DispatchPointerMove(copy_x, copy_y, 0U);
    host.DispatchPointer(copy_x, copy_y, true, 0, 1U);
    host.DispatchPointer(copy_x, copy_y, false, 0, 0U);
    host.DrainFrames();

    CHECK_FALSE(__fui_native_context_menu_visible());
    CHECK(host.ClipboardText() == document.substr(0U, 6U));

    ui_set_text_selection_range(text, 0U, 6U);
    host.DispatchPointer(menu_x, menu_y, true, 2, 2U);
    host.DispatchPointer(menu_x, menu_y, false, 2, 0U);
    host.DrainFrames();
    REQUIRE(__fui_native_context_menu_visible());
    const auto cut_item = FindSemanticDescendant(
        effindom::v2::ui::GetRuntime().root_handle(), "Cut");
    REQUIRE(cut_item != UI_INVALID_HANDLE);
    float cut_x = 0.0f;
    float cut_y = 0.0f;
    float cut_width = 0.0f;
    float cut_height = 0.0f;
    REQUIRE(ui_get_bounds(cut_item, &cut_x, &cut_y, &cut_width, &cut_height));
    cut_x += cut_width * 0.5f;
    cut_y += cut_height * 0.5f;
    host.DispatchPointerMove(cut_x, cut_y, 0U);
    host.DispatchPointer(cut_x, cut_y, true, 0, 1U);
    host.DispatchPointer(cut_x, cut_y, false, 0, 0U);
    host.DrainFrames();

    CHECK_FALSE(__fui_native_context_menu_visible());
    const auto* edited = effindom::v2::ui::GetRuntime().Resolve(text);
    REQUIRE(edited != nullptr);
    CHECK(edited->text_content == document.substr(6U));
    CHECK(host.ClipboardText() == document.substr(0U, 6U));
}

TEST_CASE("Win32 pointer capture is released after a native drag",
          "[v2][native][windows][w6][win32][capture]") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    struct SdlCleanup final {
        ~SdlCleanup() { SDL_Quit(); }
    } cleanup;
    SDL_Window* window = SDL_CreateWindow("EffinDOM capture test", 320, 200, SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);
    struct WindowCleanup final {
        SDL_Window* window;
        ~WindowCleanup() { SDL_DestroyWindow(window); }
    } window_cleanup{window};

    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    REQUIRE(hwnd != nullptr);

    std::uint32_t down_count = 0U;
    std::uint32_t move_count = 0U;
    std::uint32_t up_count = 0U;
    effindom::v2::native::WindowsScrollWheelBridge* capture_bridge = nullptr;
    auto bridge = std::make_unique<effindom::v2::native::WindowsScrollWheelBridge>(
        window,
        [](const effindom::v2::native::NativeWheelEvent&) {},
        [&](const effindom::v2::native::NativeMouseEvent& event) {
            using MouseType = effindom::v2::native::NativeMouseEvent::Type;
            if (event.type == MouseType::Down) {
                ++down_count;
                capture_bridge->SetPointerCapture(true);
            } else if (event.type == MouseType::Move) {
                ++move_count;
            } else {
                ++up_count;
                capture_bridge->SetPointerCapture(false);
            }
        },
        [] {});
    capture_bridge = bridge.get();

    REQUIRE(GetCapture() == nullptr);
    REQUIRE(SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 60)) == 0);
    REQUIRE(GetCapture() == hwnd);
    // Windows can report a transient capture transition during activation.
    // The actual HWND owner remains authoritative for the following drag/up.
    REQUIRE(SendMessageW(hwnd, WM_CAPTURECHANGED, 0U, 0U) == 0);
    REQUIRE(GetCapture() == hwnd);
    REQUIRE(SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 60)) == 0);
    REQUIRE(SendMessageW(hwnd, WM_LBUTTONUP, 0U, MAKELPARAM(180, 60)) == 0);
    CHECK(GetCapture() == nullptr);
    CHECK(down_count == 1U);
    CHECK(move_count == 1U);
    CHECK(up_count == 1U);

    REQUIRE(SendMessageW(hwnd, WM_MOUSEMOVE, 0U, MAKELPARAM(40, 60)) == 0);
    CHECK(move_count == 1U);
}

TEST_CASE("Windows native wheel bypass wakes retained rendering", "[v2][native][windows][w6][win32]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();
    REQUIRE(host.IsIdle());

    const HWND window = FindWindowW(nullptr, L"EffinDOM Native FUI-RS");
    REQUIRE(window != nullptr);
    // A negative Win32 wheel delta means wheel-down. The demo starts at the
    // top of its scroll range, so use the direction that can start scrolling.
    CHECK(SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0) == 0);
    CHECK_FALSE(host.IsIdle());
    REQUIRE(host.RunNextFrame());
    // Coarse detents use EffinDOM's retained smooth-scroll animation, matching
    // the non-precise macOS wheel path.
    CHECK_FALSE(host.IsIdle());
    const auto animation_start = std::chrono::steady_clock::now();
    std::uint32_t animation_frames = 1U;
    while (!host.IsIdle() && animation_frames < 120U) {
        REQUIRE(host.RunNextFrame());
        animation_frames += 1U;
    }
    const double animation_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - animation_start).count();
    UNSCOPED_INFO("smooth wheel frames=" << animation_frames << ", elapsed_ms=" << animation_ms
        << ", fps=" << (static_cast<double>(animation_frames) * 1000.0 / animation_ms));
    CHECK(animation_frames >= 18U);
    CHECK(animation_ms < 750.0);
    CHECK(host.IsIdle());
}

TEST_CASE("unfocused Windows wheel input does not suspend presentation", "[v2][native][windows][w6][lifecycle]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    SDL_Event focus_lost{};
    focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    REQUIRE(SDL_PushEvent(&focus_lost));
    REQUIRE(host.PumpEvent(false));
    CHECK_FALSE(host.State().presentation_suspended);

    const auto frames_before_wheel = host.State().frame_count;
    const HWND window = FindWindowW(nullptr, L"EffinDOM Native FUI-RS");
    REQUIRE(window != nullptr);
    CHECK(SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0) == 0);
    host.DrainFrames();
    CHECK(host.State().frame_count > frames_before_wheel);

    SDL_Event focus_gained{};
    focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    REQUIRE(SDL_PushEvent(&focus_gained));
    REQUIRE(host.PumpEvent(false));
    host.DrainFrames();
    CHECK_FALSE(host.State().presentation_suspended);
}

TEST_CASE("visible Windows WM_PAINT presents before UpdateWindow returns", "[v2][native][windows][w7][win32]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const HWND window = FindWindowW(nullptr, L"EffinDOM Native FUI-RS");
    REQUIRE(window != nullptr);
    const auto frames_before_paint = host.State().frame_count;
    REQUIRE(InvalidateRect(window, nullptr, FALSE));
    REQUIRE(UpdateWindow(window));
    CHECK(host.State().frame_count > frames_before_paint);
}

TEST_CASE("Windows outward border drags move the frame and repaint the live canvas",
          "[v2][native][windows][resize]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const HWND window = FindWindowW(nullptr, L"EffinDOM Native FUI-RS");
    REQUIRE(window != nullptr);
    REQUIRE(SetForegroundWindow(window));
    REQUIRE(SetWindowPos(
        window, nullptr, 200, 160, 800, 600,
        SWP_NOZORDER | SWP_NOACTIVATE));

    LiveResizePaintObserver observer{&host};
    const UINT_PTR observer_id = reinterpret_cast<UINT_PTR>(&observer);
    REQUIRE(SetWindowSubclass(
        window,
        &LiveResizePaintObserver::Procedure,
        observer_id,
        reinterpret_cast<DWORD_PTR>(&observer)));
    struct ObserverCleanup {
        HWND window;
        UINT_PTR id;
        ~ObserverCleanup() {
            RemoveWindowSubclass(window, &LiveResizePaintObserver::Procedure, id);
        }
    } observer_cleanup{window, observer_id};

    const auto check_outward_drag = [&](bool right_edge) {
        RECT rect{};
        REQUIRE(GetWindowRect(window, &rect));
        const LONG initial_frame_width = rect.right - rect.left;
        const float initial_canvas_width = host.State().logical_width;
        const int edge_x = right_edge ? rect.right - 1 : rect.left;
        const int edge_y = rect.top + (rect.bottom - rect.top) / 2;
        const int direction = right_edge ? 1 : -1;
        observer.live_paint_count = 0U;
        observer.maximum_live_width = initial_canvas_width;
        std::atomic_bool input_finished = false;
        std::thread drag([&] {
            SetCursorPos(edge_x, edge_y);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0U, 0U, 0U, 0U);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (int step = 1; step <= 4; ++step) {
                SetCursorPos(edge_x + direction * step * 40, edge_y);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            mouse_event(MOUSEEVENTF_LEFTUP, 0U, 0U, 0U, 0U);
            input_finished = true;
        });

        while (!input_finished) host.PumpEvent(true);
        drag.join();
        while (host.PumpEvent(false)) {}

        REQUIRE(GetWindowRect(window, &rect));
        CHECK(rect.right - rect.left >= initial_frame_width + 120);
        CHECK(observer.live_paint_count >= 2U);
        CHECK(observer.maximum_live_width > initial_canvas_width);
    };

    check_outward_drag(true);
    REQUIRE(SetWindowPos(
        window, nullptr, 200, 160, 800, 600,
        SWP_NOZORDER | SWP_NOACTIVATE));
    check_outward_drag(false);
}

TEST_CASE("Windows mouse drag selection renders before pointer up", "[v2][native][windows][w6]") {
    NativeHost host(false);
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
                ++changed_selection_pixels;
            }
        }
    }
    CHECK(changed_selection_pixels > 20U);

    host.DispatchPointer(x + std::min(width - 4.0f, 240.0f), pointer_y, false);
    host.DrainFrames();
}

TEST_CASE("Win32 mouse drag reaches text selection before SDL normalization", "[v2][native][windows][w6][win32]") {
    NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();

    const auto text_handle = __fui_native_body_text_handle();
    REQUIRE(text_handle != 0U);
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    REQUIRE(ui_get_bounds(text_handle, &x, &y, &width, &height));

    const HWND window = FindWindowW(nullptr, L"EffinDOM Native FUI-RS");
    REQUIRE(window != nullptr);
    const float display_scale = host.State().pixel_density;
    const int start_x = static_cast<int>((x + 4.0f) * display_scale);
    const int pointer_y = static_cast<int>((y + height * 0.5f) * display_scale);
    const int end_x = static_cast<int>((x + std::min(width - 4.0f, 240.0f)) * display_scale);
    REQUIRE(PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start_x, pointer_y)));
    REQUIRE(PostMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end_x, pointer_y)));
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        if (!host.PumpEvent(false)) break;
        host.DrainFrames();
    }
    CHECK(ui_has_text_selection(text_handle));

    REQUIRE(PostMessageW(window, WM_LBUTTONUP, 0U, MAKELPARAM(end_x, pointer_y)));
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        if (!host.PumpEvent(false)) break;
        host.DrainFrames();
    }
}

#if defined(EFFINDOM_TEST_DIRECT3D)
TEST_CASE("visible Windows presentation uses demand-driven Skia Direct3D", "[v2][native][windows][w7]") {
    NativeHost host(true);
    host.MountApplication();
    for (std::uint32_t attempt = 0U; attempt < 120U && host.State().frame_count == 0U; ++attempt) {
        host.PumpEvent(false);
        host.RunNextFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(host.State().gpu_backed);
    CHECK(host.State().frame_count > 0U);
    host.DrainFrames();
    CHECK(host.IsIdle());
}

TEST_CASE("Windows Direct3D presentation recreates graphics state", "[v2][native][windows][w7]") {
    NativeHost host(true);
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

TEST_CASE("Windows Direct3D lifecycle suspends and recovers through SDL window events", "[v2][native][windows][w7]") {
    NativeHost host(true);
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
#endif

TEST_CASE("Windows native application remount is deterministic and lifecycle remains idle", "[v2][native][windows][w10]") {
    NativeHost host(false);
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
