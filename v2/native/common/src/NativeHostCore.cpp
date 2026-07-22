#include "NativeHostCore.h"

#include "NativeFuiBridge.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/encode/SkPngEncoder.h"

#include <stdexcept>
#include <utility>

namespace effindom::v2::native {
namespace {

SkColor RgbaToSkColor(std::uint32_t rgba) {
    return SkColorSetARGB(
        rgba & 0xFFU,
        (rgba >> 24U) & 0xFFU,
        (rgba >> 16U) & 0xFFU,
        (rgba >> 8U) & 0xFFU);
}

} // namespace

NativeHostCore::NativeHostCore(NativeInputRouterOptions input_options,
    NativeHostCoreCallbacks callbacks)
    : input_router_(engine_, input_options), accessibility_([this] { RequestFrame(); }),
      callbacks_(std::move(callbacks)),
      start_time_(Clock::now()) {}

void NativeHostCore::AttachGraphics(
    std::unique_ptr<NativeGraphicsCoordinator> graphics) {
    if (graphics == nullptr) throw std::invalid_argument("native graphics coordinator is required");
    graphics_ = std::move(graphics);
    graphics_->SetBackdropColor(backdrop_color_);
    RefreshSurface();
}

void NativeHostCore::ReleaseGraphics() { graphics_.reset(); }

void NativeHostCore::AttachAccessibility(std::unique_ptr<NativeAccessibilityAdapter> adapter) {
    accessibility_.Attach(std::move(adapter));
}

void NativeHostCore::InitializeEngine() {
    if (graphics_ == nullptr) throw std::logic_error("native graphics is not attached");
    engine_.Init(physical_width_, physical_height_, pixel_density_);
    engine_.SetViewportSize(logical_width_, logical_height_);
}

void NativeHostCore::MountApplication() {
    if (mounted_) ++dispose_count_;
    mounted_ = true;
    __runApp();
    ++mount_count_;
    ui_resize_window(logical_width_, logical_height_);
    engine_.SetViewportSize(logical_width_, logical_height_);
    RequestFrame();
}

void NativeHostCore::UnmountApplication() {
    if (!mounted_) return;
    __disposeApp();
    ++dispose_count_;
    if (callbacks_.clear_application_state) callbacks_.clear_application_state();
    accessibility_.Clear();
    mounted_ = false;
    frame_pending_ = false;
}

void NativeHostCore::RequestFrame() { frame_pending_ = mounted_; }

void NativeHostCore::SetSystemDarkMode(bool dark_mode) {
    backdrop_color_ = dark_mode ? 0x111827FFU : 0xFFFFFFFFU;
    if (graphics_ != nullptr) graphics_->SetBackdropColor(backdrop_color_);
    RequestFrame();
}

bool NativeHostCore::RunNextFrame() {
    if (!mounted_ || !frame_pending_) return false;
    if (graphics_->IsSuspended()) {
        frame_pending_ = false;
        return false;
    }
    frame_pending_ = false;
    const auto frame_started = Clock::now();
    const char* diagnostic = SDL_GetEnvironmentVariable(
        SDL_GetEnvironment(), "EFFINDOM_NATIVE_CHARACTERIZE");
    const bool characterize = diagnostic != nullptr && diagnostic[0] != '\0' && diagnostic[0] != '0';
    const auto elapsed_ms = [](Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    const bool pending_assets = callbacks_.process_pending_assets
        ? callbacks_.process_pending_assets()
        : false;
    const auto assets_finished = Clock::now();
    const bool viewport_changed = RefreshWindowGeometry();
    const auto geometry_finished = Clock::now();
    if (!graphics_->PrepareFrame()) {
        if (graphics_->IsGpuBacked()) {
            graphics_->RequestRecovery();
            frame_pending_ = true;
            return false;
        }
        throw std::runtime_error("native graphics surface could not prepare a frame");
    }
    const auto prepare_finished = Clock::now();
    managed_commit_applied_ = false;
    __flushRenders();
    const auto flush_finished = Clock::now();
    const bool input_commit_requested = input_router_.ConsumeCommitRequest();
    if (!managed_commit_applied_ &&
        (viewport_changed || input_commit_requested || ui_has_pending_visual_work())) {
        ui_commit_frame(NowMilliseconds());
        ApplyCommittedCommands();
    }
    const auto commit_finished = Clock::now();
    SkCanvas* canvas = graphics_->Canvas();
    if (canvas == nullptr) {
        frame_pending_ = true;
        return false;
    }
    const SkColor backdrop = RgbaToSkColor(backdrop_color_);
    canvas->clear(backdrop);
    engine_.RenderToCanvas(canvas, NowMilliseconds());
    canvas->drawColor(backdrop, SkBlendMode::kDstOver);
    const auto render_finished = Clock::now();
    if (!graphics_->Present()) {
        if (graphics_->IsGpuBacked()) {
            graphics_->RequestRecovery();
            frame_pending_ = true;
            return false;
        }
        throw std::runtime_error(std::string("SDL raster presentation failed: ") + SDL_GetError());
    }
    const auto present_finished = Clock::now();
    if (characterize) {
        SDL_Log("EffinDOM native frame: viewport_changed=%d assets=%.3f geometry=%.3f "
                "prepare=%.3f flush=%.3f commit=%.3f render=%.3f present=%.3f total=%.3f",
            viewport_changed ? 1 : 0,
            elapsed_ms(frame_started, assets_finished),
            elapsed_ms(assets_finished, geometry_finished),
            elapsed_ms(geometry_finished, prepare_finished),
            elapsed_ms(prepare_finished, flush_finished),
            elapsed_ms(flush_finished, commit_finished),
            elapsed_ms(commit_finished, render_finished),
            elapsed_ms(render_finished, present_finished),
            elapsed_ms(frame_started, present_finished));
    }
    ++frame_count_;
    if (ui_needs_animation_frame() || pending_assets) frame_pending_ = true;
    return true;
}

void NativeHostCore::DrainFrames(std::uint32_t maximum_frames) {
    std::uint32_t count = 0U;
    while (frame_pending_ && count < maximum_frames) {
        RunNextFrame();
        ++count;
    }
    if (frame_pending_) throw std::runtime_error("native frame queue did not become idle");
}

void NativeHostCore::ApplyManagedCommittedCommands() {
    managed_commit_applied_ = true;
    ApplyCommittedCommands();
}

double NativeHostCore::NowMilliseconds() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - start_time_).count();
}

