#include "UiRuntime.h"

#include "effindom_ui.h"

#include <cstring>
#include <limits>

extern "C" {

std::uint32_t ui_get_abi_version(void) {
    return UI_ABI_VERSION;
}

void ui_reset(void) {
    effindom::v2::ui::GetRuntime().Reset();
}

std::uintptr_t ui_arena_alloc(std::uint32_t byte_length) {
    return effindom::v2::ui::GetRuntime().ArenaAlloc(byte_length);
}

ui_handle_t ui_create_node(UiNodeType type) {
    return effindom::v2::ui::GetRuntime().CreateNode(type);
}

void ui_delete_node(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().DeleteNode(handle);
}

void ui_set_node_id(ui_handle_t handle, const uint8_t* utf8_id, uint32_t len) {
    (void)effindom::v2::ui::GetRuntime().SetNodeId(handle, utf8_id, len);
}

void ui_set_semantic_role(ui_handle_t handle, UiSemanticRole role_enum) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticRole(handle, role_enum);
}

void ui_set_semantic_label(ui_handle_t handle, const uint8_t* utf8_label, uint32_t len) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticLabel(handle, utf8_label, len);
}

void ui_set_semantic_checked(ui_handle_t handle, UiSemanticCheckedState checked_state_enum) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticChecked(handle, checked_state_enum);
}

void ui_set_semantic_selected(ui_handle_t handle, bool has_selected, bool is_selected) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticSelected(handle, has_selected, is_selected);
}

void ui_set_semantic_expanded(ui_handle_t handle, bool has_expanded, bool is_expanded) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticExpanded(handle, has_expanded, is_expanded);
}

void ui_set_semantic_disabled(ui_handle_t handle, bool has_disabled, bool is_disabled) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticDisabled(handle, has_disabled, is_disabled);
}

void ui_set_semantic_value_range(ui_handle_t handle, bool has_value_range, float value_now, float value_min, float value_max) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticValueRange(handle, has_value_range, value_now, value_min, value_max);
}

void ui_set_semantic_orientation(ui_handle_t handle, UiOrientation orientation_enum) {
    (void)effindom::v2::ui::GetRuntime().SetSemanticOrientation(handle, orientation_enum);
}

void ui_request_semantic_announcement(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().RequestSemanticAnnouncement(handle);
}

uint32_t ui_push_semantic_scope(ui_handle_t handle) {
    return effindom::v2::ui::GetRuntime().PushSemanticScope(handle);
}

void ui_remove_semantic_scope(uint32_t token) {
    (void)effindom::v2::ui::GetRuntime().RemoveSemanticScope(token);
}

void ui_node_add_child(ui_handle_t parent, ui_handle_t child) {
    (void)effindom::v2::ui::GetRuntime().AddChild(parent, child);
}

void ui_node_remove_child(ui_handle_t parent, ui_handle_t child) {
    (void)effindom::v2::ui::GetRuntime().RemoveChild(parent, child);
}

void ui_set_is_portal(ui_handle_t handle, bool is_portal) {
    (void)effindom::v2::ui::GetRuntime().SetPortal(handle, is_portal);
}

void ui_set_visibility(ui_handle_t handle, UiVisibility visibility_enum) {
    (void)effindom::v2::ui::GetRuntime().SetVisibility(handle, visibility_enum);
}

void ui_set_is_shared_size_scope(ui_handle_t handle, bool is_scope) {
    (void)effindom::v2::ui::GetRuntime().SetIsSharedSizeScope(handle, is_scope);
}

void ui_set_custom_drawable(ui_handle_t handle, bool is_custom_drawable) {
    (void)effindom::v2::ui::GetRuntime().SetCustomDrawable(handle, is_custom_drawable);
}

void ui_set_flex_wrap(ui_handle_t handle, UiFlexWrap wrap_enum) {
    (void)effindom::v2::ui::GetRuntime().SetFlexWrap(handle, wrap_enum);
}

uint32_t ui_prepare_node(ui_handle_t handle) {
    return effindom::v2::ui::GetRuntime().PrepareNode(handle) ? 1U : 0U;
}

void ui_set_dynamic_text_charset(ui_handle_t handle, const uint8_t* utf8_charset, uint32_t len) {
    (void)effindom::v2::ui::GetRuntime().SetDynamicTextCharset(handle, utf8_charset, len);
}

