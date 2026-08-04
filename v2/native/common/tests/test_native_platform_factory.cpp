#include "NativeGraphicsCoordinator.h"
#include "NativeFuiBridge.h"
#include "NativeHost.h"
#include "NativeFramePacer.h"
#include "NativeHostCharacterization.h"
#include "NativeHostCore.h"
#include "NativePageZoomController.h"
#include "NativePlatformFactory.h"
#include "NativePlatformHost.h"
#include "SdlEventAdapter.h"
#include "NativeTimerCoordinator.h"
#include "NativeTextInputCoordinator.h"
#include "NativeTouchGestureController.h"
#include "UiPlatformHost.h"
#include "UiRuntime.h"
#include "fui_host_abi.h"

#include "SDL3/SDL.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
bool fui_dispatch_to_ui(std::uint64_t callback_id);
bool fui_cancel_ui_dispatch_async(std::uint64_t callback_id);
std::uint32_t fui_get_platform_family();
std::uint32_t fui_get_host_capabilities();
void fui_set_pointer_capture(std::uint64_t handle);
void fui_release_pointer_capture();
void fui_set_application_caption(std::uintptr_t pointer, std::uint32_t length);
void fui_set_page_zoom_enabled(bool enabled);
}

namespace effindom::v2::native {
namespace {

struct TestPlatformState {
    bool visible = true;
    std::uint32_t pump_count = 0U;
    std::uint32_t present_count = 0U;
    std::uint32_t recovery_count = 0U;
    bool gpu_backed = false;
    bool fail_next_prepare = false;
    std::uint64_t posted_dispatch = 0U;
    std::uint64_t cancelled_dispatch = 0U;
    std::uint32_t started_timer = 0U;
    std::int32_t timer_delay = 0;
    std::uint32_t cancelled_timer = 0U;
    std::uint64_t clipboard_read_handle = 0U;
    std::string clipboard;
    std::string caption;
    std::string external_url;
    std::string opened_file;
    std::string revealed_file;
    std::uint64_t dialog_request = 0U;
    std::string dialog_filters;
    bool pointer_captured = false;
    std::uint32_t cursor = 0U;
    std::uint32_t font_id = 0U;
    std::string font_source;
    std::uint32_t svg_id = 0U;
    std::uint32_t texture_id = 0U;
    std::uint32_t drop_type = 0U;
    std::string drop_data;
    std::uint32_t backdrop_color = 0U;
};

std::shared_ptr<TestPlatformState> latest_state;

class TestGraphicsSurface final : public NativeGraphicsSurface {
public:
    TestGraphicsSurface(std::shared_ptr<TestPlatformState> state, int width, int height)
        : state_(std::move(state)), width_(width), height_(height) {}

    void Resize(int width, int height) {
        width_ = width;
        height_ = height;
    }

    bool PrepareFrame(std::uint32_t width, std::uint32_t height, float) override {
        if (state_->fail_next_prepare) {
            state_->fail_next_prepare = false;
            surface_.reset();
            return false;
        }
        surface_ = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(
            static_cast<int>(width), static_cast<int>(height)));
        return surface_ != nullptr;
    }
    bool QueryOutputSize(int& width, int& height) const override {
        width = width_;
        height = height_;
        return true;
    }
    bool Present() override {
        ++state_->present_count;
        return true;
    }
    void SetBackdropColor(std::uint32_t rgba) override {
        state_->backdrop_color = rgba;
    }
    void RequestRecovery() override { ++state_->recovery_count; }
    bool HandleRecoveryEvent(const SDL_Event&) override { return false; }
    SkCanvas* Canvas() const override { return surface_ == nullptr ? nullptr : surface_->getCanvas(); }
    SkSurface* Surface() const override { return surface_.get(); }
    std::uint64_t Generation() const override { return 1U; }
    std::uint64_t RecoveryCount() const override { return state_->recovery_count; }
    bool IsGpuBacked() const override { return state_->gpu_backed; }

private:
    std::shared_ptr<TestPlatformState> state_;
    int width_;
    int height_;
    sk_sp<SkSurface> surface_;
};

class TestUiPlatformHost final : public ui::UiPlatformHost {
public:
    explicit TestUiPlatformHost(std::shared_ptr<TestPlatformState> state)
        : state_(std::move(state)) {}

