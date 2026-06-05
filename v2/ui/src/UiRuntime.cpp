#include "UiRuntime.h"

#include "CommandBuilder.h"
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

bool IsHorizontalFlexDirection(YGFlexDirection direction) {
    return direction == YGFlexDirectionRow || direction == YGFlexDirectionRowReverse;
}

class SemanticBufferBuilder {
public:
    enum StateFlags : std::uint32_t {
        HAS_SELECTED = 1U << 0U,
        IS_SELECTED = 1U << 1U,
        HAS_EXPANDED = 1U << 2U,
        IS_EXPANDED = 1U << 3U,
        HAS_DISABLED = 1U << 4U,
        IS_DISABLED = 1U << 5U,
        HAS_VALUE_RANGE = 1U << 6U,
        HAS_READONLY = 1U << 7U,
        IS_READONLY = 1U << 8U,
        HAS_MULTILINE = 1U << 9U,
        IS_MULTILINE = 1U << 10U,
    };

    explicit SemanticBufferBuilder(std::vector<std::uint32_t>& words)
        : words_(words) {}

    void Clear() {
        words_.clear();
        words_.push_back(0U);
    }

    void AddRecord(
        UiSemanticRole role,
        std::uint64_t handle,
        float x,
        float y,
        float width,
        float height,
        const UINode& node,
        std::string_view label) {
        std::uint32_t state_flags = 0U;
        if (node.has_semantic_selected) {
            state_flags |= HAS_SELECTED;
            if (node.semantic_selected) {
                state_flags |= IS_SELECTED;
            }
        }
        if (node.has_semantic_expanded) {
            state_flags |= HAS_EXPANDED;
            if (node.semantic_expanded) {
                state_flags |= IS_EXPANDED;
            }
        }
        if (node.has_semantic_disabled) {
            state_flags |= HAS_DISABLED;
            if (node.semantic_disabled) {
                state_flags |= IS_DISABLED;
            }
        }
        if (node.has_semantic_value_range) {
            state_flags |= HAS_VALUE_RANGE;
        }
        if (node.semantic_role == UI_SEMANTIC_TEXTBOX) {
            state_flags |= HAS_READONLY;
            if (!node.is_editable) {
                state_flags |= IS_READONLY;
            }
            state_flags |= HAS_MULTILINE;
            if (node.max_lines != 1) {
                state_flags |= IS_MULTILINE;
            }
        }

        words_.push_back(static_cast<std::uint32_t>(role));
        words_.push_back(static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL));
        words_.push_back(static_cast<std::uint32_t>(handle >> 32U));
        words_.push_back(CommandBuilder::FloatToWord(x));
        words_.push_back(CommandBuilder::FloatToWord(y));
        words_.push_back(CommandBuilder::FloatToWord(width));
        words_.push_back(CommandBuilder::FloatToWord(height));
        words_.push_back(state_flags);
        words_.push_back(static_cast<std::uint32_t>(node.semantic_checked_state));
        words_.push_back(static_cast<std::uint32_t>(node.semantic_orientation));
        words_.push_back(CommandBuilder::FloatToWord(node.semantic_value_now));
        words_.push_back(CommandBuilder::FloatToWord(node.semantic_value_min));
        words_.push_back(CommandBuilder::FloatToWord(node.semantic_value_max));
        words_.push_back(static_cast<std::uint32_t>(label.size()));

        const std::size_t word_count = (label.size() + 3U) / 4U;
        const std::size_t start = words_.size();
        words_.resize(start + word_count, 0U);
        if (!label.empty()) {
            std::memcpy(words_.data() + start, label.data(), label.size());
        }
        words_[0] += 1U;
    }

private:
    std::vector<std::uint32_t>& words_;
};

constexpr std::size_t kDefaultTextboxSemanticLabelMaxCodepoints = 1000U;

std::size_t Utf8PrefixLengthForCodepoints(std::string_view text, std::size_t max_codepoints) {
    std::size_t offset = 0U;
    std::size_t count = 0U;
    while (offset < text.size() && count < max_codepoints) {
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        std::size_t advance = 1U;
        if ((lead & 0xE0U) == 0xC0U && offset + 1U < text.size()) {
            advance = 2U;
        } else if ((lead & 0xF0U) == 0xE0U && offset + 2U < text.size()) {
            advance = 3U;
        } else if ((lead & 0xF8U) == 0xF0U && offset + 3U < text.size()) {
            advance = 4U;
        }
        offset += advance;
        count += 1U;
    }
    return offset;
}

std::string BuildSemanticLabel(const UINode& node) {
    if (!node.semantic_label.empty()) {
        return node.semantic_label;
    }
    if (node.is_text_node && !node.text_content.empty()) {
        if (node.semantic_role == UI_SEMANTIC_TEXTBOX) {
            const std::size_t prefix_length =
                Utf8PrefixLengthForCodepoints(node.text_content, kDefaultTextboxSemanticLabelMaxCodepoints);
            if (prefix_length < node.text_content.size()) {
                return node.text_content.substr(0U, prefix_length) + "...";
            }
        }
        return node.text_content;
    }
    return {};
}

bool IntersectRect(Rect& rect, const Rect& clip) {
    const float left = std::max(rect.x, clip.x);
    const float top = std::max(rect.y, clip.y);
    const float right = std::min(rect.x + rect.width, clip.x + clip.width);
    const float bottom = std::min(rect.y + rect.height, clip.y + clip.height);
    rect.x = left;
    rect.y = top;
    rect.width = std::max(0.0f, right - left);
    rect.height = std::max(0.0f, bottom - top);
    return rect.width > 0.0f && rect.height > 0.0f;
}

} // namespace

UiRuntime::UiRuntime()
    : string_arena_(kFrameArenaCapacity, 0U) {
    command_buffer_.reserve(1024);
    semantic_buffer_.reserve(256);
}

void UiRuntime::ApplyLayoutStyles(std::uint64_t handle, std::uint64_t parent_handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return;
    }

    if (node->has_width) {
        switch (node->width_unit) {
        case UI_SIZE_UNIT_PIXEL:
            YGNodeStyleSetWidth(node->yg_node, node->width);
            break;
        case UI_SIZE_UNIT_AUTO:
            YGNodeStyleSetWidthAuto(node->yg_node);
            break;
        case UI_SIZE_UNIT_PERCENT:
            YGNodeStyleSetWidthPercent(node->yg_node, node->width);
            break;
        default:
            YGNodeStyleSetWidthAuto(node->yg_node);
            break;
        }
    } else {
        YGNodeStyleSetWidthAuto(node->yg_node);
    }

    if (node->has_height) {
        switch (node->height_unit) {
        case UI_SIZE_UNIT_PIXEL:
            YGNodeStyleSetHeight(node->yg_node, node->height);
            break;
        case UI_SIZE_UNIT_AUTO:
            YGNodeStyleSetHeightAuto(node->yg_node);
            break;
        case UI_SIZE_UNIT_PERCENT:
            YGNodeStyleSetHeightPercent(node->yg_node, node->height);
            break;
        default:
            YGNodeStyleSetHeightAuto(node->yg_node);
            break;
        }
    } else {
        YGNodeStyleSetHeightAuto(node->yg_node);
    }

    if (node->has_flex_basis) {
        YGNodeStyleSetFlexBasis(node->yg_node, node->flex_basis);
    } else {
        YGNodeStyleSetFlexBasisAuto(node->yg_node);
    }
    YGNodeStyleSetFlexGrow(node->yg_node, 0.0f);

    YGNodeStyleSetAlignSelf(node->yg_node, YGAlignAuto);
    if (node->has_align_self) {
        switch (node->align_self) {
        case UI_ALIGN_SELF_AUTO:
            YGNodeStyleSetAlignSelf(node->yg_node, YGAlignAuto);
            break;
        case UI_ALIGN_SELF_START:
            YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexStart);
            break;
        case UI_ALIGN_SELF_CENTER:
            YGNodeStyleSetAlignSelf(node->yg_node, YGAlignCenter);
            break;
        case UI_ALIGN_SELF_END:
            YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexEnd);
            break;
        case UI_ALIGN_SELF_STRETCH:
            YGNodeStyleSetAlignSelf(node->yg_node, YGAlignStretch);
            break;
        default:
            YGNodeStyleSetAlignSelf(node->yg_node, YGAlignAuto);
            break;
        }
    }

    if (parent_handle == UI_INVALID_HANDLE) {
        if (node->fill_width) {
            YGNodeStyleSetWidthPercent(node->yg_node, 100.0f);
        } else if (node->has_fill_width_percent) {
            YGNodeStyleSetWidthPercent(node->yg_node, node->fill_width_percent);
        }
        if (node->fill_height) {
            YGNodeStyleSetHeightPercent(node->yg_node, 100.0f);
        } else if (node->has_fill_height_percent) {
            YGNodeStyleSetHeightPercent(node->yg_node, node->fill_height_percent);
        }
    } else {
        const UINode* parent = Resolve(parent_handle);
        if (parent != nullptr && parent->yg_node != nullptr) {
            const bool parent_is_horizontal = IsHorizontalFlexDirection(YGNodeStyleGetFlexDirection(parent->yg_node));
            const bool auto_sized_width =
                !node->fill_width &&
                !node->has_fill_width_percent &&
                (!node->has_width || node->width_unit == UI_SIZE_UNIT_AUTO);
            const bool auto_sized_height =
                !node->fill_height &&
                !node->has_fill_height_percent &&
                (!node->has_height || node->height_unit == UI_SIZE_UNIT_AUTO);
            if (parent->is_scroll_view && !node->has_align_self) {
                const bool cross_axis_auto_sized = parent_is_horizontal ? auto_sized_height : auto_sized_width;
                const bool child_is_horizontal = IsHorizontalFlexDirection(YGNodeStyleGetFlexDirection(node->yg_node));
                if (cross_axis_auto_sized && child_is_horizontal != parent_is_horizontal) {
                    YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexStart);
                }
            }
            if (!parent->is_scroll_view && !node->has_align_self && !node->children.empty()) {
                const bool cross_axis_explicit_auto =
                    parent_is_horizontal
                        ? (node->has_height && node->height_unit == UI_SIZE_UNIT_AUTO)
                        : (node->has_width && node->width_unit == UI_SIZE_UNIT_AUTO);
                if (cross_axis_explicit_auto) {
                    YGNodeStyleSetAlignSelf(node->yg_node, YGAlignFlexStart);
                }
            }
            if (node->has_fill_width_percent || (node->fill_width && parent_is_horizontal)) {
                if (node->has_resolved_fill_width) {
                    YGNodeStyleSetWidth(node->yg_node, node->resolved_fill_width);
                } else {
                    YGNodeStyleSetWidthAuto(node->yg_node);
                }
            }
            if (node->has_fill_height_percent || (node->fill_height && !parent_is_horizontal)) {
                if (node->has_resolved_fill_height) {
                    YGNodeStyleSetHeight(node->yg_node, node->resolved_fill_height);
                } else {
                    YGNodeStyleSetHeightAuto(node->yg_node);
                }
            }
            if (node->fill_width && !parent_is_horizontal) {
                YGNodeStyleSetAlignSelf(node->yg_node, YGAlignStretch);
            }
            if (node->fill_height && parent_is_horizontal) {
                YGNodeStyleSetAlignSelf(node->yg_node, YGAlignStretch);
            }
        }
    }

    for (const std::uint64_t child_handle : node->children) {
        ApplyLayoutStyles(child_handle, handle);
    }
}