bool ui_get_text_metrics(
    ui_handle_t handle,
    float* out_width,
    float* out_height,
    float* out_baseline,
    uint32_t* out_line_count,
    float* out_max_line_width) {
    return effindom::v2::ui::GetRuntime().GetTextMetrics(
        handle,
        out_width,
        out_height,
        out_baseline,
        out_line_count,
        out_max_line_width);
}

void ui_grid_set_columns(ui_handle_t handle, uint32_t count, const float* values, const uint8_t* types) {
    (void)effindom::v2::ui::GetRuntime().SetGridColumns(handle, count, values, types);
}

void ui_grid_set_rows(ui_handle_t handle, uint32_t count, const float* values, const uint8_t* types) {
    (void)effindom::v2::ui::GetRuntime().SetGridRows(handle, count, values, types);
}

void ui_grid_set_column_shared_size_group(
    ui_handle_t handle,
    uint32_t index,
    const uint8_t* utf8_group,
    uint32_t len) {
    (void)effindom::v2::ui::GetRuntime().SetGridColumnSharedSizeGroup(handle, index, utf8_group, len);
}

void ui_grid_set_row_shared_size_group(
    ui_handle_t handle,
    uint32_t index,
    const uint8_t* utf8_group,
    uint32_t len) {
    (void)effindom::v2::ui::GetRuntime().SetGridRowSharedSizeGroup(handle, index, utf8_group, len);
}

void ui_node_set_grid_placement(ui_handle_t child, uint32_t row, uint32_t col, uint32_t row_span, uint32_t col_span) {
    (void)effindom::v2::ui::GetRuntime().SetGridPlacement(child, row, col, row_span, col_span);
}

void ui_set_root(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().SetRoot(handle);
}

void ui_set_width(ui_handle_t handle, float value, UiSizeUnit unit_enum) {
    (void)effindom::v2::ui::GetRuntime().SetWidth(handle, value, unit_enum);
}

void ui_set_height(ui_handle_t handle, float value, UiSizeUnit unit_enum) {
    (void)effindom::v2::ui::GetRuntime().SetHeight(handle, value, unit_enum);
}

void ui_set_fill_width(ui_handle_t handle, bool fill) {
    (void)effindom::v2::ui::GetRuntime().SetFillWidth(handle, fill);
}

void ui_set_fill_height(ui_handle_t handle, bool fill) {
    (void)effindom::v2::ui::GetRuntime().SetFillHeight(handle, fill);
}

void ui_set_fill_width_percent(ui_handle_t handle, float percent) {
    (void)effindom::v2::ui::GetRuntime().SetFillWidthPercent(handle, percent);
}

void ui_set_fill_height_percent(ui_handle_t handle, float percent) {
    (void)effindom::v2::ui::GetRuntime().SetFillHeightPercent(handle, percent);
}

void ui_set_min_width(ui_handle_t handle, float value, UiSizeUnit unit_enum) {
    (void)effindom::v2::ui::GetRuntime().SetMinWidth(handle, value, unit_enum);
}

void ui_set_max_width(ui_handle_t handle, float value, UiSizeUnit unit_enum) {
    (void)effindom::v2::ui::GetRuntime().SetMaxWidth(handle, value, unit_enum);
}

void ui_set_min_height(ui_handle_t handle, float value, UiSizeUnit unit_enum) {
    (void)effindom::v2::ui::GetRuntime().SetMinHeight(handle, value, unit_enum);
}

void ui_set_max_height(ui_handle_t handle, float value, UiSizeUnit unit_enum) {
    (void)effindom::v2::ui::GetRuntime().SetMaxHeight(handle, value, unit_enum);
}

void ui_set_flex_direction(ui_handle_t handle, UiFlexDirection dir_enum) {
    (void)effindom::v2::ui::GetRuntime().SetFlexDirection(handle, dir_enum);
}

void ui_set_flex_basis(ui_handle_t handle, float basis) {
    (void)effindom::v2::ui::GetRuntime().SetFlexBasis(handle, basis);
}

void ui_set_justify_content(ui_handle_t handle, UiJustifyContent justify_enum) {
    (void)effindom::v2::ui::GetRuntime().SetJustifyContent(handle, justify_enum);
}

void ui_set_align_items(ui_handle_t handle, UiAlignItems align_enum) {
    (void)effindom::v2::ui::GetRuntime().SetAlignItems(handle, align_enum);
}

void ui_set_align_self(ui_handle_t handle, UiAlignSelf align_enum) {
    (void)effindom::v2::ui::GetRuntime().SetAlignSelf(handle, align_enum);
}