    void WriteClipboard(std::string_view plain_text, std::string_view) override {
        state_->clipboard = plain_text;
    }
    void RequestClipboardRead(std::uint64_t handle) override {
        state_->clipboard_read_handle = handle;
    }
    void RequestFontLoad(std::uint32_t font_id, std::string_view source) override {
        state_->font_id = font_id;
        state_->font_source = source;
    }
    void ReportMissingFontCoverage(std::uint32_t, std::uint32_t, std::string_view) override {}
    void RequestSemanticAnnouncement(std::uint64_t) override {}

private:
    std::shared_ptr<TestPlatformState> state_;
};

class TestPlatformHost final : public NativePlatformHost {
public:
    TestPlatformHost(bool visible, std::shared_ptr<TestPlatformState> state)
        : state_(std::move(state)), ui_host_(state_),
          core_(NativeInputRouterOptions{false, false, false}, NativeHostCoreCallbacks{
              {},
              [this] {
                  if (timer_coordinator_ != nullptr) timer_coordinator_->Clear();
                  __fui_clear_ui_dispatches();
                  __fui_clear_native_file_dialog_callbacks();
              },
          }) {
        state_->visible = visible;
        timer_coordinator_ = std::make_unique<NativeTimerCoordinator>(
            [this](NativeTimerCoordinator::UiTask task) {
                {
                    std::lock_guard lock(ui_task_mutex_);
                    ui_tasks_.push_back(std::move(task));
                }
                ui_task_changed_.notify_all();
                return true;
            });
        auto surface = std::make_unique<TestGraphicsSurface>(state_, 640, 480);
        surface_ = surface.get();
        auto graphics = NativeGraphicsCoordinator::Create(
            nullptr,
            NativeGraphicsOptions{NativePixelDensitySource::Fixed, {}, 2.0f},
            std::move(surface));
        REQUIRE(graphics != nullptr);
        core_.AttachGraphics(std::move(graphics));
        ui::SetGlobalUiPlatformHost(ui_host_);
        UiHostCallbacks callbacks{};
        callbacks.on_focus_changed = &as_on_focus_changed;
        callbacks.on_pointer_event = &as_on_pointer_event;
        callbacks.on_text_changed = &as_on_text_changed;
        callbacks.on_text_replaced = &as_on_text_replaced;
        callbacks.on_scroll = &as_on_scroll;
        callbacks.on_selection_changed = &as_on_selection_changed;
        callbacks.on_cross_selection_changed = &as_on_cross_selection_changed;
        callbacks.on_clipboard_write = &as_on_clipboard_write;
        callbacks.on_request_clipboard_read = &as_on_request_clipboard_read;
        callbacks.on_request_font_load = &as_on_request_font_load;
        callbacks.on_missing_font_coverage = &as_on_missing_font_coverage;
        callbacks.on_request_semantic_announcement = &as_on_request_semantic_announcement;
        ui_set_host_callbacks(&callbacks);
        core_.InitializeEngine();
    }

    ~TestPlatformHost() override {
        timer_coordinator_.reset();
        ui::ClearGlobalUiPlatformHost(ui_host_);
        core_.ReleaseGraphics();
    }

    NativeHostCore& Core() override { return core_; }
    const NativeHostCore& Core() const override { return core_; }
    bool PumpEvent(bool wait_when_idle) override {
        ++state_->pump_count;
        std::deque<NativeTimerCoordinator::UiTask> tasks;
        {
            std::unique_lock lock(ui_task_mutex_);
            if (wait_when_idle && ui_tasks_.empty()) {
                ui_task_changed_.wait_for(lock, std::chrono::milliseconds(50), [this] {
                    return !ui_tasks_.empty();
                });
            }
            tasks.swap(ui_tasks_);
        }
        bool rendered = false;
        for (auto& task : tasks) rendered = task() || rendered;
        return rendered;
    }
    void Resize(std::uint32_t width, std::uint32_t height) override {
        surface_->Resize(static_cast<int>(width * 2U), static_cast<int>(height * 2U));
        core_.RefreshWindowGeometry();
        core_.RequestFrame();
    }
    void RecreateGraphicsSurface() override {
        core_.Graphics().RequestRecovery();
        core_.RequestFrame();
    }
    void DispatchWindowFocusLost() override {
        core_.InputRouter().HandleWindowFocusLost(core_.NowMilliseconds());
    }
    std::uint32_t CurrentPointerButtons() const override { return 0U; }
    std::uint32_t CurrentModifiers() const override { return 0U; }
    bool PostUiTask(std::function<bool()> task) override {
        {
            std::lock_guard lock(ui_task_mutex_);
            ui_tasks_.push_back(std::move(task));
        }
        ui_task_changed_.notify_all();
        return true;
    }
    bool PostUiDispatch(std::uint64_t callback_id) override {
        state_->posted_dispatch = callback_id;
        return true;
    }
    bool CancelUiDispatch(std::uint64_t callback_id) override {
        state_->cancelled_dispatch = callback_id;
        return true;
    }
    void StartTimer(std::uint32_t timer_id, std::int32_t delay_ms) override {
        state_->started_timer = timer_id;
        state_->timer_delay = delay_ms;
        timer_coordinator_->Start(timer_id, delay_ms);
    }
    void CancelTimer(std::uint32_t timer_id) override {
        state_->cancelled_timer = timer_id;
        timer_coordinator_->Cancel(timer_id);
    }
    void SetClipboardText(const std::string& text) override { state_->clipboard = text; }
    std::string ClipboardText() const override { return state_->clipboard; }
    void RequestClipboardRead(std::uint64_t handle) override { state_->clipboard_read_handle = handle; }
    bool OpenExternalUrl(const std::string& url) const override {
        state_->external_url = url;
        return true;
    }
    bool OpenFile(const std::filesystem::path& path) const override {
        state_->opened_file = path.string();
        return true;
    }
    bool RevealFile(const std::filesystem::path& path) const override {
        state_->revealed_file = path.string();
        return true;
    }
    bool ShowFileDialog(
        std::uint32_t,
        std::uint64_t request_id,
        const std::string& filters,
        const std::string&,
        bool) override {
        state_->dialog_request = request_id;
        state_->dialog_filters = filters;
        return true;
    }
    bool IsDarkMode() const override { return true; }
    std::uint32_t AccentColor() const override { return 0x123456FFU; }
    std::uint32_t PlatformFamily() const override { return FUI_PLATFORM_LINUX; }
    std::uint32_t HostCapabilities() const override {
        return FUI_HOST_CAPABILITY_OPEN_EXTERNAL_URI |
               FUI_HOST_CAPABILITY_CLIPBOARD_READ |
               FUI_HOST_CAPABILITY_CLIPBOARD_WRITE |
               FUI_HOST_CAPABILITY_FILE_DIALOGS;
    }
    bool IsCoarsePointer() const override { return false; }
    void SetApplicationCaption(const std::string& caption) override { state_->caption = caption; }
    bool SetApplicationIcon(const std::filesystem::path&) override { return true; }
    void SetNativePointerCapture(bool captured) override { state_->pointer_captured = captured; }
    void SetCursor(std::uint32_t style) override { state_->cursor = style; }
    void RequestFontLoad(std::uint32_t font_id, const std::string& source) override {
        state_->font_id = font_id;
        state_->font_source = source;
    }
    void ReportMissingFontCoverage(
        std::uint32_t font_id,
        std::uint32_t,
        const std::string& sample) override {
        state_->font_id = font_id;
        state_->font_source = sample;
    }
    void LoadSvg(std::uint32_t svg_id, const std::string&) override { state_->svg_id = svg_id; }
    void ReleaseSvg(std::uint32_t svg_id) override { state_->svg_id = svg_id; }
    void LoadTexture(std::uint32_t texture_id, const std::string&) override {
        state_->texture_id = texture_id;
    }
    void ReleaseTexture(std::uint32_t texture_id) override { state_->texture_id = texture_id; }
    void CompleteFileDialogForTesting(
        std::uint64_t request_id,
        std::uint32_t,
        std::vector<std::string>,
        std::string,
        std::int32_t) override {
        state_->dialog_request = request_id;
    }
    void DispatchDropEventForTesting(
        std::uint32_t event_type,
        float,
        float,
        const std::string& data) override {
        state_->drop_type = event_type;
        state_->drop_data = data;
    }
    std::size_t FallbackFontCountForTesting() const override { return 0U; }

private:
    std::shared_ptr<TestPlatformState> state_;
    TestUiPlatformHost ui_host_;
    NativeHostCore core_;
    TestGraphicsSurface* surface_ = nullptr;
    std::unique_ptr<NativeTimerCoordinator> timer_coordinator_;
    std::mutex ui_task_mutex_;
    std::condition_variable ui_task_changed_;
    std::deque<NativeTimerCoordinator::UiTask> ui_tasks_;
};

class TestPlatformFactory final : public NativePlatformFactory {
public:
    std::unique_ptr<NativePlatformHost> CreateHost(bool visible) override {
        latest_state = std::make_shared<TestPlatformState>();
        return std::make_unique<TestPlatformHost>(visible, latest_state);
    }
};

} // namespace

