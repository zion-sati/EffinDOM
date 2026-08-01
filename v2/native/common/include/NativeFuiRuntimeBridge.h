#pragma once

#include <cstdint>

namespace effindom::v2::native {

class NativePlatformHost;
class NativeHostCore;

struct NativeFuiDrawingMetrics {
    std::uint64_t batch_count = 0U;
    std::uint64_t batch_bytes = 0U;
    std::uint64_t bitmap_upload_count = 0U;
    std::uint64_t bitmap_upload_bytes = 0U;
    std::uint64_t dirty_upload_count = 0U;
    std::uint64_t dirty_upload_bytes = 0U;
};

// The platform factory binds exactly one live host while the native FUI
// runtime is mounted. The C ABI remains platform-neutral and delegates only
// genuine OS operations through NativePlatformHost.
void SetActiveNativePlatformHost(NativePlatformHost* host);
NativePlatformHost* ActiveNativePlatformHost();

// Acceptance-only observability for the shared native drawing bridge. These
// counters deliberately live above Metal, D3D, Vulkan, and raster presentation
// so every native target measures the same FUI traffic.
NativeFuiDrawingMetrics NativeFuiDrawingMetricsForTesting();
void ResetNativeFuiDrawingMetricsForTesting();

// Validates the native pointer boundary, then delegates the unchanged FUI
// command stream to the engine's transactional batch decoder.
bool DrawCanvasBatch(
    NativeHostCore& host,
    std::uintptr_t canvas_pointer,
    std::uintptr_t words_pointer,
    std::uint32_t word_count);
std::uint32_t CreatePath(NativeHostCore& host);
bool DestroyPath(NativeHostCore& host, std::uint32_t path_id);
bool PathMoveTo(NativeHostCore& host, std::uint32_t path_id, float x, float y);
bool PathLineTo(NativeHostCore& host, std::uint32_t path_id, float x, float y);
bool PathQuadTo(NativeHostCore& host, std::uint32_t path_id, float cx, float cy, float x, float y);
bool PathCubicTo(
    NativeHostCore& host,
    std::uint32_t path_id,
    float cx1,
    float cy1,
    float cx2,
    float cy2,
    float x,
    float y);
bool PathClose(NativeHostCore& host, std::uint32_t path_id);
bool PathAddRect(NativeHostCore& host, std::uint32_t path_id, float x, float y, float width, float height);
bool PathAddCircle(NativeHostCore& host, std::uint32_t path_id, float cx, float cy, float radius);
bool CommitBitmap(
    NativeHostCore& host,
    std::uint32_t texture_id,
    std::uintptr_t pixels_pointer,
    std::uint32_t byte_length,
    std::uint32_t width,
    std::uint32_t height);
bool CommitBitmapDirty(
    NativeHostCore& host,
    std::uint32_t texture_id,
    std::uintptr_t pixels_pointer,
    std::uint32_t byte_length,
    std::uint32_t full_width,
    std::uint32_t full_height,
    std::uint32_t sub_x,
    std::uint32_t sub_y,
    std::uint32_t sub_width,
    std::uint32_t sub_height);
bool ReleaseBitmap(NativeHostCore& host, std::uint32_t texture_id);
std::uint32_t CreateOffscreenSurface(NativeHostCore& host, std::uint32_t width, std::uint32_t height);
std::uintptr_t GetOffscreenCanvas(NativeHostCore& host, std::uint32_t offscreen_id);
bool ReadOffscreenPixels(
    NativeHostCore& host,
    std::uint32_t offscreen_id,
    std::uintptr_t output_pointer,
    std::uint32_t width,
    std::uint32_t height);
bool DestroyOffscreenSurface(NativeHostCore& host, std::uint32_t offscreen_id);
std::uint32_t RenderNodeToRgba(
    NativeHostCore& host,
    std::uint64_t handle,
    std::uint32_t width,
    std::uint32_t height,
    std::uintptr_t output_pointer,
    std::uint32_t output_capacity,
    float scale,
    float x,
    float y);

} // namespace effindom::v2::native
