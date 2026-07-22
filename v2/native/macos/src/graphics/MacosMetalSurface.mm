#include "MacosMetalSurface.h"

#include "SDL3/SDL.h"
#include "SDL3/SDL_metal.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/gpu/ganesh/mtl/SkSurfaceMetal.h"
#include "include/ports/SkCFObject.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#include <atomic>
#include <memory>

namespace effindom::v2::native {
namespace {

std::atomic_bool fail_next_initialization{false};
std::atomic_bool fail_next_recovery{false};

} // namespace

struct MacosMetalSurface::Impl {
    explicit Impl(SDL_Window* owner_window) : window(owner_window) {}

    ~Impl() {
        WaitForIdle();
        ReleaseDrawable();
        if (context != nullptr) {
            if (!context->abandoned()) context->abandonContext();
            context.reset();
        }
        queue.reset();
        device.reset();
        if (view != nullptr) SDL_Metal_DestroyView(view);
    }

    bool Initialize() {
        if (fail_next_initialization.exchange(false)) return false;
        SDL_SetHint(SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE, "0");
        recovery_event = SDL_RegisterEvents(1);
        if (recovery_event == 0U) return false;
        view = SDL_Metal_CreateView(window);
        if (view == nullptr) return false;
        layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);
        if (layer == nil || !CreateContext()) return false;
        generation = 1U;
        return true;
    }

    void ConfigureLayer() {
        layer.device = device.get();
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;
        layer.opaque = YES;
        ApplyBackdropColor();
        layer.contentsGravity = kCAGravityTopLeft;
        layer.masksToBounds = YES;
        layer.actions = @{
            @"bounds": [NSNull null],
            @"position": [NSNull null],
            @"contents": [NSNull null],
        };
    }

    void ApplyBackdropColor() {
        const CGFloat red = static_cast<CGFloat>((backdrop_color >> 24U) & 0xFFU) / 255.0;
        const CGFloat green = static_cast<CGFloat>((backdrop_color >> 16U) & 0xFFU) / 255.0;
        const CGFloat blue = static_cast<CGFloat>((backdrop_color >> 8U) & 0xFFU) / 255.0;
        const CGFloat alpha = static_cast<CGFloat>(backdrop_color & 0xFFU) / 255.0;
        NSColor* color = [NSColor colorWithSRGBRed:red green:green blue:blue alpha:alpha];
        if (layer != nil) layer.backgroundColor = color.CGColor;
        NSWindow* native_window = (__bridge NSWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        if (native_window != nil) native_window.backgroundColor = color;
    }

    bool CreateContext() {
        device.reset(MTLCreateSystemDefaultDevice());
        if (device == nil) return false;
        queue.reset([device.get() newCommandQueue]);
        if (queue == nil) return false;
        ConfigureLayer();
        GrMtlBackendContext backend{};
        backend.fDevice.retain((__bridge GrMTLHandle)device.get());
        backend.fQueue.retain((__bridge GrMTLHandle)queue.get());
        context = GrDirectContexts::MakeMetal(backend);
        return context != nullptr;
    }

    void ReleaseDrawable() {
        surface.reset();
        if (drawable != nullptr) {
            CFRelease(drawable);
            drawable = nullptr;
        }
    }

    void WaitForIdle() {
        if (last_command_buffer == nil) return;
        [last_command_buffer.get() waitUntilCompleted];
        last_command_buffer.reset();
    }

    bool Recover() {
        if (fail_next_recovery.exchange(false)) return false;
        WaitForIdle();
        ReleaseDrawable();
        if (context != nullptr) {
            if (!context->abandoned()) context->abandonContext();
            context.reset();
        }
        queue.reset();
        device.reset();
        if (!CreateContext()) return false;
        command_failed->store(false);
        generation += 1U;
        recovery_count += 1U;
        return true;
    }

    bool PrepareFrame(std::uint32_t width, std::uint32_t height, float pixel_density) {
        const bool context_lost = context == nullptr || context->abandoned() || command_failed->exchange(false);
        if (recovery_requested || context_lost) {
            if (!Recover()) {
                // Preserve Metal as the selected strategy and retry rather
                // than silently changing renderer after a runtime failure.
                ReleaseDrawable();
                recovery_requested = true;
                return true;
            }
            recovery_requested = false;
        }

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        layer.contentsScale = pixel_density;
        layer.drawableSize = CGSizeMake(width, height);
        [CATransaction commit];

        ReleaseDrawable();
        surface = SkSurfaces::WrapCAMetalLayer(
            context.get(),
            (__bridge GrMTLHandle)layer,
            kTopLeft_GrSurfaceOrigin,
            1,
            kBGRA_8888_SkColorType,
            nullptr,
            nullptr,
            &drawable);
        return surface != nullptr;
    }

    bool Present() {
        context->flushAndSubmit(surface.get());
        if (drawable == nullptr) return false;
        id<CAMetalDrawable> current_drawable = (__bridge id<CAMetalDrawable>)drawable;
        id<MTLCommandBuffer> command_buffer = [queue.get() commandBuffer];
        if (command_buffer == nil) return false;
        command_buffer.label = @"EffinDOM Present";
        const auto failure_flag = command_failed;
        const auto event_type = recovery_event;
        const auto window_id = SDL_GetWindowID(window);
        [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if (completed.status != MTLCommandBufferStatusError) return;
            failure_flag->store(true);
            SDL_Event event{};
            event.type = event_type;
            event.user.windowID = window_id;
            SDL_PushEvent(&event);
        }];
        [command_buffer presentDrawable:current_drawable];
        [command_buffer commit];
        last_command_buffer.retain(command_buffer);
        CFRelease(drawable);
        drawable = nullptr;
        return true;
    }

