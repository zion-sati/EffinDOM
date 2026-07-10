#include "UiRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace effindom::v2::ui {

namespace {

bool IsHorizontalContainer(const UINode* node) {
    if (node == nullptr || node->yg_node == nullptr) {
        return false;
    }
    return YGNodeStyleGetFlexDirection(node->yg_node) == YGFlexDirectionRow;
}

struct ClipboardRichTextPart {
    std::string text{};
    bool has_style = false;
    std::uint32_t font_id = 0U;
    float font_size = 16.0f;
    std::uint32_t color = 0U;
    std::uint32_t bg_color = 0U;
    std::uint32_t decoration_flags = 0U;
};

void AppendJsonEscapedString(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const unsigned char ch : text) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20U) {
                    char buffer[7]{};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(ch));
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    out.push_back('"');
}

bool CanMergeClipboardParts(const ClipboardRichTextPart& lhs, const ClipboardRichTextPart& rhs) {
    return lhs.has_style == rhs.has_style &&
        lhs.font_id == rhs.font_id &&
        std::abs(lhs.font_size - rhs.font_size) < 0.001f &&
        lhs.color == rhs.color &&
        lhs.bg_color == rhs.bg_color &&
        lhs.decoration_flags == rhs.decoration_flags;
}

void PushClipboardPart(std::vector<ClipboardRichTextPart>& parts, ClipboardRichTextPart part) {
    if (part.text.empty()) {
        return;
    }
    if (!parts.empty() && CanMergeClipboardParts(parts.back(), part)) {
        parts.back().text += part.text;
        return;
    }
    parts.push_back(std::move(part));
}

bool AppendClipboardSelectionParts(
    const UINode& node,
    std::uint32_t start,
    std::uint32_t end,
    std::vector<ClipboardRichTextPart>& out_parts) {
    if (node.is_obscured) {
        return false;
    }
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(std::max(clamped_start, end), text_length);
    if (clamped_start >= clamped_end) {
        return false;
    }

    bool has_styled_parts = false;
    if (!node.has_text_style_runs || node.text_style_runs.empty()) {
        PushClipboardPart(out_parts, ClipboardRichTextPart{
            node.text_content.substr(
                static_cast<std::size_t>(clamped_start),
                static_cast<std::size_t>(clamped_end - clamped_start)),
            false,
        });
        return false;
    }

    std::uint32_t cursor = clamped_start;
    for (const TextStyleRun& run : node.text_style_runs) {
        if (run.end <= clamped_start || run.start >= clamped_end) {
            continue;
        }
        const std::uint32_t run_start = std::max(run.start, clamped_start);
        const std::uint32_t run_end = std::min(run.end, clamped_end);
        if (cursor < run_start) {
            PushClipboardPart(out_parts, ClipboardRichTextPart{
                node.text_content.substr(
                    static_cast<std::size_t>(cursor),
                    static_cast<std::size_t>(run_start - cursor)),
                false,
            });
        }
        PushClipboardPart(out_parts, ClipboardRichTextPart{
            node.text_content.substr(
                static_cast<std::size_t>(run_start),
                static_cast<std::size_t>(run_end - run_start)),
            true,
            run.font_id,
            run.font_size,
            run.color,
            run.bg_color,
            run.decoration_flags,
        });
        has_styled_parts = true;
        cursor = run_end;
    }
    if (cursor < clamped_end) {
        PushClipboardPart(out_parts, ClipboardRichTextPart{
            node.text_content.substr(
                static_cast<std::size_t>(cursor),
                static_cast<std::size_t>(clamped_end - cursor)),
            false,
        });
    }
    return has_styled_parts;
}

bool BuildClipboardRichJson(const std::vector<ClipboardRichTextPart>& parts, std::string& out_json) {
    if (parts.empty()) {
        out_json.clear();
        return false;
    }
    out_json = "{\"version\":1,\"parts\":[";
    for (std::size_t index = 0; index < parts.size(); index += 1U) {
        if (index > 0U) {
            out_json.push_back(',');
        }
        const ClipboardRichTextPart& part = parts[index];
        out_json += "{\"text\":";
        AppendJsonEscapedString(out_json, part.text);
        if (part.has_style) {
            out_json += ",\"fontId\":";
            out_json += std::to_string(part.font_id);
            out_json += ",\"fontSize\":";
            out_json += std::to_string(part.font_size);
            out_json += ",\"color\":";
            out_json += std::to_string(part.color);
            out_json += ",\"bgColor\":";
            out_json += std::to_string(part.bg_color);
            out_json += ",\"decorationFlags\":";
            out_json += std::to_string(part.decoration_flags);
        }
        out_json.push_back('}');
    }
    out_json += "]}";
    return true;
}

} // namespace

