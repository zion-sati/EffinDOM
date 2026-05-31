#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "UiColor.h"
#include "effindom.h"
#include "effindom_ui.h"
#include <yoga/Yoga.h>

namespace effindom::v2::ui {

constexpr std::size_t kMaxNodes = 10'000;
constexpr std::size_t kFrameArenaCapacity = 64U * 1024U;

struct GradientStop {
    float offset = 0.0f;
    std::uint32_t color = 0;
};

struct NonWrappingTextFragment {
    std::size_t line_index = 0U;
    std::uint32_t local_byte_start = 0U;
    std::uint32_t local_byte_end = 0U;
    float x = 0.0f;
    float width = 0.0f;
};

struct GlyphPlacement {
    std::uint32_t glyph_id = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t cluster = 0;
    std::uint32_t font_id = 0;
    std::uint32_t color = 0;
};

struct TextStyleRun {
    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
    std::uint32_t font_id = 0U;
    float font_size = 16.0f;
    std::uint32_t color = 0U;
    std::uint32_t bg_color = 0U;
    std::uint32_t decoration_flags = 0U;
};

struct TextClusterStop {
    std::uint32_t index = 0U;
    float x = 0.0f;
};

struct CachedNonWrapGeometrySlice {
    std::size_t line_index = 0U;
    std::uint32_t line_start = 0U;
    std::uint32_t line_end = 0U;
    std::uint32_t slice_start = 0U;
    std::uint32_t slice_end = 0U;
    float slice_x = 0.0f;
    float full_line_width = 0.0f;
    float shaped_width = 0.0f;
    float shaped_height = 0.0f;
    float shaped_baseline = 0.0f;
    float shaped_ascent = 0.0f;
    float shaped_descent = 0.0f;
    std::vector<GlyphPlacement> glyphs{};
    std::vector<TextClusterStop> cluster_stops{};
};

struct CachedLogicalLineShape {
    struct WrappedBreakShard {
        std::size_t start_candidate_index = 0U;
        std::size_t end_candidate_index = 0U;
        float start_x = 0.0f;
        float end_x = 0.0f;
    };

    std::uint32_t raw_start = 0U;
    std::uint32_t visible_start = 0U;
    std::uint32_t end = 0U;
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    bool ascii_only = false;
    bool break_candidate_cache_valid = false;
    std::vector<std::int32_t> break_candidates{};
    std::vector<float> break_candidate_x_offsets{};
    std::vector<WrappedBreakShard> break_shards{};
    std::vector<GlyphPlacement> glyphs{};
    std::vector<TextClusterStop> cluster_stops{};
    float ascent = 0.0f;
    float descent = 0.0f;
    bool monospace_fast_path_eligible = false;
    bool monospace_wrapped_metrics_eligible = false;
    float monospace_cell_width = 0.0f;
};

struct CachedVisualLineShape {
    std::size_t logical_line_index = 0U;
    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
    std::uint32_t safe_slice_start = 0U;
    std::uint32_t safe_slice_end = 0U;
    std::size_t resume_candidate_index = 0U;
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    bool cache_materialized = false;
    bool cache_dirty = true;
    std::vector<GlyphPlacement> glyphs{};
    std::vector<TextClusterStop> cluster_stops{};
    float ascent = 0.0f;
    float descent = 0.0f;
};

struct UINode {
    struct UndoEntry {
        std::string text{};
        std::uint32_t caret = 0;
        std::uint32_t sel_start = 0;
        std::uint32_t sel_end = 0;
    };

    struct CellMeasure {
        float natural_width = 0.0f;
        float natural_height = 0.0f;
        float measured_width = 0.0f;
        float measured_height = 0.0f;
    };

    static constexpr std::size_t kMaxUndo = 100U;
    static constexpr std::uint64_t kUndoDebounceMs = 300U;

    std::uint32_t generation = 0;
    bool is_active = false;
    bool needs_creation = false;
    bool is_dirty = false;
    bool is_text_node = false;
    bool is_svg_node = false;
    bool is_interactive = false;
    bool is_focusable = false;
    bool is_selectable = false;
    bool is_editable = false;
    bool is_portal = false;
    bool is_scroll_view = false;
    bool is_grid = false;
    bool is_selection_area = false;
    bool is_selection_area_barrier = false;
    bool clip_to_bounds = false;
    UiVisibility visibility = UI_VISIBILITY_NORMAL;
 