float UiRuntime::ComputeFillAxisAvailableSpace(
    const UINode& node,
    const UINode* parent,
    bool width_axis,
    bool parent_is_horizontal) const {
    if (parent == nullptr || parent->yg_node == nullptr) {
        return width_axis ? window_width_ : window_height_;
    }

    const float parent_content_width = std::max(
        0.0f,
        YGNodeLayoutGetWidth(parent->yg_node) -
            YGNodeLayoutGetBorder(parent->yg_node, YGEdgeLeft) -
            YGNodeLayoutGetBorder(parent->yg_node, YGEdgeRight) -
            YGNodeLayoutGetPadding(parent->yg_node, YGEdgeLeft) -
            YGNodeLayoutGetPadding(parent->yg_node, YGEdgeRight));
    const float parent_content_height = std::max(
        0.0f,
        YGNodeLayoutGetHeight(parent->yg_node) -
            YGNodeLayoutGetBorder(parent->yg_node, YGEdgeTop) -
            YGNodeLayoutGetBorder(parent->yg_node, YGEdgeBottom) -
            YGNodeLayoutGetPadding(parent->yg_node, YGEdgeTop) -
            YGNodeLayoutGetPadding(parent->yg_node, YGEdgeBottom));
    float available = width_axis ? parent_content_width : parent_content_height;
    const bool main_axis = width_axis == parent_is_horizontal;

    if (main_axis) {
        for (const std::uint64_t sibling_handle : parent->children) {
            const UINode* sibling = Resolve(sibling_handle);
            if (sibling == nullptr || sibling->yg_node == nullptr || sibling == &node) {
                continue;
            }
            if (YGNodeStyleGetPositionType(sibling->yg_node) == YGPositionTypeAbsolute) {
                continue;
            }
            const bool sibling_uses_main_axis_available_space = width_axis
                ? (sibling->fill_width || sibling->has_fill_width_percent || (sibling->has_width && sibling->width_unit == UI_SIZE_UNIT_PERCENT))
                : (sibling->fill_height || sibling->has_fill_height_percent || (sibling->has_height && sibling->height_unit == UI_SIZE_UNIT_PERCENT));
            if (sibling_uses_main_axis_available_space) {
                continue;
            }
            if (width_axis) {
                available -=
                    YGNodeLayoutGetWidth(sibling->yg_node) +
                    YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeLeft) +
                    YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeRight);
            } else {
                available -=
                    YGNodeLayoutGetHeight(sibling->yg_node) +
                    YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeTop) +
                    YGNodeLayoutGetMargin(sibling->yg_node, YGEdgeBottom);
            }
        }
    }

    if (parent != nullptr) {
        if (width_axis) {
            available -=
                YGNodeLayoutGetMargin(node.yg_node, YGEdgeLeft) +
                YGNodeLayoutGetMargin(node.yg_node, YGEdgeRight);
        } else {
            available -=
                YGNodeLayoutGetMargin(node.yg_node, YGEdgeTop) +
                YGNodeLayoutGetMargin(node.yg_node, YGEdgeBottom);
        }
    }

    return std::max(0.0f, available);
}

bool UiRuntime::ResolveFillPercentLayout(std::uint64_t handle, std::uint64_t parent_handle) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }

    bool changed = false;
    const UINode* parent = parent_handle == UI_INVALID_HANDLE ? nullptr : Resolve(parent_handle);
    const bool parent_is_horizontal =
        parent != nullptr && parent->yg_node != nullptr && IsHorizontalFlexDirection(YGNodeStyleGetFlexDirection(parent->yg_node));

    if (parent_handle != UI_INVALID_HANDLE && (node->has_fill_width_percent || (node->fill_width && parent_is_horizontal))) {
        const float available = ComputeFillAxisAvailableSpace(*node, parent, true, parent_is_horizontal);
        const float percent = node->fill_width ? 100.0f : node->fill_width_percent;
        const float resolved = available * (percent / 100.0f);
        if (!node->has_resolved_fill_width || std::abs(node->resolved_fill_width - resolved) > 0.01f) {
            YGNodeStyleSetWidth(node->yg_node, resolved);
            node->resolved_fill_width = resolved;
            node->has_resolved_fill_width = true;
            changed = true;
        }
    }
    if (parent_handle != UI_INVALID_HANDLE && (node->has_fill_height_percent || (node->fill_height && !parent_is_horizontal))) {
        const float available = ComputeFillAxisAvailableSpace(*node, parent, false, parent_is_horizontal);
        const float percent = node->fill_height ? 100.0f : node->fill_height_percent;
        const float resolved = available * (percent / 100.0f);
        if (!node->has_resolved_fill_height || std::abs(node->resolved_fill_height - resolved) > 0.01f) {
            YGNodeStyleSetHeight(node->yg_node, resolved);
            node->resolved_fill_height = resolved;
            node->has_resolved_fill_height = true;
            changed = true;
        }
    }

    for (const std::uint64_t child_handle : node->children) {
        changed = ResolveFillPercentLayout(child_handle, handle) || changed;
    }
    return changed;
}

void UiRuntime::Reset() {
    for (UINode& node : node_pool_) {
        if (node.yg_node != nullptr) {
            YGNodeFree(node.yg_node);
            node.yg_node = nullptr;
        }
        node.children.clear();
        node.children.shrink_to_fit();
        node = UINode{};
    }
    node_id_map_.clear();
    grid_side_tables_.clear();
    pending_text_scroll_metric_handles_.clear();
    command_buffer_.clear();
    semantic_buffer_.clear();
    semantic_scope_stack_.clear();
    pending_creations_.clear();
    pending_deletions_.clear();
    root_handle_ = UI_INVALID_HANDLE;
    last_hovered_handle_ = UI_INVALID_HANDLE;
    pending_caret_visibility_handle_ = UI_INVALID_HANDLE;
    focused_handle_ = UI_INVALID_HANDLE;
    active_selection_handle_ = UI_INVALID_HANDLE;
    text_find_handle_ = UI_INVALID_HANDLE;
    text_find_start_ = 0U;
    text_find_end_ = 0U;
    text_find_highlights_.clear();
    active_selection_dragged_ = false;
    active_scroll_handle_ = UI_INVALID_HANDLE;
    active_touch_scroll_handle_x_ = UI_INVALID_HANDLE;
    active_touch_scroll_handle_y_ = UI_INVALID_HANDLE;
    active_scroll_dragged_ = false;
    coarse_pointer_mode_ = false;
    platform_family_ = PlatformFamily::Unknown;
    primary_pointer_down_ = false;
    ClearAutoScrollState();
    last_pointer_logical_x_ = 0.0f;
    last_pointer_logical_y_ = 0.0f;
    interaction_time_ms_ = 0;
    last_click_handle_ = UI_INVALID_HANDLE;
    last_click_x_ = 0.0f;
    last_click_y_ = 0.0f;
    click_count_ = 0;
    last_click_time_ms_ = 0;
    current_modifiers_ = 0;
    selection_anchor_handle_ = UI_INVALID_HANDLE;
    selection_anchor_index_ = 0;
    selection_horizontal_extend_active_ = false;
    cross_selection_active_ = false;
    cross_selection_dragged_ = false;
    cross_selection_horizontal_extend_active_ = false;
    selection_area_handle_ = UI_INVALID_HANDLE;
    start_node_handle_ = UI_INVALID_HANDLE;
    start_index_ = 0U;
    end_node_handle_ = UI_INVALID_HANDLE;
    end_index_ = 0U;
    selection_area_nodes_.clear();
    current_selection_hit_rects_.clear();
    selection_area_nodes_dirty_ = false;
    pending_focus_node_id_.clear();
    window_width_ = 800.0f;
    window_height_ = 600.0f;
    focus_order_.clear();
    focus_order_dirty_ = true;
    layout_dirty_ = true;
    reported_missing_font_coverage_keys_.clear();
    next_semantic_scope_token_ = 1U;
    ClearTextCommitProfile();
    ResetFrameArena();
}

const UiRuntime::TextCommitProfile& UiRuntime::last_text_commit_profile() const {
    return last_text_commit_profile_;
}

void UiRuntime::ClearTextCommitProfile() {
    current_text_commit_profile_ = TextCommitProfile{};
    last_text_commit_profile_ = TextCommitProfile{};
    text_commit_profile_active_ = false;
}

void UiRuntime::ResetCurrentTextCommitProfile() const {
    current_text_commit_profile_ = TextCommitProfile{};
    text_commit_profile_active_ = true;
}

void UiRuntime::FinishCurrentTextCommitProfile(double total_commit_ms) const {
    current_text_commit_profile_.total_commit_ms = total_commit_ms;
    last_text_commit_profile_ = current_text_commit_profile_;
    text_commit_profile_active_ = false;
}

void UiRuntime::SetPlatformFamily(std::uint32_t platform_family) {
    switch (static_cast<PlatformFamily>(platform_family)) {
    case PlatformFamily::Apple:
    case PlatformFamily::Windows:
    case PlatformFamily::Linux:
        platform_family_ = static_cast<PlatformFamily>(platform_family);
        break;
    default:
        platform_family_ = PlatformFamily::Unknown;
        break;
    }
}

bool UiRuntime::IsApplePlatformFamily() const {
    return platform_family_ == PlatformFamily::Apple;
}

