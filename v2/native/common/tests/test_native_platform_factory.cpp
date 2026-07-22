#include "NativeGraphicsCoordinator.h"
#include "NativeFuiBridge.h"
#include "NativeHost.h"
#include "NativeHostCharacterization.h"
#include "NativeHostCore.h"
#include "NativePlatformFactory.h"
#include "NativePlatformHost.h"
#include "UiPlatformHost.h"
#include "fui_host_abi.h"

#include "SDL3/SDL.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
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
              [] {
                  __fui_clear_ui_dispatches();
                  __fui_clear_native_file_dialog_callbacks();
              },
          }) {
        state_->visible = visible;
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
        ui::ClearGlobalUiPlatformHost(ui_host_);
        core_.ReleaseGraphics();
    }

    NativeHostCore& Core() override { return core_; }
    const NativeHostCore& Core() const override { return core_; }
    bool PumpEvent(bool) override {
        ++state_->pump_count;
        return false;
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
    bool PostUiDispatch(std::uint64_t callback_id) override {
        state_->posted_dispatch = callback_id;
        return true;
    }
    bool CancelUiDispatch(std::uint64_t callback_id) override {
        state_->cancelled_dispatch = callback_id;
        return true;
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
    void SetNativePointerCapture(bool captured) override { state_->pointer_captured = captured; }
    void SetCursor(std::uint32_t style) override { state_->cursor = style; }
    void RequestFontLoad(std::uint32_t font_id, const std::string& source) override {
        state_->font_id = font_id;
        state_->font_source = source;
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
    void RequestMissingFontCoverageForTesting(
        std::uint32_t font_id,
        std::uint32_t,
        const std::string& sample) override {
        state_->font_id = font_id;
        state_->font_source = sample;
    }

private:
    std::shared_ptr<TestPlatformState> state_;
    TestUiPlatformHost ui_host_;
    NativeHostCore core_;
    TestGraphicsSurface* surface_ = nullptr;
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

    fui_set_pointer_capture(99U);
    CHECK(latest_state->pointer_captured);
    fui_release_pointer_capture();
    CHECK_FALSE(latest_state->pointer_captured);

    CHECK_FALSE(host.PumpEvent(false));
    CHECK(latest_state->pump_count == 1U);
    host.RecreateGraphicsSurface();
    CHECK(latest_state->recovery_count == 1U);
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
