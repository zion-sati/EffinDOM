#include "WindowsGpuSurface.h"

#include "SDL3/SDL.h"

#include <utility>

#if defined(EFFINDOM_SKIA_DIRECT3D)

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"

#include <array>
#include <algorithm>

namespace effindom::v2::native {
namespace {

constexpr std::size_t kBufferCount = 2U;

template <typename T>
gr_cp<T> Adopt(T* value) {
    return gr_cp<T>(value);
}

bool D3DSucceeded(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return true;
    SDL_Log("EffinDOM Direct3D initialization failed at %s (HRESULT 0x%08lX)",
            operation,
            static_cast<unsigned long>(result));
    return false;
}

} // namespace

struct WindowsGpuSurface::Impl {
    explicit Impl(SDL_Window* value) : window(value) {}

    bool Initialize() {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (hwnd == nullptr) {
            SDL_Log("EffinDOM Direct3D initialization failed: SDL did not provide an HWND");
            return false;
        }

        IDXGIFactory4* factory_raw = nullptr;
        if (!D3DSucceeded(CreateDXGIFactory1(IID_PPV_ARGS(&factory_raw)), "CreateDXGIFactory1")) return false;
        factory = Adopt(factory_raw);

        gr_cp<IDXGIAdapter1> software_adapter;
        for (UINT index = 0U;; ++index) {
            IDXGIAdapter1* candidate_raw = nullptr;
            if (factory->EnumAdapters1(index, &candidate_raw) == DXGI_ERROR_NOT_FOUND) break;
            gr_cp<IDXGIAdapter1> candidate = Adopt(candidate_raw);
            DXGI_ADAPTER_DESC1 description{};
            candidate->GetDesc1(&description);
            const HRESULT probe = D3D12CreateDevice(
                candidate.get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
            if (SUCCEEDED(probe)) {
                if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U) {
                    software_adapter = std::move(candidate);
                    continue;
                }
                adapter = std::move(candidate);
                break;
            }
            SDL_Log("EffinDOM Direct3D adapter %u rejected D3D12 feature level 11_0 (HRESULT 0x%08lX)",
                    index,
                    static_cast<unsigned long>(probe));
        }
        if (adapter.get() == nullptr && software_adapter.get() != nullptr) {
            SDL_Log("EffinDOM Direct3D hardware is unavailable; using the Windows software adapter");
            adapter = std::move(software_adapter);
        }
        if (adapter.get() == nullptr) {
            SDL_Log("EffinDOM Direct3D initialization failed: no D3D12 adapter was found");
            return false;
        }

        ID3D12Device* device_raw = nullptr;
        if (!D3DSucceeded(D3D12CreateDevice(
                adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_raw)), "D3D12CreateDevice")) return false;
        device = Adopt(device_raw);

        D3D12_COMMAND_QUEUE_DESC queue_description{};
        queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ID3D12CommandQueue* queue_raw = nullptr;
        if (!D3DSucceeded(device->CreateCommandQueue(
                &queue_description, IID_PPV_ARGS(&queue_raw)), "CreateCommandQueue")) return false;
        queue = Adopt(queue_raw);

        GrD3DBackendContext backend{};
        backend.fAdapter = adapter;
        backend.fDevice = device;
        backend.fQueue = queue;
        context = GrDirectContext::MakeDirect3D(backend);
        if (context == nullptr) {
            SDL_Log("EffinDOM Direct3D initialization failed: Skia rejected the D3D backend context");
            return false;
        }