std::unique_ptr<NativePlatformFactory> CreateNativePlatformFactory() {
    return std::make_unique<TestPlatformFactory>();
}

TEST_CASE("test platform factory satisfies the complete shared native host contract",
    "[v2][native][common][factory]") {
    tests::CharacterizeNativeHost<NativeHost>();
}

TEST_CASE("test platform factory routes platform strategies through the common host and FUI bridge",
    "[v2][native][common][factory]") {
    NativeHost host(false);
    REQUIRE(latest_state != nullptr);
    CHECK_FALSE(latest_state->visible);
    CHECK(host.State().pixel_density == 2.0f);

    host.SetClipboardText("native clipboard");
    CHECK(host.ClipboardText() == "native clipboard");
    CHECK(host.OpenExternalUrl("https://effindom.dev"));
    CHECK(host.OpenFile("/tmp/input.txt"));
    CHECK(host.RevealFile("/tmp/output.txt"));
    CHECK(latest_state->external_url == "https://effindom.dev");
    CHECK(latest_state->opened_file == "/tmp/input.txt");
    CHECK(latest_state->revealed_file == "/tmp/output.txt");

    CHECK(fui_dispatch_to_ui(41U));
    CHECK(fui_cancel_ui_dispatch_async(42U));
    CHECK(latest_state->posted_dispatch == 41U);
    CHECK(latest_state->cancelled_dispatch == 42U);
    CHECK(fui_get_platform_family() == FUI_PLATFORM_LINUX);
    CHECK((fui_get_host_capabilities() & FUI_HOST_CAPABILITY_FILE_DIALOGS) != 0U);

    const std::string caption = "EffinDOM â¢ native";
    fui_set_application_caption(
        reinterpret_cast<std::uintptr_t>(caption.data()),
        static_cast<std::uint32_t>(caption.size()));
    CHECK(latest_state->caption == caption);

    CHECK(host.IsPageZoomEnabledForTesting());
    fui_set_page_zoom_enabled(false);
    CHECK_FALSE(host.IsPageZoomEnabledForTesting());
    fui_set_page_zoom_enabled(true);
    CHECK(host.IsPageZoomEnabledForTesting());

    fui_set_pointer_capture(99U);
    CHECK(latest_state->pointer_captured);
    fui_release_pointer_capture();
    CHECK_FALSE(latest_state->pointer_captured);

    CHECK_FALSE(host.PumpEvent(false));
    CHECK(latest_state->pump_count == 1U);
    host.RecreateGraphicsSurface();
    CHECK(latest_state->recovery_count == 1U);
}

