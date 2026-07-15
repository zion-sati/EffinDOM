#include "UiDebugTreeProjector.h"

#include "CommandBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace effindom::v2::ui {

namespace {

class DebugTreeBufferBuilder {
public:
    static constexpr std::uint32_t kMagic = 0x44544231U; // DTB1
    static constexpr std::uint32_t kVersion = 1U;
    static constexpr std::uint32_t kFixedRecordWords = 52U;

    enum NodeFlags : std::uint32_t {
        IS_ACTIVE = 1U << 0U,
        IS_VISIBLE_NORMAL = 1U << 1U,
        CLIP_TO_BOUNDS = 1U << 2U,
        IS_CLIPPED_OR_EMPTY = 1U << 3U,
        HAS_NODE_ID = 1U << 4U,
        HAS_SEMANTIC_LABEL = 1U << 5U,
        HAS_BOX_STYLE = 1U << 6U,
        HAS_LAYER_EFFECT = 1U << 7U,
        HAS_DROP_SHADOW = 1U << 8U,
        HAS_BACKGROUND_BLUR = 1U << 9U,
        HAS_LINEAR_GRADIENT = 1U << 10U,
        HAS_IMAGE = 1U << 11U,
        HAS_IMAGE_NINE = 1U << 12U,
        HAS_SVG = 1U << 13U,
        HAS_TEXT_STYLE_RUNS = 1U << 14U,
    };

    enum BehaviorFlags : std::uint32_t {
        IS_INTERACTIVE = 1U << 0U,
        IS_FOCUSABLE = 1U << 1U,
        IS_SELECTABLE = 1U << 2U,
        IS_EDITABLE = 1U << 3U,
        IS_PORTAL = 1U << 4U,
        IS_SCROLL_VIEW = 1U << 5U,
        IS_GRID = 1U << 6U,
        IS_SELECTION_AREA = 1U << 7U,
        IS_SELECTION_AREA_BARRIER = 1U << 8U,
        IS_CUSTOM_DRAWABLE = 1U << 9U,
        SCROLL_ENABLED_X = 1U << 10U,
        SCROLL_ENABLED_Y = 1U << 11U,
        IS_TEXT_NODE = 1U << 13U,
        IS_SVG_NODE = 1U << 14U,
        EDITOR_COMMAND_KEYS = 1U << 15U,
        EDITOR_ACCEPTS_TAB = 1U << 16U,
        IS_TEXT_EDITOR = 1U << 17U,
    };

    explicit DebugTreeBufferBuilder(std::vector<std::uint32_t>& words)
        : words_(words) {}

    void AddWord(std::uint32_t value) { words_.push_back(value); }
    void AddFloat(float value) { words_.push_back(CommandBuilder::FloatToWord(value)); }
    void AddHandle(std::uint64_t handle) {
        words_.push_back(static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL));
        words_.push_back(static_cast<std::uint32_t>(handle >> 32U));
    }
    void AddString(std::string_view value) {
        words_.push_back(static_cast<std::uint32_t>(value.size()));
        const std::size_t word_count = (value.size() + 3U) / 4U;
        const std::size_t start = words_.size();
        words_.resize(start + word_count, 0U);
        if (!value.empty()) {
            std::memcpy(words_.data() + start, value.data(), value.size());
        }
    }
    void FinishRecord() { words_[3] += 1U; }

private:
    std::vector<std::uint32_t>& words_;
};

struct Insets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

float ReadLayoutValue(const UINode& node, YGEdge edge, bool padding) {
    if (node.yg_node == nullptr) {
        return 0.0f;
    }
    return std::max(0.0f, padding
        ? YGNodeLayoutGetPadding(node.yg_node, edge)
        : YGNodeLayoutGetBorder(node.yg_node, edge));
}

Insets ReadInsets(const UINode& node, bool padding) {
    return Insets{
        ReadLayoutValue(node, YGEdgeLeft, padding),
        ReadLayoutValue(node, YGEdgeTop, padding),
        ReadLayoutValue(node, YGEdgeRight, padding),
        ReadLayoutValue(node, YGEdgeBottom, padding),
    };
}

float ReadMargin(const UINode& node, YGEdge edge) {
    return node.yg_node == nullptr ? 0.0f : YGNodeLayoutGetMargin(node.yg_node, edge);
}

