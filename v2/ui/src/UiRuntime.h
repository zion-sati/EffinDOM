#pragma once

#include "UiTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <hb.h>

namespace effindom::v2::ui {

class CommandBuilder;

inline constexpr std::size_t kTextboxHardClampMaxCodepoints = 10000U;

enum class PlatformFamily : std::uint32_t {
    Unknown = 0U,
    Apple = 1U,
    Windows = 2U,
    Linux = 3U,
};

class UiRuntime {
public:
    struct TextCommitProfile {
        double total_commit_ms = 0.0;
        double yoga_layout_ms = 0.0;
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
        std::uint32_t measure_single_line_width_calls = 0U;
        std::uint64_t measure_single_line_width_bytes = 0U;
    };

    struct TextFindHighlight {
        std::uint64_t handle = UI_INVALID_HANDLE;
        std::uint32_t start = 0U;
        std::uint32_t end = 0U;
        std::uint32_t color = 0U;
    };

    UiRuntime();

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
    bool SetFlexDirection(std::uint64_t handle, std::uint32_t dir_enum);
    bool SetFlexGrow(std::uint64_t handle, float grow);
    bool SetFlexBasis(std::uint64_t handle, float basis);
    bool SetJustifyContent(std::uint64_t handle, std::uint32_t justify_enum);
    bool SetAlignItems(std::uint64_t handle, std::uint32_t align_enum);
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
    bool SetImage(std::uint64_t handle, std::uint32_t texture_id, std::uint32_t object_fit_enum);
    bool SetImageNine(
        std::uint64_t handle,
        std::uint32_t texture_id,
        float inset_left,
        float inset_top,
        float inset_right,
        float inset_bottom);
    bool SetSvg(std::uint64_t handle, std::uint32_t svg_id, std::uint32_t tint_color);

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
    bool SetScrollProxyTarget(std::uint64_t handle, std::uint64_t scroll_handle);
    bool SetScrollEnabled(std::uint64_t handle, bool enabled_x, bool enabled_y);
    bool SetShowScrollbars(std::uint64_t handle, bool show_scrollbars);
    bool SetScrollFriction(std::uint64_t handle, float friction);
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
    bool SetEditable(std::uint64_t handle, bool editable);
    bool SetCaretColor(std::uint64_t handle, std::uint32_t color);
    bool RegisterFont(std::uint32_t font_id, const std::uint8_t* bytes, std::uint32_t length);
    bool RegisterFontFallback(std::uint32_t font_id, std::uint32_t fallback_font_id);
    bool UnregisterFont(std::uint32_t font_id);
    bool UnregisterFontFallback(std::uint32_t font_id, std::uint32_t fallback_font_id);
    bool RegisterIcuData(const std::uint8_t* bytes, std::uint32_t length);
    void FontLoaded(std::uint32_t font_id);
    void SetKeyModifiers(std::uint32_t modifiers);
    void SetInteractionTime(std::uint64_t interaction_time_ms);
    void HandlePointerEvent(std::uint32_t event_enum, std::uint64_t handle, float logical_x, float logical_y);
    void HandleWheelEvent(float delta_x, float delta_y);
    void BeginTouchScroll(std::uint64_t handle, float logical_x, float logical_y);
    void UpdateTouchScroll(float delta_x, float delta_y);
    void EndTouchScroll();
    void ClearMomentumScroll();
    bool ActiveTouchScrollAllowsPullToRefresh() const;
    void SetCoarsePointerMode(bool coarse_pointer_mode);
    void SetPlatformFamily(std::uint32_t platform_family);
    void HandleKeyEvent(std::uint32_t type_enum, const std::uint8_t* key_utf8, std::uint32_t len, std::uint32_t modifiers);
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
    std::vector<std::uint64_t> GetTextSnapshotHandles() const;
    std::pair<float, float> GetTextScenePositionFromIndex(std::uint64_t handle, std::uint32_t byte_index) const;
    std::optional<std::string_view> GetTextSnapshotDocument(std::uint64_t handle) const;
    std::optional<Rect> GetTextVisibleBounds(std::uint64_t handle) const;
    std::vector<Rect> GetTextRangeSceneRects(std::uint64_t handle, std::uint32_t start, std::uint32_t end) const;
    bool RevealTextRange(std::uint64_t handle, std::uint32_t start, std::uint32_t end);
    bool SetTextFindMatch(std::uint64_t handle, std::uint32_t start, std::uint32_t end);
    void ClearTextFindMatch();
    bool PushTextFindHighlight(std::uint64_t handle, std::uint32_t start, std::uint32_t end, std::uint32_t color);
    void ClearTextFindHighlights();