    YGNodeRef yg_node = nullptr;
    bool has_width = false;
    float width = 0.0f;
    std::uint32_t width_unit = 0U;
    bool has_height = false;
    float height = 0.0f;
    std::uint32_t height_unit = 0U;
    bool has_flex_basis = false;
    float flex_basis = 0.0f;
    bool fill_width = false;
    bool fill_height = false;
    std::uint64_t parent_handle = UI_INVALID_HANDLE;
    std::uint64_t scroll_proxy_target_handle = UI_INVALID_HANDLE;
    std::vector<std::uint64_t> children{};
    float abs_x = 0.0f;
    float abs_y = 0.0f;
    float scene_x = 0.0f;
    float scene_y = 0.0f;
    float layout_width = 0.0f;
    float layout_height = 0.0f;
    float scroll_offset_x = 0.0f;
    float scroll_offset_y = 0.0f;
    bool scroll_offset_dirty = false;
    float reported_scroll_offset_x = 0.0f;
    float reported_scroll_offset_y = 0.0f;
    float pending_scroll_offset_x = 0.0f;
    float pending_scroll_offset_y = 0.0f;
    bool has_pending_scroll_offset = false;
    std::uint64_t pending_scroll_offset_generation = 0U;
    bool is_applying_scroll_offset = false;
    bool has_deferred_scroll_notification = false;
    float scroll_content_width = 0.0f;
    float scroll_content_height = 0.0f;
    float explicit_scroll_content_width = 0.0f;
    float explicit_scroll_content_height = 0.0f;
    bool has_explicit_scroll_content_width = false;
    bool has_explicit_scroll_content_height = false;
    float reported_scroll_content_width = 0.0f;
    float reported_scroll_content_height = 0.0f;
    float reported_viewport_width = 0.0f;
    float reported_viewport_height = 0.0f;
    bool has_reported_scroll_state = false;
    float scroll_velocity_x = 0.0f;
    float scroll_velocity_y = 0.0f;
    bool scroll_enabled_x = true;
    bool scroll_enabled_y = true;
    bool show_scrollbars = true;
    float friction = 0.95f;
    bool scroll_friction_overridden = false;
    float edge_hot_zone = 20.0f;
    float auto_scroll_speed = 5.0f;
    std::vector<float> column_values{};
    std::vector<std::uint8_t> column_types{};
    std::vector<float> row_values{};
    std::vector<std::uint8_t> row_types{};
    std::vector<CellMeasure> cell_measures{};
    std::uint32_t grid_row = 0U;
    std::uint32_t grid_col = 0U;
    std::uint32_t grid_row_span = 1U;
    std::uint32_t grid_col_span = 1U;

