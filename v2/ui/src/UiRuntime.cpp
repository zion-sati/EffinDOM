#include "UiRuntime.h"

#include "CommandBuilder.h"
#include "UiDebugTreeProjector.h"
#include "UiSemanticProjector.h"
#include "UiTreePainter.h"
#include "UiSceneGeometryResolver.h"
#include "effindom_ui.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <numeric>

namespace effindom::v2::ui {

namespace {

using ProfileClock = std::chrono::steady_clock;
double ElapsedMilliseconds(ProfileClock::time_point start, ProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool MatchesShortcutCharacter(std::string_view key, char expected) {
    if (key.size() != 1U) {
        return false;
    }
    return static_cast<char>(std::tolower(static_cast<unsigned char>(key.front()))) == expected;
}

float FocusedTextCaretScrollOverscan(const UINode& node) {
    const float caret_width = std::max(1.0f, std::min(node.font_size * 0.125f, 2.0f));
    const float caret_margin = std::max(4.0f, std::min(node.font_size * 0.5f, 12.0f));
    return caret_width + caret_margin;
}

} // namespace

UiRuntime::UiRuntime()
    : UiRuntime(GetGlobalUiPlatformHost()) {}

UiRuntime::UiRuntime(UiPlatformHost& platform_host)
    : platform_host_(platform_host),
      string_arena_(kFrameArenaCapacity, 0U) {
    command_buffer_.reserve(1024);
    semantic_buffer_.reserve(256);
    focus_coordinator_ = std::make_unique<FocusCoordinator>(node_store_.Reader(), event_sink_);
    scroll_coordinator_ = std::make_unique<ScrollCoordinator>(
        node_store_.Reader(),
        node_store_.Writer(),
        node_store_.Traversal(),
        VisibilityResolver(node_store_.Reader()),
        [this](const UINode& node, float layout_width) {
            const Rect text_bounds = ComputeTextContentBounds(node);
            const ParagraphLayout paragraph = LayoutParagraph(
                node,
                text_bounds.width > 0.0f ? std::optional<float>(text_bounds.width) : std::nullopt);
            float width = std::max(layout_width, paragraph.width);
            if ((node.is_editable || IsEditorTextNode(node)) && paragraph.width > text_bounds.width + 0.001f) {
                width += FocusedTextCaretScrollOverscan(node);
            }
            return width;
        },
        event_sink_,
        [this](const UINode& node) { return ResolveScrollFriction(node); });
    input_router_ = std::make_unique<InputRouter>(
        node_store_.Reader(),
        node_store_.Writer(),
        node_store_.Traversal(),
        Focus(),
        Selection(),
        (*scroll_coordinator_),
        static_cast<SelectionHost&>(*this),
        event_sink_,
        static_cast<InputRouter::Host&>(*this));
    layout_coordinator_ = std::make_unique<LayoutCoordinator>(
        node_store_.Reader(),
        node_store_.Writer(),
        (*scroll_coordinator_));
    tree_painter_ = std::make_unique<TreePainter>(
        node_store_.Reader(),
        node_store_.Writer(),
        VisibilityResolver(node_store_.Reader()),
        layout_dirty_,
        static_cast<const GridLayoutSource&>(*this),
        TextPaintAccess(*this));
    scene_geometry_resolver_ = std::make_unique<SceneGeometryResolver>(
        node_store_.Writer(),
        static_cast<const GridLayoutSource&>(*this));
    frame_commit_coordinator_ = std::make_unique<FrameCommitCoordinator>(
        node_store_,
        command_buffer_,
        pending_prepare_commands_,
        semantic_buffer_,
        debug_tree_buffer_,
        pending_text_scroll_metric_handles_,
        window_width_,
        window_height_,
        layout_dirty_,
        *layout_coordinator_,
        *scroll_coordinator_,
        selection_coordinator_,
        *tree_painter_,
        static_cast<FrameCommitHost&>(*this));
    debug_tree_buffer_.reserve(512);
}

UiRuntime::~UiRuntime() {
    for (auto& [font_id, font] : font_registry_) {
        (void)font_id;
        DestroyRegisteredFont(font);
    }
    font_registry_.clear();
    DestroyShapingBuffers();
}

UiRuntime::ShapingBufferLease::~ShapingBufferLease() {
    Release();
}

UiRuntime::ShapingBufferLease::ShapingBufferLease(ShapingBufferLease&& other) noexcept
    : owner_(other.owner_), index_(other.index_), buffer_(other.buffer_) {
    other.owner_ = nullptr;
    other.buffer_ = nullptr;
}

UiRuntime::ShapingBufferLease& UiRuntime::ShapingBufferLease::operator=(ShapingBufferLease&& other) noexcept {
    if (this != &other) {
        Release();
        owner_ = other.owner_;
        index_ = other.index_;
        buffer_ = other.buffer_;
        other.owner_ = nullptr;
        other.buffer_ = nullptr;
    }
    return *this;
}

void UiRuntime::ShapingBufferLease::Release() {
    if (owner_ != nullptr && buffer_ != nullptr) {
        owner_->ReleaseShapingBuffer(index_, buffer_);
    }
    owner_ = nullptr;
    buffer_ = nullptr;
}

UiRuntime::ShapingBufferLease UiRuntime::AcquireShapingBuffer() const {
    for (std::size_t index = 0U; index < shaping_buffers_.size(); index += 1U) {
        ShapingBufferEntry& entry = shaping_buffers_[index];
        if (!entry.leased && entry.buffer != nullptr) {
            hb_buffer_reset(entry.buffer);
            entry.leased = true;
            shaping_resource_profile_.buffer_cache_hits += 1U;
            if (text_commit_profile_active_) {
                current_text_commit_profile_.shaping_buffer_cache_hits += 1U;
            }
            return ShapingBufferLease(this, index, entry.buffer);
        }
    }

    hb_buffer_t* buffer = hb_buffer_create();
    if (buffer == nullptr) {
        return {};
    }
    shaping_resource_profile_.buffer_creations += 1U;
    if (text_commit_profile_active_) {
        current_text_commit_profile_.shaping_buffer_creations += 1U;
    }
    shaping_buffers_.push_back(ShapingBufferEntry{buffer, true});
    return ShapingBufferLease(this, shaping_buffers_.size() - 1U, buffer);
}

void UiRuntime::ReleaseShapingBuffer(std::size_t index, hb_buffer_t* buffer) const {
    if (index >= shaping_buffers_.size()) {
        return;
    }
    ShapingBufferEntry& entry = shaping_buffers_[index];
    if (entry.buffer != buffer || !entry.leased) {
        return;
    }
    hb_buffer_reset(entry.buffer);
    entry.leased = false;
}

void UiRuntime::DestroyShapingBuffers() {
    for (ShapingBufferEntry& entry : shaping_buffers_) {
        if (entry.buffer != nullptr) {
            hb_buffer_destroy(entry.buffer);
            shaping_resource_profile_.buffer_destructions += 1U;
            entry.buffer = nullptr;
        }
        entry.leased = false;
    }
    shaping_buffers_.clear();
}

UiRuntime::VisualGeometryWindow UiRuntime::ResolveVisualGeometryWindow(
    const UINode& node,
    const ParagraphLayout& paragraph,
    const Rect& scene_visible_bounds,
    float node_abs_x,
    float node_abs_y) const {
    VisualGeometryWindow window{};
    window.local_clip = Rect{
        scene_visible_bounds.x - node_abs_x,
        scene_visible_bounds.y - node_abs_y,
        std::max(scene_visible_bounds.width, 0.0f),
        std::max(scene_visible_bounds.height, 0.0f),
    };
    const std::size_t line_count = paragraph.visible_line_count;
    if (line_count == 0U || window.local_clip.width <= 0.0f || window.local_clip.height <= 0.0f) {
        return window;
    }
    if (paragraph.height <= 0.0f) {
        window.line_end = line_count;
        return window;
    }

    const float content_offset_y = GetAlignedTextYOffset(node, paragraph.height);
    const float relative_top = window.local_clip.y - content_offset_y;
    const float relative_bottom = window.local_clip.y + window.local_clip.height - content_offset_y;
    if (relative_bottom <= 0.0f || relative_top >= paragraph.height) {
        return window;
    }

    window.line_start = LineIndexForYOffset(node, std::max(relative_top, 0.0f), line_count);
    const auto end_it = std::lower_bound(
        node.line_y_offsets.begin(),
        node.line_y_offsets.begin() + static_cast<std::ptrdiff_t>(line_count + 1U),
        std::max(relative_bottom, 0.0f));
    window.line_end = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
        std::distance(node.line_y_offsets.begin(), end_it),
        0,
        static_cast<std::ptrdiff_t>(line_count)));
    if (window.line_end <= window.line_start) {
        window.line_start = 0U;
        window.line_end = 0U;
    }
    return window;
}