TEST_CASE("native page zoom controller anchors pans resets and honors policy",
    "[v2][native][common][zoom]") {
    Engine engine;
    engine.Init(200U, 100U, 1.0f);
    engine.SetViewportSize(200.0f, 100.0f);
    std::uint32_t frame_requests = 0U;
    NativePageZoomController controller(engine, [&frame_requests] { ++frame_requests; });

    CHECK(controller.IsEnabled());
    CHECK(controller.SetScaleFromScreenAnchor(2.0f, 50.0f, 25.0f));
    CHECK(controller.State().scale == Catch::Approx(2.0f));
    CHECK(controller.State().offset_x == Catch::Approx(-50.0f));
    CHECK(controller.State().offset_y == Catch::Approx(-25.0f));
    const NativeScenePoint anchor = controller.ScreenToScene(50.0f, 25.0f);
    CHECK(anchor.x == Catch::Approx(50.0f));
    CHECK(anchor.y == Catch::Approx(25.0f));
    const NativeScenePoint screen = controller.SceneToScreen(anchor.x, anchor.y);
    CHECK(screen.x == Catch::Approx(50.0f));
    CHECK(screen.y == Catch::Approx(25.0f));
    const NativeSceneRect projected = controller.SceneToScreen({10.0f, 20.0f, 30.0f, 40.0f});
    CHECK(projected.x == Catch::Approx(-30.0f));
    CHECK(projected.y == Catch::Approx(15.0f));
    CHECK(projected.width == Catch::Approx(60.0f));
    CHECK(projected.height == Catch::Approx(80.0f));

    CHECK(controller.PanBy(10.0f, 5.0f));
    CHECK(controller.State().offset_x == Catch::Approx(-60.0f));
    CHECK(controller.State().offset_y == Catch::Approx(-30.0f));
    CHECK(frame_requests == 2U);

    controller.SetEnabled(false);
    CHECK_FALSE(controller.IsEnabled());
    CHECK(controller.State().scale == Catch::Approx(1.0f));
    CHECK_FALSE(controller.SetScaleFromScreenAnchor(2.0f, 50.0f, 25.0f));
    CHECK(frame_requests == 3U);

    controller.SetEnabled(true);
    CHECK(controller.IsEnabled());
    CHECK(controller.SetScaleFromScreenAnchor(99.0f, 100.0f, 50.0f));
    CHECK(controller.State().scale == Catch::Approx(4.0f));
    CHECK(controller.ScaleByFactorFromScreenAnchor(0.5f, 100.0f, 50.0f));
    CHECK(controller.State().scale == Catch::Approx(2.0f));
    CHECK(controller.BeginPinch(100.0f, 50.0f));
    CHECK(controller.UpdatePinch(1.5f, 100.0f, 50.0f));
    CHECK(controller.State().scale == Catch::Approx(3.0f));
    controller.EndPinch();

    controller.BeginPan(100.0);
    controller.UpdatePan(-20.0f, 0.0f, 110.0);
    controller.EndPan(110.0);
    CHECK(controller.TickMomentum(120.0));
    CHECK(frame_requests >= 5U);
}

TEST_CASE("native text composition updates commits cancels and separates printable key input",
    "[v2][native][common][input][text][ime]") {
    auto state = std::make_shared<TestPlatformState>();
    TestPlatformHost host(false, state);
    NativeHostCore& core = host.Core();
    core.MountApplication();
    core.DrainFrames(16);
    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(handle != 0U);
    ui_set_interactive(handle, true);
    ui_set_selectable(handle, true, 0x40007AFFU);
    ui_set_editable(handle, true);
    ui_set_editor_command_keys(handle, true);
    ui_set_focusable(handle, true, 0);
    const auto read_text = [handle] {
        const auto value = effindom::v2::ui::GetRuntime().GetEditableTextDocument(handle);
        CHECK(value.has_value());
        return value.has_value() ? std::string(*value) : std::string{};
    };

    const std::string original = "hello";
    ui_set_text(handle, reinterpret_cast<const std::uint8_t*>(original.data()),
        static_cast<std::uint32_t>(original.size()));
    ui_set_text_selection_range(handle, 1U, 4U);
    ui_request_focus(handle);
    core.RequestFrame();
    REQUIRE(core.RunNextFrame());
    REQUIRE(ui_get_focused_handle() == handle);

    core.InputRouter().DispatchKey("x", true, 0U, core.NowMilliseconds(), true);
    CHECK(read_text() == original);
    core.InputRouter().DispatchKey("x", false, 0U, core.NowMilliseconds(), true);
    CHECK(read_text() == original);
    SdlEventAdapter sdl_events(core.InputRouter(), {});
    CHECK(core.InputRouter().DispatchTextComposition(NativeTextCompositionInput{
        NativeTextCompositionPhase::Start, "", kNativeTextRangeUnspecified,
        kNativeTextRangeUnspecified, kNativeTextRangeUnspecified,
        kNativeTextRangeUnspecified, core.NowMilliseconds(),
    }));
    SDL_Event edit_event{};
    edit_event.type = SDL_EVENT_TEXT_EDITING;
    edit_event.edit.text = "\xE4\xBD\xA0";
    edit_event.edit.start = 3;
    edit_event.edit.length = 0;
    CHECK(sdl_events.HandleEvent(edit_event, core.NowMilliseconds()));
    CHECK(read_text() == "h\xE4\xBD\xA0o");
    edit_event.edit.text = "";
    edit_event.edit.start = 0;
    CHECK(sdl_events.HandleEvent(edit_event, core.NowMilliseconds()));
    CHECK(read_text() == original);
    std::uint32_t selection_start = 0U;
    std::uint32_t selection_end = 0U;
    REQUIRE(effindom::v2::ui::GetRuntime().GetTextSelectionRange(
        handle, selection_start, selection_end));
    CHECK(selection_start == 1U);
    CHECK(selection_end == 4U);

    SDL_Event text_event{};
    text_event.type = SDL_EVENT_TEXT_INPUT;
    text_event.text.text = "\xE4\xB8\x96\xE7\x95\x8C";
    CHECK(sdl_events.HandleEvent(text_event, core.NowMilliseconds()));
    CHECK(read_text() == "h\xE4\xB8\x96\xE7\x95\x8Co");
    CHECK(ui_has_pending_visual_work());
    CHECK(ShouldCommitNativeRuntimeFrame(true, false, true, false, true));
    ui_request_focus(UI_INVALID_HANDLE);
    ui_delete_node(handle);
    core.UnmountApplication();
}