void ui_set_padding(ui_handle_t handle, float left, float top, float right, float bottom) {
    (void)effindom::v2::ui::GetRuntime().SetPadding(handle, left, top, right, bottom);
}

void ui_set_margin(ui_handle_t handle, float left, float top, float right, float bottom) {
    (void)effindom::v2::ui::GetRuntime().SetMargin(handle, left, top, right, bottom);
}

void ui_set_position_type(ui_handle_t handle, UiPositionType pos_enum) {
    (void)effindom::v2::ui::GetRuntime().SetPositionType(handle, pos_enum);
}

void ui_set_position(ui_handle_t handle, float left, float top, float right, float bottom) {
    (void)effindom::v2::ui::GetRuntime().SetPosition(handle, left, top, right, bottom);
}

void ui_set_clip_to_bounds(ui_handle_t handle, bool clip) {
    (void)effindom::v2::ui::GetRuntime().SetClipToBounds(handle, clip);
}

void ui_set_bg_color(ui_handle_t handle, ui_color_t color) {
    (void)effindom::v2::ui::GetRuntime().SetNodeColor(handle, color);
}

void ui_set_box_style(
    ui_handle_t handle,
    ui_color_t bg_color,
    float radius_tl,
    float radius_tr,
    float radius_br,
    float radius_bl,
    float border_width,
    ui_color_t border_color,
    EdBorderStyle border_style_enum,
    float border_dash_on,
    float border_dash_off) {
    (void)effindom::v2::ui::GetRuntime().SetBoxStyle(
        handle,
        bg_color,
        radius_tl,
        radius_tr,
        radius_br,
        radius_bl,
        border_width,
        border_color,
        border_style_enum,
        border_dash_on,
        border_dash_off);
}

void ui_set_layer_effect(ui_handle_t handle, float opacity, float blur_sigma, EdBlendMode blend_mode_enum) {
    (void)effindom::v2::ui::GetRuntime().SetLayerEffect(handle, opacity, blur_sigma, blend_mode_enum);
}

void ui_set_drop_shadow(
    ui_handle_t handle,
    ui_color_t color,
    float offset_x,
    float offset_y,
    float blur_sigma,
    float spread) {
    (void)effindom::v2::ui::GetRuntime().SetDropShadow(handle, color, offset_x, offset_y, blur_sigma, spread);
}

void ui_set_background_blur(ui_handle_t handle, float blur_sigma) {
    (void)effindom::v2::ui::GetRuntime().SetBackgroundBlur(handle, blur_sigma);
}

void ui_set_image(
    ui_handle_t handle,
    uint32_t texture_id,
    EdObjectFit object_fit_enum,
    EdImageSampling sampling_kind,
    uint32_t max_aniso) {
    (void)effindom::v2::ui::GetRuntime().SetImage(handle, texture_id, object_fit_enum, sampling_kind, max_aniso);
}

void ui_set_image_nine(
    ui_handle_t handle,
    uint32_t texture_id,
    float inset_l,
    float inset_t,
    float inset_r,
    float inset_b,
    EdImageSampling sampling_kind,
    uint32_t max_aniso) {
    (void)effindom::v2::ui::GetRuntime().SetImageNine(
        handle,
        texture_id,
        inset_l,
        inset_t,
        inset_r,
        inset_b,
        sampling_kind,
        max_aniso);
}

void ui_set_svg(
    ui_handle_t handle,
    uint32_t svg_id,
    ui_color_t tint_color,
    EdImageSampling sampling_kind,
    uint32_t max_aniso) {
    (void)effindom::v2::ui::GetRuntime().SetSvg(handle, svg_id, tint_color, sampling_kind, max_aniso);
}

void ui_set_linear_gradient(
    ui_handle_t handle,
    float sx,
    float sy,
    float ex,
    float ey,
    uint32_t stop_count,
    const float* offsets,
    const ui_color_t* colors) {
    (void)effindom::v2::ui::GetRuntime().SetLinearGradient(
        handle,
        sx,
        sy,
        ex,
        ey,
        stop_count,
        offsets,
        reinterpret_cast<const std::uint32_t*>(colors));
}

void ui_set_text(ui_handle_t handle, const std::uint8_t* utf8_str, std::uint32_t len) {
    (void)effindom::v2::ui::GetRuntime().SetText(handle, utf8_str, len);
}

void ui_set_text_style_runs(ui_handle_t handle, std::uint32_t run_count, const std::uint32_t* runs_words) {
    (void)effindom::v2::ui::GetRuntime().SetTextStyleRuns(handle, run_count, runs_words);
}