void UiRuntime::Reset() {
    node_store_.Reset();
    node_id_map_.clear();
    grid_side_tables_.clear();
    pending_text_scroll_metric_handles_.clear();
    command_buffer_.clear();
    semantic_buffer_.clear();
    debug_tree_buffer_.clear();
    semantic_scope_stack_.clear();
    pending_prepare_commands_.clear();
    Input().Reset();
    pending_caret_visibility_handle_ = UI_INVALID_HANDLE;
    Focus().Reset();
    Selection().Reset();
    text_find_handle_ = UI_INVALID_HANDLE;
    text_find_start_ = 0U;
    text_find_end_ = 0U;
    text_find_highlights_.clear();
    (*scroll_coordinator_).ResetInteraction();
    frame_commit_coordinator_->ResetClock();
    ClearAutoScrollState();
    window_width_ = 800.0f;
    window_height_ = 600.0f;
    layout_dirty_ = true;
    reported_missing_font_coverage_keys_.clear();
    next_semantic_scope_token_ = 1U;
    ClearTextCommitProfile();
    ClearDynamicTextPrepareProfile();
    ResetFrameArena();
}

const UiRuntime::TextCommitProfile& UiRuntime::last_text_commit_profile() const {
    return last_text_commit_profile_;
}

void UiRuntime::ClearTextCommitProfile() {
    current_text_commit_profile_ = TextCommitProfile{};
    last_text_commit_profile_ = TextCommitProfile{};
    pending_text_edit_profile_ = TextCommitProfile{};
    text_commit_profile_active_ = false;
}