TEST_CASE("focused editable nodes activate native text input before selection geometry exists",
    "[v2][native][common][input][text][activation]") {
    auto state = std::make_shared<TestPlatformState>();
    TestPlatformHost host(false, state);
    NativeHostCore& core = host.Core();
    core.MountApplication();

    const std::uint64_t handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(handle != 0U);
    ui_set_editable(handle, true);
    ui_set_focusable(handle, true, 0);
    ui_request_focus(handle);
    core.RequestFrame();
    REQUIRE(core.RunNextFrame());
    REQUIRE(ui_get_focused_handle() == handle);

    const auto target = core.InputRouter().FocusedTextInputTarget();
    REQUIRE(target.has_value());
    CHECK(target->handle == handle);

    ui_request_focus(UI_INVALID_HANDLE);
    ui_delete_node(handle);
    core.UnmountApplication();
}

TEST_CASE("native text input lifecycle follows editable focus and composition phases",
    "[v2][native][common][input][text][ime][lifecycle]") {
    NativeTextInputCoordinator coordinator;
    std::uint32_t activations = 0U;
    std::uint32_t deactivations = 0U;
    std::vector<NativeTextInputTarget> areas;
    const NativeTextInputCallbacks callbacks{
        [&activations] {
            ++activations;
            return true;
        },
        [&deactivations] { ++deactivations; },
        [&areas](const NativeTextInputTarget& target) { areas.push_back(target); },
    };

    const NativeTextInputTarget first{41U, 10.0f, 20.0f, 1.0f, 18.0f};
    coordinator.Synchronize(first, true, callbacks);
    CHECK(coordinator.State() == NativeTextInputState::Editing);
    CHECK(coordinator.ActiveHandle() == 41U);
    CHECK(activations == 1U);
    REQUIRE(areas.size() == 1U);
    CHECK(areas.back().y == Catch::Approx(20.0f));

    coordinator.BeginComposition();
    CHECK(coordinator.State() == NativeTextInputState::Composing);
    coordinator.BeginCommit();
    CHECK(coordinator.State() == NativeTextInputState::Committing);
    coordinator.FinishComposition();
    CHECK(coordinator.State() == NativeTextInputState::Editing);
    coordinator.BeginCancel();
    CHECK(coordinator.State() == NativeTextInputState::Cancelling);
    coordinator.FinishComposition();

    const NativeTextInputTarget second{42U, 30.0f, 40.0f, 2.0f, 19.0f};
    coordinator.Synchronize(second, true, callbacks);
    CHECK(activations == 2U);
    CHECK(deactivations == 1U);
    CHECK(coordinator.ActiveHandle() == 42U);
    coordinator.Synchronize(std::nullopt, true, callbacks);
    CHECK(coordinator.State() == NativeTextInputState::Inactive);
    CHECK(deactivations == 2U);

    coordinator.Synchronize(first, false, callbacks);
    CHECK(coordinator.State() == NativeTextInputState::Inactive);
    CHECK(activations == 2U);
}

TEST_CASE("native text input retries rejected platform activation",
    "[v2][native][common][input][text][lifecycle]") {
    NativeTextInputCoordinator coordinator;
    std::uint32_t activations = 0U;
    const NativeTextInputCallbacks callbacks{
        [&activations] { return ++activations > 1U; },
        {},
        {},
    };
    const NativeTextInputTarget target{41U, 10.0f, 20.0f, 1.0f, 18.0f};

    coordinator.Synchronize(target, true, callbacks);
    CHECK(coordinator.State() == NativeTextInputState::Inactive);
    coordinator.Synchronize(target, true, callbacks);
    CHECK(coordinator.State() == NativeTextInputState::Editing);
    CHECK(activations == 2U);
}