void UiRuntime::EmitClipboardWrite(std::string_view plain_text, const std::string* rich_json) const {
    as_on_clipboard_write(
        plain_text.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(plain_text.data()),
        static_cast<std::uint32_t>(plain_text.size()),
        (rich_json == nullptr || rich_json->empty()) ? nullptr : reinterpret_cast<const std::uint8_t*>(rich_json->data()),
        (rich_json == nullptr || rich_json->empty()) ? 0U : static_cast<std::uint32_t>(rich_json->size()));
}

bool UiRuntime::BuildSelectionClipboardRichPayload(
    const UINode& node,
    std::uint32_t start,
    std::uint32_t end,
    std::string& out_plain_text,
    std::string& out_rich_json) const {
    if (node.is_obscured) {
        out_plain_text.clear();
        out_rich_json.clear();
        return false;
    }
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(std::max(clamped_start, end), text_length);
    if (clamped_start >= clamped_end) {
        out_plain_text.clear();
        out_rich_json.clear();
        return false;
    }

    out_plain_text = node.text_content.substr(
        static_cast<std::size_t>(clamped_start),
        static_cast<std::size_t>(clamped_end - clamped_start));
    std::vector<ClipboardRichTextPart> parts{};
    const bool has_styled_parts = AppendClipboardSelectionParts(node, clamped_start, clamped_end, parts);
    if (!has_styled_parts) {
        out_rich_json.clear();
        return false;
    }
    return BuildClipboardRichJson(parts, out_rich_json);
}

bool UiRuntime::BuildCrossSelectionRichPayload(std::string& out_plain_text, std::string& out_rich_json) const {
    out_plain_text.clear();
    out_rich_json.clear();
    if (!cross_selection_active_ || selection_area_handle_ == UI_INVALID_HANDLE || selection_area_nodes_.empty()) {
        return false;
    }

    const int start_position = FindSelectionAreaNodeIndex(start_node_handle_);
    const int end_position = FindSelectionAreaNodeIndex(end_node_handle_);
    if (start_position < 0 || end_position < 0) {
        return false;
    }

    const bool forward =
        start_position < end_position ||
        (start_position == end_position && start_index_ <= end_index_);
    const int first_index = forward ? start_position : end_position;
    const int last_index = forward ? end_position : start_position;
    const std::uint32_t first_offset = forward ? start_index_ : end_index_;
    const std::uint32_t last_offset = forward ? end_index_ : start_index_;

    bool has_styled_parts = false;
    std::vector<ClipboardRichTextPart> parts{};
    for (int index = first_index; index <= last_index; index += 1) {
        const UINode* node = Resolve(selection_area_nodes_[static_cast<std::size_t>(index)]);
        if (node == nullptr || node->is_obscured) {
            continue;
        }

        const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
        std::uint32_t slice_start = 0U;
        std::uint32_t slice_end = text_length;
        if (first_index == last_index) {
            slice_start = std::min(first_offset, text_length);
            slice_end = std::min(last_offset, text_length);
        } else if (index == first_index) {
            slice_start = std::min(first_offset, text_length);
        } else if (index == last_index) {
            slice_end = std::min(last_offset, text_length);
        }
        if (slice_start >= slice_end) {
            continue;
        }

        if (!out_plain_text.empty()) {
            const UINode* previous = Resolve(selection_area_nodes_[static_cast<std::size_t>(index - 1)]);
            const char separator = IsHorizontalContainer(previous == nullptr ? nullptr : Resolve(previous->parent_handle))
                ? ' '
                : '\n';
            out_plain_text.push_back(separator);
            PushClipboardPart(parts, ClipboardRichTextPart{std::string(1, separator), false});
        }

        out_plain_text.append(node->text_content.substr(
            static_cast<std::size_t>(slice_start),
            static_cast<std::size_t>(slice_end - slice_start)));
        has_styled_parts = AppendClipboardSelectionParts(*node, slice_start, slice_end, parts) || has_styled_parts;
    }

    if (!has_styled_parts) {
        out_rich_json.clear();
        return false;
    }
    return BuildClipboardRichJson(parts, out_rich_json);
}

} // namespace effindom::v2::ui