float UiRuntime::DefaultTouchScrollFriction() const {
    if (!coarse_pointer_mode_) {
        return 0.95f;
    }
    if (platform_family_ == PlatformFamily::Apple) {
        return 0.988f;
    }
    return 0.982f;
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

bool UiRuntime::ApplyScrollOffset(
    std::uint64_t handle,
    UINode& node,
    float offset_x,
    float offset_y,
    bool notify) {
    const float viewport_width = GetScrollViewportWidth(node);
    const float viewport_height = GetScrollViewportHeight(node);
    const float max_x = std::max(0.0f, node.scroll_content_width - viewport_width);
    const float max_y = std::max(0.0f, node.scroll_content_height - viewport_height);
    const float clamped_x = node.scroll_enabled_x ? std::clamp(offset_x, 0.0f, max_x) : 0.0f;
    const float clamped_y = node.scroll_enabled_y ? std::clamp(offset_y, 0.0f, max_y) : 0.0f;
    if (std::abs(node.scroll_offset_x - clamped_x) < 0.001f && std::abs(node.scroll_offset_y - clamped_y) < 0.001f) {
        return false;
    }

    if (node.is_applying_scroll_offset) {
        node.scroll_offset_x = clamped_x;
        node.scroll_offset_y = clamped_y;
        node.scroll_offset_dirty = true;
        if (notify) {
            node.has_deferred_scroll_notification = true;
        }
        return true;
    }

    node.is_applying_scroll_offset = true;
    node.scroll_offset_x = clamped_x;
    node.scroll_offset_y = clamped_y;
    node.scroll_offset_dirty = true;
    if (notify) {
        NotifyScrollChanged(handle, node);
        constexpr std::uint32_t kMaxDeferredScrollNotifications = 4U;
        std::uint32_t deferred_notifications = 0U;
        while (node.has_deferred_scroll_notification &&
               deferred_notifications < kMaxDeferredScrollNotifications) {
            node.has_deferred_scroll_notification = false;
            NotifyScrollChanged(handle, node);
            deferred_notifications += 1U;
        }
        if (node.has_deferred_scroll_notification) {
            node.has_pending_scroll_offset = true;
            node.pending_scroll_offset_x = node.scroll_offset_x;
            node.pending_scroll_offset_y = node.scroll_offset_y;
            node.pending_scroll_offset_generation += 1U;
            node.has_deferred_scroll_notification = false;
        }
    }
    node.is_applying_scroll_offset = false;
    return true;
}

void UiRuntime::NotifyScrollChanged(std::uint64_t handle, UINode& node) {
    const float viewport_width = GetScrollViewportWidth(node);
    const float viewport_height = GetScrollViewportHeight(node);
    as_on_scroll(
        handle,
        node.scroll_offset_x,
        node.scroll_offset_y,
        node.scroll_content_width,
        node.scroll_content_height,
        viewport_width,
        viewport_height);
    node.reported_scroll_offset_x = node.scroll_offset_x;
    node.reported_scroll_offset_y = node.scroll_offset_y;
    node.reported_scroll_content_width = node.scroll_content_width;
    node.reported_scroll_content_height = node.scroll_content_height;
    node.reported_viewport_width = viewport_width;
    node.reported_viewport_height = viewport_height;
    node.has_reported_scroll_state = true;
}

void UiRuntime::UpdateScrollMetrics(std::uint64_t handle, UINode& node) {
    if (!node.is_scroll_view || node.yg_node == nullptr) {
        return;
    }

    node.layout_width = YGNodeLayoutGetWidth(node.yg_node);
    node.layout_height = YGNodeLayoutGetHeight(node.yg_node);

    const Rect content_bounds = ComputeContentBounds(node, 0.0f, 0.0f);
    float content_width = 0.0f;
    float content_height = 0.0f;
    for (const std::uint64_t child_handle : node.children) {
        const UINode* child = Resolve(child_handle);
        if (child == nullptr || child->yg_node == nullptr) {
            continue;
        }
        float child_content_width = YGNodeLayoutGetWidth(child->yg_node);
        if (child->is_text_node && !child->text_wrap) {
            const Rect child_text_bounds = ComputeTextContentBounds(*child);
            const ParagraphLayout paragraph = LayoutParagraph(
                *child,
                child_text_bounds.width > 0.0f ? std::optional<float>(child_text_bounds.width) : std::nullopt);
            child_content_width = std::max(child_content_width, paragraph.width);
            const bool has_horizontal_text_overflow =
                paragraph.width > (child_text_bounds.width + 0.001f);
            if ((child->is_editable || child->semantic_role == UI_SEMANTIC_TEXTBOX) &&
                has_horizontal_text_overflow) {
                child_content_width += FocusedTextCaretScrollOverscan(*child);
            }
        }
        content_width = std::max(
            content_width,
            YGNodeLayoutGetLeft(child->yg_node) + child_content_width + YGNodeLayoutGetMargin(child->yg_node, YGEdgeRight) - content_bounds.x);
        content_height = std::max(
            content_height,
            YGNodeLayoutGetTop(child->yg_node) + YGNodeLayoutGetHeight(child->yg_node) + YGNodeLayoutGetMargin(child->yg_node, YGEdgeBottom) - content_bounds.y);
    }

    node.scroll_content_width = node.has_explicit_scroll_content_width
        ? node.explicit_scroll_content_width
        : std::max(content_width, 0.0f);
    node.scroll_content_height = node.has_explicit_scroll_content_height
        ? node.explicit_scroll_content_height
        : std::max(content_height, 0.0f);
    const std::uint64_t pending_generation = node.pending_scroll_offset_generation;
    const float target_x = node.has_pending_scroll_offset ? node.pending_scroll_offset_x : node.scroll_offset_x;
    const float target_y = node.has_pending_scroll_offset ? node.pending_scroll_offset_y : node.scroll_offset_y;
    const bool offset_changed = ApplyScrollOffset(handle, node, target_x, target_y, true);
    if (!offset_changed) {
        const bool metrics_changed = !node.has_reported_scroll_state ||
            std::abs(node.reported_scroll_offset_x - node.scroll_offset_x) >= 0.001f ||
            std::abs(node.reported_scroll_offset_y - node.scroll_offset_y) >= 0.001f ||
            std::abs(node.reported_scroll_content_width - node.scroll_content_width) >= 0.001f ||
            std::abs(node.reported_scroll_content_height - node.scroll_content_height) >= 0.001f ||
            std::abs(node.reported_viewport_width - GetScrollViewportWidth(node)) >= 0.001f ||
            std::abs(node.reported_viewport_height - GetScrollViewportHeight(node)) >= 0.001f;
        if (metrics_changed) {
            NotifyScrollChanged(handle, node);
        }
    }
    if (node.pending_scroll_offset_generation == pending_generation) {
        node.has_pending_scroll_offset = false;
    }
}

void UiRuntime::CommitFrame() {
    const ProfileClock::time_point commit_start = ProfileClock::now();
    ResetCurrentTextCommitProfile();
    command_buffer_.clear();
    ResetFrameArena();
    SemanticBufferBuilder semantic_builder(semantic_buffer_);
    semantic_builder.Clear();

    CommandBuilder builder(command_buffer_);

    for (const std::uint64_t handle : pending_deletions_) {
        builder.DeleteNode(handle);
    }
    pending_deletions_.clear();

    for (const std::uint64_t handle : pending_creations_) {
        if (UINode* node = ResolveMutable(handle); node != nullptr && node->needs_creation) {
            builder.CreateNode(handle);
            node->needs_creation = false;
        }
    }
    pending_creations_.clear();

    UINode* root = ResolveMutable(root_handle_);
    if (root == nullptr || root->yg_node == nullptr) {
        if (!command_buffer_.empty()) {
            builder.CommitPaintOrder({});
            builder.CommitScene({});
        }
        layout_dirty_ = false;
        pending_text_scroll_metric_handles_.clear();
        FinishCurrentTextCommitProfile(ElapsedMilliseconds(commit_start, ProfileClock::now()));
        return;
    }

    bool emit_layout_updates = layout_dirty_;
    if (layout_dirty_) {
        constexpr std::uint32_t kMaxLayoutStabilizationPasses = 4U;
        std::uint32_t layout_pass = 0U;
        do {
            layout_dirty_ = false;
            ApplyLayoutStyles(root_handle_, UI_INVALID_HANDLE);
            const ProfileClock::time_point layout_start = ProfileClock::now();
            YGNodeCalculateLayout(root->yg_node, window_width_, window_height_, YGDirectionLTR);
            current_text_commit_profile_.yoga_layout_ms += ElapsedMilliseconds(layout_start, ProfileClock::now());

            const ProfileClock::time_point scroll_metrics_start = ProfileClock::now();
            for (std::uint64_t index = 1U; index < node_pool_.size(); index += 1U) {
                UINode& node = node_pool_[index];
                if (!node.is_active || !node.is_scroll_view) {
                    continue;
                }
                const std::uint64_t handle =
                    (static_cast<std::uint64_t>(node.generation) << 32U) | static_cast<std::uint64_t>(index);
                UpdateScrollMetrics(handle, node);
            }
            current_text_commit_profile_.scroll_metrics_ms +=
                ElapsedMilliseconds(scroll_metrics_start, ProfileClock::now());
            if (ResolveFillPercentLayout(root_handle_, UI_INVALID_HANDLE)) {
                layout_dirty_ = true;
            }
            emit_layout_updates = emit_layout_updates || layout_dirty_;
            layout_pass += 1U;
        } while (layout_dirty_ && layout_pass < kMaxLayoutStabilizationPasses);
    } else {
        const ProfileClock::time_point scroll_metrics_start = ProfileClock::now();
        for (std::uint64_t index = 1U; index < node_pool_.size(); index += 1U) {
            UINode& node = node_pool_[index];
            if (!node.is_active || !node.is_scroll_view) {
                continue;
            }
            if (!node.has_pending_scroll_offset) {
                continue;
            }
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(node.generation) << 32U) | static_cast<std::uint64_t>(index);
            const std::uint64_t pending_generation = node.pending_scroll_offset_generation;
            (void)ApplyScrollOffset(handle, node, node.pending_scroll_offset_x, node.pending_scroll_offset_y, true);
            if (node.pending_scroll_offset_generation == pending_generation) {
                node.has_pending_scroll_offset = false;
            }
        }
        current_text_commit_profile_.scroll_metrics_ms +=
            ElapsedMilliseconds(scroll_metrics_start, ProfileClock::now());
    }

    if (auto_scroll_active_) {
        if (UINode* scroll_node = ResolveMutable(auto_scroll_view_handle_); scroll_node != nullptr) {
            (void)ApplyScrollOffset(
                auto_scroll_view_handle_,
                *scroll_node,
                scroll_node->scroll_offset_x + (auto_scroll_factor_x_ * scroll_node->auto_scroll_speed),
                scroll_node->scroll_offset_y + (auto_scroll_factor_y_ * scroll_node->auto_scroll_speed),
                true);
        } else {
            ClearAutoScrollState();
        }
    }

    for (std::uint64_t index = 1U; index < node_pool_.size(); index += 1U) {
        UINode& node = node_pool_[index];
        if (!node.is_active || !node.is_scroll_view) {
            continue;
        }
        const std::uint64_t handle =
            (static_cast<std::uint64_t>(node.generation) << 32U) | static_cast<std::uint64_t>(index);
        if (active_scroll_handle_ == handle ||
            active_touch_scroll_handle_x_ == handle ||
            active_touch_scroll_handle_y_ == handle) {
            continue;
        }
        if (std::abs(node.scroll_velocity_x) < 0.001f && std::abs(node.scroll_velocity_y) < 0.001f) {
            node.scroll_velocity_x = 0.0f;
            node.scroll_velocity_y = 0.0f;
            continue;
        }
        const float next_x = node.scroll_offset_x + node.scroll_velocity_x;
        const float next_y = node.scroll_offset_y + node.scroll_velocity_y;
        const bool changed = ApplyScrollOffset(handle, node, next_x, next_y, true);
        const float friction = ResolveScrollFriction(node);
        node.scroll_velocity_x *= friction;
        node.scroll_velocity_y *= friction;
        if (!changed ||
            (std::abs(node.scroll_velocity_x) < 0.1f && std::abs(node.scroll_velocity_y) < 0.1f)) {
            node.scroll_velocity_x = 0.0f;
            node.scroll_velocity_y = 0.0f;
        }
    }

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

    std::vector<std::uint64_t> paint_order{};
    std::vector<SceneInstruction> scene{};
    std::vector<std::uint64_t> deferred_portal_roots{};
    current_selection_hit_rects_.clear();
    paint_order.reserve(node_pool_.size() / 4U);
    scene.reserve(node_pool_.size() / 4U);
    deferred_portal_roots.reserve(node_pool_.size() / 8U);

    const bool needs_follow_up_layout = layout_dirty_;
    layout_dirty_ = emit_layout_updates;

    const ProfileClock::time_point walk_tree_start = ProfileClock::now();
    WalkTree(root_handle_, 0.0f, 0.0f, 0.0f, 0.0f, false, builder, paint_order, scene, deferred_portal_roots);
    for (std::size_t index = 0; index < deferred_portal_roots.size(); index += 1U) {
        UINode* portal = ResolveMutable(deferred_portal_roots[index]);
        if (portal != nullptr) {
            const float child_origin_x = portal->abs_x - (portal->is_scroll_view ? portal->scroll_offset_x : 0.0f);
            const float child_origin_y = portal->abs_y - (portal->is_scroll_view ? portal->scroll_offset_y : 0.0f);
            const float child_scene_x = portal->abs_x;
            const float child_scene_y = portal->abs_y;
            const bool inherited_scroll_dirty =
                portal->scroll_offset_dirty ||
                std::abs(portal->abs_x - portal->scene_x) >= 0.001f ||
                std::abs(portal->abs_y - portal->scene_y) >= 0.001f;
            if (portal->clip_to_bounds || portal->is_scroll_view) {
                scene.push_back(SceneInstruction{OP_PUSH_CLIP, deferred_portal_roots[index]});
            }
            if (portal->is_scroll_view &&
                (std::abs(portal->scroll_offset_x) >= 0.001f || std::abs(portal->scroll_offset_y) >= 0.001f)) {
                scene.push_back(SceneInstruction{
                    OP_PUSH_TRANSLATE,
                    deferred_portal_roots[index],
                    -portal->scroll_offset_x,
                    -portal->scroll_offset_y,
                });
            }
            for (const std::uint64_t child_handle : portal->children) {
                WalkTree(
                    child_handle,
                    child_origin_x,
                    child_origin_y,
                    child_scene_x,
                    child_scene_y,
                    inherited_scroll_dirty,
                    builder,
                    paint_order,
                    scene,
                    deferred_portal_roots);
            }
            if (portal->is_scroll_view &&
                (std::abs(portal->scroll_offset_x) >= 0.001f || std::abs(portal->scroll_offset_y) >= 0.001f)) {
                scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
            }
            if (portal->clip_to_bounds || portal->is_scroll_view) {
                scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
            }
            portal->scroll_offset_dirty = false;
        }
    }
    current_text_commit_profile_.walk_tree_ms += ElapsedMilliseconds(walk_tree_start, ProfileClock::now());

    const std::uint64_t semantic_scope_root = GetActiveSemanticScopeRoot();

    const ProfileClock::time_point semantic_start = ProfileClock::now();
    for (const std::uint64_t handle : paint_order) {
        UINode* node = ResolveMutable(handle);
        if (node == nullptr) {
            continue;
        }
        UiSemanticRole semantic_role = node->semantic_role;
        const std::string label = BuildSemanticLabel(*node);
        if (semantic_role == UI_SEMANTIC_NONE) {
            if (!node->is_text_node || label.empty()) {
                continue;
            }
            semantic_role = UI_SEMANTIC_STATIC_TEXT;
        }
        if (semantic_scope_root != UI_INVALID_HANDLE && !SubtreeContains(semantic_scope_root, handle)) {
            continue;
        }
        Rect visible_bounds = ComputeVisibleBounds(*node);
        if (semantic_role == UI_SEMANTIC_TEXTBOX && node->max_lines != 1) {
            const UINode* parent = Resolve(node->parent_handle);
            if (parent != nullptr && parent->is_scroll_view) {
                visible_bounds = ComputeClipBounds(*parent);
                for (std::uint64_t current = parent->parent_handle; current != UI_INVALID_HANDLE;) {
                    const UINode* ancestor = Resolve(current);
                    if (ancestor == nullptr) {
                        break;
                    }
                    if ((ancestor->clip_to_bounds || ancestor->is_scroll_view) &&
                        !IntersectRect(visible_bounds, ComputeClipBounds(*ancestor))) {
                        break;
                    }
                    if (ancestor->is_portal) {
                        break;
                    }
                    current = ancestor->parent_handle;
                }
            }
        }
        if (visible_bounds.width <= 0.0f || visible_bounds.height <= 0.0f) {
            continue;
        }
        semantic_builder.AddRecord(
            semantic_role,
            handle,
            visible_bounds.x,
            visible_bounds.y,
            visible_bounds.width,
            visible_bounds.height,
            *node,
            label);
    }
    current_text_commit_profile_.semantic_sync_ms += ElapsedMilliseconds(semantic_start, ProfileClock::now());

    builder.CommitPaintOrder(paint_order);
    builder.CommitScene(scene);
    layout_dirty_ = needs_follow_up_layout;
    FinishCurrentTextCommitProfile(ElapsedMilliseconds(commit_start, ProfileClock::now()));
}