    std::uint32_t bg_color = EF_RGBA(0x00U, 0x00U, 0x00U, 0x00U);
    bool has_box_style = false;
    float corner_radius_tl = 0.0f;
    float corner_radius_tr = 0.0f;
    float corner_radius_br = 0.0f;
    float corner_radius_bl = 0.0f;
    float border_width = 0.0f;
    std::uint32_t border_color = 0;
    std::uint32_t border_style = ED_BORDER_SOLID;
    float border_dash_on = 0.0f;
    float border_dash_off = 0.0f;
    bool has_layer_effect = false;
    float opacity = 1.0f;
    float blur_sigma = 0.0f;
    bool has_drop_shadow = false;
    std::uint32_t drop_shadow_color = 0;
    float drop_shadow_offset_x = 0.0f;
    float drop_shadow_offset_y = 0.0f;
    float drop_shadow_blur_sigma = 0.0f;
    float drop_shadow_spread = 0.0f;
    bool has_background_blur = false;
    float background_blur_sigma = 0.0f;
    std::uint32_t blend_mode = ED_BLEND_SRC_OVER;
    bool has_linear_gradient = false;
    float gradient_start_x = 0.0f;
    float gradient_start_y = 0.0f;
    float gradient_end_x = 0.0f;
    float gradient_end_y = 0.0f;
    std::vector<GradientStop> gradient_stops{};
    bool has_image = false;
    std::uint32_t texture_id = 0U;
    std::uint32_t object_fit = ED_OBJECT_FIT_FILL;
    bool has_image_nine = false;
    std::uint32_t image_nine_texture_id = 0U;
    float image_nine_inset_left = 0.0f;
    float image_nine_inset_top = 0.0f;
    float image_nine_inset_right = 0.0f;
    float image_nine_inset_bottom = 0.0f;
    bool has_svg = false;
    std::uint32_t svg_id = 0U;
    std::uint32_t svg_tint_color = 0U;
    std::string node_id{};
    UiSemanticRole semantic_role = UI_SEMANTIC_NONE;
    std::string semantic_label{};
    UiSemanticCheckedState semantic_checked_state = UI_SEMANTIC_CHECKED_NONE;
    bool has_semantic_selected = false;
    bool semantic_selected = false;
    bool has_semantic_expanded = false;
    bool semantic_expanded = false;
    bool has_semantic_disabled = false;
    bool semantic_disabled = false;
    bool has_semantic_value_range = false;
    float semantic_value_now = 0.0f;
    float semantic_value_min = 0.0f;
    float semantic_value_max = 0.0f;
    UiOrientation semantic_orientation = UI_ORIENTATION_NONE;
    std::string text_content{};
    std::uint32_t font_id = 0;
    float font_size = 16.0f;
    float authored_line_height = 0.0f;
    std::uint32_t text_color = EF_RGB(0x00U, 0x00U, 0x00U);
    std::uint32_t text_align = 0U;
    std::uint32_t text_vertical_align = 0U;
    std::int32_t max_chars = 0;
    std::int32_t max_lines = 0;
    bool text_wrap = true;
    std::int32_t tab_index = 0;
    std::uint32_t text_overflow = 0U;
    bool text_overflow_fade_horizontal = false;
    bool text_overflow_fade_vertical = false;
    bool is_obscured = false;
    bool has_text_style_runs = false;
    std::vector<TextStyleRun> text_style_runs{};
    std::vector<std::uint32_t> text_line_starts{0U};
    bool text_line_starts_dirty = true;
    std::vector<std::int32_t> break_offsets{0};
    std::vector<float> line_widths{};
    std::vector<float> line_heights{};
    std::vector<float> line_ascents{};
    std::vector<float> line_y_offsets{};
    float line_height = 0.0f;
    std::size_t visible_line_count = 0;
    std::size_t total_line_count = 0;
    bool text_layout_cache_valid = false;
    float text_layout_cache_width_limit = -1.0f;
    float text_layout_cache_max_line_width = 0.0f;
    bool text_glyphs_dirty = false;
    bool logical_line_shape_cache_valid = false;
    std::vector<CachedLogicalLineShape> logical_line_shapes{};
    bool visual_line_shape_cache_valid = false;
    std::vector<CachedVisualLineShape> visual_line_shapes{};
    std::uint32_t wrapped_single_line_tail_patch_generation = 0U;
    bool nonwrap_fragment_cache_valid = false;
    std::vector<std::size_t> nonwrap_fragment_line_offsets{};
    std::vector<NonWrappingTextFragment> nonwrap_fragments{};
    std::uint32_t nonwrap_fragment_cache_generation = 0U;
    mutable std::vector<CachedNonWrapGeometrySlice> cached_nonwrap_geometry_slices{};
    bool nonwrap_render_fragment_window_valid = false;
    std::size_t nonwrap_render_fragment_start = 0U;
    std::size_t nonwrap_render_fragment_end = 0U;
    bool text_render_window_valid = false;
    std::size_t text_render_line_start = 0U;
    std::size_t text_render_line_end = 0U;
    mutable float textbox_viewport_offset_x = 0.0f;
    std::uint32_t selection_start = 0;
    std::uint32_t selection_end = 0;
    bool text_selection_visuals_dirty = false;
    std::uint32_t caret_color = EF_RGB(0x00U, 0x00U, 0x00U);
    std::uint32_t selection_color = EF_RGBA(0x00U, 0x7AU, 0xFFU, 0x40U);
    std::uint64_t last_interaction_time = 0;
    std::vector<UndoEntry> undo_stack{};
    std::vector<UndoEntry> redo_stack{};
    bool undo_group_open = false;
    std::uint64_t undo_group_timestamp = 0;
    std::uint32_t undo_group_caret_before = 0;
    std::uint32_t undo_group_sel_start_before = 0;
    std::uint32_t undo_group_sel_end_before = 0;
};

struct HandleParts {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
};

struct SceneInstruction {
    std::uint32_t opcode = 0;
    std::uint64_t handle = 0;
    float arg0 = 0.0f;
    float arg1 = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ColoredRect {
    Rect rect{};
    std::uint32_t color = 0U;
};

enum UiTextAlign : std::uint32_t {
    ALIGN_LEFT = 0,
    ALIGN_CENTER = 1,
    ALIGN_RIGHT = 2,
};

enum UiTextVerticalAlign : std::uint32_t {
    VERTICAL_ALIGN_TOP = 0,
    VERTICAL_ALIGN_CENTER = 1,
    VERTICAL_ALIGN_BOTTOM = 2,
};

enum UiTextOverflow : std::uint32_t {
    OVERFLOW_CLIP = 0,
    OVERFLOW_ELLIPSIS = 1,
    OVERFLOW_FADE = 2,
};

inline std::uint64_t PackHandle(std::uint32_t index, std::uint32_t generation) {
    return (static_cast<std::uint64_t>(generation) << 32U) | static_cast<std::uint64_t>(index);
}

inline HandleParts DecodeHandle(std::uint64_t handle) {
    return HandleParts{
        static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL),
        static_cast<std::uint32_t>(handle >> 32U),
    };
}

} // namespace effindom::v2::ui