TEST_CASE("SDL touch and pen events preserve contact identity and current pen axes",
    "[v2][native][common][input][pointer][touch][pen]") {
    auto state = std::make_shared<TestPlatformState>();
    TestPlatformHost host(false, state);
    NativeHostCore& core = host.Core();
    core.MountApplication();
    core.DrainFrames(16);
    bool touch_device_available = false;
    SdlEventAdapterOptions options{};
    options.coarse_pointer_query = [&touch_device_available] {
        return touch_device_available;
    };
    SdlEventAdapter events(core.InputRouter(), std::move(options));
    CHECK_FALSE(events.IsCoarsePointer());
    touch_device_available = true;
    CHECK(events.IsCoarsePointer());
    touch_device_available = false;

    SDL_Event touch{};
    touch.type = SDL_EVENT_FINGER_DOWN;
    touch.tfinger.fingerID = 41U;
    touch.tfinger.x = 0.25f;
    touch.tfinger.y = 0.5f;
    touch.tfinger.pressure = 0.4f;
    CHECK(events.HandleEvent(touch, 10.0));
    const NativePointerMetadata first_touch = core.InputRouter().PointerMetadata();
    CHECK(first_touch.event_type == UI_EVENT_POINTER_DOWN);
    CHECK(first_touch.pointer_type == UI_POINTER_TYPE_TOUCH);
    CHECK(first_touch.primary);
    CHECK(first_touch.pressure == Catch::Approx(0.4f));
    CHECK(events.IsCoarsePointer());

    touch.type = SDL_EVENT_FINGER_MOTION;
    touch.tfinger.pressure = 0.7f;
    CHECK(events.HandleEvent(touch, 11.0));
    CHECK(core.InputRouter().PointerMetadata().pointer_id == first_touch.pointer_id);
    CHECK(core.InputRouter().PointerMetadata().event_type == UI_EVENT_POINTER_MOVE);

    SDL_Event second_touch = touch;
    second_touch.type = SDL_EVENT_FINGER_DOWN;
    second_touch.tfinger.fingerID = 99U;
    CHECK(events.HandleEvent(second_touch, 12.0));
    CHECK(core.InputRouter().PointerMetadata().pointer_id != first_touch.pointer_id);
    CHECK_FALSE(core.InputRouter().PointerMetadata().primary);

    touch.type = SDL_EVENT_FINGER_CANCELED;
    CHECK(events.HandleEvent(touch, 13.0));
    CHECK(core.InputRouter().PointerMetadata().event_type == UI_EVENT_POINTER_CANCEL);
    CHECK(core.InputRouter().PointerMetadata().buttons == 0U);
    CHECK(events.IsCoarsePointer());

    SDL_Event focus_lost{};
    focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    CHECK_FALSE(events.HandleEvent(focus_lost, 13.5));
    CHECK(core.InputRouter().PointerMetadata().event_type == UI_EVENT_POINTER_CANCEL);
    CHECK(core.InputRouter().PointerMetadata().buttons == 0U);
    CHECK_FALSE(events.IsCoarsePointer());

    SDL_Event pen_axis{};
    pen_axis.type = SDL_EVENT_PEN_AXIS;
    pen_axis.paxis.which = 7U;
    pen_axis.paxis.axis = SDL_PEN_AXIS_PRESSURE;
    pen_axis.paxis.value = 0.8f;
    pen_axis.paxis.pen_state = SDL_PEN_INPUT_DOWN;
    pen_axis.paxis.x = 25.0f;
    pen_axis.paxis.y = 30.0f;
    CHECK(events.HandleEvent(pen_axis, 14.0));
    const NativePointerMetadata pen = core.InputRouter().PointerMetadata();
    CHECK(pen.pointer_type == UI_POINTER_TYPE_PEN);
    CHECK(pen.event_type == UI_EVENT_POINTER_MOVE);
    CHECK(pen.pressure == Catch::Approx(0.8f));
    CHECK(pen.primary);
    CHECK(pen.buttons == 1U);
    CHECK(pen.pointer_id != first_touch.pointer_id);

    pen_axis.paxis.axis = SDL_PEN_AXIS_XTILT;
    pen_axis.paxis.value = 18.0f;
    CHECK(events.HandleEvent(pen_axis, 14.1));
    pen_axis.paxis.axis = SDL_PEN_AXIS_YTILT;
    pen_axis.paxis.value = -12.0f;
    CHECK(events.HandleEvent(pen_axis, 14.2));
    pen_axis.paxis.axis = SDL_PEN_AXIS_ROTATION;
    pen_axis.paxis.value = 90.0f;
    CHECK(events.HandleEvent(pen_axis, 14.3));
    pen_axis.paxis.axis = SDL_PEN_AXIS_TANGENTIAL_PRESSURE;
    pen_axis.paxis.value = -0.2f;
    CHECK(events.HandleEvent(pen_axis, 14.4));
    CHECK(core.InputRouter().PointerMetadata().tilt_x == Catch::Approx(18.0f));
    CHECK(core.InputRouter().PointerMetadata().tilt_y == Catch::Approx(-12.0f));
    CHECK(core.InputRouter().PointerMetadata().twist == Catch::Approx(90.0f));
    CHECK(core.InputRouter().PointerMetadata().tangential_pressure == Catch::Approx(-0.2f));

    SDL_Event pen_up{};
    pen_up.type = SDL_EVENT_PEN_UP;
    pen_up.ptouch.which = 7U;
    pen_up.ptouch.x = 25.0f;
    pen_up.ptouch.y = 30.0f;
    CHECK(events.HandleEvent(pen_up, 15.0));
    CHECK(core.InputRouter().PointerMetadata().pointer_id == pen.pointer_id);
    CHECK(core.InputRouter().PointerMetadata().event_type == UI_EVENT_POINTER_UP);
    CHECK(core.InputRouter().PointerMetadata().buttons == 0U);
    core.UnmountApplication();
}

