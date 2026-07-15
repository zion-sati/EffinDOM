#pragma once

#include <cstdint>
#include <string_view>

namespace effindom::v2::ui {

struct ScrollMetrics {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float content_width = 0.0f;
    float content_height = 0.0f;
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
};

// Host-neutral boundary between retained-runtime subsystems and the existing ABI callbacks.
class UiEventSink {
public:
    void FocusChanged(std::uint64_t handle, bool focused) const;
    void PointerEvent(std::uint64_t handle, std::uint32_t event_type) const;
    void ScrollChanged(std::uint64_t handle, const ScrollMetrics& metrics) const;
    void SelectionChanged(std::uint64_t handle, std::uint32_t start, std::uint32_t end) const;
    void CrossSelectionChanged(std::uint64_t area_handle, std::string_view utf8_text) const;
    void TextChanged(std::uint64_t handle, std::string_view utf8_text) const;
    void TextReplaced(
        std::uint64_t handle,
        std::uint32_t start,
        std::uint32_t removed_end,
        std::string_view inserted_utf8) const;
};

} // namespace effindom::v2::ui