void NativeHostCore::RefreshSurface() {
    if (!graphics_->RefreshGeometry()) {
        throw std::runtime_error(std::string("SDL drawable size query failed: ") + SDL_GetError());
    }
    const auto& geometry = graphics_->Geometry();
    physical_width_ = geometry.physical_width;
    physical_height_ = geometry.physical_height;
    pixel_density_ = geometry.pixel_density;
    logical_width_ = geometry.logical_width;
    logical_height_ = geometry.logical_height;
}

bool NativeHostCore::RefreshWindowGeometry() {
    const auto previous_physical_width = physical_width_;
    const auto previous_physical_height = physical_height_;
    const auto previous_logical_width = logical_width_;
    const auto previous_logical_height = logical_height_;
    const auto previous_pixel_density = pixel_density_;
    RefreshSurface();
    if (physical_width_ == previous_physical_width &&
        physical_height_ == previous_physical_height &&
        logical_width_ == previous_logical_width &&
        logical_height_ == previous_logical_height &&
        pixel_density_ == previous_pixel_density) {
        return false;
    }
    engine_.Resize(physical_width_, physical_height_, pixel_density_);
    engine_.SetViewportSize(logical_width_, logical_height_);
    __fui_on_viewport_changed(logical_width_, logical_height_);
    return true;
}

void NativeHostCore::ApplyCommittedCommands() {
    std::uint32_t command_count = 0U;
    const std::uint32_t* commands = ui_get_command_buffer(&command_count);
    engine_.ExecuteCommandBuffer(commands, command_count);
    std::uint32_t semantic_count = 0U;
    const std::uint32_t* semantics = ui_get_semantic_buffer(&semantic_count);
    accessibility_.Sync(semantics, semantic_count, ui_get_focused_handle());
}

NativeHostState NativeHostCore::State() const {
    return NativeHostState{
        __fui_native_activation_count(), mount_count_, dispose_count_, frame_count_,
        logical_width_, logical_height_, pixel_density_, frame_pending_,
        graphics_->IsGpuBacked(), graphics_->Generation(), graphics_->RecoveryCount(),
        graphics_->IsSuspended(),
    };
}

std::vector<std::uint8_t> NativeHostCore::SnapshotRgba() const {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(physical_width_) * physical_height_ * 4U);
    const SkImageInfo info = SkImageInfo::Make(physical_width_, physical_height_,
        kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    SkSurface* surface = graphics_->Surface();
    if (surface == nullptr || !surface->readPixels(info, pixels.data(),
            static_cast<std::size_t>(physical_width_) * 4U, 0, 0)) return {};
    return pixels;
}

bool NativeHostCore::WriteScreenshot(
    const std::filesystem::path& path, std::string& error) const {
    SkSurface* surface = graphics_->Surface();
    sk_sp<SkImage> image = surface == nullptr ? nullptr : surface->makeImageSnapshot();
    if (image == nullptr) {
        error = "failed to snapshot native surface";
        return false;
    }
    SkPixmap pixmap;
    if (!image->peekPixels(&pixmap)) {
        error = "failed to access native surface pixels";
        return false;
    }
    SkFILEWStream stream(path.string().c_str());
    if (!stream.isValid() || !SkPngEncoder::Encode(&stream, pixmap, {})) {
        error = "failed to encode native screenshot";
        return false;
    }
    return true;
}

Engine& NativeHostCore::GetEngine() { return engine_; }
const Engine& NativeHostCore::GetEngine() const { return engine_; }
NativeInputRouter& NativeHostCore::InputRouter() { return input_router_; }
const NativeInputRouter& NativeHostCore::InputRouter() const { return input_router_; }
NativeGraphicsCoordinator& NativeHostCore::Graphics() { return *graphics_; }
const NativeGraphicsCoordinator& NativeHostCore::Graphics() const { return *graphics_; }
NativeAccessibilityCoordinator& NativeHostCore::Accessibility() { return accessibility_; }
const NativeAccessibilityCoordinator& NativeHostCore::Accessibility() const { return accessibility_; }
void NativeHostCore::AnnounceSemantic(std::uint64_t handle) { accessibility_.Announce(handle); }
float NativeHostCore::LogicalWidth() const { return logical_width_; }
float NativeHostCore::LogicalHeight() const { return logical_height_; }
float NativeHostCore::PixelDensity() const { return pixel_density_; }
std::uint32_t NativeHostCore::PhysicalWidth() const { return physical_width_; }
std::uint32_t NativeHostCore::PhysicalHeight() const { return physical_height_; }
std::uint64_t NativeHostCore::FrameCount() const { return frame_count_; }
bool NativeHostCore::IsFramePending() const { return frame_pending_; }
void NativeHostCore::CancelPendingFrame() { frame_pending_ = false; }
bool NativeHostCore::IsMounted() const { return mounted_; }
bool NativeHostCore::IsRunning() const { return running_; }
void NativeHostCore::Stop() { running_ = false; }

} // namespace effindom::v2::native