void ui_set_font(ui_handle_t handle, std::uint32_t font_id, float size) {
    (void)effindom::v2::ui::GetRuntime().SetFont(handle, font_id, size);
}

void ui_set_line_height(ui_handle_t handle, float line_height) {
    (void)effindom::v2::ui::GetRuntime().SetLineHeight(handle, line_height);
}

void ui_set_text_color(ui_handle_t handle, ui_color_t color) {
    (void)effindom::v2::ui::GetRuntime().SetTextColor(handle, color);
}

void ui_set_text_align(ui_handle_t handle, UiTextAlign align_enum) {
    (void)effindom::v2::ui::GetRuntime().SetTextAlign(handle, align_enum);
}

void ui_set_text_vertical_align(ui_handle_t handle, UiTextVerticalAlign align_enum) {
    (void)effindom::v2::ui::GetRuntime().SetTextVerticalAlign(handle, align_enum);
}

void ui_set_text_limits(ui_handle_t handle, int32_t max_chars, int32_t max_lines) {
    (void)effindom::v2::ui::GetRuntime().SetTextLimits(handle, max_chars, max_lines);
}

void ui_set_text_wrapping(ui_handle_t handle, bool wrap) {
    (void)effindom::v2::ui::GetRuntime().SetTextWrapping(handle, wrap);
}

void ui_set_text_overflow(ui_handle_t handle, UiTextOverflow overflow_enum) {
    (void)effindom::v2::ui::GetRuntime().SetTextOverflow(handle, overflow_enum);
}

void ui_set_text_overflow_fade(ui_handle_t handle, bool horizontal, bool vertical) {
    (void)effindom::v2::ui::GetRuntime().SetTextOverflowFade(handle, horizontal, vertical);
}

void ui_set_text_obscured(ui_handle_t handle, bool is_password) {
    (void)effindom::v2::ui::GetRuntime().SetTextObscured(handle, is_password);
}

void ui_set_interactive(ui_handle_t handle, bool interactive) {
    (void)effindom::v2::ui::GetRuntime().SetInteractive(handle, interactive);
}

void ui_set_preserve_selection_on_pointer_down(ui_handle_t handle, bool preserve) {
    (void)effindom::v2::ui::GetRuntime().SetPreserveSelectionOnPointerDown(handle, preserve);
}

void ui_set_editor_command_keys(ui_handle_t handle, bool enabled) {
    (void)effindom::v2::ui::GetRuntime().SetEditorCommandKeys(handle, enabled);
}

void ui_set_editor_accepts_tab(ui_handle_t handle, bool enabled) {
    (void)effindom::v2::ui::GetRuntime().SetEditorAcceptsTab(handle, enabled);
}

void ui_set_scroll_proxy_target(ui_handle_t handle, ui_handle_t scroll_handle) {
    (void)effindom::v2::ui::GetRuntime().SetScrollProxyTarget(handle, scroll_handle);
}

void ui_set_scroll_enabled(ui_handle_t handle, bool enabled_x, bool enabled_y) {
    (void)effindom::v2::ui::GetRuntime().SetScrollEnabled(handle, enabled_x, enabled_y);
}

void ui_set_scroll_friction(ui_handle_t handle, float friction) {
    (void)effindom::v2::ui::GetRuntime().SetScrollFriction(handle, friction);
}

void ui_set_smooth_scrolling(ui_handle_t handle, bool smooth_scrolling) {
    (void)effindom::v2::ui::GetRuntime().SetSmoothScrolling(handle, smooth_scrolling);
}

void ui_set_focusable(ui_handle_t handle, bool focusable, int32_t tab_index) {
    (void)effindom::v2::ui::GetRuntime().SetFocusable(handle, focusable, tab_index);
}

void ui_request_focus(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().RequestFocus(handle);
}

void ui_set_scroll_offset(ui_handle_t handle, float offset_x, float offset_y) {
    (void)effindom::v2::ui::GetRuntime().SetScrollOffset(handle, offset_x, offset_y);
}

void ui_set_scroll_content_size(ui_handle_t handle, float content_width, float content_height) {
    (void)effindom::v2::ui::GetRuntime().SetScrollContentSize(handle, content_width, content_height);
}

void ui_set_selectable(ui_handle_t handle, bool selectable, ui_color_t selection_color) {
    (void)effindom::v2::ui::GetRuntime().SetSelectable(handle, selectable, selection_color);
}