    bool SetNodeColor(std::uint64_t handle, std::uint32_t color);
    const UINode* Resolve(std::uint64_t handle) const;
    bool IsSharedSizeScope(std::uint64_t handle) const;
    const std::vector<std::string>& GridColumnSharedSizeGroups(std::uint64_t handle) const;
    const std::vector<std::string>& GridRowSharedSizeGroups(std::uint64_t handle) const;

    void CommitFrame();
    const std::vector<std::uint32_t>& command_buffer() const;
    const std::vector<std::uint32_t>& semantic_buffer() const;
    const std::uint64_t& root_handle() const;
    bool HasPendingVisualWork() const;
    bool NeedsAnimationFrame() const;
    bool HasPointerAutoScroll() const;
    std::uint64_t SelectionAutoScroll(float logical_x, float logical_y, float edge_threshold);
    float window_width() const;
    float window_height() const;
    const TextCommitProfile& last_text_commit_profile() const;
    void ClearTextCommitProfile();

private:
    struct EdgeInsets {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
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
        std::optional<std::uint32_t> bullet_glyph_id{};
        std::optional<std::uint32_t> tofu_glyph_id{};
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

    struct ParagraphLayout {
        std::vector<std::int32_t> break_offsets{0};
        std::vector<float> line_widths{};
        std::vector<float> line_heights{};
        std::vector<float> line_ascents{};
        std::vector<float> line_y_offsets{};
        float max_line_width = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float line_height = 0.0f;
        std::size_t visible_line_count = 0;
        std::size_t total_line_count = 0;
        bool clipped = false;
    };

    struct NonWrappingFragmentWindow {
        std::size_t start = 0U;
        std::size_t end = 0U;
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

    static YGSize MeasureTextCallback(
        YGNodeConstRef yg_node,
        float width,
        YGMeasureMode width_mode,
        float height,
        YGMeasureMode height_mode);

