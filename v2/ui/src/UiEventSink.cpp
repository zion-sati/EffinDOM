#include "UiEventSink.h"

#include "effindom_ui.h"

namespace effindom::v2::ui {

void UiEventSink::FocusChanged(std::uint64_t handle, bool focused) const {
    as_on_focus_changed(handle, focused);
}

bool UiEventSink::PointerEvent(std::uint64_t handle, std::uint32_t event_type) const {
    return as_on_pointer_event(handle, static_cast<UiEvent>(event_type));
}

void UiEventSink::ScrollChanged(std::uint64_t handle, const ScrollMetrics& metrics) const {
    as_on_scroll(
        handle,
        metrics.offset_x,
        metrics.offset_y,
        metrics.content_width,
        metrics.content_height,
        metrics.viewport_width,
        metrics.viewport_height);
}

void UiEventSink::SelectionChanged(std::uint64_t handle, std::uint32_t start, std::uint32_t end) const {
    as_on_selection_changed(handle, start, end);
}

void UiEventSink::CrossSelectionChanged(std::uint64_t area_handle, std::string_view utf8_text) const {
    as_on_cross_selection_changed(
        area_handle,
        reinterpret_cast<const std::uint8_t*>(utf8_text.data()),
        static_cast<std::uint32_t>(utf8_text.size()));
}

void UiEventSink::TextChanged(std::uint64_t handle, std::string_view utf8_text) const {
    as_on_text_changed(
        handle,
        utf8_text.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(utf8_text.data()),
        static_cast<std::uint32_t>(utf8_text.size()));
}

void UiEventSink::TextReplaced(
    std::uint64_t handle,
    std::uint32_t start,
    std::uint32_t removed_end,
    std::string_view inserted_utf8) const {
    as_on_text_replaced(
        handle,
        start,
        removed_end,
        inserted_utf8.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(inserted_utf8.data()),
        static_cast<std::uint32_t>(inserted_utf8.size()));
}

} // namespace effindom::v2::ui