void ui_set_selection_area(ui_handle_t handle, bool is_area) {
    (void)effindom::v2::ui::GetRuntime().SetSelectionArea(handle, is_area);
}

void ui_set_selection_area_barrier(ui_handle_t handle, bool is_barrier) {
    (void)effindom::v2::ui::GetRuntime().SetSelectionAreaBarrier(handle, is_barrier);
}

void ui_clear_selection(ui_handle_t text_node_handle) {
    (void)effindom::v2::ui::GetRuntime().ClearSelection(text_node_handle);
}

void ui_retarget_selection(ui_handle_t from_text_node_handle, ui_handle_t to_text_node_handle) {
    (void)effindom::v2::ui::GetRuntime().RetargetSelection(from_text_node_handle, to_text_node_handle);
}

bool ui_is_point_in_selection(float logical_x, float logical_y) {
    return effindom::v2::ui::GetRuntime().IsPointInSelection(logical_x, logical_y);
}

void ui_set_text_selection_range(ui_handle_t handle, uint32_t selection_start, uint32_t selection_end) {
    (void)effindom::v2::ui::GetRuntime().SetTextSelectionRange(handle, selection_start, selection_end);
}

bool ui_select_word_at(ui_handle_t handle, float logical_x, float logical_y) {
    return effindom::v2::ui::GetRuntime().SelectWordAt(handle, logical_x, logical_y);
}

bool ui_begin_selection_endpoint_drag(ui_handle_t handle, uint32_t endpoint) {
    return effindom::v2::ui::GetRuntime().BeginSelectionEndpointDrag(handle, endpoint);
}

bool ui_preserves_selection_on_pointer_down(ui_handle_t handle) {
    return effindom::v2::ui::GetRuntime().PreservesSelectionOnPointerDown(handle);
}

uint32_t ui_get_text_snapshot_handle_count(void) {
    const auto handles = effindom::v2::ui::GetRuntime().GetTextSnapshotHandles();
    return static_cast<std::uint32_t>(handles.size());
}

uint32_t ui_copy_text_snapshot_handles(uint32_t* out_handle_words, uint32_t max_handle_count) {
    const auto handles = effindom::v2::ui::GetRuntime().GetTextSnapshotHandles();
    const std::uint32_t handle_count = static_cast<std::uint32_t>(handles.size());
    if (handle_count == 0U) {
        return 0U;
    }
    if (out_handle_words == nullptr || max_handle_count < handle_count) {
        return 0U;
    }
    for (std::uint32_t index = 0; index < handle_count; index += 1U) {
        const std::uint64_t handle = handles[index];
        const std::size_t base = static_cast<std::size_t>(index) * 2U;
        out_handle_words[base] = static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
        out_handle_words[base + 1U] = static_cast<std::uint32_t>(handle >> 32U);
    }
    return handle_count;
}

bool ui_set_text_find_match(ui_handle_t handle, uint32_t start, uint32_t end) {
    return effindom::v2::ui::GetRuntime().SetTextFindMatch(handle, start, end);
}

void ui_clear_text_find_match(void) {
    effindom::v2::ui::GetRuntime().ClearTextFindMatch();
}

bool ui_push_text_find_highlight(ui_handle_t handle, uint32_t start, uint32_t end, ui_color_t color) {
    return effindom::v2::ui::GetRuntime().PushTextFindHighlight(handle, start, end, color);
}

void ui_clear_text_find_highlights(void) {
    effindom::v2::ui::GetRuntime().ClearTextFindHighlights();
}