    void WalkTree(
        std::uint64_t handle,
        float parent_abs_x,
        float parent_abs_y,
        float parent_scene_x,
        float parent_scene_y,
        bool inherited_scroll_dirty,
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order,
        std::vector<SceneInstruction>& scene,
        std::vector<std::uint64_t>& deferred_portal_roots);
    void ClearCulledSubtree(
        std::uint64_t handle,
        float parent_abs_x,
        float parent_abs_y,
        float parent_scene_x,
        float parent_scene_y,
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order);
    void LayoutGrid(
        std::uint64_t handle,
        UINode& node,
        float abs_x,
        float abs_y,
        float scene_x,
        float scene_y,
        bool inherited_scroll_dirty,
        CommandBuilder& builder,
        std::vector<std::uint64_t>& paint_order,
        std::vector<SceneInstruction>& scene,
        std::vector<std::uint64_t>& deferred_portal_roots);
    void DestroyRegisteredFont(RegisteredFont& font);
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
    bool TryApplyIncrementalNonWrapLayoutCacheImpl(UINode& node, std::string_view previous_text) const;
    bool TryApplyIncrementalWrappedLayoutCache(UINode& node, std::string_view previous_text) const;
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
    void RebuildTextLineStarts(UINode& node) const;
    bool TryApplyIncrementalTextLineStarts(UINode& node, std::string_view previous_text) const;
    bool TryApplyIncrementalTextLineStartsImpl(UINode& node, std::string_view previous_text) const;
    std::size_t LineIndexForTextLineStarts(const UINode& node, std::uint32_t pos) const;
    std::uint32_t GetTextLineStart(const UINode& node, std::size_t line_index) const;
    std::uint32_t GetTextLineEnd(const UINode& node, std::size_t line_index) const;
    void NotifyTextStateChanged(std::uint64_t handle, UINode& node, const std::string* previous_text = nullptr);
    void NotifyTextStateChangedImpl(std::uint64_t handle, UINode& node, const std::string* previous_text);
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
    bool DeleteSelection(UINode& node);
    std::vector<Rect> BuildSelectionRects(const UINode& node, std::uint32_t start, std::uint32_t end) const;
    std::vector<ColoredRect> BuildStyleInlineRects(const UINode& node) const;
    float MeasureSingleLineWidth(std::string_view text, std::uint32_t font_id, float font_size, bool obscured) const;
    bool ShapeObscuredText(std::string_view text, std::uint32_t font_id, float font_size, ShapedTextRun& out) const;
    bool ShapeText(std::string_view text, std::uint32_t font_id, float font_size, ShapedTextRun& out, bool obscured = false) const;
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
    bool ApplyScrollOffset(std::uint64_t handle, UINode& node, float offset_x, float offset_y, bool notify);
    void NotifyScrollChanged(std::uint64_t handle, UINode& node);
    void UpdateScrollMetrics(std::uint64_t handle, UINode& node);
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
    std::uint64_t RetargetScrollHandleForDelta(std::uint64_t start_handle, float delta_x, float delta_y) const;
    std::uint64_t FindScrollableAncestor(std::uint64_t start_handle) const;
    std::uint64_t FindScrollableAncestorContainingPoint(std::uint64_t start_handle, float logical_x, float logical_y) const;
    std::uint64_t FindScrollProxyTarget(std::uint64_t start_handle, float logical_x, float logical_y) const;
    bool CanScrollOnAxis(const UINode& node, bool horizontal) const;
    bool CanConsumeScrollDelta(const UINode& node, bool horizontal, float delta) const;
    std::uint64_t FindWheelScrollableTarget(std::uint64_t start_handle, float logical_x, float logical_y) const;
    std::pair<float, float> ClampPointToScrollViewport(std::uint64_t start_handle, float logical_x, float logical_y) const;
    void ClearAutoScrollState();
    std::pair<float, float> ComputeAutoScrollFactors(
        const UINode& scroll_node,
        float logical_x,
        float logical_y,
        float edge_threshold) const;
    bool UpdateAutoScrollStateForView(
        std::uint64_t scroll_handle,
        float logical_x,
        float logical_y,
        float edge_threshold);
    EdgeInsets ComputeBorderInsets(const UINode& node) const;
    EdgeInsets ComputePaddingInsets(const UINode& node) const;
    EdgeInsets ComputeContentInsets(const UINode& node) const;
    Rect ComputeBorderBounds(const UINode& node, float origin_x, float origin_y) const;
    Rect ComputeContentBounds(const UINode& node, float origin_x, float origin_y) const;
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
    bool IsUndoShortcut(std::string_view key, std::uint32_t modifiers) const;
    bool IsRedoShortcut(std::string_view key, std::uint32_t modifiers) const;
    void ClearSelectionHighlight(std::uint64_t handle, bool notify_callback);
    void InvalidateFocusOrder();
    void RebuildFocusOrder();
    void AppendFocusableHandles(std::uint64_t handle, std::vector<std::uint64_t>& out) const;
    std::uint64_t GetActiveSemanticScopeRoot();
    bool SubtreeContains(std::uint64_t subtree_root, std::uint64_t target_handle) const;
    void CapturePendingFocusId(std::uint64_t subtree_root);
    void RestorePendingFocusIfPossible();
    void ClearHover(std::uint64_t handle);
    void SetFocus(std::uint64_t new_handle, bool ensure_visible = false, bool emit_selection_callback = true);
    std::uint64_t GetNextFocusable(std::uint64_t current, bool forward);
    UINode* ResolveMutable(std::uint64_t handle);
    GridSideTableEntry& EnsureGridSideTableEntry(std::uint64_t handle);
    GridSideTableEntry* FindMutableGridSideTableEntry(std::uint64_t handle);
    const GridSideTableEntry* FindGridSideTableEntry(std::uint64_t handle) const;
    void TrimGridSharedSizeGroups(std::uint64_t handle, bool columns, std::uint32_t count);
    void PruneGridSideTableEntry(std::uint64_t handle);
    void ResetCurrentTextCommitProfile() const;
    void FinishCurrentTextCommitProfile(double total_commit_ms) const;