void UiRuntime::ResetCurrentTextCommitProfile() const {
    if (!text_commit_profile_active_) {
        current_text_commit_profile_ = pending_text_edit_profile_;
        pending_text_edit_profile_ = TextCommitProfile{};
    }
    text_commit_profile_active_ = true;
}

void UiRuntime::FinishCurrentTextCommitProfile(double total_commit_ms) const {
    current_text_commit_profile_.total_commit_ms = total_commit_ms;
    last_text_commit_profile_ = current_text_commit_profile_;
    text_commit_profile_active_ = false;
}

const UiRuntime::DynamicTextPrepareProfile& UiRuntime::last_dynamic_text_prepare_profile() const {
    return last_dynamic_text_prepare_profile_;
}

void UiRuntime::ClearDynamicTextPrepareProfile() {
    current_dynamic_text_prepare_profile_ = DynamicTextPrepareProfile{};
    last_dynamic_text_prepare_profile_ = DynamicTextPrepareProfile{};
    dynamic_text_prepare_profile_active_ = false;
}

const UiRuntime::ShapingResourceProfile& UiRuntime::shaping_resource_profile() const {
    return shaping_resource_profile_;
}

void UiRuntime::ClearShapingResourceProfile() {
    shaping_resource_profile_ = ShapingResourceProfile{};
}

const UiRuntime::TextGeometryProfile& UiRuntime::text_geometry_profile() const {
    return text_geometry_profile_;
}

void UiRuntime::ClearTextGeometryProfile() {
    text_geometry_profile_ = TextGeometryProfile{};
}

void UiRuntime::ResetCurrentDynamicTextPrepareProfile() const {
    current_dynamic_text_prepare_profile_ = DynamicTextPrepareProfile{};
    dynamic_text_prepare_profile_active_ = true;
}

void UiRuntime::FinishCurrentDynamicTextPrepareProfile(double total_prepare_ms) const {
    current_dynamic_text_prepare_profile_.total_prepare_ms = total_prepare_ms;
    last_dynamic_text_prepare_profile_ = current_dynamic_text_prepare_profile_;
    dynamic_text_prepare_profile_active_ = false;
}