void UiRuntime::EnsureHandleVisible(std::uint64_t handle) {
    const UINode* target = Resolve(handle);
    if (target == nullptr || !target->is_active || target->yg_node == nullptr) {
        return;
    }

    float target_left = 0.0f;
    float target_top = 0.0f;
    float target_right = target->layout_width;
    float target_bottom = target->layout_height;
    std::uint64_t current_handle = handle;
    while (current_handle != UI_INVALID_HANDLE) {
        const UINode* current = Resolve(current_handle);
        if (current == nullptr || current->yg_node == nullptr) {
            break;
        }
        const std::uint64_t parent_handle = current->parent_handle;
        if (parent_handle == UI_INVALID_HANDLE) {
            break;
        }
        UINode* parent = ResolveMutable(parent_handle);
        if (parent == nullptr || parent->yg_node == nullptr) {
            break;
        }

        const float current_left = YGNodeLayoutGetLeft(current->yg_node);
        const float current_top = YGNodeLayoutGetTop(current->yg_node);
        target_left += current_left;
        target_right += current_left;
        target_top += current_top;
        target_bottom += current_top;

        if (parent->is_scroll_view) {
            EnsureRectVisibleWithinScrollAncestor(
                parent_handle,
                target_left - parent->scroll_offset_x,
                target_top - parent->scroll_offset_y,
                target_right - parent->scroll_offset_x,
                target_bottom - parent->scroll_offset_y,
                &target_left,
                &target_top,
                &target_right,
                &target_bottom);
        }
        current_handle = parent_handle;
    }
}

const UINode* UiRuntime::ResolveTextSnapshotNode(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr ||
        !node->is_active ||
        !node->is_text_node ||
        node->is_editable ||
        node->visibility != UI_VISIBILITY_NORMAL ||
        !IsAttachedToRoot(handle)) {
        return nullptr;
    }
    return node;
}

void UiRuntime::AppendTextSnapshotHandles(std::uint64_t handle, std::vector<std::uint64_t>& out) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr || !node->is_active || node->visibility != UI_VISIBILITY_NORMAL) {
        return;
    }
    if (ResolveTextSnapshotNode(handle) != nullptr && !node->text_content.empty()) {
        out.push_back(handle);
    }
    for (const std::uint64_t child_handle : node->children) {
        AppendTextSnapshotHandles(child_handle, out);
    }
}

std::vector<std::uint64_t> UiRuntime::GetTextSnapshotHandles() const {
    std::vector<std::uint64_t> handles{};
    if (root_handle_ == UI_INVALID_HANDLE) {
        return handles;
    }
    AppendTextSnapshotHandles(root_handle_, handles);
    return handles;
}

std::optional<std::string_view> UiRuntime::GetTextSnapshotDocument(std::uint64_t handle) const {
    const UINode* node = ResolveTextSnapshotNode(handle);
    if (node == nullptr) {
        return std::nullopt;
    }
    return std::string_view(node->text_content);
}