    std::array<UINode, kMaxNodes> node_pool_{};
    std::vector<std::uint32_t> command_buffer_{};
    std::vector<std::uint32_t> semantic_buffer_{};
    std::vector<SemanticScopeEntry> semantic_scope_stack_{};
    std::vector<std::uint8_t> string_arena_{};
    std::vector<std::uint64_t> pending_creations_{};
    std::vector<std::uint64_t> pending_deletions_{};
    std::unordered_map<std::uint32_t, RegisteredFont> font_registry_{};
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> font_fallbacks_{};
    std::unordered_map<std::string, std::uint64_t> node_id_map_{};
    std::unordered_map<std::uint64_t, GridSideTableEntry> grid_side_tables_{};
    std::unordered_set<std::uint64_t> pending_text_scroll_metric_handles_{};
    std::string pending_focus_node_id_{};
    std::vector<std::uint8_t> icu_data_bytes_{};
    bool icu_data_registered_ = false;
    std::uint64_t root_handle_ = UI_INVALID_HANDLE;
    std::uint64_t last_hovered_handle_ = UI_INVALID_HANDLE;
    std::uint64_t pending_caret_visibility_handle_ = UI_INVALID_HANDLE;
    std::uint64_t focused_handle_ = UI_INVALID_HANDLE;
    std::uint64_t active_selection_handle_ = UI_INVALID_HANDLE;
    std::uint64_t text_find_handle_ = UI_INVALID_HANDLE;
    std::uint32_t text_find_start_ = 0U;
    std::uint32_t text_find_end_ = 0U;
    std::vector<TextFindHighlight> text_find_highlights_{};
    bool active_selection_dragged_ = false;
    std::uint64_t active_scroll_handle_ = UI_INVALID_HANDLE;
    std::uint64_t active_touch_scroll_handle_x_ = UI_INVALID_HANDLE;
    std::uint64_t active_touch_scroll_handle_y_ = UI_INVALID_HANDLE;
    bool active_scroll_dragged_ = false;
    std::uint64_t momentum_scroll_handle_x_ = UI_INVALID_HANDLE;
    std::uint64_t momentum_scroll_handle_y_ = UI_INVALID_HANDLE;
    bool coarse_pointer_mode_ = false;
    PlatformFamily platform_family_ = PlatformFamily::Unknown;
    bool primary_pointer_down_ = false;
    bool auto_scroll_active_ = false;
    std::uint64_t auto_scroll_view_handle_ = UI_INVALID_HANDLE;
    float auto_scroll_factor_x_ = 0.0f;
    float auto_scroll_factor_y_ = 0.0f;
    float last_pointer_logical_x_ = 0.0f;
    float last_pointer_logical_y_ = 0.0f;
    std::uint64_t interaction_time_ms_ = 0;
    std::uint64_t last_click_handle_ = UI_INVALID_HANDLE;
    float last_click_x_ = 0.0f;
    float last_click_y_ = 0.0f;
    float selection_press_logical_x_ = 0.0f;
    float selection_press_logical_y_ = 0.0f;
    std::uint32_t click_count_ = 0;
    std::uint64_t last_click_time_ms_ = 0;
    std::uint64_t double_click_threshold_ms_ = 500;
    std::uint32_t current_modifiers_ = 0;
    std::uint64_t selection_anchor_handle_ = UI_INVALID_HANDLE;
    std::uint32_t selection_anchor_index_ = 0;
    bool selection_horizontal_extend_active_ = false;
    bool cross_selection_active_ = false;
    bool cross_selection_dragged_ = false;
    bool cross_selection_horizontal_extend_active_ = false;
    std::uint64_t selection_area_handle_ = UI_INVALID_HANDLE;
    std::uint64_t start_node_handle_ = UI_INVALID_HANDLE;
    std::uint32_t start_index_ = 0U;
    std::uint64_t end_node_handle_ = UI_INVALID_HANDLE;
    std::uint32_t end_index_ = 0U;
    std::vector<std::uint64_t> selection_area_nodes_{};
    std::vector<Rect> current_selection_hit_rects_{};
    bool selection_area_nodes_dirty_ = false;
    float window_width_ = 800.0f;
    float window_height_ = 600.0f;
    std::vector<std::uint64_t> focus_order_{};
    bool focus_order_dirty_ = true;
    bool layout_dirty_ = true;
    mutable std::unordered_set<std::string> reported_missing_font_coverage_keys_{};
    std::uint32_t next_semantic_scope_token_ = 1U;
    std::size_t arena_bytes_used_ = 0;
    mutable TextCommitProfile current_text_commit_profile_{};
    mutable TextCommitProfile last_text_commit_profile_{};
    mutable bool text_commit_profile_active_ = false;
    std::uint32_t text_find_color_ = EF_RGBA(0xFFU, 0xEBU, 0x3BU, 0x80U);

#ifdef __EMSCRIPTEN__
    static constexpr bool kRequiresRegisteredIcuData = true;
#else
    static constexpr bool kRequiresRegisteredIcuData = false;
#endif
};

UiRuntime& GetRuntime();

} // namespace effindom::v2::ui
