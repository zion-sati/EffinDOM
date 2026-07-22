#pragma once

#include <cstdint>

extern "C" {

void __runApp();
void __disposeApp();
void __flushRenders();
void __fui_clear_ui_dispatches();
void __fui_clear_native_file_dialog_callbacks();
bool __fui_complete_native_file_dialog(
    std::uint64_t request_id,
    std::uint32_t status,
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::int32_t selected_filter);
std::uint64_t __fui_native_action_handle();
std::uint64_t __fui_native_body_text_handle();
std::uint64_t __fui_native_selection_text_handle();
std::uint64_t __fui_native_context_editor_handle();
std::uint64_t __fui_native_scroll_handle();
std::uint64_t __fui_native_scroll_view_handle();
std::uint32_t __fui_native_activation_count();

bool __fui_on_pointer_event_with_metadata(
    std::uint32_t event_type,
    std::uint64_t handle,
    float x,
    float y,
    std::uint32_t modifiers,
    std::int32_t pointer_id,
    std::uint32_t pointer_type,
    std::int32_t button,
    std::uint32_t buttons,
    float pressure,
    float width,
    float height,
    std::int32_t click_count);
bool __fui_on_key_event(
    std::uint32_t event_type,
    const std::uint8_t* key,
    std::uint32_t length,
    std::uint32_t modifiers);
bool __fui_can_show_context_menu(std::uint64_t handle);
void __fui_on_context_menu(std::uint64_t handle, float x, float y);
void __fui_hide_active_context_menu();
void __fui_on_focus_changed(std::uint64_t handle, bool focused);
void __fui_on_text_changed(
    std::uint64_t handle,
    const std::uint8_t* text,
    std::uint32_t length);
void __fui_on_text_replaced(
    std::uint64_t handle,
    std::uint32_t start,
    std::uint32_t end,
    const std::uint8_t* text,
    std::uint32_t length);
void __fui_on_selection_changed(
    std::uint64_t handle,
    std::uint32_t start,
    std::uint32_t end);
void __fui_on_cross_selection_changed(
    std::uint64_t handle,
    const std::uint8_t* text,
    std::uint32_t length);
void __fui_on_font_loaded(std::uint32_t font_id);
void __fui_on_svg_loaded(std::uint32_t svg_id, float width, float height);
void __fui_on_viewport_changed(float width, float height);
void __fui_on_system_dark_mode_changed(bool dark_mode);
void __fui_on_system_accent_color_changed(std::uint32_t color);
void __fui_on_scroll(
    std::uint64_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height);

} // extern "C"