TEST_CASE("native touch gestures arbitrate scroll pinch long press and cancellation",
    "[v2][native][common][input][touch][gesture]") {
    struct GestureRecord {
        std::uint32_t phase;
        std::uint32_t kind;
        float scale;
    };
    std::vector<GestureRecord> gestures;
    std::vector<std::pair<float, float>> scroll_updates;
    std::uint32_t scroll_begin_count = 0U;
    std::uint32_t scroll_end_count = 0U;
    std::uint32_t pointer_cancel_count = 0U;
    std::uint32_t long_press_count = 0U;
    NativeTouchGestureCallbacks callbacks{};
    callbacks.hit_test = [](float, float) { return 10U; };
    callbacks.resolve_gesture_owner = [](std::uint64_t handle) { return handle; };
    callbacks.gesture_intent = [](std::uint64_t) { return 3U; };
    callbacks.dispatch_gesture = [&gestures](std::uint64_t, std::uint32_t phase,
        std::uint32_t kind, float, float, float, float, float scale, std::int32_t) {
        gestures.push_back({phase, kind, scale});
        return true;
    };
    callbacks.resolve_long_press_owner = [](std::uint64_t handle) { return handle; };
    callbacks.long_press_duration_ms = [](std::uint64_t) { return 500; };
    callbacks.long_press_movement_tolerance = [](std::uint64_t) { return 10.0f; };
    callbacks.long_press_continues_pointer_events = [](std::uint64_t) { return false; };
    callbacks.dispatch_long_press = [&long_press_count](std::uint64_t, float, float,
        std::int32_t, std::uint32_t, std::uint32_t, std::int32_t) {
        ++long_press_count;
        return true;
    };
    callbacks.cancel_pointer = [&pointer_cancel_count](const NativePointerContactInput&) {
        ++pointer_cancel_count;
    };
    callbacks.clear_scroll_momentum = [] {};
    callbacks.begin_touch_scroll = [&scroll_begin_count](std::uint64_t, float, float, double) {
        ++scroll_begin_count;
    };
    callbacks.touch_scroll_can_consume = [](float, float) { return true; };
    callbacks.update_touch_scroll = [&scroll_updates](float x, float y, double) {
        scroll_updates.emplace_back(x, y);
    };
    callbacks.end_touch_scroll = [&scroll_end_count](double) { ++scroll_end_count; };
    callbacks.flush_retained_changes = [] {};

    NativeTouchGestureController controller(std::move(callbacks));
    NativePointerContactInput first{};
    first.pointer_id = 2;
    first.pointer_type = UI_POINTER_TYPE_TOUCH;
    first.primary = true;
    first.buttons = 1U;
    first.timestamp_ms = 10.0;
    CHECK_FALSE(controller.HandlePointer(UI_EVENT_POINTER_DOWN, first, 10U, false));
    CHECK_FALSE(controller.Advance(509.0));
    CHECK(controller.Advance(510.0));
    CHECK(long_press_count == 1U);
    CHECK(controller.ConsumesTerminal(first.pointer_id));
    first.buttons = 0U;
    first.timestamp_ms = 520.0;
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_UP, first, 10U, false));

    first.buttons = 1U;
    first.timestamp_ms = 600.0;
    CHECK_FALSE(controller.HandlePointer(UI_EVENT_POINTER_DOWN, first, 10U, false));
    first.y = 20.0f;
    first.timestamp_ms = 610.0;
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_MOVE, first, 10U, false));
    CHECK(scroll_begin_count == 1U);
    CHECK(pointer_cancel_count == 1U);
    CHECK_FALSE(scroll_updates.empty());
    CHECK(controller.ConsumesTerminal(first.pointer_id));
    first.buttons = 0U;
    first.timestamp_ms = 620.0;
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_UP, first, 10U, false));
    CHECK(scroll_end_count == 1U);

    first = {};
    first.pointer_id = 4;
    first.pointer_type = UI_POINTER_TYPE_TOUCH;
    first.primary = true;
    first.buttons = 1U;
    first.x = 10.0f;
    first.y = 10.0f;
    first.timestamp_ms = 700.0;
    NativePointerContactInput second = first;
    second.pointer_id = 5;
    second.primary = false;
    second.x = 30.0f;
    CHECK_FALSE(controller.HandlePointer(UI_EVENT_POINTER_DOWN, first, 10U, false));
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_DOWN, second, 10U, false));
    first.x = 0.0f;
    first.timestamp_ms = 710.0;
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_MOVE, first, 10U, false));
    CHECK_FALSE(gestures.empty());
    CHECK(gestures.front().phase == 1U);
    CHECK(gestures.front().kind == 2U);
    CHECK(gestures.back().scale > 1.0f);
    CHECK(controller.Cancel(720.0));
    CHECK(gestures.back().phase == 4U);
}

TEST_CASE("native two-contact gestures fall back to page zoom when controls do not handle pinch",
    "[v2][native][common][input][touch][zoom]") {
    std::uint32_t begin_count = 0U;
    std::uint32_t update_count = 0U;
    std::uint32_t end_count = 0U;
    float latest_scale = 1.0f;
    NativeTouchGestureCallbacks callbacks{};
    callbacks.hit_test = [](float, float) { return 10U; };
    callbacks.resolve_gesture_owner = [](std::uint64_t handle) { return handle; };
    callbacks.gesture_intent = [](std::uint64_t) { return 0U; };
    callbacks.page_zoom_enabled = [] { return true; };
    callbacks.begin_page_zoom = [&begin_count](float, float) {
        ++begin_count;
        return true;
    };
    callbacks.update_page_zoom = [&update_count, &latest_scale](float, float, float scale) {
        ++update_count;
        latest_scale = scale;
        return true;
    };
    callbacks.end_page_zoom = [&end_count] { ++end_count; };
    callbacks.cancel_pointer = [](const NativePointerContactInput&) {};

    NativeTouchGestureController controller(std::move(callbacks));
    NativePointerContactInput first{};
    first.pointer_id = 2;
    first.pointer_type = UI_POINTER_TYPE_TOUCH;
    first.primary = true;
    first.buttons = 1U;
    first.x = 10.0f;
    first.y = 10.0f;
    first.timestamp_ms = 10.0;
    NativePointerContactInput second = first;
    second.pointer_id = 3;
    second.primary = false;
    second.x = 30.0f;
    CHECK_FALSE(controller.HandlePointer(UI_EVENT_POINTER_DOWN, first, 10U, false));
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_DOWN, second, 10U, false));
    first.x = 0.0f;
    first.timestamp_ms = 20.0;
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_MOVE, first, 10U, false));
    CHECK(begin_count == 1U);
    CHECK(update_count == 1U);
    CHECK(latest_scale > 1.0f);
    first.buttons = 0U;
    first.timestamp_ms = 30.0;
    CHECK(controller.HandlePointer(UI_EVENT_POINTER_UP, first, 10U, false));
    CHECK(end_count == 1U);
}