uint32_t ui_get_text_document_utf8_length(ui_handle_t handle) {
    const auto text = effindom::v2::ui::GetRuntime().GetTextSnapshotDocument(handle);
    if (!text.has_value()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(text->size());
}

bool ui_copy_text_document_utf8(ui_handle_t handle, uint8_t* out_utf8, uint32_t buffer_length) {
    const auto text = effindom::v2::ui::GetRuntime().GetTextSnapshotDocument(handle);
    if (!text.has_value()) {
        return false;
    }
    if (buffer_length != static_cast<std::uint32_t>(text->size())) {
        return false;
    }
    if (!text->empty()) {
        if (out_utf8 == nullptr) {
            return false;
        }
        std::memcpy(out_utf8, text->data(), text->size());
    }
    return true;
}

bool ui_get_text_visible_bounds(
    ui_handle_t handle,
    float* out_x,
    float* out_y,
    float* out_width,
    float* out_height) {
    const auto bounds = effindom::v2::ui::GetRuntime().GetTextVisibleBounds(handle);
    if (!bounds.has_value() ||
        out_x == nullptr ||
        out_y == nullptr ||
        out_width == nullptr ||
        out_height == nullptr) {
        return false;
    }
    *out_x = bounds->x;
    *out_y = bounds->y;
    *out_width = bounds->width;
    *out_height = bounds->height;
    return true;
}

uint32_t ui_get_text_range_rect_count(ui_handle_t handle, uint32_t start, uint32_t end) {
    const auto rects = effindom::v2::ui::GetRuntime().GetTextRangeSceneRects(handle, start, end);
    return static_cast<std::uint32_t>(rects.size());
}

uint32_t ui_copy_text_range_rects(
    ui_handle_t handle,
    uint32_t start,
    uint32_t end,
    float* out_rect_words,
    uint32_t max_rect_count) {
    const auto rects = effindom::v2::ui::GetRuntime().GetTextRangeSceneRects(handle, start, end);
    const std::uint32_t rect_count = static_cast<std::uint32_t>(rects.size());
    if (rect_count == 0U) {
        return 0U;
    }
    if (out_rect_words == nullptr || max_rect_count < rect_count) {
        return 0U;
    }
    for (std::uint32_t index = 0; index < rect_count; index += 1U) {
        const auto& rect = rects[index];
        const std::size_t base = static_cast<std::size_t>(index) * 4U;
        out_rect_words[base] = rect.x;
        out_rect_words[base + 1U] = rect.y;
        out_rect_words[base + 2U] = rect.width;
        out_rect_words[base + 3U] = rect.height;
    }
    return rect_count;
}

bool ui_copy_cross_selection_endpoint_rects(ui_handle_t area_handle, float* out_rect_words) {
    if (out_rect_words == nullptr) {
        return false;
    }
    effindom::v2::ui::Rect start_rect{};
    effindom::v2::ui::Rect end_rect{};
    if (!effindom::v2::ui::GetRuntime().GetCrossSelectionEndpointSceneRects(area_handle, start_rect, end_rect)) {
        return false;
    }
    out_rect_words[0U] = start_rect.x;
    out_rect_words[1U] = start_rect.y;
    out_rect_words[2U] = start_rect.width;
    out_rect_words[3U] = start_rect.height;
    out_rect_words[4U] = end_rect.x;
    out_rect_words[5U] = end_rect.y;
    out_rect_words[6U] = end_rect.width;
    out_rect_words[7U] = end_rect.height;
    return true;
}

bool ui_reveal_text_range(ui_handle_t handle, uint32_t start, uint32_t end) {
    return effindom::v2::ui::GetRuntime().RevealTextRange(handle, start, end);
}

void ui_clear_current_selection(void) {
    (void)effindom::v2::ui::GetRuntime().ClearCurrentSelection(true);
}

void ui_copy_current_selection(void) {
    (void)effindom::v2::ui::GetRuntime().CopyCurrentSelection();
}

bool ui_can_undo_text_edit(ui_handle_t handle) {
    return effindom::v2::ui::GetRuntime().CanUndoTextEdit(handle);
}

bool ui_can_redo_text_edit(ui_handle_t handle) {
    return effindom::v2::ui::GetRuntime().CanRedoTextEdit(handle);
}

bool ui_has_text_selection(ui_handle_t handle) {
    return effindom::v2::ui::GetRuntime().HasTextSelection(handle);
}

void ui_undo_text_edit(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().UndoTextEditAtHandle(handle);
}

void ui_redo_text_edit(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().RedoTextEditAtHandle(handle);
}

void ui_copy_text_selection(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().CopyTextSelection(handle);
}

void ui_cut_text_selection(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().CutTextSelection(handle);
}

void ui_paste_text(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().PasteText(handle);
}

void ui_select_all_text(ui_handle_t handle) {
    (void)effindom::v2::ui::GetRuntime().SelectAllText(handle);
}

void ui_set_editable(ui_handle_t handle, bool editable) {
    (void)effindom::v2::ui::GetRuntime().SetEditable(handle, editable);
}

void ui_set_caret_color(ui_handle_t handle, ui_color_t color) {
    (void)effindom::v2::ui::GetRuntime().SetCaretColor(handle, color);
}

void ui_commit_frame(double timestamp_ms) {
    effindom::v2::ui::GetRuntime().CommitFrame(timestamp_ms);
}

bool ui_has_pending_visual_work(void) {
    return effindom::v2::ui::GetRuntime().HasPendingVisualWork();
}

bool ui_needs_animation_frame(void) {
    return effindom::v2::ui::GetRuntime().NeedsAnimationFrame();
}

bool ui_has_pointer_autoscroll(void) {
    return effindom::v2::ui::GetRuntime().HasPointerAutoScroll();
}

ui_handle_t ui_selection_autoscroll(float logical_x, float logical_y, float edge_threshold) {
    return effindom::v2::ui::GetRuntime().SelectionAutoScroll(logical_x, logical_y, edge_threshold);
}

void ui_resize_window(float logical_w, float logical_h) {
    effindom::v2::ui::GetRuntime().ResizeWindow(logical_w, logical_h);
}

bool ui_register_font(uint32_t font_id, const uint8_t* bytes, uint32_t len) {
    return effindom::v2::ui::GetRuntime().RegisterFont(font_id, bytes, len);
}

void ui_register_font_fallback(uint32_t font_id, uint32_t fallback_font_id) {
    (void)effindom::v2::ui::GetRuntime().RegisterFontFallback(font_id, fallback_font_id);
}

bool ui_unregister_font_fallback(uint32_t font_id, uint32_t fallback_font_id) {
    return effindom::v2::ui::GetRuntime().UnregisterFontFallback(font_id, fallback_font_id);
}

bool ui_unregister_font(uint32_t font_id) {
    return effindom::v2::ui::GetRuntime().UnregisterFont(font_id);
}

void ui_register_icu_data(const uint8_t* bytes, uint32_t len) {
    (void)effindom::v2::ui::GetRuntime().RegisterIcuData(bytes, len);
}

void ui_on_pointer_event(
    UiEvent event_enum,
    ui_handle_t handle,
    float logical_x,
    float logical_y,
    int32_t pointer_id,
    UiPointerType pointer_type,
    int32_t button,
    uint32_t buttons,
    float pressure,
    float width,
    float height,
    int32_t click_count,
    uint32_t modifiers) {
    effindom::v2::ui::GetRuntime().HandlePointerEvent(
        event_enum,
        handle,
        logical_x,
        logical_y,
        pointer_id,
        pointer_type,
        button,
        buttons,
        pressure,
        width,
        height,
        click_count,
        modifiers);
}

void ui_on_wheel_event(float delta_x, float delta_y) {
    effindom::v2::ui::GetRuntime().HandleWheelEvent(delta_x, delta_y);
}

void ui_touch_scroll_begin(ui_handle_t handle, float logical_x, float logical_y, double timestamp_ms) {
    effindom::v2::ui::GetRuntime().BeginTouchScroll(handle, logical_x, logical_y, timestamp_ms);
}

void ui_touch_scroll_update(float delta_x, float delta_y, double timestamp_ms) {
    effindom::v2::ui::GetRuntime().UpdateTouchScroll(delta_x, delta_y, timestamp_ms);
}

bool ui_wheel_scroll_can_consume(float delta_x, float delta_y) {
    return effindom::v2::ui::GetRuntime().WheelScrollCanConsume(delta_x, delta_y);
}

bool ui_touch_scroll_can_consume(float delta_x, float delta_y) {
    return effindom::v2::ui::GetRuntime().ActiveTouchScrollCanConsume(delta_x, delta_y);
}

void ui_touch_scroll_end(double timestamp_ms) {
    effindom::v2::ui::GetRuntime().EndTouchScroll(timestamp_ms);
}

void ui_clear_momentum_scroll(void) {
    effindom::v2::ui::GetRuntime().ClearMomentumScroll();
}

bool ui_touch_scroll_allows_pull_to_refresh(void) {
    return effindom::v2::ui::GetRuntime().ActiveTouchScrollAllowsPullToRefresh();
}

void ui_set_coarse_pointer_mode(bool coarse_pointer_mode) {
    effindom::v2::ui::GetRuntime().SetCoarsePointerMode(coarse_pointer_mode);
}

void ui_set_platform_family(uint32_t platform_family) {
    effindom::v2::ui::GetRuntime().SetPlatformFamily(platform_family);
}

bool ui_on_key_event(UiKeyEventType type_enum, const uint8_t* key_utf8, uint32_t len, uint32_t modifiers) {
    return effindom::v2::ui::GetRuntime().HandleKeyEvent(type_enum, key_utf8, len, modifiers);
}

void ui_set_interaction_time(uint64_t interaction_time_ms) {
    effindom::v2::ui::GetRuntime().SetInteractionTime(interaction_time_ms);
}

void ui_on_ime_update(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len, uint32_t caret_idx) {
    effindom::v2::ui::GetRuntime().HandleImeUpdate(handle, utf8_str, len, caret_idx);
}

void ui_replace_text_range(
    ui_handle_t handle,
    uint32_t start_idx,
    uint32_t end_idx,
    const uint8_t* utf8_str,
    uint32_t len,
    uint32_t caret_idx) {
    effindom::v2::ui::GetRuntime().HandleTextReplaceRange(handle, start_idx, end_idx, utf8_str, len, caret_idx);
}

void ui_on_paste_text(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len) {
    effindom::v2::ui::GetRuntime().HandlePasteText(handle, utf8_str, len);
}

void ui_font_loaded(uint32_t font_id) {
    effindom::v2::ui::GetRuntime().FontLoaded(font_id);
}

void ui_measure_text(
    const uint8_t* utf8_str,
    uint32_t len,
    uint32_t font_id,
    float size,
    float max_width,
    float* out_width,
    float* out_height) {
    effindom::v2::ui::GetRuntime().MeasureText(utf8_str, len, font_id, size, max_width, out_width, out_height);
}

const std::uint32_t* ui_get_command_buffer(std::uint32_t* out_length) {
    const auto& command_buffer = effindom::v2::ui::GetRuntime().command_buffer();
    if (out_length != nullptr) {
        *out_length = static_cast<std::uint32_t>(command_buffer.size());
    }
    return command_buffer.empty() ? nullptr : command_buffer.data();
}

const std::uint32_t* ui_get_semantic_buffer(std::uint32_t* out_length) {
    const auto& semantic_buffer = effindom::v2::ui::GetRuntime().semantic_buffer();
    if (out_length != nullptr) {
        *out_length = static_cast<std::uint32_t>(semantic_buffer.size());
    }
    return semantic_buffer.empty() ? nullptr : semantic_buffer.data();
}

const std::uint32_t* ui_get_debug_tree_buffer(std::uint32_t* out_length) {
    const auto& debug_tree_buffer = effindom::v2::ui::GetRuntime().debug_tree_buffer();
    if (out_length != nullptr) {
        *out_length = static_cast<std::uint32_t>(debug_tree_buffer.size());
    }
    return debug_tree_buffer.empty() ? nullptr : debug_tree_buffer.data();
}

const std::uint32_t* ui_get_live_fallback_font_buffer(std::uint32_t* out_length) {
    const auto& live_fallback_font_buffer = effindom::v2::ui::GetRuntime().LiveFallbackFontBuffer();
    if (out_length != nullptr) {
        *out_length = static_cast<std::uint32_t>(live_fallback_font_buffer.size());
    }
    return live_fallback_font_buffer.empty() ? nullptr : live_fallback_font_buffer.data();
}

bool ui_get_bounds(
    ui_handle_t handle,
    float* out_x,
    float* out_y,
    float* out_width,
    float* out_height) {
    const auto* node = effindom::v2::ui::GetRuntime().Resolve(handle);
    if (node == nullptr) {
        return false;
    }
    if (out_x != nullptr) {
        *out_x = node->abs_x;
    }
    if (out_y != nullptr) {
        *out_y = node->abs_y;
    }
    if (out_width != nullptr) {
        *out_width = node->layout_width;
    }
    if (out_height != nullptr) {
        *out_height = node->layout_height;
    }
    return true;
}

bool ui_get_visible_bounds(
    ui_handle_t handle,
    float* out_x,
    float* out_y,
    float* out_width,
    float* out_height) {
    const auto bounds = effindom::v2::ui::GetRuntime().GetVisibleBounds(handle);
    if (!bounds.has_value() ||
        out_x == nullptr ||
        out_y == nullptr ||
        out_width == nullptr ||
        out_height == nullptr) {
        return false;
    }
    *out_x = bounds->x;
    *out_y = bounds->y;
    *out_width = bounds->width;
    *out_height = bounds->height;
    return true;
}

// Unmanaged buffer API for non-linear-memory callers (Kotlin/Wasm GC, etc.).
// Allocates from the frame arena; freed automatically on next ResetFrameArena().
std::uintptr_t ui_alloc_unmanaged_buffer(std::uint32_t byte_length) {
    return effindom::v2::ui::GetRuntime().ArenaAlloc(byte_length);
}

void ui_free_unmanaged_buffer(std::uintptr_t ptr) {
    (void)ptr;
    // Frame arena resets automatically; explicit free is a no-op for now.
}

} // extern "C"
