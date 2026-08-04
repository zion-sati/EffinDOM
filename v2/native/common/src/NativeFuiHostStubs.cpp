#include "effindom_ui.h"
#include "NativeFuiUnsupportedCapabilities.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Desktop-neutral definitions for intentional browser-only and native-fallback
// FUI host imports. Genuine desktop capabilities live in
// NativeFuiRuntimeBridge.cpp. Public unsupported operations reject through the
// normal FUI completion channel and report one diagnostic per capability.
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

// Browser networking. Native applications use their language's networking APIs.
void fui_fetch_start(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFetch(request_id);
}
void fui_fetch_cancel(std::uint32_t) {
    effindom::v2::native::ReportUnsupportedFuiCapability(
        "fui_fetch_cancel", "FUI Fetch is a browser host capability; no native request was started.");
}

// Browser history-backed retained-state restoration has no native equivalent.
// Universal applications call these as part of normal control lifecycle, so
// native hosts quietly use the documented unavailable defaults.
void fui_set_persisted_scroll_offset(std::uintptr_t, std::uint32_t, float, float) {}
bool fui_try_get_persisted_scroll_offset(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uintptr_t) {
    return false;
}
void fui_set_persisted_state(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t,
    std::uintptr_t, std::uint32_t) {}
std::int32_t fui_copy_persisted_state(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uintptr_t, std::uint32_t) {
    return -1;
}

// Browser file handles and streams. Native file dialogs are exposed separately
// by the desktop host; application file I/O belongs to native language APIs.
std::uint32_t fui_file_capabilities() { return 0U; }
void fui_file_pick(std::uint32_t request_id, std::uintptr_t, std::uint32_t, bool) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::Pick, request_id);
}
void fui_file_read_chunk(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uint64_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::Read, request_id);
}
void fui_file_save_text(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::Save, request_id);
}
void fui_file_save_bytes(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::Save, request_id);
}
void fui_file_create_writer(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::CreateWriter, request_id);
}
void fui_file_writer_write_text(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::Write, request_id);
}
void fui_file_writer_write_bytes(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::Write, request_id);
}
void fui_file_writer_finish(std::uint32_t request_id, std::uintptr_t, std::uint32_t) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::Finish, request_id);
}
void fui_file_process_worker_start(
    std::uint32_t request_id, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t, bool) {
    effindom::v2::native::RejectUnsupportedFile(
        effindom::v2::native::UnsupportedFileOperation::ProcessInWorker, request_id);
}
void fui_file_process_worker_cancel(std::uint32_t) {
    effindom::v2::native::ReportUnsupportedFuiCapability(
        "fui_file_process_worker_cancel", "Browser file-processing Workers are unavailable on native hosts.");
}

// Browser navigation and URL chrome. Native NavLink navigation to an external
// URI remains implemented by fui_navigate_to in NativeFuiRuntimeBridge.cpp.
void fui_reload_page() {
    effindom::v2::native::ReportUnsupportedFuiCapability(
        "fui_reload_page", "Browser page reload is unavailable on native hosts.");
}
bool fui_can_navigate_back() { return false; }
bool fui_can_navigate_forward() { return false; }
void fui_navigate_back() {
    effindom::v2::native::ReportUnsupportedFuiCapability(
        "fui_navigate_back", "Browser history navigation is unavailable on native hosts.");
}
void fui_navigate_forward() {
    effindom::v2::native::ReportUnsupportedFuiCapability(
        "fui_navigate_forward", "Browser history navigation is unavailable on native hosts.");
}
void fui_show_url_preview(std::uintptr_t, std::uint32_t) {}
void fui_hide_url_preview() {}

} // extern "C"
