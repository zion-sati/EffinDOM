#pragma once

#include "UiTypes.h"
#include "UiTextEdit.h"
#include "UiNodeStoreAccess.h"
#include "UiEventSink.h"
#include "UiPlatformHost.h"
#include "UiFocusCoordinator.h"
#include "UiFixedPitchTabs.h"
#include "UiFrameCommitCoordinator.h"
#include "UiGridLayoutSource.h"
#include "UiNodeStore.h"
#include "UiInputRouter.h"
#include "UiLayoutCoordinator.h"
#include "UiSelectionCoordinator.h"
#include "UiTextEditingCoordinator.h"
#include "UiVisibilityResolver.h"
#include "UiScrollCoordinator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <hb.h>

namespace effindom::v2::ui {

class TreePainter;
class SceneGeometryResolver;

class CommandBuilder;

inline constexpr std::size_t kTextboxHardClampMaxCodepoints = 10000U;

class UiRuntime :
    private SelectionHost,
    private TextEditingHost,
    private InputRouter::Host,
    private GridLayoutSource,
    private FrameCommitHost {
    friend class TextPaintEncoder;
    friend class TextPaintAccess;
public:
    struct TextCommitProfile {
        double total_commit_ms = 0.0;
        double yoga_layout_ms = 0.0;
        std::uint32_t layout_stabilization_passes = 0U;
        double scroll_metrics_ms = 0.0;
        double caret_visibility_ms = 0.0;
        double walk_tree_ms = 0.0;
        double semantic_sync_ms = 0.0;
        double layout_paragraph_total_ms = 0.0;
        double logical_line_shape_build_ms = 0.0;
        double break_candidate_build_ms = 0.0;
        double wrapped_segment_break_ms = 0.0;
        double nonwrap_fragment_build_ms = 0.0;
        double shape_text_ms = 0.0;
        double measure_single_line_width_ms = 0.0;
        std::uint32_t layout_paragraph_calls = 0U;
        std::uint32_t layout_paragraph_cache_hits = 0U;
        std::uint32_t wrapped_layout_calls = 0U;
        std::uint32_t nonwrap_layout_calls = 0U;
        std::uint32_t logical_line_shape_cache_hits = 0U;
        std::uint32_t logical_line_shape_cache_builds = 0U;
        std::uint32_t logical_line_shapes_built = 0U;
        std::uint32_t break_candidate_cache_hits = 0U;
        std::uint32_t break_candidate_cache_builds = 0U;
        std::uint32_t wrapped_segment_break_calls = 0U;
        std::uint32_t nonwrap_fragment_line_builds = 0U;
        std::uint32_t shape_text_calls = 0U;
        std::uint64_t shape_text_bytes = 0U;
        std::uint32_t harfbuzz_shape_calls = 0U;
        std::uint64_t harfbuzz_shape_bytes = 0U;
        std::uint32_t measure_single_line_width_calls = 0U;
        std::uint64_t measure_single_line_width_bytes = 0U;
        std::uint32_t exact_text_edit_applications = 0U;
        std::uint64_t exact_text_edit_inserted_bytes = 0U;
        std::uint64_t exact_text_edit_removed_bytes = 0U;
        std::uint64_t previous_text_materialized_bytes = 0U;
        std::uint32_t full_text_replacement_fallbacks = 0U;
        std::uint64_t full_text_replacement_compared_bytes = 0U;
        std::uint64_t shaping_buffer_creations = 0U;
        std::uint64_t shaping_buffer_cache_hits = 0U;
        std::uint64_t shaping_sized_font_creations = 0U;
        std::uint64_t shaping_sized_font_cache_hits = 0U;
        std::uint64_t shaping_sized_font_evictions = 0U;
        std::uint64_t glyph_run_commands = 0U;
        std::uint64_t glyph_run_plain_commands = 0U;
        std::uint64_t glyph_run_colored_commands = 0U;
        std::uint64_t glyph_run_styled_commands = 0U;
        std::uint64_t glyph_run_encoded_words = 0U;
        std::uint64_t glyphs_emitted = 0U;
    };

    struct DynamicTextPrepareProfile {
        double total_prepare_ms = 0.0;
        std::uint32_t fast_path_attempts = 0U;
        std::uint32_t fast_path_successes = 0U;
        std::uint32_t fast_path_fallbacks = 0U;
        std::uint32_t cache_hits = 0U;
        std::uint32_t cache_misses = 0U;
        std::uint32_t composed_glyphs = 0U;
        std::uint64_t composed_bytes = 0U;
    };

    struct ShapingResourceProfile {
        std::uint64_t buffer_creations = 0U;
        std::uint64_t buffer_destructions = 0U;
        std::uint64_t buffer_cache_hits = 0U;
        std::uint64_t sized_font_creations = 0U;
        std::uint64_t sized_font_destructions = 0U;
        std::uint64_t sized_font_cache_hits = 0U;
        std::uint64_t sized_font_evictions = 0U;
        std::uint64_t tab_expansion_calls = 0U;
        std::uint64_t tab_expanded_bytes = 0U;
        std::uint64_t tab_cluster_map_entries = 0U;
        std::uint64_t fixed_pitch_tab_attempts = 0U;
        std::uint64_t fixed_pitch_tab_successes = 0U;
        std::uint64_t fixed_pitch_tab_rejections = 0U;
    };

    struct TextGeometryProfile {
        std::uint64_t bounded_calls = 0U;
        std::uint64_t unrestricted_calls = 0U;
        std::uint64_t lines_visited = 0U;
        std::uint64_t rectangles_emitted = 0U;
        std::uint64_t find_rectangles_emitted = 0U;
        std::uint64_t style_rectangles_emitted = 0U;
        std::uint64_t shaping_calls = 0U;
        std::uint64_t shaping_bytes = 0U;
    };

    struct TextFindHighlight {
        std::uint64_t handle = UI_INVALID_HANDLE;
        std::uint32_t start = 0U;
        std::uint32_t end = 0U;
        std::uint32_t color = 0U;
    };

    struct TextRenderPlan {
        std::vector<GlyphPlacement> glyphs{};
        std::vector<ColoredRect> style_highlights{};
        float width = 0.0f;
        float height = 0.0f;
    };

    UiRuntime();
    explicit UiRuntime(UiPlatformHost& platform_host);
    ~UiRuntime();
    UiRuntime(const UiRuntime&) = delete;
    UiRuntime& operator=(const UiRuntime&) = delete;

    void Reset();
    void ResetFrameArena();
    std::uintptr_t ArenaAlloc(std::uint32_t byte_length);

    std::uint64_t CreateNode(std::uint32_t type);
    bool DeleteNode(std::uint64_t handle);
    bool AddChild(std::uint64_t parent_handle, std::uint64_t child_handle);
    bool RemoveChild(std::uint64_t parent_handle, std::uint64_t child_handle);
    bool SetRoot(std::uint64_t handle);
    bool SetNodeId(std::uint64_t handle, const std::uint8_t* utf8_id, std::uint32_t len);
    bool SetSemanticRole(std::uint64_t handle, std::uint32_t role_enum);
    bool SetSemanticLabel(std::uint64_t handle, const std::uint8_t* utf8_label, std::uint32_t len);
    bool SetSemanticChecked(std::uint64_t handle, std::uint32_t checked_state_enum);
    bool SetSemanticSelected(std::uint64_t handle, bool has_selected, bool is_selected);
    bool SetSemanticExpanded(std::uint64_t handle, bool has_expanded, bool is_expanded);
    bool SetSemanticDisabled(std::uint64_t handle, bool has_disabled, bool is_disabled);
    bool SetSemanticValueRange(
        std::uint64_t handle,
        bool has_value_range,
        float value_now,
        float value_min,
        float value_max);
    bool SetSemanticOrientation(std::uint64_t handle, std::uint32_t orientation_enum);
    bool RequestSemanticAnnouncement(std::uint64_t handle);
    std::uint32_t PushSemanticScope(std::uint64_t handle);
    bool RemoveSemanticScope(std::uint32_t token);
    bool SetPortal(std::uint64_t handle, bool is_portal);
    bool SetVisibility(std::uint64_t handle, std::uint32_t visibility_enum);
    bool SetSelectionArea(std::uint64_t handle, bool is_selection_area);
    bool SetSelectionAreaBarrier(std::uint64_t handle, bool is_barrier);
    bool SetIsSharedSizeScope(std::uint64_t handle, bool is_scope);
    bool SetCustomDrawable(std::uint64_t handle, bool is_custom_drawable);
    bool SetFlexWrap(std::uint64_t handle, std::uint32_t wrap_enum);
    bool SetGridColumns(std::uint64_t handle, std::uint32_t count, const float* values, const std::uint8_t* types);
    bool SetGridRows(std::uint64_t handle, std::uint32_t count, const float* values, const std::uint8_t* types);
    bool SetGridColumnSharedSizeGroup(
        std::uint64_t handle,
        std::uint32_t index,
        const std::uint8_t* utf8_group,
        std::uint32_t len);
    bool SetGridRowSharedSizeGroup(
        std::uint64_t handle,
        std::uint32_t index,
        const std::uint8_t* utf8_group,
        std::uint32_t len);
    bool SetGridPlacement(
        std::uint64_t handle,
        std::uint32_t row,
        std::uint32_t col,
        std::uint32_t row_span,
        std::uint32_t col_span);
    void ResizeWindow(float logical_width, float logical_height);

    bool SetWidth(std::uint64_t handle, float value, std::uint32_t unit_enum);
    bool SetHeight(std::uint64_t handle, float value, std::uint32_t unit_enum);
    bool SetFillWidth(std::uint64_t handle, bool fill);
    bool SetFillHeight(std::uint64_t handle, bool fill);
    bool SetFillWidthPercent(std::uint64_t handle, float percent);
    bool SetFillHeightPercent(std::uint64_t handle, float percent);
    bool SetMinWidth(std::uint64_t handle, float value, std::uint32_t unit_enum);
    bool SetMaxWidth(std::uint64_t handle, float value, std::uint32_t unit_enum);
    bool SetMinHeight(std::uint64_t handle, float value, std::uint32_t unit_enum);
    bool SetMaxHeight(std::uint64_t handle, float value, std::uint32_t unit_enum);
    bool SetFlexDirection(std::uint64_t handle, std::uint32_t dir_enum);
    bool SetFlexBasis(std::uint64_t handle, float basis);
    bool SetJustifyContent(std::uint64_t handle, std::uint32_t justify_enum);
    bool SetAlignItems(std::uint64_t handle, std::uint32_t align_enum);
    bool SetAlignSelf(std::uint64_t handle, std::uint32_t align_enum);
    bool SetPadding(std::uint64_t handle, float left, float top, float right, float bottom);
    bool SetMargin(std::uint64_t handle, float left, float top, float right, float bottom);
    bool SetPositionType(std::uint64_t handle, std::uint32_t pos_enum);
    bool SetPosition(std::uint64_t handle, float left, float top, float right, float bottom);
    bool SetClipToBounds(std::uint64_t handle, bool clip);
    bool SetBoxStyle(
        std::uint64_t handle,
        std::uint32_t bg_color,
        float radius_tl,
        float radius_tr,
        float radius_br,
        float radius_bl,
        float border_width,
        std::uint32_t border_color,
        std::uint32_t border_style,
        float border_dash_on,
        float border_dash_off);
    bool SetDropShadow(
        std::uint64_t handle,
        std::uint32_t color,
        float offset_x,
        float offset_y,
        float blur_sigma,
        float spread);
    bool SetLayerEffect(std::uint64_t handle, float opacity, float blur_sigma, std::uint32_t blend_mode);
    bool SetBackgroundBlur(std::uint64_t handle, float blur_sigma);
    bool SetLinearGradient(
        std::uint64_t handle,
        float start_x,
        float start_y,
        float end_x,
        float end_y,
        std::uint32_t stop_count,
        const float* offsets,
        const std::uint32_t* colors);
    bool SetImage(
        std::uint64_t handle,
        std::uint32_t texture_id,
        std::uint32_t object_fit_enum,
        std::uint32_t sampling_kind,
        std::uint32_t max_aniso);
    bool SetImageNine(
        std::uint64_t handle,
        std::uint32_t texture_id,
        float inset_left,
        float inset_top,
        float inset_right,
        float inset_bottom,
        std::uint32_t sampling_kind,
        std::uint32_t max_aniso);
    bool SetSvg(
        std::uint64_t handle,
        std::uint32_t svg_id,
        std::uint32_t tint_color,
        std::uint32_t sampling_kind,
        std::uint32_t max_aniso);

    bool SetText(std::uint64_t handle, const std::uint8_t* utf8_str, std::uint32_t len);
    bool SetTextStyleRuns(std::uint64_t handle, std::uint32_t run_count, const std::uint32_t* runs_words);
    bool SetFont(std::uint64_t handle, std::uint32_t font_id, float size);
    bool SetLineHeight(std::uint64_t handle, float line_height);
    bool SetTextColor(std::uint64_t handle, std::uint32_t color);
    bool SetTextAlign(std::uint64_t handle, std::uint32_t align_enum);
    bool SetTextVerticalAlign(std::uint64_t handle, std::uint32_t align_enum);
    bool SetTextLimits(std::uint64_t handle, std::int32_t max_chars, std::int32_t max_lines);
    bool SetTextWrapping(std::uint64_t handle, bool wrap);
    bool SetTextOverflow(std::uint64_t handle, std::uint32_t overflow_enum);
    bool SetTextOverflowFade(std::uint64_t handle, bool horizontal, bool vertical);
    bool SetTextObscured(std::uint64_t handle, bool is_password);
    bool SetInteractive(std::uint64_t handle, bool interactive);
    bool SetPreserveSelectionOnPointerDown(std::uint64_t handle, bool preserve);
    bool SetEditorCommandKeys(std::uint64_t handle, bool enabled);
    bool SetEditorAcceptsTab(std::uint64_t handle, bool enabled);
    bool SetScrollProxyTarget(std::uint64_t handle, std::uint64_t scroll_handle);
    bool SetScrollEnabled(std::uint64_t handle, bool enabled_x, bool enabled_y);
    bool SetScrollFriction(std::uint64_t handle, float friction);
    bool SetSmoothScrolling(std::uint64_t handle, bool smooth_scrolling);
    bool SetFocusable(std::uint64_t handle, bool focusable, std::int32_t tab_index);
    bool RequestFocus(std::uint64_t handle);
    bool SetScrollOffset(std::uint64_t handle, float offset_x, float offset_y);
    bool SetScrollContentSize(std::uint64_t handle, float content_width, float content_height);
    bool SetSelectable(std::uint64_t handle, bool selectable, std::uint32_t selection_color);
    bool ClearSelection(std::uint64_t handle);
    bool RetargetSelection(std::uint64_t from_handle, std::uint64_t to_handle);
    bool IsPointInSelection(float logical_x, float logical_y);
    bool ClearCurrentSelection(bool notify_callback);
    bool CopyCurrentSelection() const;
    bool SetTextSelectionRange(std::uint64_t handle, std::uint32_t selection_start, std::uint32_t selection_end);
    bool CanUndoTextEdit(std::uint64_t handle) const;
    bool CanRedoTextEdit(std::uint64_t handle) const;
    bool HasTextSelection(std::uint64_t handle) const;
    bool UndoTextEditAtHandle(std::uint64_t handle);
    bool RedoTextEditAtHandle(std::uint64_t handle);
    bool CopyTextSelection(std::uint64_t handle) const;
    bool CutTextSelection(std::uint64_t handle);
    bool PasteText(std::uint64_t handle);
    bool SelectAllText(std::uint64_t handle);
    bool SelectWordAt(std::uint64_t handle, float logical_x, float logical_y);
    bool BeginSelectionEndpointDrag(std::uint64_t handle, std::uint32_t endpoint);
    bool PreservesSelectionOnPointerDown(std::uint64_t handle) const;
    bool SetEditable(std::uint64_t handle, bool editable);
    bool SetCaretColor(std::uint64_t handle, std::uint32_t color);
    bool RegisterFont(
        std::uint32_t font_id,
        const std::uint8_t* bytes,
        std::uint32_t length,
        std::uint32_t face_index = 0U);
    bool RegisterFontFallback(std::uint32_t font_id, std::uint32_t fallback_font_id);
    bool UnregisterFont(std::uint32_t font_id);
    bool UnregisterFontFallback(std::uint32_t font_id, std::uint32_t fallback_font_id);
    bool RegisterIcuData(const std::uint8_t* bytes, std::uint32_t length);
    void FontLoaded(std::uint32_t font_id);
    void SetInteractionTime(std::uint64_t interaction_time_ms);
    void HandlePointerEvent(
        std::uint32_t event_enum,
        std::uint64_t handle,
        float logical_x,
        float logical_y,
        std::int32_t pointer_id = -1,
        std::uint32_t pointer_type = UI_POINTER_TYPE_UNKNOWN,
        std::int32_t button = 0,
        std::uint32_t buttons = 0,
        float pressure = 0.0f,
        float width = 0.0f,
        float height = 0.0f,
        std::int32_t click_count = 0,
        std::uint32_t modifiers = 0);
    void HandleWheelEvent(float delta_x, float delta_y);
    void HandlePreciseWheelEvent(float delta_x, float delta_y, bool begins_gesture, bool ends_gesture);
    void BeginTouchScroll(std::uint64_t handle, float logical_x, float logical_y, double timestamp_ms = -1.0);
    void UpdateTouchScroll(float delta_x, float delta_y, double timestamp_ms = -1.0);
    void EndTouchScroll(double timestamp_ms = -1.0);
    void ClearMomentumScroll();
    bool ActiveTouchScrollAllowsPullToRefresh() const;
    bool WheelScrollCanConsume(float delta_x, float delta_y) const;
    bool ActiveTouchScrollCanConsume(float delta_x, float delta_y) const;
    void SetCoarsePointerMode(bool coarse_pointer_mode);
    void SetPlatformFamily(std::uint32_t platform_family);
    bool HandleKeyEvent(std::uint32_t type_enum, const std::uint8_t* key_utf8, std::uint32_t len, std::uint32_t modifiers);
    void HandleImeUpdate(std::uint64_t handle, const std::uint8_t* utf8_str, std::uint32_t len, std::uint32_t caret_idx);
    void HandleTextReplaceRange(
        std::uint64_t handle,
        std::uint32_t start_idx,
        std::uint32_t end_idx,
        const std::uint8_t* utf8_str,
        std::uint32_t len,
        std::uint32_t caret_idx);
    void HandlePasteText(std::uint64_t handle, const std::uint8_t* utf8_str, std::uint32_t len);
    void MeasureText(
        const std::uint8_t* utf8_str,
        std::uint32_t len,
        std::uint32_t font_id,
        float size,
        float max_width,
        float* out_width,
        float* out_height) const;
    bool GetTextMetrics(
        std::uint64_t handle,
        float* out_width,
        float* out_height,
        float* out_baseline,
        std::uint32_t* out_line_count,
        float* out_max_line_width) const;
    std::optional<Rect> GetVisibleBounds(std::uint64_t handle) const;
    std::vector<std::uint64_t> GetTextSnapshotHandles() const;
    std::optional<std::string_view> GetTextSnapshotDocument(std::uint64_t handle) const;
    std::optional<Rect> GetTextVisibleBounds(std::uint64_t handle) const;
    std::vector<Rect> GetTextRangeSceneRects(std::uint64_t handle, std::uint32_t start, std::uint32_t end) const;
    bool GetCrossSelectionEndpointSceneRects(std::uint64_t area_handle, Rect& out_start_rect, Rect& out_end_rect);
    bool RevealTextRange(std::uint64_t handle, std::uint32_t start, std::uint32_t end);
    bool SetTextFindMatch(std::uint64_t handle, std::uint32_t start, std::uint32_t end);
    void ClearTextFindMatch();
    bool PushTextFindHighlight(std::uint64_t handle, std::uint32_t start, std::uint32_t end, std::uint32_t color);
    void ClearTextFindHighlights();

    bool SetNodeColor(std::uint64_t handle, std::uint32_t color);
    const UINode* Resolve(std::uint64_t handle) const;
    bool IsSharedSizeScope(std::uint64_t handle) const;
    const std::vector<std::string>& GridColumnSharedSizeGroups(std::uint64_t handle) const;
    const std::vector<std::string>& GridRowSharedSizeGroups(std::uint64_t handle) const override;

    void CommitFrame(double timestamp_ms = -1.0);
    bool PrepareNode(std::uint64_t handle);
    bool SetDynamicTextCharset(std::uint64_t handle, const std::uint8_t* utf8_charset, std::uint32_t len);
    const std::vector<std::uint32_t>& command_buffer() const;
    const std::vector<std::uint32_t>& semantic_buffer() const;
    const std::vector<std::uint32_t>& debug_tree_buffer() const;
    const std::vector<std::uint32_t>& LiveFallbackFontBuffer() const;
    const std::uint64_t& root_handle() const;
    bool HasPendingVisualWork() const;
    bool NeedsAnimationFrame() const;
    bool HasPointerAutoScroll() const;
    std::uint64_t SelectionAutoScroll(float logical_x, float logical_y, float edge_threshold);
    float window_width() const;
    float window_height() const;
    const TextCommitProfile& last_text_commit_profile() const;
    void ClearTextCommitProfile();
    const DynamicTextPrepareProfile& last_dynamic_text_prepare_profile() const;
    void ClearDynamicTextPrepareProfile();
    const ShapingResourceProfile& shaping_resource_profile() const;
    void ClearShapingResourceProfile();
    const TextGeometryProfile& text_geometry_profile() const;
    void ClearTextGeometryProfile();

private:
    Rect ComputeInputVisibleBounds(const UINode& node) const override {
        return ComputeVisibleBounds(node);
    }
    void ClearInputSelectionHighlight(std::uint64_t handle, bool notify_callback) override {
        ClearSelectionHighlight(handle, notify_callback);
    }
    bool IsInputEditorTextNode(const UINode& node) const override { return IsEditorTextNode(node); }
    std::uint32_t GetInputStringIndexFromPoint(
        const UINode& node,
        float local_x,
        float local_y) const override {
        return GetStringIndexFromPoint(node, local_x, local_y);
    }
    std::pair<std::uint32_t, std::uint32_t> GetInputWordBoundaries(
        const UINode& node,
        std::uint32_t index) const override {
        return GetWordBoundaries(node, index);
    }
    std::pair<std::uint32_t, std::uint32_t> GetInputParagraphBoundaries(
        const UINode& node,
        std::uint32_t index) const override {
        return GetParagraphBoundaries(node, index);
    }
    bool ShouldUseInputTrailingCaretEdge(
        const UINode& node,
        std::uint32_t index,
        float local_x,
        float local_y) const override {
        const auto [leading_x, leading_line] = GetLocalPositionFromIndex(node, index, false);
        const auto [trailing_x, trailing_line] = GetLocalPositionFromIndex(node, index, true);
        if (trailing_line < 0 || leading_line < 0 || trailing_line + 1 != leading_line) return false;
        const float leading_line_top = GetLineTopForIndex(node, static_cast<std::size_t>(leading_line));
        const float leading_line_height = GetLineHeightForIndex(node, static_cast<std::size_t>(leading_line));
        const float x_slop = std::max(4.0f, std::min(node.font_size * 0.5f, 10.0f));
        (void)leading_x;
        return local_x >= trailing_x - x_slop &&
            local_y <= leading_line_top + (leading_line_height * 0.5f);
    }
    void MarkInputTextSelectionVisualsDirty(UINode& node) override {
        MarkTextSelectionVisualsDirty(node);
    }
    bool SetInputTextSelectionRange(
        std::uint64_t handle,
        std::uint32_t start,
        std::uint32_t end) override {
        return SetTextSelectionRange(handle, start, end);
    }
    void EnsureInputTextCaretVisible(std::uint64_t handle, UINode& node) override {
        EnsureTextCaretVisible(handle, node);
    }
    void SetInputPendingCaretVisibility(std::uint64_t handle) override {
        pending_caret_visibility_handle_ = handle;
    }
    bool ClearInputSelection(bool notify_callback) override {
        return ClearCurrentSelection(notify_callback);
    }
    std::uint64_t ActiveInputFocusScope() override { return GetActiveSemanticScopeRoot(); }
    void SetInputFocus(
        std::uint64_t handle,
        bool ensure_visible,
        bool emit_selection_callback) override {
        SetFocus(handle, ensure_visible, emit_selection_callback);
    }
    std::uint64_t FindInputSelectionAreaAncestor(std::uint64_t handle) const override {
        return FindSelectionAreaAncestor(handle);
    }
    std::string BuildInputCrossSelectionText() const override { return BuildCrossSelectionText(); }
    bool BuildInputCrossSelectionRichPayload(std::string& plain_text, std::string& rich_json) const override {
        return BuildCrossSelectionRichPayload(plain_text, rich_json);
    }
    void EmitInputClipboardWrite(const std::string& plain_text, const std::string* rich_json) const override {
        EmitClipboardWrite(plain_text, rich_json);
    }
    bool HandleInputCrossSelectionNavigation(
        std::uint64_t area_handle,
        UINode& node,
        std::string_view key,
        std::uint32_t modifiers) override {
        return HandleCrossSelectionNavigation(area_handle, node, key, modifiers);
    }
    bool IsInputPrimaryShortcut(std::string_view key, std::uint32_t modifiers, char expected) const override {
        return IsPrimaryShortcut(key, modifiers, expected);
    }
    bool IsInputUndoShortcut(std::string_view key, std::uint32_t modifiers) const override {
        return IsUndoShortcut(key, modifiers);
    }
    bool IsInputRedoShortcut(std::string_view key, std::uint32_t modifiers) const override {
        return IsRedoShortcut(key, modifiers);
    }
    bool UndoInputTextEdit(std::uint64_t handle, UINode& node) override { return UndoTextEdit(handle, node); }
    bool RedoInputTextEdit(std::uint64_t handle, UINode& node) override { return RedoTextEdit(handle, node); }
    bool SelectAllInputText(std::uint64_t handle) override { return SelectAllText(handle); }
    void CopyInputText(const UINode& node) const override { HandleCopy(node); }
    bool CutInputText(UINode& node) override { return HandleCut(node); }
    bool PasteInputText(UINode& node) override { return HandlePaste(node); }
    bool HandleInputTextEditingKey(
        UINode& node,
        std::string_view key,
        std::uint32_t modifiers) override {
        return HandleTextEditingKey(node, key, modifiers);
    }

    void SetTextEditingSelectionAnchor(std::uint64_t handle, std::uint32_t index) override {
        Selection().state().anchor_handle = handle;
        Selection().state().anchor_index = index;
    }
    void BeginTextEditTransaction() override {
        if (!text_commit_profile_active_) {
            current_text_commit_profile_ = pending_text_edit_profile_;
            pending_text_edit_profile_ = TextCommitProfile{};
            text_commit_profile_active_ = true;
        }
    }
    void RecordTextEditApplication(std::size_t inserted_bytes, std::size_t removed_bytes) override {
        current_text_commit_profile_.exact_text_edit_applications += 1U;
        current_text_commit_profile_.exact_text_edit_inserted_bytes += inserted_bytes;
        current_text_commit_profile_.exact_text_edit_removed_bytes += removed_bytes;
    }
    void RecordTextEditMaterializedPreviousText(std::size_t bytes) override {
        current_text_commit_profile_.previous_text_materialized_bytes += bytes;
    }
    bool WouldApplyTextHardClamp(const UINode& node) const override { return WouldApplyAbsurdLineClamp(node); }
    bool ApplyTextHardClamp(UINode& node) const override { return ApplyAbsurdLineClamp(node); }
    void NotifyTextEditApplied(std::uint64_t handle, UINode& node, const TextEdit& edit) override {
        NotifyTextStateChanged(handle, node, edit);
    }
    void NotifyTextEditClamped(
        std::uint64_t handle,
        UINode& node,
        const std::string& previous_text) override {
        NotifyTextStateChanged(handle, node, &previous_text);
    }
    bool TryApplyTextEditLineStarts(UINode& node, const TextEdit& edit) const override {
        return TryApplyIncrementalTextLineStarts(node, edit);
    }
    bool TryApplyTextEditLineStarts(UINode& node, std::string_view previous_text) const override {
        return TryApplyIncrementalTextLineStarts(node, previous_text);
    }
    void RebuildTextEditLineStarts(UINode& node) const override { RebuildTextLineStarts(node); }
    std::uint64_t TextEditInteractionTime() const override { return Input().state().interaction_time_ms; }
    bool TryApplyTextEditNonWrapCache(UINode& node, const TextEdit& edit) const override {
        return TryApplyIncrementalNonWrapLayoutCache(node, edit);
    }
    bool TryApplyTextEditWrappedCache(UINode& node, const TextEdit& edit) const override {
        return TryApplyIncrementalWrappedLayoutCache(node, edit);
    }
    bool TryApplyTextEditNonWrapCache(UINode& node, std::string_view previous_text) const override {
        return TryApplyIncrementalNonWrapLayoutCache(node, previous_text);
    }
    bool TryApplyTextEditWrappedCache(UINode& node, std::string_view previous_text) const override {
        return TryApplyIncrementalWrappedLayoutCache(node, previous_text);
    }
    void InvalidateTextEditLayoutCache(UINode& node) override { InvalidateTextLayoutCache(node); }
    void MarkTextEditYogaDirty(UINode& node) override {
        if (node.yg_node != nullptr) {
            YGNodeMarkDirty(node.yg_node);
        }
    }
    void MarkTextEditLayoutDirty() override { layout_dirty_ = true; }
    bool RegisterTextEditScrollMetrics(std::uint64_t handle) override {
        return pending_text_scroll_metric_handles_.insert(handle).second;
    }
    bool IsTextEditFocused(std::uint64_t handle) const override { return Focus().IsFocused(handle); }
    void UpdateTextEditAncestorScrollMetrics(std::uint64_t handle) override { UpdateAncestorScrollMetrics(handle); }
    void EnsureTextEditCaretVisible(std::uint64_t handle, UINode& node) override { EnsureTextCaretVisible(handle, node); }
    void SetTextEditPendingCaretVisibility(std::uint64_t handle) override {
        pending_caret_visibility_handle_ = handle;
    }
    std::optional<TextEdit> CreateTextEditFullReplacement(
        std::string_view old_text,
        std::string_view new_text) const override {
        return CreateFullReplacementTextEdit(old_text, new_text);
    }
    std::uint32_t NextTextEditCharacterIndex(
        std::string_view utf8_text,
        std::uint32_t index,
        bool forward) const override {
        return NextCharacterIndex(utf8_text, index, forward);
    }
    void SetTextEditHorizontalSelectionActive(bool active) override {
        Selection().state().horizontal_extend_active = active;
    }
    bool TextEditHorizontalSelectionActive() const override {
        return Selection().state().horizontal_extend_active;
    }
    bool HasTextEditSelectionAnchor(std::uint64_t handle) const override {
        return Selection().state().anchor_handle == handle;
    }
    std::uint32_t TextEditSelectionAnchorIndex() const override {
        return Selection().state().anchor_index;
    }
    std::uint32_t NextTextEditWordIndex(const UINode& node, std::uint32_t index, bool forward) const override {
        return NextWordIndex(node, index, forward);
    }
    std::uint32_t TextEditLineBegin(const UINode& node, std::uint32_t index) const override {
        return IndexForLineBegin(node, index);
    }
    std::uint32_t TextEditLineEnd(const UINode& node, std::uint32_t index) const override {
        return IndexForLineEnd(node, index);
    }
    std::uint32_t TextEditVerticalMove(const UINode& node, std::uint32_t index, bool down) const override {
        return IndexForVerticalMove(node, index, down);
    }
    std::uint32_t TextEditPageMove(const UINode& node, std::uint32_t index, bool down) const override {
        return IndexForPageMove(node, index, down);
    }
    std::pair<float, int> TextEditLocalPosition(const UINode& node, std::uint32_t index) const override {
        return GetLocalPositionFromIndex(node, index);
    }
    void MarkTextEditSelectionVisualsDirty(UINode& node) override { MarkTextSelectionVisualsDirty(node); }
    void CopyTextEditSelection(const UINode& node) const override { HandleCopy(node); }
    void RequestTextEditClipboardRead(std::uint64_t handle) const override { platform_host_.RequestClipboardRead(handle); }

    const UINode* ResolveSelectionNode(std::uint64_t handle) const override { return Resolve(handle); }
    UINode* ResolveSelectionNodeMutable(std::uint64_t handle) override { return ResolveMutable(handle); }
    std::uint32_t GetSelectionIndexFromPoint(const UINode& node, float local_x, float local_y) const override {
        return GetStringIndexFromPoint(node, local_x, local_y);
    }
    std::vector<Rect> GetSelectionRangeSceneRects(
        std::uint64_t handle,
        std::uint32_t start,
        std::uint32_t end) const override {
        return GetTextRangeSceneRects(handle, start, end);
    }
    std::pair<float, float> ClampSelectionPointToViewport(
        std::uint64_t handle,
        float logical_x,
        float logical_y) const override {
        return ClampPointToScrollViewport(handle, logical_x, logical_y);
    }
    void MarkSelectionVisualsDirty(UINode& node) override { MarkTextSelectionVisualsDirty(node); }
    void NotifySelectionChanged(
        std::uint64_t handle,
        std::uint32_t start,
        std::uint32_t end) const override {
        event_sink_.SelectionChanged(handle, start, end);
    }
    void MarkSelectionLayoutDirty() override { layout_dirty_ = true; }
    void NotifyCrossSelectionChanged(std::uint64_t area_handle, std::string_view utf8_text) const override {
        event_sink_.CrossSelectionChanged(area_handle, utf8_text);
    }
    bool HasSelectionWordNavigationModifier(std::uint32_t modifiers) const override {
        return HasWordNavigationModifier(modifiers);
    }
    std::uint32_t NextSelectionWordIndex(const UINode& node, std::uint32_t index, bool forward) const override {
        return NextWordIndex(node, index, forward);
    }
    std::uint32_t NextSelectionCharacterIndex(
        std::string_view utf8_text,
        std::uint32_t index,
        bool forward) const override {
        return NextCharacterIndex(utf8_text, index, forward);
    }
    std::pair<float, std::size_t> GetSelectionLocalPositionFromIndex(
        const UINode& node,
        std::uint32_t index) const override {
        return GetLocalPositionFromIndex(node, index);
    }
    std::uint32_t GetSelectionIndexForVerticalMove(const UINode& node, std::uint32_t index, bool down) const override {
        return IndexForVerticalMove(node, index, down);
    }
    std::uint32_t GetSelectionIndexForPageMove(const UINode& node, std::uint32_t index, bool down) const override {
        return IndexForPageMove(node, index, down);
    }
    float GetSelectionAlignedLineXOffset(const UINode& node, float line_width) const override {
        return GetAlignedLineXOffset(node, line_width);
    }
    float GetSelectionAlignedTextYOffset(const UINode& node, float content_height) const override {
        return GetAlignedTextYOffset(node, content_height);
    }
    float GetSelectionTextContentHeight(const UINode& node, std::size_t visible_line_count) const override {
        return GetTextContentHeight(node, visible_line_count);
    }
    float GetSelectionLineTopForIndex(const UINode& node, std::size_t line_index) const override {
        return GetLineTopForIndex(node, line_index);
    }
    float GetSelectionLineHeightForIndex(const UINode& node, std::size_t line_index) const override {
        return GetLineHeightForIndex(node, line_index);
    }
    std::uint64_t FocusedSelectionHandle() const override { return Focus().FocusedHandle(); }
    void SetSelectionFocus(std::uint64_t handle) override { SetFocus(handle); }

    struct ShapingBufferEntry {
        hb_buffer_t* buffer = nullptr;
        bool leased = false;
    };

    class ShapingBufferLease {
    public:
        ShapingBufferLease() = default;
        ~ShapingBufferLease();
        ShapingBufferLease(const ShapingBufferLease&) = delete;
        ShapingBufferLease& operator=(const ShapingBufferLease&) = delete;
        ShapingBufferLease(ShapingBufferLease&& other) noexcept;
        ShapingBufferLease& operator=(ShapingBufferLease&& other) noexcept;

        hb_buffer_t* get() const { return buffer_; }
        explicit operator bool() const { return buffer_ != nullptr; }

    private:
        friend class UiRuntime;
        ShapingBufferLease(const UiRuntime* owner, std::size_t index, hb_buffer_t* buffer)
            : owner_(owner), index_(index), buffer_(buffer) {}
        void Release();

        const UiRuntime* owner_ = nullptr;
        std::size_t index_ = 0U;
        hb_buffer_t* buffer_ = nullptr;
    };

    struct EdgeInsets {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    struct SizedFontKey {
        int scale = 0;
        unsigned int ppem = 0U;

        bool operator==(const SizedFontKey& other) const {
            return scale == other.scale && ppem == other.ppem;
        }
        bool operator<(const SizedFontKey& other) const {
            return scale < other.scale || (scale == other.scale && ppem < other.ppem);
        }
    };

    struct SizedFontEntry {
        SizedFontKey key{};
        hb_font_t* font = nullptr;
        std::uint64_t access_sequence = 0U;
    };

    struct RegisteredFont {
        std::vector<std::uint8_t> bytes{};
        hb_blob_t* blob = nullptr;
        hb_face_t* face = nullptr;
        hb_font_t* font = nullptr;
        std::uint32_t upem = 0;
        hb_font_extents_t extents{};
        bool has_extents = false;
        bool is_fixed_pitch = false;
        bool is_ascii_fixed_pitch = false;
        std::uint64_t generation = 0U;
        std::optional<std::uint32_t> bullet_glyph_id{};
        std::optional<std::uint32_t> tofu_glyph_id{};
        mutable std::vector<SizedFontEntry> sized_fonts{};
    };

    struct GridSideTableEntry {
        bool is_shared_size_scope = false;
        std::vector<std::string> column_shared_size_groups{};
        std::vector<std::string> row_shared_size_groups{};
    };

    struct FontMetrics {
        float ascent = 0.0f;
        float descent = 0.0f;
        float height = 0.0f;
    };

    struct SemanticScopeEntry {
        std::uint32_t token = 0U;
        std::uint64_t handle = UI_INVALID_HANDLE;
    };

    struct ShapedTextRun {
        std::uint32_t font_id = 0;
        float width = 0.0f;
        float height = 0.0f;
        float baseline = 0.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        std::vector<GlyphPlacement> glyphs{};
    };

    template <typename T>
    class RetainedArrayView {
    public:
        RetainedArrayView() = default;
        RetainedArrayView(const std::vector<T>& values)
            : data_(values.data()), size_(values.size()) {}

        RetainedArrayView& operator=(const std::vector<T>& values) {
            data_ = values.data();
            size_ = values.size();
            return *this;
        }

        const T& operator[](std::size_t index) const { return data_[index]; }
        const T& front() const { return data_[0]; }
        const T* begin() const { return data_; }
        const T* end() const { return data_ + size_; }
        const T* data() const { return data_; }
        std::size_t size() const { return size_; }
        bool empty() const { return size_ == 0U; }
        bool operator==(const RetainedArrayView& other) const {
            return size_ == other.size_ && std::equal(begin(), end(), other.begin());
        }
        bool operator!=(const RetainedArrayView& other) const { return !(*this == other); }

    private:
        const T* data_ = nullptr;
        std::size_t size_ = 0U;
    };

    struct ParagraphLayout {
        RetainedArrayView<std::int32_t> break_offsets{};
        RetainedArrayView<float> line_widths{};
        RetainedArrayView<float> line_heights{};
        RetainedArrayView<float> line_ascents{};
        RetainedArrayView<float> line_y_offsets{};
        float max_line_width = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float line_height = 0.0f;
        std::size_t visible_line_count = 0;
        std::size_t total_line_count = 0;
        bool clipped = false;
    };

    struct VisualGeometryWindow {
        std::size_t line_start = 0U;
        std::size_t line_end = 0U;
        Rect local_clip{};

        bool visible() const {
            return line_start < line_end && local_clip.width > 0.0f && local_clip.height > 0.0f;
        }
    };

    struct NonWrappingFragmentWindow {
        std::size_t start = 0U;
        std::size_t end = 0U;
    };

    struct WalkTextState {
        ParagraphLayout paragraph{};
        bool render_window_visible = false;
        bool render_window_changed = false;
        std::size_t render_line_start = 0U;
        std::size_t render_line_end = 0U;
        VisualGeometryWindow geometry_window{};
        bool fragment_window_visible = false;
        bool fragment_window_changed = false;
        NonWrappingFragmentWindow fragment_window{};
    };

    struct FragmentGeometrySlice {
        std::uint32_t line_start = 0U;
        std::uint32_t line_end = 0U;
        std::uint32_t slice_start = 0U;
        std::uint32_t slice_end = 0U;
        float slice_x = 0.0f;
        float full_line_width = 0.0f;
        ShapedTextRun shaped{};
        std::vector<TextClusterStop> cluster_stops{};
    };

private:

    static YGSize MeasureTextCallback(
        YGNodeConstRef yg_node,
        float width,
        YGMeasureMode width_mode,
        float height,
        YGMeasureMode height_mode);

    void ResetCommitFrameArena() override;
    void BeginCommitProfile() override;
    void FinishCommitProfile(double total_commit_ms) override;
    void RecordCommitLayoutProfile(const LayoutResult& result) override;
    void RecordCommitPaintProfile(double walk_tree_ms) override;
    void UpdateCommitAutoScrollSelection() override;
    void ResolveCommitCaretVisibility() override;
    void BuildCommitSemantics(const std::vector<std::uint64_t>& paint_order) override;
    void BuildCommitDebugTree() override;
    void DestroyRegisteredFont(RegisteredFont& font);
    hb_font_t* AcquireSizedFont(const RegisteredFont& font, float font_size) const;
    void DestroySizedFonts(RegisteredFont& font);
    ShapingBufferLease AcquireShapingBuffer() const;
    void ReleaseShapingBuffer(std::size_t index, hb_buffer_t* buffer) const;
    void DestroyShapingBuffers();
    const RegisteredFont* LookupFont(std::uint32_t font_id) const;
    FontMetrics GetFontMetrics(const RegisteredFont& font, float font_size) const;
    float GetFontLineHeight(std::uint32_t font_id, float font_size) const;
    FontMetrics ResolvePrimaryLineBoxMetrics(const UINode& node) const;
    FontMetrics ResolveLineMetrics(
        const UINode& node,
        const FontMetrics& primary_line_box_metrics,
        float content_ascent,
        float content_descent) const;
    bool FontHasGlyph(const RegisteredFont& font, std::uint32_t codepoint) const;
    void InvalidateTextLayoutCache(UINode& node, bool preserve_logical_line_shapes = false);
    bool ShapeTextWithFont(
        std::string_view text,
        const RegisteredFont& font,
        std::uint32_t font_id,
        float font_size,
        ShapedTextRun& out) const;
    bool TryShapeFixedPitchTabbedText(
        std::string_view text,
        const RegisteredFont& font,
        std::uint32_t font_id,
        float font_size,
        ShapedTextRun& out) const;
    bool ShapeMissingTextWithFont(
        std::string_view text,
        const RegisteredFont& font,
        std::uint32_t font_id,
        float font_size,
        ShapedTextRun& out) const;
    std::vector<std::uint32_t> ResolveFontChain(std::uint32_t font_id) const;
    static std::uint32_t ClassifyMissingFontCoverage(std::uint32_t codepoint);
    void ReportMissingFontCoverage(
        const std::vector<std::uint32_t>& font_chain,
        std::uint32_t primary_font_id,
        std::uint32_t coverage_kind,
        std::string_view sample_text) const;
    void InvalidateAllTextLayoutForFontChange();
    YGSize MeasureTextNode(const UINode& node, float width, YGMeasureMode width_mode) const;
    ParagraphLayout LayoutParagraph(const UINode& node, std::optional<float> max_width) const;
    ParagraphLayout LayoutParagraphImpl(const UINode& node, std::optional<float> max_width) const;
    VisualGeometryWindow ResolveVisualGeometryWindow(
        const UINode& node,
        const ParagraphLayout& paragraph,
        const Rect& scene_visible_bounds,
        float node_abs_x,
        float node_abs_y) const;
    std::vector<std::int32_t> ComputeBreakCandidates(std::string_view utf8) const;
    std::vector<std::int32_t> ComputeBreakCandidatesImpl(std::string_view utf8) const;
    std::vector<std::int32_t> ComputeLineBreaks(
        std::string_view utf8,
        float max_width,
        std::uint32_t font_id,
        float font_size,
        bool obscured) const;
    std::vector<std::int32_t> ComputeLineBreaksImpl(
        std::string_view utf8,
        float max_width,
        std::uint32_t font_id,
        float font_size,
        bool obscured) const;
    std::uint32_t GetNonWrapVisibleLineStart(const UINode& node, std::size_t line_index) const;
    std::uint32_t GetNonWrapFragmentAbsoluteStart(
        const UINode& node,
        std::size_t line_index,
        const NonWrappingTextFragment& fragment) const;
    std::uint32_t GetNonWrapFragmentAbsoluteEnd(
        const UINode& node,
        std::size_t line_index,
        const NonWrappingTextFragment& fragment) const;
    std::vector<NonWrappingTextFragment> BuildNonWrappingFragmentsForLine(
        std::size_t line_index,
        std::string_view line_text,
        const ShapedTextRun& shaped) const;
    std::vector<NonWrappingTextFragment> BuildNonWrappingFragmentsForLineImpl(
        std::size_t line_index,
        std::string_view line_text,
        const ShapedTextRun& shaped) const;
    NonWrappingFragmentWindow ResolveNonWrappingFragmentWindow(
        const UINode& node,
        std::size_t line_index,
        float visible_left,
        float visible_right) const;
    NonWrappingFragmentWindow ResolveNonWrappingFragmentWindowImpl(
        const UINode& node,
        std::size_t line_index,
        float visible_left,
        float visible_right) const;
    bool TryShapeFragmentGeometrySliceForIndex(
        const UINode& node,
        std::size_t line_index,
        std::uint32_t byte_index,
        FragmentGeometrySlice& out) const;
    bool TryShapeFragmentGeometrySliceForX(
        const UINode& node,
        std::size_t line_index,
        float aligned_x,
        FragmentGeometrySlice& out) const;
    bool BuildCachedLogicalLineShape(
        const UINode& node,
        std::string_view source_text,
        std::uint32_t raw_start,
        std::uint32_t raw_end,
        CachedLogicalLineShape& out_shape) const;
    bool BuildCachedLogicalLineShapeImpl(
        const UINode& node,
        std::string_view source_text,
        std::uint32_t raw_start,
        std::uint32_t raw_end,
        CachedLogicalLineShape& out_shape) const;
    void EnsureCachedLogicalLineBreakCandidates(
        std::string_view source_text,
        CachedLogicalLineShape& shape) const;
    bool TryBuildWrappedVisualLineShapeFromLogicalLineShape(
        const CachedLogicalLineShape& line_shape,
        std::uint32_t slice_start,
        std::uint32_t slice_end,
        CachedVisualLineShape& out_shape) const;
    std::vector<std::int32_t> ComputeWrappedSegmentBreaks(
        const UINode& node,
        std::string_view segment,
        const CachedLogicalLineShape* cached_shape,
        float width_limit) const;
    std::vector<std::int32_t> ComputeIncrementalWrappedSegmentBreaks(
        const UINode& node,
        std::string_view segment,
        const CachedLogicalLineShape& cached_shape,
        float width_limit,
        const std::vector<std::int32_t>& previous_breaks,
        std::uint32_t changed_start,
        std::uint32_t old_changed_end,
        std::uint32_t new_changed_end,
        std::int64_t byte_delta,
        std::size_t start_candidate_index) const;
    bool TryApplyIncrementalNonWrapLayoutCache(UINode& node, std::string_view previous_text) const;
    bool TryApplyIncrementalNonWrapLayoutCache(UINode& node, const TextEdit& edit) const;
    bool TryApplyIncrementalNonWrapLayoutCacheImpl(UINode& node, std::string_view previous_text) const;
    bool TryApplyIncrementalNonWrapLayoutCacheImpl(UINode& node, const TextEdit& edit) const;
    bool TryApplyIncrementalWrappedLayoutCache(UINode& node, std::string_view previous_text) const;
    bool TryApplyIncrementalWrappedLayoutCache(UINode& node, const TextEdit& edit) const;
    bool TryMaterializeWrappedVisualLineShape(UINode& node, std::size_t visual_line_index) const;
    const CachedVisualLineShape* EnsureWrappedVisualLineShape(const UINode& node, std::size_t visual_line_index) const;
    bool HandleTextEditingKey(UINode& node, std::string_view key, std::uint32_t modifiers);
    void MarkTextSelectionVisualsDirty(UINode& node);
    void RemoveTextFindHighlightsInSubtree(std::uint64_t subtree_root);
    void ClearUndoHistory(UINode& node);
    void BeginUndoGroup(UINode& node);
    bool UndoTextEdit(std::uint64_t handle, UINode& node);
    bool RedoTextEdit(std::uint64_t handle, UINode& node);
    bool ApplyAbsurdLineClamp(UINode& node) const;
    bool ApplyAbsurdLineClampImpl(UINode& node) const;
    bool WouldApplyAbsurdLineClamp(const UINode& node) const;
    void RebuildTextLineStarts(UINode& node) const;
    bool TryApplyIncrementalTextLineStarts(UINode& node, std::string_view previous_text) const;
    bool TryApplyIncrementalTextLineStarts(UINode& node, const TextEdit& edit) const;
    bool TryApplyIncrementalTextLineStartsImpl(UINode& node, std::string_view previous_text) const;
    bool TryApplyIncrementalTextLineStartsImpl(UINode& node, const TextEdit& edit) const;
    std::size_t LineIndexForTextLineStarts(const UINode& node, std::uint32_t pos) const;
    std::uint32_t GetTextLineStart(const UINode& node, std::size_t line_index) const;
    std::uint32_t GetTextLineEnd(const UINode& node, std::size_t line_index) const;
    void NotifyTextStateChanged(
        std::uint64_t handle,
        UINode& node,
        const std::string* previous_text = nullptr);
    void NotifyTextStateChanged(std::uint64_t handle, UINode& node, const TextEdit& edit);
    bool ApplyTextEdit(
        std::uint64_t handle,
        UINode& node,
        TextEdit edit,
        std::uint32_t selection_start,
        std::uint32_t selection_end);
    std::optional<TextEdit> CreateFullReplacementTextEdit(
        std::string_view old_text,
        std::string_view new_text) const;
    float RawLineXForIndex(
        std::string_view line_text,
        const ShapedTextRun& shaped,
        std::uint32_t local_index,
        bool trailing_edge = false) const;
    float GetTextboxViewportOffsetX(
        const UINode& node,
        const ShapedTextRun& line,
        std::uint32_t line_start,
        std::uint32_t line_end) const;
    float GetLineHeightForIndex(const UINode& node, std::size_t line_index) const;
    float GetLineAscentForIndex(const UINode& node, std::size_t line_index) const;
    float GetLineTopForIndex(const UINode& node, std::size_t line_index) const;
    float GetTextContentHeight(const UINode& node, std::size_t visible_line_count) const;
    std::size_t LineIndexForYOffset(const UINode& node, float local_y, std::size_t line_count) const;
    float GetAlignedLineXOffset(const UINode& node, float line_width) const;
    float GetAlignedTextYOffset(const UINode& node, float content_height) const;
    std::uint32_t ResolveTextFadeMask(const UINode& node, const ParagraphLayout& paragraph) const;
    std::uint32_t GetStringIndexFromPoint(const UINode& node, float local_x, float local_y) const;
    std::pair<float, int> GetLocalPositionFromIndex(
        const UINode& node,
        std::uint32_t byte_index,
        bool trailing_edge = false) const;
    std::pair<std::uint32_t, std::uint32_t> GetWordBoundaries(const UINode& node, std::uint32_t byte_index) const;
    std::pair<std::uint32_t, std::uint32_t> GetParagraphBoundaries(const UINode& node, std::uint32_t byte_index) const;
    std::uint32_t NextCharacterIndex(std::string_view text, std::uint32_t pos, bool forward) const;
    std::uint32_t NextWordIndex(const UINode& node, std::uint32_t pos, bool forward) const;
    std::uint32_t IndexForLineBegin(const UINode& node, std::uint32_t pos) const;
    std::uint32_t IndexForLineEnd(const UINode& node, std::uint32_t pos) const;
    std::uint32_t IndexForVerticalMove(const UINode& node, std::uint32_t pos, bool down) const;
    std::uint32_t IndexForPageMove(const UINode& node, std::uint32_t pos, bool down) const;
    void EnsureTextCaretVisible(std::uint64_t handle, UINode& node);
    void UpdateAncestorScrollMetrics(std::uint64_t handle);
    void HandleCopy(const UINode& node) const;
    void EmitClipboardWrite(std::string_view plain_text, const std::string* rich_json = nullptr) const;
    bool BuildSelectionClipboardRichPayload(
        const UINode& node,
        std::uint32_t start,
        std::uint32_t end,
        std::string& out_plain_text,
        std::string& out_rich_json) const;
    bool HandleCut(UINode& node);
    bool HandlePaste(UINode& node) const;
    std::vector<Rect> BuildSelectionRects(
        const UINode& node,
        std::uint32_t start,
        std::uint32_t end,
        // Logical ranges remain UTF-8 byte ranges. A window produces a
        // disposable retained-paint projection; nullopt requests complete
        // logical geometry for ABI, reveal, endpoint, or prepared-text use.
        std::optional<VisualGeometryWindow> window,
        bool clip_to_window = true) const;
    std::vector<ColoredRect> BuildStyleInlineRects(
        const UINode& node,
        std::optional<VisualGeometryWindow> window) const;
    void AppendResolvedGlyphPlacements(
        const UINode& node,
        const ShapedTextRun& shaped,
        float x,
        float baseline_y,
        std::vector<GlyphPlacement>& out) const;
    void EmitTextGlyphRun(
        CommandBuilder& builder,
        std::uint64_t handle,
        const UINode& node,
        const std::vector<GlyphPlacement>& glyphs) const;
    TextRenderPlan BuildPreparedTextRenderPlan(
        const UINode& node,
        const ParagraphLayout& paragraph,
        float width) const;
    float MeasureSingleLineWidth(std::string_view text, std::uint32_t font_id, float font_size, bool obscured) const;
    bool ShapeObscuredText(std::string_view text, std::uint32_t font_id, float font_size, ShapedTextRun& out) const;
    bool ShapeText(
        std::string_view text,
        std::uint32_t font_id,
        float font_size,
        ShapedTextRun& out,
        bool obscured = false,
        bool allow_fixed_pitch_tabs = true) const;
    bool ShapeDynamicTextFastPath(const UINode& node, std::string_view text, ShapedTextRun& out) const;
    bool ShapeTextStyledRange(const UINode& node, std::uint32_t start, std::uint32_t end, ShapedTextRun& out) const;
    std::vector<TextClusterStop> BuildTextClusterStops(
        const std::vector<GlyphPlacement>& glyphs,
        float shaped_width,
        std::size_t text_length) const;
    std::vector<TextClusterStop> BuildTextClusterStopsImpl(
        const std::vector<GlyphPlacement>& glyphs,
        float shaped_width,
        std::size_t text_length) const;
    bool TryResolveMonospaceFastPathMetrics(
        std::string_view text,
        const ShapedTextRun& shaped,
        float& out_cell_width) const;
    float ClusterXForIndex(
        const std::vector<TextClusterStop>& stops,
        float shaped_width,
        std::uint32_t local_index,
        std::size_t text_length) const;
    bool TryBuildFragmentGeometrySliceFromLogicalLineShape(
        const UINode& node,
        std::size_t line_index,
        std::uint32_t slice_start,
        std::uint32_t slice_end,
        FragmentGeometrySlice& out) const;
    bool TryBuildFragmentGeometrySliceFromLogicalLineShapeImpl(
        const UINode& node,
        std::size_t line_index,
        std::uint32_t slice_start,
        std::uint32_t slice_end,
        FragmentGeometrySlice& out) const;
    bool TryGetCachedNonWrapGeometrySliceForIndex(
        const UINode& node,
        std::size_t line_index,
        std::uint32_t byte_index,
        FragmentGeometrySlice& out) const;
    bool TryGetCachedNonWrapGeometrySliceForX(
        const UINode& node,
        std::size_t line_index,
        float aligned_x,
        FragmentGeometrySlice& out) const;
    void StoreCachedNonWrapGeometrySlice(UINode& node, std::size_t line_index, const FragmentGeometrySlice& slice) const;
    void EnsureHandleVisible(std::uint64_t handle);
    void EnsureRectVisibleWithinScrollAncestor(
        std::uint64_t scroll_handle,
        float target_left,
        float target_top,
        float target_right,
        float target_bottom,
        float* adjusted_left,
        float* adjusted_top,
        float* adjusted_right,
        float* adjusted_bottom);
    bool IsAttachedToRoot(std::uint64_t handle) const;
    std::uint64_t FindDeepestNodeContainingPoint(std::uint64_t handle, float logical_x, float logical_y) const;
    std::uint64_t FindBestNodeContainingPoint(float logical_x, float logical_y) const;
    std::uint64_t FindDeepestScrollViewContainingPoint(std::uint64_t handle, float logical_x, float logical_y) const;
    std::uint64_t ResolveScrollTarget(std::uint64_t start_handle, float logical_x, float logical_y) const;
    std::uint64_t FindScrollableAncestor(std::uint64_t start_handle) const;
    std::uint64_t FindScrollableAncestorContainingPoint(std::uint64_t start_handle, float logical_x, float logical_y) const;
    std::uint64_t FindScrollProxyTarget(std::uint64_t start_handle, float logical_x, float logical_y) const;
    std::uint64_t FindWheelScrollableTarget(std::uint64_t start_handle, float logical_x, float logical_y) const;
    std::pair<float, float> ClampPointToScrollViewport(std::uint64_t start_handle, float logical_x, float logical_y) const;
    void ClearAutoScrollState();
    EdgeInsets ComputeBorderInsets(const UINode& node) const;
    EdgeInsets ComputePaddingInsets(const UINode& node) const;
    EdgeInsets ComputeContentInsets(const UINode& node) const;
    Rect ComputeBorderBounds(const UINode& node, float origin_x, float origin_y) const;
    Rect ComputeContentBounds(const UINode& node, float origin_x, float origin_y) const override;
    Rect ComputeScrollViewportBounds(const UINode& node, float origin_x, float origin_y) const;
    Rect ComputeTextContentBounds(const UINode& node) const;
    float GetScrollViewportWidth(const UINode& node) const;
    float GetScrollViewportHeight(const UINode& node) const;
    Rect ComputeClipBounds(const UINode& node) const;
    Rect ComputeClipBounds(const UINode& node, float origin_x, float origin_y) const;
    std::uint32_t ComputeClipMode(const UINode& node) const;
    Rect ComputeVisibleBounds(const UINode& node) const;
    bool PointInVisibleBounds(const UINode& node, float logical_x, float logical_y) const;
    void UpdateAutoScrollState(std::uint64_t start_handle, float logical_x, float logical_y);
    std::uint64_t FindSelectionAreaAncestor(std::uint64_t start_handle) const;
    void CollectSelectionAreaNodes(std::uint64_t handle, std::vector<std::uint64_t>& out) const;
    void EnsureSelectionAreaNodes(std::uint64_t area_handle);
    int FindSelectionAreaNodeIndex(std::uint64_t handle) const;
    void MarkSelectionAreaNodesDirty();
    void ClearCrossSelection(bool notify_callback);
    bool UpdateCrossSelectionEndpoint(std::uint64_t handle, float logical_x, float logical_y);
    void NormalizeCrossSelectionEndpoints();
    bool GetCrossSelectionHighlight(std::uint64_t handle, std::uint32_t& out_start, std::uint32_t& out_end) const;
    std::string BuildCrossSelectionText() const;
    bool BuildCrossSelectionRichPayload(std::string& out_plain_text, std::string& out_rich_json) const;
    void NotifyCrossSelectionChanged() const;
    bool HandleCrossSelectionNavigation(std::uint64_t area_handle, UINode& focused_node, std::string_view key, std::uint32_t modifiers);
    bool IsApplePlatformFamily() const;
    float DefaultTouchScrollFriction() const;
    float ResolveScrollFriction(const UINode& node) const;
    bool HasPrimaryShortcutModifier(std::uint32_t modifiers) const;
    bool HasWordNavigationModifier(std::uint32_t modifiers) const;
    bool HasLineBoundaryModifier(std::uint32_t modifiers) const;
    bool HasDocumentBoundaryModifier(std::uint32_t modifiers) const;
    bool IsPrimaryShortcut(std::string_view key, std::uint32_t modifiers, char expected) const;
    void AppendTextSnapshotHandles(std::uint64_t handle, std::vector<std::uint64_t>& out) const;
    const UINode* ResolveTextSnapshotNode(std::uint64_t handle) const;
    const UINode* ResolveTextGeometryNode(std::uint64_t handle) const;
    bool IsUndoShortcut(std::string_view key, std::uint32_t modifiers) const;
    bool IsRedoShortcut(std::string_view key, std::uint32_t modifiers) const;
    void ClearSelectionHighlight(std::uint64_t handle, bool notify_callback);
    void InvalidateFocusOrder();
    std::uint64_t GetActiveSemanticScopeRoot();
    bool SubtreeContains(std::uint64_t subtree_root, std::uint64_t target_handle) const;
    void CapturePendingFocusId(std::uint64_t subtree_root);
    void RestorePendingFocusIfPossible();
    void ClearHover(std::uint64_t handle);
    void SetFocus(std::uint64_t new_handle, bool ensure_visible = false, bool emit_selection_callback = true);
    std::uint64_t GetNextFocusable(std::uint64_t current, bool forward);
    UINode* ResolveMutable(std::uint64_t handle);
    FocusCoordinator& Focus() { return *focus_coordinator_; }
    const FocusCoordinator& Focus() const { return *focus_coordinator_; }
    InputRouter& Input() { return *input_router_; }
    const InputRouter& Input() const { return *input_router_; }
    SelectionCoordinator& Selection() { return selection_coordinator_; }
    const SelectionCoordinator& Selection() const { return selection_coordinator_; }
    GridSideTableEntry& EnsureGridSideTableEntry(std::uint64_t handle);
    GridSideTableEntry* FindMutableGridSideTableEntry(std::uint64_t handle);
    const GridSideTableEntry* FindGridSideTableEntry(std::uint64_t handle) const;
    void TrimGridSharedSizeGroups(std::uint64_t handle, bool columns, std::uint32_t count);
    void PruneGridSideTableEntry(std::uint64_t handle);
    void ResetCurrentTextCommitProfile() const;
    void FinishCurrentTextCommitProfile(double total_commit_ms) const;
    void RebuildLiveFallbackFontBuffer() const;

    // Ownership order is intentional. Stable node storage and shared state are
    // constructed before coordinators; coordinators hold only narrow views or
    // references into these owners and are destroyed before their dependencies.
    UiPlatformHost& platform_host_;
    NodeStore node_store_{};
    std::vector<std::uint32_t> command_buffer_{};
    std::vector<std::uint32_t> pending_prepare_commands_{};
    std::vector<std::uint32_t> semantic_buffer_{};
    std::vector<std::uint32_t> debug_tree_buffer_{};
    mutable std::vector<std::uint32_t> live_fallback_font_buffer_{};
    std::vector<SemanticScopeEntry> semantic_scope_stack_{};
    std::vector<std::uint8_t> string_arena_{};
    std::unordered_map<std::uint32_t, RegisteredFont> font_registry_{};
    mutable FixedPitchMetricsCache fixed_pitch_metrics_cache_{};
    std::uint64_t next_font_generation_ = 1U;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> font_fallbacks_{};
    std::unordered_map<std::string, std::uint64_t> node_id_map_{};
    std::unordered_map<std::uint64_t, GridSideTableEntry> grid_side_tables_{};
    std::unordered_set<std::uint64_t> pending_text_scroll_metric_handles_{};
    std::vector<std::uint8_t> icu_data_bytes_{};
    bool icu_data_registered_ = false;
    UiEventSink event_sink_{};
    // Heap ownership avoids fragile value-construction ordering while keeping
    // every subsystem uniquely owned by this retained-runtime facade.
    std::unique_ptr<FocusCoordinator> focus_coordinator_{};
    std::unique_ptr<ScrollCoordinator> scroll_coordinator_{};
    std::unique_ptr<LayoutCoordinator> layout_coordinator_{};
    std::unique_ptr<InputRouter> input_router_{};
    std::unique_ptr<TreePainter> tree_painter_{};
    std::unique_ptr<SceneGeometryResolver> scene_geometry_resolver_{};
    std::unique_ptr<FrameCommitCoordinator> frame_commit_coordinator_{};
    SelectionCoordinator selection_coordinator_{};
    TextEditingCoordinator text_editing_coordinator_{};
    std::uint64_t pending_caret_visibility_handle_ = UI_INVALID_HANDLE;
    std::uint64_t text_find_handle_ = UI_INVALID_HANDLE;
    std::uint32_t text_find_start_ = 0U;
    std::uint32_t text_find_end_ = 0U;
    std::vector<TextFindHighlight> text_find_highlights_{};
    float window_width_ = 800.0f;
    float window_height_ = 600.0f;
    bool layout_dirty_ = true;
    mutable std::unordered_set<std::string> reported_missing_font_coverage_keys_{};
    std::uint32_t next_semantic_scope_token_ = 1U;
    std::size_t arena_bytes_used_ = 0;
    mutable TextCommitProfile current_text_commit_profile_{};
    mutable TextCommitProfile last_text_commit_profile_{};
    mutable TextCommitProfile pending_text_edit_profile_{};
    mutable bool text_commit_profile_active_ = false;
    mutable DynamicTextPrepareProfile current_dynamic_text_prepare_profile_{};
    mutable DynamicTextPrepareProfile last_dynamic_text_prepare_profile_{};
    mutable bool dynamic_text_prepare_profile_active_ = false;
    mutable ShapingResourceProfile shaping_resource_profile_{};
    mutable TextGeometryProfile text_geometry_profile_{};
    mutable bool text_geometry_profile_active_ = false;
    mutable std::vector<ShapingBufferEntry> shaping_buffers_{};
    mutable std::uint64_t shaping_font_access_sequence_ = 0U;
    void ResetCurrentDynamicTextPrepareProfile() const;
    void FinishCurrentDynamicTextPrepareProfile(double total_prepare_ms) const;
    void RecordDynamicTextFastPathFallback() const;
    void ClearDynamicTextFastPathCache(UINode& node) const;
    std::uint32_t text_find_color_ = EF_RGBA(0xFFU, 0xEBU, 0x3BU, 0x80U);

#ifdef __EMSCRIPTEN__
    static constexpr bool kRequiresRegisteredIcuData = true;
#else
    static constexpr bool kRequiresRegisteredIcuData = false;
#endif
};

UiRuntime& GetRuntime();

} // namespace effindom::v2::ui