void UiRuntime::RecordDynamicTextFastPathFallback() const {
    if (!dynamic_text_prepare_profile_active_) {
        return;
    }
    current_dynamic_text_prepare_profile_.fast_path_fallbacks += 1U;
}

void UiRuntime::SetPlatformFamily(std::uint32_t platform_family) {
    Input().SetPlatformFamily(platform_family);
}

bool UiRuntime::IsApplePlatformFamily() const {
    return Input().IsApplePlatformFamily();
}

float UiRuntime::DefaultTouchScrollFriction() const {
    if (!Input().state().coarse_pointer_mode) {
        return 0.95f;
    }
    if (Input().state().platform_family == PlatformFamily::Apple) {
        return 0.960f;
    }
    return 0.955f;
}

float UiRuntime::ResolveScrollFriction(const UINode& node) const {
    if (node.scroll_friction_overridden || std::abs(node.friction - 0.95f) > 0.0001f) {
        return node.friction;
    }
    return DefaultTouchScrollFriction();
}

bool UiRuntime::HasPrimaryShortcutModifier(std::uint32_t modifiers) const {
    const std::uint32_t required = IsApplePlatformFamily() ? UI_KEY_MOD_META : UI_KEY_MOD_CTRL;
    return (modifiers & required) != 0U;
}

bool UiRuntime::HasWordNavigationModifier(std::uint32_t modifiers) const {
    const std::uint32_t required = IsApplePlatformFamily() ? UI_KEY_MOD_ALT : UI_KEY_MOD_CTRL;
    return (modifiers & required) != 0U;
}

bool UiRuntime::HasLineBoundaryModifier(std::uint32_t modifiers) const {
    return IsApplePlatformFamily() && (modifiers & UI_KEY_MOD_META) != 0U;
}

bool UiRuntime::HasDocumentBoundaryModifier(std::uint32_t modifiers) const {
    const std::uint32_t required = IsApplePlatformFamily() ? UI_KEY_MOD_META : UI_KEY_MOD_CTRL;
    return (modifiers & required) != 0U;
}

bool UiRuntime::IsPrimaryShortcut(std::string_view key, std::uint32_t modifiers, char expected) const {
    return HasPrimaryShortcutModifier(modifiers) && MatchesShortcutCharacter(key, expected);
}

bool UiRuntime::IsUndoShortcut(std::string_view key, std::uint32_t modifiers) const {
    return (modifiers & UI_KEY_MOD_SHIFT) == 0U && IsPrimaryShortcut(key, modifiers, 'z');
}

bool UiRuntime::IsRedoShortcut(std::string_view key, std::uint32_t modifiers) const {
    if (IsApplePlatformFamily()) {
        return (modifiers & UI_KEY_MOD_SHIFT) != 0U && IsPrimaryShortcut(key, modifiers, 'z');
    }
    return IsPrimaryShortcut(key, modifiers, 'y') ||
        (((modifiers & UI_KEY_MOD_SHIFT) != 0U) && IsPrimaryShortcut(key, modifiers, 'z'));
}

void UiRuntime::ResetFrameArena() {
    arena_bytes_used_ = 0;
}

std::uintptr_t UiRuntime::ArenaAlloc(std::uint32_t byte_length) {
    if (byte_length == 0U || arena_bytes_used_ + byte_length > string_arena_.size()) {
        return 0;
    }
    const std::size_t offset = arena_bytes_used_;
    arena_bytes_used_ += byte_length;
    return reinterpret_cast<std::uintptr_t>(string_arena_.data() + offset);
}

void UiRuntime::ResetCommitFrameArena() {
    ResetFrameArena();
}

void UiRuntime::BeginCommitProfile() {
    ResetCurrentTextCommitProfile();
}

void UiRuntime::FinishCommitProfile(double total_commit_ms) {
    FinishCurrentTextCommitProfile(total_commit_ms);
}

void UiRuntime::RecordCommitLayoutProfile(const LayoutResult& result) {
    current_text_commit_profile_.yoga_layout_ms += result.yoga_layout_ms;
    current_text_commit_profile_.scroll_metrics_ms += result.scroll_metrics_ms;
    current_text_commit_profile_.layout_stabilization_passes += result.stabilization_passes;
}