        int drawable_width = 0;
        int drawable_height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height)) {
            SDL_Log("EffinDOM Direct3D initialization failed: %s", SDL_GetError());
            return false;
        }
        width = static_cast<std::uint32_t>(std::max(drawable_width, 1));
        height = static_cast<std::uint32_t>(std::max(drawable_height, 1));

        DXGI_SWAP_CHAIN_DESC1 swap_description{};
        swap_description.BufferCount = static_cast<UINT>(kBufferCount);
        swap_description.Width = width;
        swap_description.Height = height;
        swap_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_description.SampleDesc.Count = 1U;
        // Present(1) can queue another frame without waiting for the next
        // display interval. Pace retained animation frames with DXGI's native
        // latency object, equivalent to requestAnimationFrame on the web.
        swap_description.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        // Do not let DXGI stretch the last presented frame while the HWND and
        // swap-chain sizes briefly differ during an interactive resize. The
        // WM_PAINT bridge resizes and repaints the back buffer synchronously.
        swap_description.Scaling = DXGI_SCALING_NONE;

        IDXGISwapChain1* swap_raw = nullptr;
        if (!D3DSucceeded(factory->CreateSwapChainForHwnd(
                queue.get(), hwnd, &swap_description, nullptr, nullptr, &swap_raw),
                "CreateSwapChainForHwnd")) return false;
        gr_cp<IDXGISwapChain1> swap1 = Adopt(swap_raw);
        DXGI_SWAP_CHAIN_DESC1 actual_swap_description{};
        if (FAILED(swap1->GetDesc1(&actual_swap_description)) ||
            actual_swap_description.Scaling != DXGI_SCALING_NONE) return false;
        IDXGISwapChain3* swap3_raw = nullptr;
        if (!D3DSucceeded(swap1->QueryInterface(IID_PPV_ARGS(&swap3_raw)), "IDXGISwapChain3")) return false;
        swap_chain = Adopt(swap3_raw);
        if (!D3DSucceeded(swap_chain->SetMaximumFrameLatency(1U), "SetMaximumFrameLatency")) return false;
        frame_latency_waitable_object = swap_chain->GetFrameLatencyWaitableObject();
        if (frame_latency_waitable_object == nullptr) {
            SDL_Log("EffinDOM Direct3D initialization failed: no frame-latency waitable object");
            return false;
        }
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        ID3D12Fence* fence_raw = nullptr;
        if (!D3DSucceeded(device->CreateFence(
                0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_raw)), "CreateFence")) return false;
        fence = Adopt(fence_raw);
        buffer_fence_values.fill(0U);
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event == nullptr) return false;

        if (!CreateSurfaces()) return false;
        ++generation;
        return true;
    }

    bool CreateSurfaces() {
        GrD3DTextureResourceInfo resource_info(
            nullptr,
            nullptr,
            D3D12_RESOURCE_STATE_PRESENT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            1U,
            1U,
            0U);
        for (std::size_t index = 0U; index < kBufferCount; ++index) {
            ID3D12Resource* buffer_raw = nullptr;
            if (FAILED(swap_chain->GetBuffer(static_cast<UINT>(index), IID_PPV_ARGS(&buffer_raw)))) return false;
            buffers[index] = Adopt(buffer_raw);
            resource_info.fResource = buffers[index];
            GrBackendRenderTarget target(static_cast<int>(width), static_cast<int>(height), resource_info);
            surfaces[index] = SkSurfaces::WrapBackendRenderTarget(
                context.get(),
                target,
                kTopLeft_GrSurfaceOrigin,
                kRGBA_8888_SkColorType,
                SkColorSpace::MakeSRGB(),
                nullptr);
            if (surfaces[index] == nullptr) return false;
        }
        buffer_index = swap_chain->GetCurrentBackBufferIndex();
        return true;
    }

    void ReleaseSurfaces() {
        current_surface = nullptr;
        for (auto& surface : surfaces) surface.reset();
        for (auto& buffer : buffers) buffer.reset(nullptr);
    }

    bool WaitForGpu() {
        const std::uint64_t value = ++fence_value;
        if (FAILED(queue->Signal(fence.get(), value))) return false;
        for (auto& buffer_value : buffer_fence_values) buffer_value = value;
        return WaitForFence(value);
    }

    bool WaitForFence(std::uint64_t value) {
        if (value == 0U) return true;
        if (fence->GetCompletedValue() >= value) return true;
        if (FAILED(fence->SetEventOnCompletion(value, fence_event))) return false;
        return WaitForSingleObject(fence_event, 5000U) == WAIT_OBJECT_0;
    }

    bool SignalSubmittedBuffer(std::uint32_t index) {
        const std::uint64_t value = ++fence_value;
        if (FAILED(queue->Signal(fence.get(), value))) return false;
        buffer_fence_values[index] = value;
        return true;
    }

    bool WaitForFrameLatency() const {
        return frame_latency_waitable_object != nullptr &&
            WaitForSingleObject(frame_latency_waitable_object, 5000U) == WAIT_OBJECT_0;
    }

    bool Resize(std::uint32_t next_width, std::uint32_t next_height) {
        if (!WaitForGpu()) return false;
        context->flushAndSubmit(GrSyncCpu::kYes);
        ReleaseSurfaces();
        if (FAILED(swap_chain->ResizeBuffers(
                static_cast<UINT>(kBufferCount),
                next_width,
                next_height,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) return false;
        width = next_width;
        height = next_height;
        return CreateSurfaces();
    }

    void Shutdown() {
        if (context != nullptr) {
            context->flushAndSubmit(GrSyncCpu::kYes);
        }
        if (queue.get() != nullptr && fence.get() != nullptr && fence_event != nullptr) {
            WaitForGpu();
        }
        ReleaseSurfaces();
        context.reset();
        frame_latency_waitable_object = nullptr;
        swap_chain.reset(nullptr);
        fence.reset(nullptr);
        if (fence_event != nullptr) CloseHandle(fence_event);
        fence_event = nullptr;
        queue.reset(nullptr);
        device.reset(nullptr);
        adapter.reset(nullptr);
        factory.reset(nullptr);
    }

    SDL_Window* window = nullptr;
    gr_cp<IDXGIFactory4> factory;
    gr_cp<IDXGIAdapter1> adapter;
    gr_cp<ID3D12Device> device;
    gr_cp<ID3D12CommandQueue> queue;
    gr_cp<IDXGISwapChain3> swap_chain;
    gr_cp<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    HANDLE frame_latency_waitable_object = nullptr;
    sk_sp<GrDirectContext> context;
    std::array<gr_cp<ID3D12Resource>, kBufferCount> buffers;
    std::array<sk_sp<SkSurface>, kBufferCount> surfaces;
    std::array<std::uint64_t, kBufferCount> buffer_fence_values{};
    SkSurface* current_surface = nullptr;
    std::uint32_t buffer_index = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t fence_value = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t recovery_count = 0U;
    bool recovery_requested = false;
};

WindowsGpuSurface::WindowsGpuSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<WindowsGpuSurface> WindowsGpuSurface::Create(SDL_Window* window) {
    auto impl = std::make_unique<Impl>(window);
    if (!impl->Initialize()) {
        impl->Shutdown();
        return nullptr;
    }
    return std::unique_ptr<WindowsGpuSurface>(new WindowsGpuSurface(std::move(impl)));
}

WindowsGpuSurface::~WindowsGpuSurface() { impl_->Shutdown(); }

bool WindowsGpuSurface::PrepareFrame(std::uint32_t width, std::uint32_t height, float) {
    if (impl_->recovery_requested) {
        const std::uint64_t recoveries = impl_->recovery_count + 1U;
        impl_->Shutdown();
        impl_->recovery_count = recoveries;
        impl_->recovery_requested = false;
        if (!impl_->Initialize()) {
            impl_->Shutdown();
            impl_->recovery_requested = true;
            return true;
        }
    }
    width = std::max(width, 1U);
    height = std::max(height, 1U);
    if (!impl_->WaitForFrameLatency()) return false;
    if ((width != impl_->width || height != impl_->height) && !impl_->Resize(width, height)) return false;
    impl_->buffer_index = impl_->swap_chain->GetCurrentBackBufferIndex();
    if (!impl_->WaitForFence(impl_->buffer_fence_values[impl_->buffer_index])) return false;
    impl_->current_surface = impl_->surfaces[impl_->buffer_index].get();
    return impl_->current_surface != nullptr;
}

bool WindowsGpuSurface::QueryOutputSize(int& width, int& height) const {
    return SDL_GetWindowSizeInPixels(impl_->window, &width, &height);
}

SkCanvas* WindowsGpuSurface::Canvas() const {
    return impl_->current_surface == nullptr ? nullptr : impl_->current_surface->getCanvas();
}

SkSurface* WindowsGpuSurface::Surface() const { return impl_->current_surface; }

bool WindowsGpuSurface::Present() {
    if (impl_->current_surface == nullptr) return false;
    const std::uint32_t submitted_buffer = impl_->buffer_index;
    GrFlushInfo info{};
    impl_->context->flush(impl_->current_surface, SkSurfaces::BackendSurfaceAccess::kPresent, info);
    impl_->context->submit();
    if (FAILED(impl_->swap_chain->Present(1U, 0U))) return false;
    return impl_->SignalSubmittedBuffer(submitted_buffer);
}

void WindowsGpuSurface::RequestRecovery() { impl_->recovery_requested = true; }
bool WindowsGpuSurface::HandleRecoveryEvent(const SDL_Event&) { return false; }
std::uint64_t WindowsGpuSurface::Generation() const { return impl_->generation; }
std::uint64_t WindowsGpuSurface::RecoveryCount() const { return impl_->recovery_count; }

} // namespace effindom::v2::native

#else

namespace effindom::v2::native {

struct WindowsGpuSurface::Impl {};

std::unique_ptr<WindowsGpuSurface> WindowsGpuSurface::Create(SDL_Window*) { return nullptr; }
WindowsGpuSurface::WindowsGpuSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WindowsGpuSurface::~WindowsGpuSurface() = default;
bool WindowsGpuSurface::PrepareFrame(std::uint32_t, std::uint32_t, float) { return false; }
bool WindowsGpuSurface::QueryOutputSize(int&, int&) const { return false; }
SkCanvas* WindowsGpuSurface::Canvas() const { return nullptr; }
SkSurface* WindowsGpuSurface::Surface() const { return nullptr; }
bool WindowsGpuSurface::Present() { return false; }
void WindowsGpuSurface::RequestRecovery() {}
bool WindowsGpuSurface::HandleRecoveryEvent(const SDL_Event&) { return false; }
std::uint64_t WindowsGpuSurface::Generation() const { return 0U; }
std::uint64_t WindowsGpuSurface::RecoveryCount() const { return 0U; }

} // namespace effindom::v2::native

#endif