std::uint32_t NodeFlags(const UINode& node, bool clipped_or_empty, std::string_view semantic_label) {
    std::uint32_t flags = DebugTreeBufferBuilder::IS_ACTIVE;
    if (node.visibility == UI_VISIBILITY_NORMAL) flags |= DebugTreeBufferBuilder::IS_VISIBLE_NORMAL;
    if (node.clip_to_bounds) flags |= DebugTreeBufferBuilder::CLIP_TO_BOUNDS;
    if (clipped_or_empty) flags |= DebugTreeBufferBuilder::IS_CLIPPED_OR_EMPTY;
    if (!node.node_id.empty()) flags |= DebugTreeBufferBuilder::HAS_NODE_ID;
    if (!semantic_label.empty()) flags |= DebugTreeBufferBuilder::HAS_SEMANTIC_LABEL;
    if (node.has_box_style) flags |= DebugTreeBufferBuilder::HAS_BOX_STYLE;
    if (node.has_layer_effect) flags |= DebugTreeBufferBuilder::HAS_LAYER_EFFECT;
    if (node.has_drop_shadow) flags |= DebugTreeBufferBuilder::HAS_DROP_SHADOW;
    if (node.has_background_blur) flags |= DebugTreeBufferBuilder::HAS_BACKGROUND_BLUR;
    if (node.has_linear_gradient) flags |= DebugTreeBufferBuilder::HAS_LINEAR_GRADIENT;
    if (node.has_image) flags |= DebugTreeBufferBuilder::HAS_IMAGE;
    if (node.has_image_nine) flags |= DebugTreeBufferBuilder::HAS_IMAGE_NINE;
    if (node.has_svg) flags |= DebugTreeBufferBuilder::HAS_SVG;
    if (node.has_text_style_runs) flags |= DebugTreeBufferBuilder::HAS_TEXT_STYLE_RUNS;
    return flags;
}

std::uint32_t BehaviorFlags(const UINode& node) {
    std::uint32_t flags = 0U;
    if (node.is_interactive) flags |= DebugTreeBufferBuilder::IS_INTERACTIVE;
    if (node.is_focusable) flags |= DebugTreeBufferBuilder::IS_FOCUSABLE;
    if (node.is_selectable) flags |= DebugTreeBufferBuilder::IS_SELECTABLE;
    if (node.is_editable) flags |= DebugTreeBufferBuilder::IS_EDITABLE;
    if (node.is_text_editor) flags |= DebugTreeBufferBuilder::IS_TEXT_EDITOR;
    if (node.is_portal) flags |= DebugTreeBufferBuilder::IS_PORTAL;
    if (node.is_scroll_view) flags |= DebugTreeBufferBuilder::IS_SCROLL_VIEW;
    if (node.is_grid) flags |= DebugTreeBufferBuilder::IS_GRID;
    if (node.is_selection_area) flags |= DebugTreeBufferBuilder::IS_SELECTION_AREA;
    if (node.is_selection_area_barrier) flags |= DebugTreeBufferBuilder::IS_SELECTION_AREA_BARRIER;
    if (node.is_custom_drawable) flags |= DebugTreeBufferBuilder::IS_CUSTOM_DRAWABLE;
    if (node.scroll_enabled_x) flags |= DebugTreeBufferBuilder::SCROLL_ENABLED_X;
    if (node.scroll_enabled_y) flags |= DebugTreeBufferBuilder::SCROLL_ENABLED_Y;
    if (node.is_text_node) flags |= DebugTreeBufferBuilder::IS_TEXT_NODE;
    if (node.is_svg_node) flags |= DebugTreeBufferBuilder::IS_SVG_NODE;
    if (node.uses_editor_command_keys) flags |= DebugTreeBufferBuilder::EDITOR_COMMAND_KEYS;
    if (node.accepts_tab) flags |= DebugTreeBufferBuilder::EDITOR_ACCEPTS_TAB;
    return flags;
}

} // namespace

void DebugTreeProjector::ClearOutput(std::vector<std::uint32_t>& output) {
    output.clear();
    output.push_back(DebugTreeBufferBuilder::kMagic);
    output.push_back(DebugTreeBufferBuilder::kVersion);
    output.push_back(DebugTreeBufferBuilder::kFixedRecordWords);
    output.push_back(0U);
}

void DebugTreeProjector::Build(
    std::uint64_t root_handle,
    const DebugVisibleBoundsResolver& visible_bounds_for,
    std::vector<std::uint32_t>& output) const {
    if (root_handle != UI_INVALID_HANDLE) {
        AppendRecord(root_handle, UI_INVALID_HANDLE, visible_bounds_for, output);
    }
}