void UiRuntime::RecordCommitPaintProfile(double walk_tree_ms) {
    current_text_commit_profile_.walk_tree_ms += walk_tree_ms;
}

void UiRuntime::UpdateCommitAutoScrollSelection() {
    if (!(*scroll_coordinator_).HasAutoScroll()) {
        return;
    }

    scene_geometry_resolver_->Resolve(node_store_.RootHandle());
    SelectionAutoScrollRequest request{};
    request.active = true;
    request.touch_tap_moved = Input().state().touch_text_tap_moved;
    request.touch_tap_handle = Input().state().touch_text_tap_handle;
    request.logical_x = Input().state().last_pointer_logical_x;
    request.logical_y = Input().state().last_pointer_logical_y;

    node_store_.Traversal().ForEachActive([&](std::uint64_t handle, UINode& node) {
        if (!node.is_text_node || !node.is_selectable || node.visibility != UI_VISIBILITY_NORMAL) {
            return;
        }
        const Rect text_bounds = ComputeTextContentBounds(node);
        (void)LayoutParagraph(
            node,
            text_bounds.width > 0.0f ? std::optional<float>(text_bounds.width) : std::nullopt);
        request.is_editor_text_node = IsEditorTextNode(node);
        Selection().UpdateAutoScrollSelection(
            *this,
            request,
            handle,
            node,
            node.abs_x,
            node.abs_y,
            node.layout_width,
            node.layout_height);
    });
}

void UiRuntime::ResolveCommitCaretVisibility() {
    if (pending_caret_visibility_handle_ != UI_INVALID_HANDLE) {
        const ProfileClock::time_point caret_visibility_start = ProfileClock::now();
        if (UINode* caret_node = ResolveMutable(pending_caret_visibility_handle_);
            caret_node != nullptr && caret_node->is_text_node && caret_node->is_selectable) {
            EnsureTextCaretVisible(pending_caret_visibility_handle_, *caret_node);
        }
        current_text_commit_profile_.caret_visibility_ms +=
            ElapsedMilliseconds(caret_visibility_start, ProfileClock::now());
        pending_caret_visibility_handle_ = UI_INVALID_HANDLE;
    }
    pending_text_scroll_metric_handles_.clear();
}

void UiRuntime::BuildCommitSemantics(const std::vector<std::uint64_t>& paint_order) {
    const ProfileClock::time_point semantic_start = ProfileClock::now();
    const VisibilityResolver visibility = VisibilityResolver(node_store_.Reader());
    const SemanticProjector projector(node_store_.Reader(), visibility);
    projector.Build(
        paint_order,
        GetActiveSemanticScopeRoot(),
        [this](const UINode& node) { return ComputeVisibleBounds(node); },
        semantic_buffer_);
    current_text_commit_profile_.semantic_sync_ms += ElapsedMilliseconds(semantic_start, ProfileClock::now());
}

void UiRuntime::CommitFrame(double timestamp_ms) {
    frame_commit_coordinator_->Commit(timestamp_ms);
}

void UiRuntime::BuildCommitDebugTree() {
    const VisibilityResolver visibility = VisibilityResolver(node_store_.Reader());
    const DebugTreeProjector projector(node_store_.Reader(), visibility);
    projector.Build(
        node_store_.RootHandle(),
        [this](const UINode& node) { return ComputeVisibleBounds(node); },
        debug_tree_buffer_);
}

YGSize UiRuntime::MeasureTextCallback(
    YGNodeConstRef yg_node,
    float width,
    YGMeasureMode width_mode,
    float height,
    YGMeasureMode height_mode) {
    (void)height;
    (void)height_mode;
    const auto* node = static_cast<const UINode*>(YGNodeGetContext(yg_node));
    return node == nullptr ? YGSize{0.0f, 0.0f} : GetRuntime().MeasureTextNode(*node, width, width_mode);
}

UINode* UiRuntime::ResolveMutable(std::uint64_t handle) {
    return node_store_.Writer().Resolve(handle);
}

UiRuntime& GetRuntime() {
    static UiRuntime runtime;
    return runtime;
}

} // namespace effindom::v2::ui
