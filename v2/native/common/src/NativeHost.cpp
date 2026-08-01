#include "NativeHost.h"

#include "NativePlatformFactory.h"
#include "NativePlatformHost.h"
#include "NativeHostCore.h"
#include "NativeWindowIcon.h"
#include "NativeFuiBridge.h"
#include "NativeFuiRsWorkerAdapter.h"
#include "NativeFuiRuntimeBridge.h"
#include "NativeWorkerHost.h"
#include "Engine.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

extern "C" {
void __fui_on_worker_progress(std::uint32_t, const std::uint8_t*, std::uint32_t);
void __fui_on_worker_complete(std::uint32_t, const std::uint8_t*, std::uint32_t);
void __fui_on_worker_error(std::uint32_t, const std::uint8_t*, std::uint32_t);
}

namespace effindom::v2::native {

NativeHost::NativeHost(bool visible) {
    std::unique_ptr<NativePlatformFactory> factory = CreateNativePlatformFactory();
    if (factory == nullptr) throw std::runtime_error("native platform factory is unavailable");
    platform_ = factory->CreateHost(visible);
    if (platform_ == nullptr) throw std::runtime_error("native platform host creation failed");
    const std::filesystem::path icon = FindPackagedApplicationIcon();
    if (!icon.empty()) platform_->SetApplicationIcon(icon);
    SetActiveNativePlatformHost(platform_.get());
    std::size_t worker_count = 0U;
    const auto* workers = fui_native_worker_registry(&worker_count);
    worker_host_ = std::make_unique<NativeWorkerHost>(
        workers,
        worker_count,
        [this](NativeWorkerCoordinator::UiTask task) {
            return platform_->PostUiTask(std::move(task));
        },
        NativeWorkerHostCallbacks{
            [](std::uint32_t id, const std::string& text) {
                __fui_on_worker_progress(
                    id,
                    reinterpret_cast<const std::uint8_t*>(text.data()),
                    static_cast<std::uint32_t>(text.size()));
            },
            [](std::uint32_t id, const std::string& text) {
                __fui_on_worker_complete(
                    id,
                    reinterpret_cast<const std::uint8_t*>(text.data()),
                    static_cast<std::uint32_t>(text.size()));
            },
            [](std::uint32_t id, const std::string& text) {
                __fui_on_worker_error(
                    id,
                    reinterpret_cast<const std::uint8_t*>(text.data()),
                    static_cast<std::uint32_t>(text.size()));
            },
        });
    SetActiveNativeWorkerHost(worker_host_.get());
}

NativeHost::~NativeHost() {
    SetActiveNativeWorkerHost(nullptr);
    worker_host_->SetSessionGeneration(++worker_session_generation_);
    worker_host_.reset();
    platform_->Core().UnmountApplication();
    if (ActiveNativePlatformHost() == platform_.get()) SetActiveNativePlatformHost(nullptr);
}

void NativeHost::MountApplication() {
    worker_host_->SetSessionGeneration(++worker_session_generation_);
    platform_->Core().MountApplication();
    ui_set_platform_family(platform_->PlatformFamily());
}
void NativeHost::Unmount() {
    worker_host_->SetSessionGeneration(++worker_session_generation_);
    platform_->Core().UnmountApplication();
}
void NativeHost::RequestFrame() { platform_->Core().RequestFrame(); }
bool NativeHost::RunNextFrame() { return platform_->Core().RunNextFrame(); }
void NativeHost::DrainFrames(std::uint32_t maximum_frames) { platform_->Core().DrainFrames(maximum_frames); }
bool NativeHost::PumpEvent(bool wait_when_idle) { return platform_->PumpEvent(wait_when_idle); }
bool NativeHost::ShouldPresentAfterLastEvent() const {
    return platform_->ShouldPresentAfterLastEvent();
}
void NativeHost::Resize(std::uint32_t width, std::uint32_t height) { platform_->Resize(width, height); }
void NativeHost::RecreateGraphicsSurface() { platform_->RecreateGraphicsSurface(); }
void NativeHost::DispatchPointer(
    float x,
    float y,
    bool down,
    std::int32_t button,
    std::uint32_t buttons,
    std::int32_t click_count) {
    platform_->Core().InputRouter().DispatchPointer(NativePointerInput{
        x,
        y,
        down,
        button,
        buttons,
        click_count,
        platform_->CurrentModifiers(),
        platform_->Core().NowMilliseconds(),
    });
    RequestFrame();
}
void NativeHost::DispatchPointerMove(float x, float y, std::uint32_t modifiers) {
    platform_->Core().InputRouter().DispatchPointerMove(NativePointerMoveInput{
        x,
        y,
        platform_->CurrentPointerButtons(),
        modifiers,
        platform_->Core().NowMilliseconds(),
    });
    RequestFrame();
}
void NativeHost::DispatchWheel(float delta_x, float delta_y) {
    platform_->Core().InputRouter().DispatchWheel(delta_x, delta_y, platform_->Core().NowMilliseconds());
    RequestFrame();
}
void NativeHost::DispatchKey(const std::string& key, bool down, std::uint32_t modifiers) {
    platform_->Core().InputRouter().DispatchKey(key, down, modifiers, platform_->Core().NowMilliseconds());
    RequestFrame();
}
void NativeHost::DispatchWindowFocusLost() { platform_->DispatchWindowFocusLost(); }
void NativeHost::SetSystemDarkModeForTesting(bool dark_mode) {
    platform_->Core().SetSystemDarkMode(dark_mode);
    __fui_on_system_dark_mode_changed(dark_mode);
    RequestFrame();
}
void NativeHost::SetClipboardText(const std::string& text) { platform_->SetClipboardText(text); }
std::string NativeHost::ClipboardText() const { return platform_->ClipboardText(); }
bool NativeHost::OpenExternalUrl(const std::string& url) const { return platform_->OpenExternalUrl(url); }
bool NativeHost::OpenFile(const std::filesystem::path& path) const { return platform_->OpenFile(path); }
bool NativeHost::RevealFile(const std::filesystem::path& path) const { return platform_->RevealFile(path); }
void NativeHost::CompleteFileDialogForTesting(
    std::uint64_t request_id,
    std::uint32_t status,
    std::vector<std::string> paths,
    std::string error,
    std::int32_t selected_filter) {
    platform_->CompleteFileDialogForTesting(
        request_id, status, std::move(paths), std::move(error), selected_filter);
}
void NativeHost::DispatchDropEventForTesting(
    std::uint32_t event_type,
    float x,
    float y,
    const std::string& data) {
    platform_->DispatchDropEventForTesting(event_type, x, y, data);
}
std::uint64_t NativeHost::HitTest(float x, float y) const { return platform_->Core().GetEngine().HitTest(x, y); }
bool NativeHost::HasFontForTesting(std::uint32_t font_id) const {
    return platform_->Core().GetEngine().HasFontForTesting(font_id);
}
bool NativeHost::LoadFontForTesting(std::uint32_t font_id, const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.good()) return false;
    const std::streamoff size = input.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return false;
    if (!ui::GetRuntime().RegisterFont(
            font_id, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
        return false;
    }
    platform_->Core().GetEngine().RegisterFont(
        font_id, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    return HasFontForTesting(font_id);
}
bool NativeHost::FontHasGlyphForTesting(std::uint32_t font_id, std::uint32_t codepoint) const {
    return platform_->Core().GetEngine().FontHasGlyphForTesting(font_id, codepoint);
}
std::optional<std::pair<float, float>> NativeHost::SvgSizeForTesting(std::uint32_t svg_id) const {
    return platform_->Core().GetEngine().GetSvgSizeForTesting(svg_id);
}
void NativeHost::RegisterSvgForTesting(std::uint32_t svg_id, std::string_view source) {
    platform_->Core().GetEngine().RegisterSvg(
        svg_id,
        reinterpret_cast<const std::uint8_t*>(source.data()),
        static_cast<std::uint32_t>(source.size()));
}
std::optional<std::pair<std::uint32_t, std::uint32_t>> NativeHost::TextureSizeForTesting(
    std::uint32_t texture_id) const {
    return platform_->Core().GetEngine().GetTextureSizeForTesting(texture_id);
}
std::size_t NativeHost::TextureCountForTesting() const {
    return platform_->Core().GetEngine().TextureCountForTesting();
}
std::size_t NativeHost::PathCountForTesting() const {
    return platform_->Core().GetEngine().PathCountForTesting();
}
std::size_t NativeHost::OffscreenSurfaceCountForTesting() const {
    return platform_->Core().GetEngine().OffscreenSurfaceCountForTesting();
}
std::size_t NativeHost::FallbackFontCountForTesting() const { return platform_->FallbackFontCountForTesting(); }
void NativeHost::RequestMissingFontCoverageForTesting(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    const std::string& sample_text) {
    platform_->ReportMissingFontCoverage(primary_font_id, coverage_kind, sample_text);
}
NativeHostState NativeHost::State() const { return platform_->Core().State(); }
const NativeAccessibilitySnapshot& NativeHost::AccessibilitySnapshotForTesting() const {
    return platform_->Core().Accessibility().Snapshot();
}
std::vector<std::uint8_t> NativeHost::SnapshotRgba() const { return platform_->Core().SnapshotRgba(); }
bool NativeHost::WriteScreenshot(const std::filesystem::path& path, std::string& error) const {
    return platform_->Core().WriteScreenshot(path, error);
}
bool NativeHost::IsIdle() const { return !platform_->Core().IsFramePending(); }
bool NativeHost::IsRunning() const { return platform_->Core().IsRunning(); }

} // namespace effindom::v2::native
