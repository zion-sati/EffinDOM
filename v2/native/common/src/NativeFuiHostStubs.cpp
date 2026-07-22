#include "effindom_ui.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Desktop-neutral definitions for browser-only FUI host imports. Native
// capabilities implemented by the desktop host live in
// NativeFuiRuntimeBridge.cpp; unsupported browser facilities remain explicit.
extern "C" {

void fui_register_text_input_metadata(std::uint64_t, bool, std::uintptr_t, std::uint32_t) {}
bool fui_has_text_selection_snapshot(std::uint64_t) { return false; }
void fui_freeze_text_selection_snapshot(std::uint64_t) {}
bool fui_copy_text_selection_snapshot(std::uint64_t) { return false; }
bool fui_cut_focused_text_selection() { return false; }
bool fui_cut_text_selection_snapshot(std::uint64_t) { return false; }
bool fui_cut_text_range_snapshot(std::uint64_t handle, std::uint32_t start, std::uint32_t end) {
    if (handle == 0U || start == end) return false;
    const std::uint32_t range_start = std::min(start, end);
    const std::uint32_t range_end = std::max(start, end);
    ui_replace_text_range(handle, range_start, range_end, nullptr, 0U, range_start);
    return true;
}
bool fui_delete_focused_text_range(std::uint32_t, std::uint32_t) { return false; }
void fui_commit_text_action_focus(std::uint64_t) {}
void fui_start_timer(std::uint32_t, std::int32_t) {}
void fui_cancel_timer(std::uint32_t) {}
void fui_bitmap_commit(std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
void fui_bitmap_commit_dirty(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
void fui_bitmap_release(std::uint32_t) {}
std::uint32_t fui_render_node_to_rgba(
    std::uint64_t, std::uint32_t, std::uint32_t, std::uintptr_t,
    std::uint32_t, float, float, float) { return 0U; }
void fui_fetch_start(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t) {}
void fui_fetch_cancel(std::uint32_t) {}
void fui_set_persisted_scroll_offset(std::uintptr_t, std::uint32_t, float, float) {}
bool fui_try_get_persisted_scroll_offset(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uintptr_t) { return false; }
void fui_set_persisted_state(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t,
    std::uintptr_t, std::uint32_t) {}
std::int32_t fui_copy_persisted_state(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uintptr_t, std::uint32_t) { return -1; }
void fui_worker_cancel(std::uint32_t) {}
std::uint32_t fui_file_capabilities() { return 0U; }
void fui_file_process_worker_start(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t, bool) {}
void fui_file_process_worker_cancel(std::uint32_t) {}
std::uint32_t fui_path_create() { return 0U; }
void fui_path_destroy(std::uint32_t) {}
void fui_path_move_to(std::uint32_t, float, float) {}
void fui_path_line_to(std::uint32_t, float, float) {}
void fui_path_quad_to(std::uint32_t, float, float, float, float) {}
void fui_path_cubic_to(std::uint32_t, float, float, float, float, float, float) {}
void fui_path_close(std::uint32_t) {}
void fui_path_add_rect(std::uint32_t, float, float, float, float) {}
void fui_path_add_circle(std::uint32_t, float, float, float) {}
void fui_canvas_draw_batch(std::uintptr_t, std::uintptr_t, std::uint32_t) {}
std::uint32_t fui_canvas_create_offscreen(std::uint32_t, std::uint32_t) { return 0U; }
std::uintptr_t fui_canvas_get_offscreen_ptr(std::uint32_t) { return 0U; }
void fui_canvas_read_offscreen_pixels(std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t) {}
void fui_canvas_destroy_offscreen(std::uint32_t) {}

} // extern "C"
