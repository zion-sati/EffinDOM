#include "TestUiSupport.h"

namespace test_ui_support {

std::vector<PointerEventRecord> g_pointer_events{};
std::vector<FocusEventRecord> g_focus_events{};
std::vector<TextChangeRecord> g_text_changes{};
std::vector<SelectionChangeRecord> g_selection_changes{};
std::vector<CrossSelectionChangeRecord> g_cross_selection_changes{};
std::vector<ClipboardWriteRecord> g_clipboard_writes{};
std::vector<ClipboardReadRequestRecord> g_clipboard_read_requests{};
std::vector<ScrollChangeRecord> g_scroll_changes{};
std::vector<MissingFontCoverageRecord> g_missing_font_coverage_requests{};
PointerEventCallback g_pointer_event_callback = &RecordPointerEvent;
FocusEventCallback g_focus_event_callback = &RecordFocusEvent;
TextChangeCallback g_text_change_callback = &RecordTextChange;
SelectionChangeCallback g_selection_change_callback = &RecordSelectionChange;
ScrollChangeCallback g_scroll_change_callback = &RecordScrollChange;

void RecordPointerEvent(std::uint64_t handle, std::uint32_t event) {
    g_pointer_events.push_back(PointerEventRecord{handle, event});
}

void RecordFocusEvent(std::uint64_t handle, bool is_focused) {
    g_focus_events.push_back(FocusEventRecord{handle, is_focused});
}

void RecordTextChange(std::uint64_t handle, const std::string& text) {
    g_text_changes.push_back(TextChangeRecord{handle, text});
}

void RecordSelectionChange(std::uint64_t handle, std::uint32_t start, std::uint32_t end) {
    g_selection_changes.push_back(SelectionChangeRecord{handle, start, end});
}

void RecordScrollChange(
    std::uint64_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height) {
    g_scroll_changes.push_back(ScrollChangeRecord{
        handle,
        offset_x,
        offset_y,
        content_width,
        content_height,
        viewport_width,
        viewport_height,
    });
}

} // namespace test_ui_support

extern "C" void as_on_pointer_event(ui_handle_t handle, uint32_t event_enum) {
    if (test_ui_support::g_pointer_event_callback != nullptr) {
        test_ui_support::g_pointer_event_callback(handle, event_enum);
    }
}

extern "C" void as_on_focus_changed(ui_handle_t handle, bool is_focused) {
    if (test_ui_support::g_focus_event_callback != nullptr) {
        test_ui_support::g_focus_event_callback(handle, is_focused);
    }
}

extern "C" void as_on_text_changed(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len) {
    if (test_ui_support::g_text_change_callback != nullptr) {
        test_ui_support::g_text_change_callback(
            handle,
            utf8_str == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(utf8_str), len));
    }
}

extern "C" void as_on_selection_changed(ui_handle_t handle, uint32_t start_idx, uint32_t end_idx) {
    if (test_ui_support::g_selection_change_callback != nullptr) {
        test_ui_support::g_selection_change_callback(handle, start_idx, end_idx);
    }
}

extern "C" void as_on_scroll(
    ui_handle_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height) {
    if (test_ui_support::g_scroll_change_callback != nullptr) {
        test_ui_support::g_scroll_change_callback(
            handle,
            offset_x,
            offset_y,
            content_width,
            content_height,
            viewport_width,
            viewport_height);
    }
}

extern "C" void as_on_cross_selection_changed(ui_handle_t area_handle, const uint8_t* utf8_str, uint32_t len) {
    test_ui_support::g_cross_selection_changes.push_back(test_ui_support::CrossSelectionChangeRecord{
        area_handle,
        utf8_str == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(utf8_str), len),
    });
}

extern "C" void as_on_clipboard_write(
    const uint8_t* utf8_plain_text,
    uint32_t plain_text_len,
    const uint8_t* utf8_rich_json,
    uint32_t rich_json_len) {
    test_ui_support::g_clipboard_writes.push_back(test_ui_support::ClipboardWriteRecord{
        utf8_plain_text == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(utf8_plain_text), plain_text_len),
        utf8_rich_json == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(utf8_rich_json), rich_json_len),
    });
}

extern "C" void as_on_request_clipboard_read(ui_handle_t handle) {
    test_ui_support::g_clipboard_read_requests.push_back(test_ui_support::ClipboardReadRequestRecord{handle});
}

extern "C" void as_on_missing_font_coverage(uint32_t font_id, uint32_t coverage_kind, const uint8_t* utf8_sample, uint32_t len) {
    test_ui_support::g_missing_font_coverage_requests.push_back(test_ui_support::MissingFontCoverageRecord{
        font_id,
        coverage_kind,
        utf8_sample == nullptr || len == 0U
            ? std::string{}
            : std::string(reinterpret_cast<const char*>(utf8_sample), static_cast<std::size_t>(len)),
    });
}
