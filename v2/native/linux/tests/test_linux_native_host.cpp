#include "NativeHost.h"
#include "NativeHostCharacterization.h"
#include "NativeWorkerDemoContract.h"
#include "LinuxSystemThemeBridge.h"
#include "LinuxAccessibilityAdapter.h"
#include "LinuxAccessibilityText.h"
#include "effindom_ui.h"
#include "fui_host_abi.h"
#include "SDL3/SDL.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

using effindom::v2::native::NativeHost;

extern "C" std::uint32_t fui_get_platform_family();
extern "C" std::uint32_t fui_get_host_capabilities();
extern "C" std::uint32_t fui_get_accent_color();
extern "C" std::uint64_t __fui_native_scroll_view_handle();
extern "C" std::uint64_t __fui_native_drop_zone_handle();
extern "C" void __fui_native_schedule_ui_dispatch();
extern "C" void __fui_native_schedule_cancelled_ui_dispatch();
extern "C" std::uint32_t __fui_native_ui_dispatch_count();
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

namespace {

std::string NativeFileDialogResult() {
    std::string result(__fui_native_file_dialog_result_length(), '\0');
    result.resize(__fui_native_copy_file_dialog_result(
        reinterpret_cast<std::uint8_t*>(result.data()),
        static_cast<std::uint32_t>(result.size())));
    return result;
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

} // namespace

namespace {

class FakeLinuxTextProvider final
    : public effindom::v2::native::NativeAccessibilityTextProvider {
public:
    bool GetInfo(std::uint64_t, effindom::v2::native::NativeAccessibilityTextInfo& output)
        const override {
        if (obscured) return false;
        output = {revision, 4U, selection_start, selection_end, read_only, true};
        return true;
    }

    effindom::v2::native::NativeAccessibilityTextStatus ReadRange(
        std::uint64_t, std::uint64_t requested_revision, std::uint32_t start,
        std::uint32_t end, std::string& output) const override {
        using Status = effindom::v2::native::NativeAccessibilityTextStatus;
        if (requested_revision != revision) return Status::StaleRevision;
        if (start == 0U && end == 4U) { output = text; return Status::Ok; }
        if (start == 1U && end == 2U) { output = "我"; return Status::Ok; }
        return Status::InvalidRange;
    }

    effindom::v2::native::NativeAccessibilityTextStatus RangeRects(
        std::uint64_t, std::uint64_t requested_revision, std::uint32_t,
        std::uint32_t, std::vector<effindom::v2::native::NativeAccessibilityTextRect>& output)
        const override {
        using Status = effindom::v2::native::NativeAccessibilityTextStatus;
        if (requested_revision != revision) return Status::StaleRevision;
        output = {{1.0f, 2.0f, 3.0f, 4.0f}};
        return Status::Ok;
    }

    effindom::v2::native::NativeAccessibilityTextStatus SetSelection(
        std::uint64_t, std::uint64_t requested_revision, std::uint32_t start,
        std::uint32_t end) const override {
        using Status = effindom::v2::native::NativeAccessibilityTextStatus;
        if (requested_revision != revision) return Status::StaleRevision;
        selection_start = start;
        selection_end = end;
        return Status::Ok;
    }

    effindom::v2::native::NativeAccessibilityTextStatus RevealRange(
        std::uint64_t, std::uint64_t requested_revision, std::uint32_t start,
        std::uint32_t end) const override {
        using Status = effindom::v2::native::NativeAccessibilityTextStatus;
        if (requested_revision != revision) return Status::StaleRevision;
        revealed_start = start;
        revealed_end = end;
        return Status::Ok;
    }

    effindom::v2::native::NativeAccessibilityTextStatus ReplaceRange(
        std::uint64_t, std::uint64_t requested_revision, std::uint32_t start,
        std::uint32_t end, const std::string& replacement,
        std::uint64_t& output_revision) const override {
        using Status = effindom::v2::native::NativeAccessibilityTextStatus;
        if (requested_revision != revision) return Status::StaleRevision;
        if (start != 0U || end != 4U) return Status::InvalidRange;
        text = replacement;
        output_revision = ++revision;
        return Status::Ok;
    }

    mutable std::string text = "A我😀B";
    mutable std::uint64_t revision = 7U;
    mutable std::uint32_t selection_start = 1U;
    mutable std::uint32_t selection_end = 3U;
    mutable std::uint32_t revealed_start = 0U;
    mutable std::uint32_t revealed_end = 0U;
    bool obscured = false;
    bool read_only = false;
};

DBusMessage* TextCall(const char* interface_name, const char* member) {
    DBusMessage* message = dbus_message_new_method_call("org.test.EffinDOM",
        "/org/a11y/atspi/accessible/effindom/h2", interface_name, member);
    dbus_message_set_serial(message, 1U);
    return message;
}

} // namespace

TEST_CASE("Linux AT-SPI projection maps roles stable paths interfaces and actions",
    "[v2][native][linux][accessibility]") {
    using namespace effindom::v2::native;
    NativeAccessibilityNode button_node;
    button_node.role = NativeAccessibilityRole::Button;
    button_node.handle = 0x100000002ULL;
    CHECK(detail::LinuxAtSpiRole(button_node.role) == 43U);
    CHECK(detail::LinuxAtSpiObjectPath(button_node.handle) ==
        "/org/a11y/atspi/accessible/effindom/h100000002");
    CHECK(detail::LinuxAtSpiActionCount(button_node) == 1U);
    CHECK(detail::LinuxAtSpiInterfaces(button_node) == std::vector<std::string>{
        "org.a11y.atspi.Accessible", "org.a11y.atspi.Component", "org.a11y.atspi.Action"});

    NativeAccessibilityNode slider_node;
    slider_node.role = NativeAccessibilityRole::Slider;
    slider_node.has_value_range = true;
    CHECK(detail::LinuxAtSpiRole(slider_node.role) == 51U);
    CHECK(detail::LinuxAtSpiActionCount(slider_node) == 2U);
    CHECK(detail::LinuxAtSpiInterfaces(slider_node).back() == "org.a11y.atspi.Value");
}

TEST_CASE("Linux AT-SPI text interfaces query Unicode lazily and forward operations",
    "[v2][native][linux][accessibility][text]") {
    using namespace effindom::v2::native;
    NativeAccessibilityNode editor;
    editor.role = NativeAccessibilityRole::TextBox;
    CHECK(detail::LinuxAtSpiInterfaces(editor) == std::vector<std::string>{
        "org.a11y.atspi.Accessible", "org.a11y.atspi.Component",
        "org.a11y.atspi.Text", "org.a11y.atspi.EditableText"});

    auto provider = std::make_shared<FakeLinuxTextProvider>();
    detail::LinuxAtSpiTextObject object{42U, 10, 20, provider, {}};
    DBusMessage* request = TextCall(detail::kLinuxAtSpiTextInterface, "GetText");
    dbus_int32_t start = 0;
    dbus_int32_t end = 4;
    dbus_message_append_args(request, DBUS_TYPE_INT32, &start,
        DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
    DBusMessage* reply = detail::HandleLinuxAtSpiTextMessage(request, object);
    REQUIRE(reply != nullptr);
    const char* value = nullptr;
    REQUIRE(dbus_message_get_args(
        reply, nullptr, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID));
    CHECK(std::string(value) == "A我😀B");
    dbus_message_unref(reply);
    dbus_message_unref(request);

    request = TextCall(detail::kLinuxAtSpiTextInterface, "SetSelection");
    dbus_int32_t index = 0;
    start = 3;
    end = 1;
    dbus_message_append_args(request, DBUS_TYPE_INT32, &index,
        DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
    reply = detail::HandleLinuxAtSpiTextMessage(request, object);
    REQUIRE(reply != nullptr);
    CHECK(provider->selection_start == 3U);
    CHECK(provider->selection_end == 1U);
    dbus_message_unref(reply);
    dbus_message_unref(request);

    request = TextCall(detail::kLinuxAtSpiTextInterface, "GetCharacterExtents");
    start = 1;
    dbus_uint32_t coordinates = 0U;
    dbus_message_append_args(request, DBUS_TYPE_INT32, &start,
        DBUS_TYPE_UINT32, &coordinates, DBUS_TYPE_INVALID);
    reply = detail::HandleLinuxAtSpiTextMessage(request, object);
    REQUIRE(reply != nullptr);
    dbus_int32_t x = 0;
    dbus_int32_t y = 0;
    dbus_int32_t width = 0;
    dbus_int32_t height = 0;
    REQUIRE(dbus_message_get_args(reply, nullptr, DBUS_TYPE_INT32, &x,
        DBUS_TYPE_INT32, &y, DBUS_TYPE_INT32, &width,
        DBUS_TYPE_INT32, &height, DBUS_TYPE_INVALID));
    CHECK(x == 11);
    CHECK(y == 22);
    CHECK(width == 3);
    CHECK(height == 4);
    dbus_message_unref(reply);
    dbus_message_unref(request);

    request = TextCall(detail::kLinuxAtSpiTextInterface, "ScrollSubstringTo");
    start = 1;
    end = 3;
    dbus_uint32_t scroll_type = 0U;
    dbus_message_append_args(request, DBUS_TYPE_INT32, &start,
        DBUS_TYPE_INT32, &end, DBUS_TYPE_UINT32, &scroll_type, DBUS_TYPE_INVALID);
    reply = detail::HandleLinuxAtSpiTextMessage(request, object);
    REQUIRE(reply != nullptr);
    CHECK(provider->revealed_start == 1U);
    CHECK(provider->revealed_end == 3U);
    dbus_message_unref(reply);
    dbus_message_unref(request);

    request = TextCall(detail::kLinuxAtSpiEditableTextInterface, "SetTextContents");
    const char* replacement = "changed";
    dbus_message_append_args(request, DBUS_TYPE_STRING, &replacement, DBUS_TYPE_INVALID);
    reply = detail::HandleLinuxAtSpiTextMessage(request, object);
    REQUIRE(reply != nullptr);
    CHECK(provider->text == "changed");
    dbus_message_unref(reply);
    dbus_message_unref(request);
}

TEST_CASE("Linux AT-SPI text rejects stale and obscured requests and maps events",
    "[v2][native][linux][accessibility][text]") {
    using namespace effindom::v2::native;
    auto provider = std::make_shared<FakeLinuxTextProvider>();
    detail::LinuxAtSpiTextObject object{42U, 0, 0, provider, {}};
    DBusMessage* request = TextCall(detail::kLinuxAtSpiTextInterface, "GetText");
    dbus_int32_t start = 0;
    dbus_int32_t end = 4;
    dbus_message_append_args(request, DBUS_TYPE_INT32, &start,
        DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
    object.execute = [provider](const std::function<void()>& operation) {
        ++provider->revision;
        operation();
        return true;
    };
    DBusMessage* reply = detail::HandleLinuxAtSpiTextMessage(request, object);
    REQUIRE(reply != nullptr);
    CHECK(dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR);
    dbus_message_unref(reply);
    dbus_message_unref(request);

    object.execute = {};
    provider->obscured = true;
    request = TextCall(detail::kLinuxAtSpiTextInterface, "GetText");
    dbus_message_append_args(request, DBUS_TYPE_INT32, &start,
        DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
    reply = detail::HandleLinuxAtSpiTextMessage(request, object);
    REQUIRE(reply != nullptr);
    CHECK(dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR);
    CHECK(std::string(dbus_message_get_error_name(reply)) == DBUS_ERROR_ACCESS_DENIED);
    dbus_message_unref(reply);
    dbus_message_unref(request);

    const auto document = detail::LinuxAtSpiTextEvent(
        NativeAccessibilityTextEvent::DocumentChanged);
    CHECK(std::string(document.member) == "TextChanged");
    CHECK_FALSE(document.emit_caret);
    const auto selection = detail::LinuxAtSpiTextEvent(
        NativeAccessibilityTextEvent::SelectionChanged);
    CHECK(std::string(selection.member) == "TextSelectionChanged");
    CHECK(selection.emit_caret);
}

TEST_CASE("Linux AT-SPI state projection preserves focus editing and control state",
    "[v2][native][linux][accessibility]") {
    using namespace effindom::v2::native;
    NativeAccessibilityNode node;
    node.role = NativeAccessibilityRole::TextBox;
    node.has_read_only = true;
    node.read_only = true;
    node.has_multiline = true;
    node.multiline = true;
    const auto states = detail::LinuxAtSpiStates(node, true);
    const auto has_state = [&states](std::uint32_t state) {
        return states.size() > state / 32U &&
            (states[state / 32U] & (1U << (state % 32U))) != 0U;
    };
    CHECK(has_state(8U));  // enabled
    CHECK(has_state(11U)); // focusable
    CHECK(has_state(12U)); // focused
    CHECK(has_state(17U)); // multi-line
    CHECK(has_state(43U)); // read-only
    CHECK_FALSE(has_state(7U)); // editable
    CHECK(has_state(25U)); // showing
    CHECK(has_state(30U)); // visible
}

TEST_CASE("Linux portal accents validate and pack as opaque RGBA",
    "[v2][native][linux][theme]") {
    using effindom::v2::native::detail::PackLinuxPortalAccentColor;

    CHECK(PackLinuxPortalAccentColor(0.0, 0.5, 1.0) == 0x0080FFFFU);
    CHECK(PackLinuxPortalAccentColor(1.0, 0.0, 0.0) == 0xFF0000FFU);
    CHECK_FALSE(PackLinuxPortalAccentColor(-0.001, 0.0, 0.0).has_value());
    CHECK_FALSE(PackLinuxPortalAccentColor(0.0, 1.001, 0.0).has_value());
    CHECK_FALSE(PackLinuxPortalAccentColor(
        std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0).has_value());
    CHECK_FALSE(PackLinuxPortalAccentColor(
        0.0, std::numeric_limits<double>::infinity(), 0.0).has_value());
}

TEST_CASE("Linux portal accent state falls back and suppresses duplicates",
    "[v2][native][linux][theme]") {
    using effindom::v2::native::detail::kLinuxAccentColorFallback;
    using effindom::v2::native::detail::LinuxAccentColorState;

    std::vector<std::uint32_t> changes;
    const std::thread::id ui_thread = std::this_thread::get_id();
    LinuxAccentColorState state(std::nullopt, [&](std::uint32_t color) {
        CHECK(std::this_thread::get_id() == ui_thread);
        changes.push_back(color);
    });
    CHECK(state.Current() == kLinuxAccentColorFallback);
    CHECK_FALSE(state.Apply(std::nullopt));
    CHECK_FALSE(state.Apply(kLinuxAccentColorFallback));
    CHECK(state.Apply(0x123456FFU));
    CHECK_FALSE(state.Apply(0x123456FFU));
    CHECK(state.Current() == 0x123456FFU);
    REQUIRE(changes.size() == 1U);
    CHECK(changes[0] == 0x123456FFU);
}

TEST_CASE("Linux native host satisfies shared pointer characterization",
    "[v2][native][linux][characterization]") {
    effindom::v2::native::tests::CharacterizePointerActivation<NativeHost>();
}

TEST_CASE("Linux native host satisfies shared lifecycle characterization",
    "[v2][native][linux][characterization]") {
    effindom::v2::native::tests::CharacterizeLifecycleAndFrameDemand<NativeHost>();
}

TEST_CASE("Linux native host satisfies shared viewport characterization",
    "[v2][native][linux][characterization]") {
    effindom::v2::native::tests::CharacterizeViewportReconciliation<NativeHost>();
}

TEST_CASE("Linux native host reports desktop capabilities", "[v2][native][linux][host]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    CHECK(fui_get_platform_family() == FUI_PLATFORM_LINUX);
    const std::uint32_t capabilities = fui_get_host_capabilities();
    CHECK((capabilities & FUI_HOST_CAPABILITY_OPEN_EXTERNAL_URI) != 0U);
    CHECK((capabilities & FUI_HOST_CAPABILITY_CLIPBOARD_READ) != 0U);
    CHECK((capabilities & FUI_HOST_CAPABILITY_CLIPBOARD_WRITE) != 0U);
    CHECK((capabilities & FUI_HOST_CAPABILITY_FILE_DIALOGS) != 0U);
    CHECK((fui_get_accent_color() & 0xFFU) == 0xFFU);
}

TEST_CASE("Linux native demo runs the browser prime Worker contract without a platform branch",
    "[v2][native][linux][worker][demo]") {
    effindom::v2::native::tests::CharacterizeNativeWorkerDemo<NativeHost>();
}

TEST_CASE("Linux raster presentation remains demand driven after logical resize",
    "[v2][native][linux][graphics]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    const auto baseline = host.State();

    CHECK_FALSE(host.RunNextFrame());
    host.Resize(612U, 407U);
    host.DrainFrames();
    const auto resized = host.State();
    CHECK(resized.logical_width == 612.0f);
    CHECK(resized.logical_height == 407.0f);
    CHECK(resized.frame_count > baseline.frame_count);
    CHECK_FALSE(host.SnapshotRgba().empty());
    CHECK(host.IsIdle());
}

TEST_CASE("Linux platform exposes resize presentation boundaries",
    "[v2][native][linux][graphics]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    while (host.PumpEvent(false)) {}

    SDL_Event boundary{};
    boundary.type = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED;
    REQUIRE(SDL_PushEvent(&boundary));
    REQUIRE(host.PumpEvent(false));
    CHECK(host.ShouldPresentAfterLastEvent());
}

TEST_CASE("Linux coalesces only consecutive resize geometry events",
    "[v2][native][linux][graphics]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    while (host.PumpEvent(false)) {}

    int window_count = 0;
    SDL_Window** windows = SDL_GetWindows(&window_count);
    REQUIRE(windows != nullptr);
    REQUIRE(window_count == 1);
    const std::uint32_t window_id = SDL_GetWindowID(windows[0]);
    SDL_free(windows);

    SDL_Event logical{};
    logical.type = SDL_EVENT_WINDOW_RESIZED;
    logical.window.windowID = window_id;
    SDL_Event pixels{};
    pixels.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    pixels.window.windowID = window_id;
    SDL_Event scale{};
    scale.type = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED;
    scale.window.windowID = window_id;
    const std::uint32_t boundary_type = SDL_RegisterEvents(1);
    REQUIRE(boundary_type != 0U);
    SDL_Event boundary{};
    boundary.type = boundary_type;

    REQUIRE(SDL_PushEvent(&logical));
    REQUIRE(SDL_PushEvent(&pixels));
    REQUIRE(SDL_PushEvent(&scale));
    REQUIRE(SDL_PushEvent(&boundary));

    REQUIRE(host.PumpEvent(false));
    CHECK(host.ShouldPresentAfterLastEvent());
    REQUIRE(host.PumpEvent(false));
    CHECK_FALSE(host.ShouldPresentAfterLastEvent());
    CHECK_FALSE(host.PumpEvent(false));
}

TEST_CASE("Linux expose events produce one queued presentation",
    "[v2][native][linux][graphics]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    while (host.PumpEvent(false)) host.DrainFrames();

    int window_count = 0;
    SDL_Window** windows = SDL_GetWindows(&window_count);
    REQUIRE(windows != nullptr);
    REQUIRE(window_count == 1);
    SDL_Window* window = windows[0];
    SDL_free(windows);

    const auto frames_before_expose = host.State().frame_count;
    SDL_Event expose{};
    expose.type = SDL_EVENT_WINDOW_EXPOSED;
    expose.window.windowID = SDL_GetWindowID(window);
    REQUIRE(SDL_PushEvent(&expose));
    CHECK(host.State().frame_count == frames_before_expose);
    REQUIRE(host.PumpEvent(false));
    REQUIRE(host.ShouldPresentAfterLastEvent());
    REQUIRE(host.RunNextFrame());
    CHECK(host.State().frame_count == frames_before_expose + 1U);
}

TEST_CASE("Linux clipboard and freedesktop targets validate without launching when hidden",
    "[v2][native][linux][services]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const std::string clipboard = "Linux clipboard: \xE4\xBD\xA0\xE5\xA5\xBD";
    host.SetClipboardText(clipboard);
    CHECK(host.ClipboardText() == clipboard);
    CHECK(host.OpenExternalUrl("https://effindom.dev/native"));
    CHECK(host.OpenExternalUrl("HTTP://example.com"));
    CHECK_FALSE(host.OpenExternalUrl("file:///tmp/unsafe"));
    CHECK_FALSE(host.OpenExternalUrl("javascript:alert(1)"));

    const auto file = std::filesystem::temp_directory_path() / "effindom-linux-open.txt";
    { std::ofstream output(file); output << "native"; }
    CHECK(host.OpenFile(file));
    CHECK(host.RevealFile(file));
    std::filesystem::remove(file);
    CHECK_FALSE(host.OpenFile(file));
    CHECK_FALSE(host.RevealFile(file));
}

TEST_CASE("Linux focus loss cancels pointer interaction without suspending the host",
    "[v2][native][linux][input]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    host.DispatchPointer(20.0f, 20.0f, true, 0, 1U, 1);
    host.DispatchWindowFocusLost();
    host.DrainFrames();
    CHECK(host.IsRunning());
    CHECK(host.IsIdle());
}

TEST_CASE("Linux UI dispatch wakes and cancellation drains safely",
    "[v2][native][linux][services]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    while (host.PumpEvent(false)) host.DrainFrames();
    const auto initial_dispatches = __fui_native_ui_dispatch_count();

    __fui_native_schedule_ui_dispatch();
    REQUIRE(host.PumpEvent(false));
    host.DrainFrames();
    CHECK(__fui_native_ui_dispatch_count() == initial_dispatches + 1U);

    __fui_native_schedule_cancelled_ui_dispatch();
    for (std::uint32_t attempt = 0U; attempt < 8U; ++attempt) host.PumpEvent(false);
    CHECK(__fui_native_ui_dispatch_count() == initial_dispatches + 1U);
    CHECK(host.IsIdle());
}

TEST_CASE("Linux file dialogs preserve selected cancelled and error completions",
    "[v2][native][linux][services]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    const auto selected = __fui_native_start_test_file_dialog();
    host.CompleteFileDialogForTesting(selected, 0U, {"/tmp/first.txt", "/tmp/second.md"}, {}, 0);
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

TEST_CASE("Linux SDL drops preserve routing and multi-item payloads",
    "[v2][native][linux][services]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    REQUIRE(ui_get_bounds(__fui_native_drop_zone_handle(), &x, &y, &width, &height));
    ui_set_scroll_offset(__fui_native_scroll_view_handle(), 0.0f, std::max(0.0f, y - 120.0f));
    host.RequestFrame();
    host.DrainFrames();
    REQUIRE(ui_get_visible_bounds(__fui_native_drop_zone_handle(), &x, &y, &width, &height));
    REQUIRE(width > 0.0f);
    REQUIRE(height > 0.0f);
    const float target_x = x + width * 0.5f;
    const float target_y = y + height * 0.5f;

    const auto file = std::filesystem::temp_directory_path() / "effindom-linux-drop.txt";
    { std::ofstream output(file); output << "drop"; }
    const std::string file_utf8 = file.string();
    __fui_native_clear_drop_result();
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_BEGIN, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_POSITION, target_x, target_y);
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_FILE, target_x, target_y, file_utf8);
    host.DispatchDropEventForTesting(
        SDL_EVENT_DROP_TEXT, target_x, target_y, "https://effindom.dev/drop");
    host.DispatchDropEventForTesting(SDL_EVENT_DROP_COMPLETE, target_x, target_y);
    host.DrainFrames();
    CHECK(NativeDropResult() ==
        "enter,over,over,over,drop:2:file=" + file_utf8 +
        ":uri=https://effindom.dev/drop,leave");
    std::filesystem::remove(file);
    CHECK(host.IsIdle());
}

TEST_CASE("Linux packaged assets and Fontconfig fallback load through native services",
    "[v2][native][linux][assets]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    CHECK(host.HasFontForTesting(1U));

    const std::string packaged_texture = "app/demo-texture.png";
    __fui_native_set_test_image_source(
        reinterpret_cast<const std::uint8_t*>(packaged_texture.data()),
        static_cast<std::uint32_t>(packaged_texture.size()));
    host.DrainFrames();
    CHECK(__fui_native_test_image_state() == 2U);
    CHECK(__fui_native_test_image_width() > 0.0f);
    CHECK(__fui_native_test_image_height() > 0.0f);
    __fui_native_clear_test_image();

    host.RequestMissingFontCoverageForTesting(
        1U, UI_MISSING_FONT_COVERAGE_CJK, "\xE4\xBD\xA0\xE5\xA5\xBD");
    bool has_cjk = false;
    for (std::uint32_t attempt = 0U; attempt < 200U && !has_cjk; ++attempt) {
        host.RunNextFrame();
        std::uint32_t count = 0U;
        const std::uint32_t* fallbacks = ui_get_live_fallback_font_buffer(&count);
        for (std::uint32_t index = 0U; index < count; ++index) {
            has_cjk = has_cjk || host.FontHasGlyphForTesting(fallbacks[index], 0x4F60U);
        }
        if (!has_cjk) SDL_Delay(5U);
    }
    CHECK(has_cjk);
    CHECK(host.FallbackFontCountForTesting() >= 1U);
    host.DrainFrames();
}
