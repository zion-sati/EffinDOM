#include "UiSemanticProjector.h"

#include "CommandBuilder.h"

#include <cstring>

namespace effindom::v2::ui {

namespace {

constexpr std::size_t kDefaultTextboxSemanticLabelMaxCodepoints = 1000U;

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
        if (role == UI_SEMANTIC_TEXTBOX) {
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

} // namespace

std::string BuildSemanticLabel(const UINode& node) {
    if (!node.semantic_label.empty()) {
        return node.semantic_label;
    }
    if (node.is_text_node && !node.text_content.empty()) {
        if (node.is_obscured) {
            return "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";
        }
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

void SemanticProjector::ClearOutput(std::vector<std::uint32_t>& output) {
    output.clear();
    output.push_back(0U);
}

bool SemanticProjector::HasSemanticAncestor(const UINode& node) const {
    for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
        const UINode* ancestor = nodes_.Resolve(current);
        if (ancestor == nullptr) {
            break;
        }
        if (ancestor->semantic_role != UI_SEMANTIC_NONE) {
            return true;
        }
        current = ancestor->parent_handle;
    }
    return false;
}

void SemanticProjector::Build(
    const std::vector<std::uint64_t>& paint_order,
    std::uint64_t semantic_scope_root,
    const SemanticVisibleBoundsResolver& visible_bounds_for,
    std::vector<std::uint32_t>& output) const {
    SemanticBufferBuilder builder(output);
    for (const std::uint64_t handle : paint_order) {
        const UINode* node = nodes_.Resolve(handle);
        if (node == nullptr) {
            continue;
        }
        UiSemanticRole role = node->semantic_role;
        const std::string label = BuildSemanticLabel(*node);
        if (role == UI_SEMANTIC_NONE) {
            if (!node->is_text_node || label.empty() || HasSemanticAncestor(*node)) {
                continue;
            }
            role = UI_SEMANTIC_STATIC_TEXT;
        }
        if (semantic_scope_root != UI_INVALID_HANDLE && !nodes_.SubtreeContains(semantic_scope_root, handle)) {
            continue;
        }
        Rect visible_bounds = visible_bounds_for(*node);
        if (role == UI_SEMANTIC_TEXTBOX && node->max_lines != 1) {
            visibility_.TryGetMultilineTextboxViewportBounds(*node, visible_bounds);
        }
        if (visible_bounds.width <= 0.0f || visible_bounds.height <= 0.0f) {
            continue;
        }
        builder.AddRecord(
            role,
            handle,
            visible_bounds.x,
            visible_bounds.y,
            visible_bounds.width,
            visible_bounds.height,
            *node,
            label);
    }
}

} // namespace effindom::v2::ui