    SDL_Window* window = nullptr;
    SDL_MetalView view = nullptr;
    CAMetalLayer* layer = nil;
    sk_cfp<id<MTLDevice>> device;
    sk_cfp<id<MTLCommandQueue>> queue;
    sk_cfp<id<MTLCommandBuffer>> last_command_buffer;
    sk_sp<GrDirectContext> context;
    sk_sp<SkSurface> surface;
    GrMTLHandle drawable = nullptr;
    std::shared_ptr<std::atomic_bool> command_failed = std::make_shared<std::atomic_bool>(false);
    std::uint32_t recovery_event = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t recovery_count = 0U;
    bool recovery_requested = false;
    std::uint32_t backdrop_color = 0xFFFFFFFFU;
};

std::unique_ptr<MacosMetalSurface> MacosMetalSurface::Create(SDL_Window* window) {
    auto impl = std::make_unique<Impl>(window);
    if (!impl->Initialize()) return nullptr;
    return std::unique_ptr<MacosMetalSurface>(new MacosMetalSurface(std::move(impl)));
}

void MacosMetalSurface::FailNextInitializationForTesting() {
    fail_next_initialization.store(true);
}

void MacosMetalSurface::FailNextRecoveryForTesting() {
    fail_next_recovery.store(true);
}

MacosMetalSurface::MacosMetalSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MacosMetalSurface::~MacosMetalSurface() = default;

bool MacosMetalSurface::PrepareFrame(std::uint32_t width, std::uint32_t height, float pixel_density) {
    return impl_->PrepareFrame(width, height, pixel_density);
}

bool MacosMetalSurface::QueryOutputSize(int& width, int& height) const {
    return SDL_GetWindowSizeInPixels(impl_->window, &width, &height);
}

bool MacosMetalSurface::Present() { return impl_->Present(); }
void MacosMetalSurface::SetBackdropColor(std::uint32_t rgba) {
    impl_->backdrop_color = rgba;
    impl_->ApplyBackdropColor();
}
void MacosMetalSurface::RequestRecovery() { impl_->recovery_requested = true; }

bool MacosMetalSurface::HandleRecoveryEvent(const SDL_Event& event) {
    if (event.type != impl_->recovery_event) return false;
    if (impl_->command_failed->exchange(false)) impl_->recovery_requested = true;
    return true;
}

SkCanvas* MacosMetalSurface::Canvas() const {
    return impl_->surface == nullptr ? nullptr : impl_->surface->getCanvas();
}

SkSurface* MacosMetalSurface::Surface() const { return impl_->surface.get(); }
std::uint64_t MacosMetalSurface::Generation() const { return impl_->generation; }
std::uint64_t MacosMetalSurface::RecoveryCount() const { return impl_->recovery_count; }

} // namespace effindom::v2::native