bool UiRuntime::RevealTextRange(std::uint64_t handle, std::uint32_t start, std::uint32_t end) {
    const UINode* node = ResolveTextSnapshotNode(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    const std::vector<Rect> scene_rects = GetTextRangeSceneRects(handle, clamped_start, clamped_end);
    if (scene_rects.empty()) {
        EnsureHandleVisible(handle);
        return true;
    }

    float scene_left = scene_rects.front().x;
    float scene_top = scene_rects.front().y;
    float scene_right = scene_rects.front().x + scene_rects.front().width;
    float scene_bottom = scene_rects.front().y + scene_rects.front().height;
    for (std::size_t index = 1; index < scene_rects.size(); index += 1U) {
        const Rect& rect = scene_rects[index];
        scene_left = std::min(scene_left, rect.x);
        scene_top = std::min(scene_top, rect.y);
        scene_right = std::max(scene_right, rect.x + rect.width);
        scene_bottom = std::max(scene_bottom, rect.y + rect.height);
    }

    float target_left = scene_left - node->abs_x;
    float target_top = scene_top - node->abs_y;
    float target_right = scene_right - node->abs_x;
    float target_bottom = scene_bottom - node->abs_y;
    std::uint64_t current_handle = handle;
    while (current_handle != UI_INVALID_HANDLE) {
        const UINode* current = Resolve(current_handle);
        if (current == nullptr || current->yg_node == nullptr) {
            break;
        }
        const std::uint64_t parent_handle = current->parent_handle;
        if (parent_handle == UI_INVALID_HANDLE) {
            break;
        }
        UINode* parent = ResolveMutable(parent_handle);
        if (parent == nullptr || parent->yg_node == nullptr) {
            break;
        }

        const float current_left = YGNodeLayoutGetLeft(current->yg_node);
        const float current_top = YGNodeLayoutGetTop(current->yg_node);
        target_left += current_left;
        target_right += current_left;
        target_top += current_top;
        target_bottom += current_top;

        if (parent->is_scroll_view) {
            EnsureRectVisibleWithinScrollAncestor(
                parent_handle,
                target_left - parent->scroll_offset_x,
                target_top - parent->scroll_offset_y,
                target_right - parent->scroll_offset_x,
                target_bottom - parent->scroll_offset_y,
                &target_left,
                &target_top,
                &target_right,
                &target_bottom);
        }
        current_handle = parent_handle;
    }

    return true;
}

bool UiRuntime::SetTextFindMatch(std::uint64_t handle, std::uint32_t start, std::uint32_t end) {
    const UINode* resolved = ResolveTextSnapshotNode(handle);
    if (resolved == nullptr) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(resolved->text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    if (clamped_start >= clamped_end) {
        return false;
    }

    const std::uint64_t previous_handle = text_find_handle_;
    text_find_handle_ = handle;
    text_find_start_ = clamped_start;
    text_find_end_ = clamped_end;

    if (previous_handle != UI_INVALID_HANDLE && previous_handle != handle) {
        if (UINode* previous = ResolveMutable(previous_handle); previous != nullptr) {
            MarkTextSelectionVisualsDirty(*previous);
        }
    }
    if (UINode* current = ResolveMutable(handle); current != nullptr) {
        MarkTextSelectionVisualsDirty(*current);
    }
    return true;
}

void UiRuntime::ClearTextFindMatch() {
    const std::uint64_t previous_handle = text_find_handle_;
    text_find_handle_ = UI_INVALID_HANDLE;
    text_find_start_ = 0U;
    text_find_end_ = 0U;
    if (previous_handle != UI_INVALID_HANDLE) {
        if (UINode* previous = ResolveMutable(previous_handle); previous != nullptr) {
            MarkTextSelectionVisualsDirty(*previous);
        }
    }
}

bool UiRuntime::PushTextFindHighlight(
    std::uint64_t handle,
    std::uint32_t start,
    std::uint32_t end,
    std::uint32_t color) {
    const UINode* resolved = ResolveTextSnapshotNode(handle);
    if (resolved == nullptr) {
        return false;
    }

    const std::uint32_t text_length = static_cast<std::uint32_t>(resolved->text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    if (clamped_start >= clamped_end) {
        return false;
    }

    text_find_highlights_.push_back(TextFindHighlight{
        handle,
        clamped_start,
        clamped_end,
        color,
    });
    if (UINode* node = ResolveMutable(handle); node != nullptr) {
        MarkTextSelectionVisualsDirty(*node);
    }
    return true;
}

void UiRuntime::ClearTextFindHighlights() {
    for (const TextFindHighlight& highlight : text_find_highlights_) {
        if (UINode* node = ResolveMutable(highlight.handle); node != nullptr) {
            MarkTextSelectionVisualsDirty(*node);
        }
    }
    text_find_highlights_.clear();
}

void UiRuntime::EnsureRectVisibleWithinScrollAncestor(
    std::uint64_t scroll_handle,
    float target_left,
    float target_top,
    float target_right,
    float target_bottom,
    float* adjusted_left,
    float* adjusted_top,
    float* adjusted_right,
    float* adjusted_bottom) {
    UINode* scroll_node = ResolveMutable(scroll_handle);
    if (scroll_node == nullptr || !scroll_node->is_scroll_view) {
        return;
    }

    float offset_x = scroll_node->scroll_offset_x;
    float offset_y = scroll_node->scroll_offset_y;
    const float viewport_width = GetScrollViewportWidth(*scroll_node);
    const float viewport_height = GetScrollViewportHeight(*scroll_node);
    if (target_left < 0.0f) {
        offset_x += target_left;
    } else if (target_right > viewport_width) {
        offset_x += target_right - viewport_width;
    }
    if (target_top < 0.0f) {
        offset_y += target_top;
    } else if (target_bottom > viewport_height) {
        offset_y += target_bottom - viewport_height;
    }

    const float delta_x = offset_x - scroll_node->scroll_offset_x;
    const float delta_y = offset_y - scroll_node->scroll_offset_y;
    ApplyScrollOffset(scroll_handle, *scroll_node, offset_x, offset_y, true);

    if (adjusted_left != nullptr) {
        *adjusted_left = target_left - delta_x;
    }
    if (adjusted_top != nullptr) {
        *adjusted_top = target_top - delta_y;
    }
    if (adjusted_right != nullptr) {
        *adjusted_right = target_right - delta_x;
    }
    if (adjusted_bottom != nullptr) {
        *adjusted_bottom = target_bottom - delta_y;
    }
}

void UiRuntime::UpdateAncestorScrollMetrics(std::uint64_t handle) {
    for (std::uint64_t current_handle = handle; current_handle != UI_INVALID_HANDLE;) {
        const UINode* current = Resolve(current_handle);
        if (current == nullptr) {
            break;
        }
        const std::uint64_t parent_handle = current->parent_handle;
        if (parent_handle == UI_INVALID_HANDLE) {
            break;
        }

        UINode* parent = ResolveMutable(parent_handle);
        if (parent == nullptr) {
            break;
        }
        if (parent->is_scroll_view) {
            UpdateScrollMetrics(parent_handle, *parent);
        }
        current_handle = parent_handle;
    }
}

const std::vector<std::uint32_t>& UiRuntime::command_buffer() const {
    return command_buffer_;
}

const std::vector<std::uint32_t>& UiRuntime::semantic_buffer() const {
    return semantic_buffer_;
}

const std::uint64_t& UiRuntime::root_handle() const {
    return root_handle_;
}

bool UiRuntime::HasPendingVisualWork() const {
    if (layout_dirty_ ||
        !pending_creations_.empty() ||
        !pending_deletions_.empty() ||
        pending_caret_visibility_handle_ != UI_INVALID_HANDLE ||
        auto_scroll_active_ ||
        !pending_text_scroll_metric_handles_.empty()) {
        return true;
    }

    for (std::uint64_t index = 1U; index < node_pool_.size(); index += 1U) {
        const UINode& node = node_pool_[index];
        if (!node.is_active) {
            continue;
        }
        if (node.needs_creation ||
            node.is_dirty ||
            node.text_glyphs_dirty ||
            node.text_selection_visuals_dirty ||
            node.scroll_offset_dirty ||
            node.has_pending_scroll_offset ||
            std::abs(node.scroll_velocity_x) >= 0.001f ||
            std::abs(node.scroll_velocity_y) >= 0.001f) {
            return true;
        }
    }

    return false;
}

bool UiRuntime::NeedsAnimationFrame() const {
    for (std::uint64_t index = 1U; index < node_pool_.size(); index += 1U) {
        const UINode& node = node_pool_[index];
        if (!node.is_active || !node.is_scroll_view) {
            continue;
        }
        if (std::abs(node.scroll_velocity_x) >= 0.001f || std::abs(node.scroll_velocity_y) >= 0.001f) {
            return true;
        }
    }

    return false;
}

bool UiRuntime::HasPointerAutoScroll() const {
    return primary_pointer_down_ && auto_scroll_active_;
}

float UiRuntime::window_width() const {
    return window_width_;
}

float UiRuntime::window_height() const {
    return window_height_;
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

void UiRuntime::WalkTree(
    std::uint64_t handle,
    float parent_abs_x,
    float parent_abs_y,
    float parent_scene_x,
    float parent_scene_y,
    bool inherited_scroll_dirty,
    CommandBuilder& builder,
    std::vector<std::uint64_t>& paint_order,
    std::vector<SceneInstruction>& scene,
    std::vector<std::uint64_t>& deferred_portal_roots) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return;
    }

    const float abs_x = parent_abs_x + YGNodeLayoutGetLeft(node->yg_node);
    const float abs_y = parent_abs_y + YGNodeLayoutGetTop(node->yg_node);
    const float scene_x = parent_scene_x + YGNodeLayoutGetLeft(node->yg_node);
    const float scene_y = parent_scene_y + YGNodeLayoutGetTop(node->yg_node);
    const float width = YGNodeLayoutGetWidth(node->yg_node);
    const float height = YGNodeLayoutGetHeight(node->yg_node);
    node->abs_x = abs_x;
    node->abs_y = abs_y;
    node->scene_x = scene_x;
    node->scene_y = scene_y;
    node->layout_width = width;
    node->layout_height = height;
    if (node->visibility != UI_VISIBILITY_NORMAL) {
        if (node->is_text_node) {
            node->text_render_window_valid = false;
            node->text_render_line_start = 0U;
            node->text_render_line_end = 0U;
            node->nonwrap_render_fragment_window_valid = false;
            node->nonwrap_render_fragment_start = 0U;
            node->nonwrap_render_fragment_end = 0U;
        }
        node->scroll_offset_dirty = false;
        return;
    }
    Rect visible_bounds{abs_x, abs_y, width, height};
    if (visible_bounds.width <= 0.0f || visible_bounds.height <= 0.0f) {
        visible_bounds.width = 0.0f;
        visible_bounds.height = 0.0f;
    } else {
        for (std::uint64_t current = node->parent_handle; current != UI_INVALID_HANDLE;) {
            const UINode* parent = Resolve(current);
            if (parent == nullptr) {
                break;
            }
            if ((parent->clip_to_bounds || parent->is_scroll_view) &&
                !IntersectRect(visible_bounds, ComputeClipBounds(*parent))) {
                break;
            }
            if (parent->is_portal) {
                break;
            }
            current = parent->parent_handle;
        }
    }
    const bool emit_layout_updates = layout_dirty_;
    const bool scroll_dirty = inherited_scroll_dirty || node->scroll_offset_dirty;
    Rect text_bounds{};
    ParagraphLayout text_paragraph{};
    bool text_render_window_visible = false;
    bool text_render_window_changed = false;
    std::size_t text_render_line_start = 0U;
    std::size_t text_render_line_end = 0U;
    bool nonwrap_fragment_window_visible = false;
    bool nonwrap_fragment_window_changed = false;
    NonWrappingFragmentWindow nonwrap_fragment_window{};
    if (node->is_text_node) {
        text_bounds = ComputeTextContentBounds(*node);
        text_paragraph =
            LayoutParagraph(*node, text_bounds.width > 0.0f ? std::optional<float>(text_bounds.width) : std::nullopt);
        const bool uses_internal_textbox_viewport =
            node->semantic_role == UI_SEMANTIC_TEXTBOX && node->max_lines == 1;
        if (!node->text_wrap && !uses_internal_textbox_viewport) {
            Rect nonwrap_visible_bounds{
                abs_x,
                abs_y,
                width + std::max(text_paragraph.width - text_bounds.width, 0.0f),
                height,
            };
            if (nonwrap_visible_bounds.width <= 0.0f || nonwrap_visible_bounds.height <= 0.0f) {
                nonwrap_visible_bounds.width = 0.0f;
                nonwrap_visible_bounds.height = 0.0f;
            } else {
                Rect window_clip{0.0f, 0.0f, window_width_, window_height_};
                (void)IntersectRect(nonwrap_visible_bounds, window_clip);
                for (std::uint64_t current = node->parent_handle; current != UI_INVALID_HANDLE;) {
                    const UINode* parent = Resolve(current);
                    if (parent == nullptr) {
                        break;
                    }
                    if ((parent->clip_to_bounds || parent->is_scroll_view) &&
                        !IntersectRect(nonwrap_visible_bounds, ComputeClipBounds(*parent))) {
                        break;
                    }
                    if (parent->is_portal) {
                        break;
                    }
                    current = parent->parent_handle;
                }
            }
            visible_bounds = nonwrap_visible_bounds;
        }
        const bool can_reuse_render_window_cache =
            !emit_layout_updates &&
            !scroll_dirty &&
            !node->text_glyphs_dirty &&
            node->text_layout_cache_valid;
        if (can_reuse_render_window_cache) {
            text_render_window_visible = node->text_render_window_valid;
            text_render_line_start = node->text_render_line_start;
            text_render_line_end = node->text_render_line_end;
            nonwrap_fragment_window_visible = node->nonwrap_render_fragment_window_valid;
            nonwrap_fragment_window.start = node->nonwrap_render_fragment_start;
            nonwrap_fragment_window.end = node->nonwrap_render_fragment_end;
        } else {
            const std::size_t line_count = text_paragraph.visible_line_count;
            if (line_count > 0U && visible_bounds.width > 0.0f && visible_bounds.height > 0.0f) {
                if (text_paragraph.height <= 0.0f) {
                    text_render_line_end = line_count;
                    text_render_window_visible = true;
                } else {
                    const float content_offset_y = GetAlignedTextYOffset(*node, text_paragraph.height);
                    const float local_visible_top = visible_bounds.y - abs_y;
                    const float local_visible_bottom = (visible_bounds.y + visible_bounds.height) - abs_y;
                    const float relative_top = local_visible_top - content_offset_y;
                    const float relative_bottom = local_visible_bottom - content_offset_y;
                    if (relative_bottom > 0.0f && relative_top < text_paragraph.height) {
                        text_render_line_start =
                            LineIndexForYOffset(*node, std::max(relative_top, 0.0f), line_count);
                        const auto end_it = std::lower_bound(
                            node->line_y_offsets.begin(),
                            node->line_y_offsets.begin() + static_cast<std::ptrdiff_t>(line_count + 1U),
                            std::max(relative_bottom, 0.0f));
                        text_render_line_end = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                            std::distance(node->line_y_offsets.begin(), end_it),
                            0,
                            static_cast<std::ptrdiff_t>(line_count)));
                        if (text_render_line_end > text_render_line_start) {
                            text_render_window_visible = true;
                        }
                    }
                }
            }
            const bool single_line_nonwrap_fragment_window =
                text_render_window_visible &&
                !node->text_wrap &&
                !uses_internal_textbox_viewport &&
                text_paragraph.total_line_count == 1U &&
                node->nonwrap_fragment_cache_valid &&
                !text_paragraph.line_widths.empty();
            const bool multiline_nonwrap_fragment_window =
                text_render_window_visible &&
                !node->text_wrap &&
                !uses_internal_textbox_viewport &&
                text_paragraph.total_line_count > 1U &&
                node->nonwrap_fragment_cache_valid;
            if (single_line_nonwrap_fragment_window) {
                const float x_offset = GetAlignedLineXOffset(*node, text_paragraph.line_widths.front());
                nonwrap_fragment_window = ResolveNonWrappingFragmentWindow(
                    *node,
                    0U,
                    (visible_bounds.x - abs_x) - x_offset,
                    ((visible_bounds.x + visible_bounds.width) - abs_x) - x_offset);
                nonwrap_fragment_window_visible = nonwrap_fragment_window.start < nonwrap_fragment_window.end;
            }
            text_render_window_changed =
                node->text_render_window_valid != text_render_window_visible ||
                (text_render_window_visible &&
                 (node->text_render_line_start != text_render_line_start ||
                  node->text_render_line_end != text_render_line_end));
            nonwrap_fragment_window_changed =
                node->nonwrap_render_fragment_window_valid != nonwrap_fragment_window_visible ||
                (nonwrap_fragment_window_visible &&
                 (node->nonwrap_render_fragment_start != nonwrap_fragment_window.start ||
                  node->nonwrap_render_fragment_end != nonwrap_fragment_window.end));
            if (multiline_nonwrap_fragment_window && scroll_dirty) {
                nonwrap_fragment_window_changed = true;
            }
        }
    }
    const bool needs_content_update =
        node->is_text_node
        ? (emit_layout_updates ||
           node->is_dirty ||
           (scroll_dirty && (text_render_window_changed || nonwrap_fragment_window_changed)))
        : (emit_layout_updates || node->is_dirty);
    const bool selection_visuals_only_update =
        node->is_text_node &&
        node->text_selection_visuals_dirty &&
        !node->text_glyphs_dirty &&
        !emit_layout_updates &&
        !text_render_window_changed &&
        !nonwrap_fragment_window_changed;
    const bool needs_bounds_update =
        (needs_content_update && !selection_visuals_only_update) ||
        (scroll_dirty && node->is_interactive);

    if (auto_scroll_active_ && active_selection_handle_ == handle && node->is_text_node && node->is_selectable) {
        const auto [clamped_x, clamped_y] = ClampPointToScrollViewport(handle, last_pointer_logical_x_, last_pointer_logical_y_);
        const std::uint32_t next_selection_end = GetStringIndexFromPoint(
            *node,
            clamped_x - abs_x,
            clamped_y - abs_y);
        if (node->selection_end != next_selection_end) {
            node->selection_end = next_selection_end;
            as_on_selection_changed(
                handle,
                node->selection_start,
                node->selection_end);
        }
    } else if (
        auto_scroll_active_ &&
        cross_selection_active_ &&
        node->is_text_node &&
        node->is_selectable &&
        FindSelectionAreaAncestor(handle) == selection_area_handle_) {
        const auto [clamped_x, clamped_y] = ClampPointToScrollViewport(
            end_node_handle_ != UI_INVALID_HANDLE ? end_node_handle_ : handle,
            last_pointer_logical_x_,
            last_pointer_logical_y_);
        if (clamped_x >= abs_x &&
            clamped_x <= (abs_x + width) &&
            clamped_y >= abs_y &&
            clamped_y <= (abs_y + height)) {
            (void)UpdateCrossSelectionEndpoint(handle, clamped_x, clamped_y);
        }
    }

    if (visible_bounds.width <= 0.0f || visible_bounds.height <= 0.0f) {
        if (node->is_text_node) {
            node->text_render_window_valid = false;
            node->text_render_line_start = 0U;
            node->text_render_line_end = 0U;
            node->nonwrap_render_fragment_window_valid = false;
            node->nonwrap_render_fragment_start = 0U;
            node->nonwrap_render_fragment_end = 0U;
        }
        paint_order.push_back(handle);
        if (node->is_interactive) {
            const Rect clip_bounds = (node->clip_to_bounds || node->is_scroll_view)
                ? ComputeClipBounds(*node, scene_x, scene_y)
                : Rect{scene_x, scene_y, width, height};
            builder.SetBounds(
                handle,
                scene_x,
                scene_y,
                width,
                height,
                abs_x,
                abs_y,
                0.0f,
                0.0f,
                clip_bounds.x,
                clip_bounds.y,
                clip_bounds.width,
                clip_bounds.height,
                true,
                ComputeClipMode(*node));
        }
        if (node->is_portal) {
            deferred_portal_roots.push_back(handle);
            node->scroll_offset_dirty = false;
            return;
        }
        if (!node->is_portal) {
            const float child_origin_x = abs_x - (node->is_scroll_view ? node->scroll_offset_x : 0.0f);
            const float child_origin_y = abs_y - (node->is_scroll_view ? node->scroll_offset_y : 0.0f);
            for (const std::uint64_t child_handle : node->children) {
                ClearCulledSubtree(child_handle, child_origin_x, child_origin_y, scene_x, scene_y, builder, paint_order);
            }
        }
        node->scroll_offset_dirty = false;
        return;
    }

    if (needs_bounds_update) {
        const Rect clip_bounds = (node->clip_to_bounds || node->is_scroll_view)
            ? ComputeClipBounds(*node, scene_x, scene_y)
            : Rect{scene_x, scene_y, width, height};
        builder.SetBounds(
            handle,
            scene_x,
            scene_y,
            width,
            height,
            visible_bounds.x,
            visible_bounds.y,
            visible_bounds.width,
            visible_bounds.height,
            clip_bounds.x,
            clip_bounds.y,
            clip_bounds.width,
            clip_bounds.height,
            node->is_interactive,
            ComputeClipMode(*node));
    }
    if (needs_content_update) {
        if (!selection_visuals_only_update) {
            if (node->has_box_style || node->bg_color != 0U) {
                builder.SetBoxStyle(
                    handle,
                    node->bg_color,
                    node->corner_radius_tl,
                    node->corner_radius_tr,
                    node->corner_radius_br,
                    node->corner_radius_bl,
                    node->border_width,
                    node->border_color,
                    node->border_style,
                    node->border_dash_on,
                    node->border_dash_off);
            }
            if (node->has_layer_effect) {
                builder.SetLayerEffect(handle, node->opacity, node->blur_sigma, node->blend_mode);
            }
            if (node->has_drop_shadow) {
                builder.SetDropShadow(
                    handle,
                    node->drop_shadow_color,
                    node->drop_shadow_offset_x,
                    node->drop_shadow_offset_y,
                    node->drop_shadow_blur_sigma,
                    node->drop_shadow_spread);
            }
            if (node->has_background_blur) {
                builder.SetBackgroundBlur(handle, node->background_blur_sigma);
            }
            if (node->has_linear_gradient && !node->gradient_stops.empty()) {
                builder.SetLinearGradient(
                    handle,
                    node->gradient_start_x,
                    node->gradient_start_y,
                    node->gradient_end_x,
                    node->gradient_end_y,
                    node->gradient_stops);
            }
            if (node->has_image) {
                builder.SetImage(handle, node->texture_id, node->object_fit);
            }
            if (node->has_image_nine) {
                builder.SetImageNine(
                    handle,
                    node->image_nine_texture_id,
                    node->image_nine_inset_left,
                    node->image_nine_inset_top,
                    node->image_nine_inset_right,
                    node->image_nine_inset_bottom);
            }
            if (node->is_svg_node || node->has_svg) {
                builder.SetSvg(handle, node->has_svg ? node->svg_id : 0U, node->svg_tint_color);
            }
        }

        if (node->is_text_node) {
            const ParagraphLayout& paragraph = text_paragraph;
            const bool uses_internal_textbox_viewport =
                node->semantic_role == UI_SEMANTIC_TEXTBOX && node->max_lines == 1;
            const float text_offset_y = GetAlignedTextYOffset(*node, paragraph.height);
            if (!selection_visuals_only_update) {
                std::vector<GlyphPlacement> glyphs{};
                const bool use_nonwrap_fragment_culling =
                    !node->text_wrap &&
                    !uses_internal_textbox_viewport &&
                    node->nonwrap_fragment_cache_valid;
                for (std::size_t line_index = text_render_line_start; line_index < text_render_line_end; line_index += 1U) {
                    const std::int32_t start = paragraph.break_offsets[line_index];
                    const std::int32_t end = paragraph.break_offsets[line_index + 1U];
                    std::uint32_t line_start = static_cast<std::uint32_t>(start);
                    std::string_view line_text(
                        node->text_content.data() + line_start,
                        static_cast<std::size_t>(end - start));
                    while (!line_text.empty() && (line_text.front() == '\n' || line_text.front() == '\r')) {
                        line_text.remove_prefix(1U);
                        line_start += 1U;
                    }
                    if (line_text.empty()) {
                        continue;
                    }
                    const float full_line_width =
                        line_index < paragraph.line_widths.size() ? paragraph.line_widths[line_index] : 0.0f;
                    const float x_offset = GetAlignedLineXOffset(*node, full_line_width);
                    const float line_top = GetLineTopForIndex(*node, line_index);
                    const float line_ascent = GetLineAscentForIndex(*node, line_index);
                    const float line_y = text_offset_y + line_top;
                    ShapedTextRun shaped{};
                    float fragment_x_offset = 0.0f;
                    std::optional<FragmentGeometrySlice> cached_slice{};
                    const CachedVisualLineShape* cached_visual_line =
                        !use_nonwrap_fragment_culling &&
                        node->visual_line_shape_cache_valid &&
                        line_index < node->visual_line_shapes.size()
                        ? EnsureWrappedVisualLineShape(*node, line_index)
                        : nullptr;
                    if (use_nonwrap_fragment_culling) {
                        const NonWrappingFragmentWindow fragment_window = ResolveNonWrappingFragmentWindow(
                            *node,
                            line_index,
                            (visible_bounds.x - abs_x) - x_offset,
                            ((visible_bounds.x + visible_bounds.width) - abs_x) - x_offset);
                        if (fragment_window.start == fragment_window.end) {
                            continue;
                        }
                        const NonWrappingTextFragment& first_fragment = node->nonwrap_fragments[fragment_window.start];
                        const NonWrappingTextFragment& last_fragment = node->nonwrap_fragments[fragment_window.end - 1U];
                        const std::uint32_t fragment_start =
                            GetNonWrapFragmentAbsoluteStart(*node, line_index, first_fragment);
                        const std::uint32_t fragment_end =
                            GetNonWrapFragmentAbsoluteEnd(*node, line_index, last_fragment);
                        if (fragment_end <= fragment_start || fragment_start < line_start) {
                            continue;
                        }
                        const std::size_t local_fragment_start = static_cast<std::size_t>(fragment_start - line_start);
                        const std::size_t local_fragment_end = static_cast<std::size_t>(fragment_end - line_start);
                        if (local_fragment_end > line_text.size() || local_fragment_start >= local_fragment_end) {
                            continue;
                        }
                        FragmentGeometrySlice slice{};
                        if (TryBuildFragmentGeometrySliceFromLogicalLineShape(*node, line_index, fragment_start, fragment_end, slice)) {
                            shaped = slice.shaped;
                            fragment_x_offset = slice.slice_x;
                            cached_slice = std::move(slice);
                        } else {
                            if (!ShapeText(
                                    line_text.substr(local_fragment_start, local_fragment_end - local_fragment_start),
                                    node->font_id,
                                    node->font_size,
                                    shaped,
                                    node->is_obscured)) {
                                continue;
                            }
                            fragment_x_offset = first_fragment.x;
                            FragmentGeometrySlice fallback_slice{};
                            fallback_slice.line_start = line_start;
                            fallback_slice.line_end = static_cast<std::uint32_t>(end);
                            fallback_slice.slice_start = fragment_start;
                            fallback_slice.slice_end = fragment_end;
                            fallback_slice.slice_x = first_fragment.x;
                            fallback_slice.full_line_width = full_line_width;
                            fallback_slice.shaped = shaped;
                            fallback_slice.cluster_stops = BuildTextClusterStops(
                                shaped.glyphs,
                                shaped.width,
                                local_fragment_end - local_fragment_start);
                            cached_slice = std::move(fallback_slice);
                        }
                    } else if (cached_visual_line != nullptr) {
                        shaped.font_id = node->font_id;
                        shaped.width = cached_visual_line->width;
                        shaped.height = cached_visual_line->height;
                        shaped.baseline = cached_visual_line->baseline;
                        shaped.ascent = cached_visual_line->ascent;
                        shaped.descent = cached_visual_line->descent;
                        shaped.glyphs = cached_visual_line->glyphs;
                    } else if (!ShapeTextStyledRange(
                                   *node,
                                   static_cast<std::uint32_t>(paragraph.break_offsets[line_index]),
                                   static_cast<std::uint32_t>(paragraph.break_offsets[line_index + 1U]),
                                   shaped)) {
                        continue;
                    }

                    if (cached_slice.has_value()) {
                        StoreCachedNonWrapGeometrySlice(*node, line_index, *cached_slice);
                    }

                    const float viewport_offset_x =
                        use_nonwrap_fragment_culling
                        ? 0.0f
                        : (cached_visual_line != nullptr
                               ? 0.0f
                               : GetTextboxViewportOffsetX(
                                     *node,
                                     shaped,
                                     static_cast<std::uint32_t>(paragraph.break_offsets[line_index]),
                                     static_cast<std::uint32_t>(paragraph.break_offsets[line_index + 1U])));
                    for (const GlyphPlacement& glyph : shaped.glyphs) {
                        glyphs.push_back(GlyphPlacement{
                            glyph.glyph_id,
                            x_offset - viewport_offset_x + fragment_x_offset + glyph.x,
                            line_y + line_ascent - glyph.y,
                            glyph.cluster,
                            glyph.font_id,
                            glyph.color != 0U ? glyph.color : node->text_color,
                        });
                    }
                }
                node->text_render_window_valid = text_render_window_visible;
                node->text_render_line_start = text_render_line_start;
                node->text_render_line_end = text_render_line_end;
                node->nonwrap_render_fragment_window_valid = nonwrap_fragment_window_visible;
                node->nonwrap_render_fragment_start = nonwrap_fragment_window.start;
                node->nonwrap_render_fragment_end = nonwrap_fragment_window.end;
                const bool has_per_glyph_color = std::any_of(
                    glyphs.begin(),
                    glyphs.end(),
                    [node](const GlyphPlacement& glyph) {
                        return glyph.color != 0U && glyph.color != node->text_color;
                    });
                if (has_per_glyph_color) {
                    builder.SetGlyphRunColored(handle, node->font_id, node->font_size, glyphs);
                } else {
                    builder.SetGlyphRun(handle, node->font_id, node->font_size, node->text_color, glyphs);
                }
            }
            builder.SetCaret(handle, scene_x, scene_y, 0.0f, 0U, 0U);
            const std::vector<ColoredRect> background_rects = BuildStyleInlineRects(*node);
            const bool has_find_highlight =
                text_find_handle_ == handle &&
                text_find_start_ < text_find_end_;
            std::vector<ColoredRect> find_highlights{};
            if (has_find_highlight) {
                for (const Rect& highlight : BuildSelectionRects(*node, text_find_start_, text_find_end_)) {
                    find_highlights.push_back(ColoredRect{highlight, text_find_color_});
                }
            }
            for (const TextFindHighlight& highlight : text_find_highlights_) {
                if (highlight.handle != handle || highlight.start >= highlight.end) {
                    continue;
                }
                for (const Rect& rect : BuildSelectionRects(*node, highlight.start, highlight.end)) {
                    find_highlights.push_back(ColoredRect{rect, highlight.color});
                }
            }
            auto emit_highlights =
                [&](const std::vector<Rect>& selection_rects) {
                    if (background_rects.empty() &&
                        selection_rects.empty() &&
                        !find_highlights.empty()) {
                        builder.SetHighlightsColored(handle, find_highlights);
                    } else if (!background_rects.empty() || !find_highlights.empty()) {
                        std::vector<ColoredRect> combined_highlights{};
                        combined_highlights.reserve(
                            background_rects.size() +
                            selection_rects.size() +
                            find_highlights.size());
                        combined_highlights.insert(combined_highlights.end(), background_rects.begin(), background_rects.end());
                        for (const Rect& highlight : selection_rects) {
                            combined_highlights.push_back(ColoredRect{highlight, node->selection_color});
                        }
                        combined_highlights.insert(combined_highlights.end(), find_highlights.begin(), find_highlights.end());
                        builder.SetHighlightsColored(handle, combined_highlights);
                    } else {
                        builder.SetHighlights(handle, node->selection_color, selection_rects);
                    }
                };
            if (node->is_selectable || node->is_editable || has_find_highlight) {
                const std::uint32_t caret_index =
                    std::min<std::uint32_t>(node->selection_end, static_cast<std::uint32_t>(node->text_content.size()));
                std::uint32_t highlight_start = node->selection_start;
                std::uint32_t highlight_end = node->selection_end;
                const bool can_emit_selection_highlights = node->is_selectable || node->is_editable;
                const bool has_cross_highlight =
                    can_emit_selection_highlights &&
                    GetCrossSelectionHighlight(handle, highlight_start, highlight_end);
                const std::vector<Rect> highlights =
                    (can_emit_selection_highlights &&
                     (has_cross_highlight || node->selection_start != node->selection_end))
                    ? BuildSelectionRects(*node, highlight_start, highlight_end)
                    : std::vector<Rect>{};
                for (const Rect& highlight : highlights) {
                    Rect hit_rect{
                        scene_x + highlight.x,
                        scene_y + highlight.y,
                        highlight.width,
                        highlight.height,
                    };
                    if (IntersectRect(hit_rect, visible_bounds)) {
                        current_selection_hit_rects_.push_back(hit_rect);
                    }
                }
                emit_highlights(highlights);
                if (
                    can_emit_selection_highlights &&
                    highlights.empty() &&
                    focused_handle_ == handle &&
                    !has_cross_highlight &&
                    (node->is_editable || node->semantic_role == UI_SEMANTIC_TEXTBOX)) {
                    const auto [local_x, line_index] = GetLocalPositionFromIndex(*node, caret_index);
                    const std::size_t caret_line_index = static_cast<std::size_t>(line_index);
                    const bool uses_fragment_geometry =
                        !node->text_wrap &&
                        !(node->semantic_role == UI_SEMANTIC_TEXTBOX && node->max_lines == 1) &&
                        node->nonwrap_fragment_cache_valid;
                    float caret_height = GetLineHeightForIndex(*node, caret_line_index);
                    if (!uses_fragment_geometry) {
                        if (node->visual_line_shape_cache_valid &&
                            caret_line_index < node->visual_line_shapes.size()) {
                            const CachedVisualLineShape* caret_visual_line =
                                EnsureWrappedVisualLineShape(*node, caret_line_index);
                            caret_height = caret_visual_line != nullptr
                                ? caret_visual_line->height
                                : node->visual_line_shapes[caret_line_index].height;
                        } else {
                            ShapedTextRun caret_line{};
                            if (caret_line_index < paragraph.break_offsets.size() - 1U) {
                                const std::int32_t start = paragraph.break_offsets[caret_line_index];
                                const std::int32_t end = paragraph.break_offsets[caret_line_index + 1U];
                                std::string_view line_text(
                                    node->text_content.data() + start,
                                    static_cast<std::size_t>(end - start));
                                while (!line_text.empty() && (line_text.front() == '\n' || line_text.front() == '\r')) {
                                    line_text.remove_prefix(1U);
                                }
                                (void)ShapeText(line_text, node->font_id, node->font_size, caret_line, node->is_obscured);
                            }
                            caret_height = std::max(caret_line.height, GetLineHeightForIndex(*node, caret_line_index));
                        }
                    }
                    const float caret_line_height = GetLineHeightForIndex(*node, caret_line_index);
                    const float caret_line_ascent = GetLineAscentForIndex(*node, caret_line_index);
                    const float caret_line_top = GetLineTopForIndex(*node, caret_line_index);
                    float caret_ascent = caret_line_ascent;
                    if (!uses_fragment_geometry) {
                        if (node->visual_line_shape_cache_valid &&
                            caret_line_index < node->visual_line_shapes.size()) {
                            const CachedVisualLineShape* caret_visual_line =
                                EnsureWrappedVisualLineShape(*node, caret_line_index);
                            caret_ascent = caret_visual_line != nullptr
                                ? caret_visual_line->ascent
                                : node->visual_line_shapes[caret_line_index].ascent;
                        } else {
                            ShapedTextRun caret_line{};
                            if (caret_line_index < paragraph.break_offsets.size() - 1U) {
                                const std::int32_t start = paragraph.break_offsets[caret_line_index];
                                const std::int32_t end = paragraph.break_offsets[caret_line_index + 1U];
                                std::string_view line_text(
                                    node->text_content.data() + start,
                                    static_cast<std::size_t>(end - start));
                                while (!line_text.empty() && (line_text.front() == '\n' || line_text.front() == '\r')) {
                                    line_text.remove_prefix(1U);
                                }
                                (void)ShapeText(line_text, node->font_id, node->font_size, caret_line, node->is_obscured);
                            }
                            if (caret_line.height >= caret_line_height) {
                                caret_ascent = caret_line.ascent;
                            }
                        }
                    }
                    builder.SetCaret(
                        handle,
                        scene_x + local_x,
                        scene_y + text_offset_y + caret_line_top + (caret_line_ascent - caret_ascent),
                        std::max(caret_height, 1.0f),
                        node->caret_color,
                        static_cast<std::uint32_t>(std::min<std::uint64_t>(node->last_interaction_time, 0xFFFFFFFFULL)));
                }
            } else {
                emit_highlights(std::vector<Rect>{});
            }
            if (!selection_visuals_only_update) {
                builder.SetTextFade(handle, ResolveTextFadeMask(*node, paragraph));
            }
            node->text_glyphs_dirty = false;
            node->text_selection_visuals_dirty = false;
        }
        node->is_dirty = false;
    }

    paint_order.push_back(handle);
    scene.push_back(SceneInstruction{OP_DRAW_NODE, handle});
    const bool clip_children = node->clip_to_bounds || node->is_scroll_view;

    if (node->is_portal) {
        deferred_portal_roots.push_back(handle);
        return;
    }

    if (node->is_grid) {
        if (clip_children) {
            scene.push_back(SceneInstruction{OP_PUSH_CLIP, handle});
        }
        LayoutGrid(
            handle,
            *node,
            abs_x,
            abs_y,
            scene_x,
            scene_y,
            scroll_dirty,
            builder,
            paint_order,
            scene,
            deferred_portal_roots);
        if (clip_children) {
            scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
        }
        node->scroll_offset_dirty = false;
        return;
    }

    const float child_origin_x = abs_x - (node->is_scroll_view ? node->scroll_offset_x : 0.0f);
    const float child_origin_y = abs_y - (node->is_scroll_view ? node->scroll_offset_y : 0.0f);
    const float child_scene_x = scene_x;
    const float child_scene_y = scene_y;
    if (clip_children) {
        scene.push_back(SceneInstruction{OP_PUSH_CLIP, handle});
    }
    if (node->is_scroll_view &&
        (std::abs(node->scroll_offset_x) >= 0.001f || std::abs(node->scroll_offset_y) >= 0.001f)) {
        scene.push_back(SceneInstruction{
            OP_PUSH_TRANSLATE,
            handle,
            -node->scroll_offset_x,
            -node->scroll_offset_y,
        });
    }
    for (const std::uint64_t child_handle : node->children) {
        WalkTree(
            child_handle,
            child_origin_x,
            child_origin_y,
            child_scene_x,
            child_scene_y,
            scroll_dirty,
            builder,
            paint_order,
            scene,
            deferred_portal_roots);
    }
    if (node->is_scroll_view &&
        (std::abs(node->scroll_offset_x) >= 0.001f || std::abs(node->scroll_offset_y) >= 0.001f)) {
        scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
    }
    if (clip_children) {
        scene.push_back(SceneInstruction{OP_POP, UI_INVALID_HANDLE});
    }
    node->scroll_offset_dirty = false;
}