TEST_CASE("SDL pinch updates preserve their native multiplicative scale",
    "[v2][native][common][input][trackpad][pinch]") {
    auto state = std::make_shared<TestPlatformState>();
    TestPlatformHost host(false, state);
    float delivered_scale = 0.0f;
    SdlEventAdapter adapter(host.Core().InputRouter(), {}, nullptr,
        [&delivered_scale](float, float, float, float scale) {
            delivered_scale = scale;
            return true;
        });
    SDL_Event event{};
    event.type = SDL_EVENT_PINCH_UPDATE;
    event.pinch.scale = 1.25f;
    CHECK(adapter.HandleEvent(event, 10.0));
    CHECK(delivered_scale == Catch::Approx(1.25f));
}

TEST_CASE("an active GPU preparation failure retries the selected backend",
    "[v2][native][common][graphics]") {
    NativeHost host(false);
    host.MountApplication();
    host.DrainFrames();
    REQUIRE(latest_state != nullptr);

    latest_state->gpu_backed = true;
    latest_state->fail_next_prepare = true;
    const std::uint32_t recoveries_before = latest_state->recovery_count;
    host.RequestFrame();

    CHECK_FALSE(host.RunNextFrame());
    CHECK(host.State().gpu_backed);
    CHECK(host.State().frame_pending);
    CHECK(latest_state->recovery_count == recoveries_before + 1U);
    CHECK(host.RunNextFrame());
    CHECK(host.State().gpu_backed);
}

TEST_CASE("native follow-up animation frames are paced without delaying the first frame",
    "[v2][native][common][frame]") {
    using FramePacer = NativeFramePacer;
    std::uint32_t refresh_waits = 0U;
    std::vector<bool> active_states;
    FramePacer pacer(
        [&refresh_waits] { ++refresh_waits; },
        [&active_states](bool active) { active_states.push_back(active); });

    pacer.WaitForFrame();
    CHECK(refresh_waits == 0U);
    CHECK(pacer.ShouldBlockForEvent());

    pacer.FrameCompleted(true);
    REQUIRE(pacer.HasScheduledFrame());
    CHECK_FALSE(pacer.ShouldBlockForEvent());
    pacer.WaitForFrame();
    CHECK(refresh_waits == 1U);
    CHECK_FALSE(pacer.HasScheduledFrame());
    CHECK(pacer.ShouldBlockForEvent());

    pacer.FrameCompleted(false);
    pacer.WaitForFrame();
    CHECK(refresh_waits == 1U);
    REQUIRE(active_states.size() == 2U);
    CHECK(active_states.front());
    CHECK_FALSE(active_states.back());
}

TEST_CASE("native host commits each Tier 2 coarse-wheel animation frame",
    "[v2][native][common][frame][scroll]") {
    auto state = std::make_shared<TestPlatformState>();
    TestPlatformHost host(false, state);
    NativeHostCore& core = host.Core();
    core.MountApplication();
    core.DrainFrames(16);

    ui_reset();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 160.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 480.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);
    core.RequestFrame();
    REQUIRE(core.RunNextFrame());

    auto& runtime = effindom::v2::ui::GetRuntime();
    runtime.HandleWheelEventAt(scroll, 40.0f, 40.0f, 0.0f, 96.0f);
    REQUIRE(runtime.Resolve(scroll) != nullptr);
    REQUIRE(runtime.Resolve(scroll)->smooth_scroll_active);
    core.RequestFrame();
    REQUIRE(core.RunNextFrame());
    const float first_offset = runtime.Resolve(scroll)->scroll_offset_y;
    CHECK(first_offset > 0.0f);
    REQUIRE(core.State().frame_pending);

    REQUIRE(core.RunNextFrame());
    const float second_offset = runtime.Resolve(scroll)->scroll_offset_y;
    CHECK(second_offset > first_offset);
    CHECK(core.State().frame_pending);
    core.UnmountApplication();
}

TEST_CASE("native presentation backdrop follows system theme independently of app rendering",
    "[v2][native][common][graphics][theme]") {
    auto state = std::make_shared<TestPlatformState>();
    TestPlatformHost host(false, state);

    host.Core().SetSystemDarkMode(true);
    CHECK(state->backdrop_color == 0x111827FFU);

    host.Core().SetSystemDarkMode(false);
    CHECK(state->backdrop_color == 0xFFFFFFFFU);
}

} // namespace effindom::v2::native