void DebugTreeProjector::AppendRecord(
    std::uint64_t handle,
    std::uint64_t nearest_scroll_ancestor,
    const DebugVisibleBoundsResolver& visible_bounds_for,
    std::vector<std::uint32_t>& output) const {
    const UINode* node = nodes_.Resolve(handle);
    if (node == nullptr) {
        return;
    }

    DebugTreeBufferBuilder builder(output);
    const Rect bounds{node->abs_x, node->abs_y, std::max(0.0f, node->layout_width), std::max(0.0f, node->layout_height)};
    const Rect visible_bounds = visible_bounds_for(*node);
    const Insets padding = ReadInsets(*node, true);
    const Insets border = ReadInsets(*node, false);
    const bool has_visible_bounds = visible_bounds.width > 0.0f && visible_bounds.height > 0.0f;
    const bool clipped_or_empty =
        !has_visible_bounds ||
        std::abs(visible_bounds.x - bounds.x) >= 0.001f ||
        std::abs(visible_bounds.y - bounds.y) >= 0.001f ||
        std::abs(visible_bounds.width - bounds.width) >= 0.001f ||
        std::abs(visible_bounds.height - bounds.height) >= 0.001f;
    const std::string semantic_label = BuildSemanticLabel(*node);
    const Rect viewport = visibility_.ComputeClipBounds(*node, 0.0f, 0.0f);

    builder.AddHandle(handle);
    builder.AddHandle(node->parent_handle);
    builder.AddWord(node->node_type);
    builder.AddWord(NodeFlags(*node, clipped_or_empty, semantic_label));
    builder.AddWord(BehaviorFlags(*node));
    builder.AddWord(static_cast<std::uint32_t>(node->semantic_role));
    builder.AddFloat(bounds.x);
    builder.AddFloat(bounds.y);
    builder.AddFloat(bounds.width);
    builder.AddFloat(bounds.height);
    builder.AddFloat(visible_bounds.x);
    builder.AddFloat(visible_bounds.y);
    builder.AddFloat(has_visible_bounds ? visible_bounds.width : 0.0f);
    builder.AddFloat(has_visible_bounds ? visible_bounds.height : 0.0f);
    builder.AddFloat(padding.left);
    builder.AddFloat(padding.top);
    builder.AddFloat(padding.right);
    builder.AddFloat(padding.bottom);
    builder.AddFloat(ReadMargin(*node, YGEdgeLeft));
    builder.AddFloat(ReadMargin(*node, YGEdgeTop));
    builder.AddFloat(ReadMargin(*node, YGEdgeRight));
    builder.AddFloat(ReadMargin(*node, YGEdgeBottom));
    builder.AddFloat(border.left);
    builder.AddFloat(border.top);
    builder.AddFloat(border.right);
    builder.AddFloat(border.bottom);
    builder.AddWord(node->bg_color);
    builder.AddWord(node->border_color);
    builder.AddWord(node->border_style);
    builder.AddFloat(node->corner_radius_tl);
    builder.AddFloat(node->corner_radius_tr);
    builder.AddFloat(node->corner_radius_br);
    builder.AddFloat(node->corner_radius_bl);
    builder.AddFloat(node->opacity);
    builder.AddWord(node->font_id);
    builder.AddFloat(node->font_size);
    builder.AddWord(node->text_color);
    builder.AddHandle(nearest_scroll_ancestor);
    builder.AddFloat(node->scroll_offset_x);
    builder.AddFloat(node->scroll_offset_y);
    builder.AddFloat(node->scroll_content_width);
    builder.AddFloat(node->scroll_content_height);
    builder.AddFloat(viewport.width);
    builder.AddFloat(viewport.height);
    builder.AddHandle(node->scroll_proxy_target_handle);
    builder.AddWord(node->text_align);
    builder.AddWord(node->text_vertical_align);
    builder.AddWord(static_cast<std::uint32_t>(node->visibility));
    builder.AddString(node->node_id);
    builder.AddString(semantic_label);
    builder.FinishRecord();

    const std::uint64_t child_scroll_ancestor = node->is_scroll_view ? handle : nearest_scroll_ancestor;
    for (const std::uint64_t child_handle : node->children) {
        AppendRecord(child_handle, child_scroll_ancestor, visible_bounds_for, output);
    }
}

} // namespace effindom::v2::ui