void UiRuntime::ClearCulledSubtree(
    std::uint64_t handle,
    float parent_abs_x,
    float parent_abs_y,
    float parent_scene_x,
    float parent_scene_y,
    CommandBuilder& builder,
    std::vector<std::uint64_t>& paint_order) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || node->yg_node == nullptr) {
        return;
    }

    const float abs_x = parent_abs_x + YGNodeLayoutGetLeft(node->yg_node);
    const float abs_y = parent_abs_y + YGNodeLayoutGetTop(node->yg_node);
    const float scene_x = parent_scene_x + YGNodeLayoutGetLeft(node->yg_node);
    const float scene_y = parent_scene_y + YGNodeLayoutGetTop(node->yg_node);
    const float width = YGNodeLayoutGetWidth(node->yg_node);
    const float height = YGNodeLayoutGetHeight(node->yg_node);
    node->abs_x = abs_x;
    node->abs_y = abs_y;
    node->scene_x = scene_x;
    node->scene_y = scene_y;
    node->layout_width = width;
    node->layout_height = height;
    if (node->visibility != UI_VISIBILITY_NORMAL) {
        node->scroll_offset_dirty = false;
        return;
    }
    paint_order.push_back(handle);

    if (node->is_interactive) {
        const Rect clip_bounds = (node->clip_to_bounds || node->is_scroll_view)
            ? ComputeClipBounds(*node, scene_x, scene_y)
            : Rect{scene_x, scene_y, width, height};
        builder.SetBounds(
            handle,
            scene_x,
            scene_y,
            width,
            height,
            abs_x,
            abs_y,
            0.0f,
            0.0f,
            clip_bounds.x,
            clip_bounds.y,
            clip_bounds.width,
            clip_bounds.height,
            true);
    }

    if (node->is_portal) {
        node->scroll_offset_dirty = false;
        return;
    }

    const float child_origin_x = abs_x - (node->is_scroll_view ? node->scroll_offset_x : 0.0f);
    const float child_origin_y = abs_y - (node->is_scroll_view ? node->scroll_offset_y : 0.0f);
    for (const std::uint64_t child_handle : node->children) {
        ClearCulledSubtree(child_handle, child_origin_x, child_origin_y, scene_x, scene_y, builder, paint_order);
    }
    node->scroll_offset_dirty = false;
}

UINode* UiRuntime::ResolveMutable(std::uint64_t handle) {
    const HandleParts parts = DecodeHandle(handle);
    if (parts.index == 0 || parts.index >= node_pool_.size()) {
        return nullptr;
    }
    UINode& node = node_pool_[parts.index];
    return (!node.is_active || node.generation != parts.generation) ? nullptr : &node;
}

UiRuntime& GetRuntime() {
    static UiRuntime runtime;
    return runtime;
}

} // namespace effindom::v2::ui
