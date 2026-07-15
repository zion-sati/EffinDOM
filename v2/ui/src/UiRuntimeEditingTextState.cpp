#include "UiRuntime.h"

#include "effindom_ui.h"

#include <algorithm>
#include <limits>
#include <string_view>

namespace effindom::v2::ui {

namespace {

struct IncrementalTextDiff {
    std::uint32_t changed_start = 0U;
    std::uint32_t old_changed_end = 0U;
    std::uint32_t new_changed_end = 0U;
    std::int64_t byte_delta = 0;
};

bool IsLineBreakByte(char ch) {
    return ch == '\n' || ch == '\r';
}

struct TextClampRange {
    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
};

std::size_t NextUtf8Codepoint(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) {
        return text.size();
    }
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80U) {
        return offset + 1U;
    }
    const auto is_continuation = [&](std::size_t index) {
        return index < text.size() &&
               (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U;
    };
    if ((lead & 0xE0U) == 0xC0U && is_continuation(offset + 1U)) {
        return offset + 2U;
    }
    if ((lead & 0xF0U) == 0xE0U && is_continuation(offset + 1U) && is_continuation(offset + 2U)) {
        return offset + 3U;
    }
    if ((lead & 0xF8U) == 0xF0U &&
        is_continuation(offset + 1U) &&
        is_continuation(offset + 2U) &&
        is_continuation(offset + 3U)) {
        return offset + 4U;
    }
    return offset + 1U;
}

bool ComputeIncrementalTextDiff(
    std::string_view previous_text,
    std::string_view next_text,
    IncrementalTextDiff& out) {
    out = IncrementalTextDiff{};
    if (previous_text == next_text) {
        return false;
    }

    const std::size_t old_length = previous_text.size();
    const std::size_t new_length = next_text.size();
    std::size_t common_prefix = 0U;
    const std::size_t shared_prefix_limit = std::min(old_length, new_length);
    while (common_prefix < shared_prefix_limit && previous_text[common_prefix] == next_text[common_prefix]) {
        common_prefix += 1U;
    }

    std::size_t common_suffix = 0U;
    while (common_suffix < (old_length - common_prefix) &&
           common_suffix < (new_length - common_prefix) &&
           previous_text[old_length - common_suffix - 1U] == next_text[new_length - common_suffix - 1U]) {
        common_suffix += 1U;
    }

    out.changed_start = static_cast<std::uint32_t>(common_prefix);
    out.old_changed_end = static_cast<std::uint32_t>(old_length - common_suffix);
    out.new_changed_end = static_cast<std::uint32_t>(new_length - common_suffix);
    out.byte_delta = static_cast<std::int64_t>(new_length) - static_cast<std::int64_t>(old_length);
    return true;
}

std::vector<std::uint32_t> ComputeTextLineStartsForRange(
    std::string_view text,
    std::uint32_t start,
    std::uint32_t end) {
    const std::uint32_t clamped_start = std::min(start, static_cast<std::uint32_t>(text.size()));
    const std::uint32_t clamped_end = std::min(std::max(start, end), static_cast<std::uint32_t>(text.size()));
    std::vector<std::uint32_t> starts{clamped_start};
    for (std::uint32_t index = clamped_start; index < clamped_end;) {
        if (text[index] == '\r') {
            if (index + 1U < clamped_end && text[index + 1U] == '\n') {
                starts.push_back(index + 2U);
                index += 2U;
            } else {
                starts.push_back(index + 1U);
                index += 1U;
            }
            continue;
        }
        if (text[index] == '\n') {
            starts.push_back(index + 1U);
            index += 1U;
            continue;
        }
        index += 1U;
    }
    return starts;
}

bool ShouldClampTextboxText(const UINode& node) {
    return node.is_text_node && IsEditorTextNode(node);
}

void CollectTextboxHardLineClampRanges(std::string_view text, std::vector<TextClampRange>& out_ranges) {
    out_ranges.clear();
    std::size_t offset = 0U;
    while (offset < text.size()) {
        std::size_t line_cap_end = offset;
        std::size_t codepoint_count = 0U;
        while (offset < text.size() && !IsLineBreakByte(text[offset])) {
            const std::size_t next = NextUtf8Codepoint(text, offset);
            if (codepoint_count < kTextboxHardClampMaxCodepoints) {
                line_cap_end = next;
            }
            codepoint_count += 1U;
            offset = next;
        }
        if (codepoint_count > kTextboxHardClampMaxCodepoints) {
            out_ranges.push_back(TextClampRange{
                static_cast<std::uint32_t>(line_cap_end),
                static_cast<std::uint32_t>(offset),
            });
        }
        if (offset >= text.size()) {
            break;
        }
        if (text[offset] == '\r' && offset + 1U < text.size() && text[offset + 1U] == '\n') {
            offset += 2U;
        } else {
            offset += 1U;
        }
    }
}

std::uint32_t MapClampedTextIndex(std::uint32_t index, const std::vector<TextClampRange>& ranges) {
    std::uint32_t removed_before = 0U;
    for (const TextClampRange& range : ranges) {
        if (index <= range.start) {
            break;
        }
        if (index < range.end) {
            return range.start - removed_before;
        }
        removed_before += range.end - range.start;
    }
    return index - removed_before;
}

} // namespace

bool UiRuntime::ApplyAbsurdLineClampImpl(UINode& node) const {
    if (!ShouldClampTextboxText(node) || node.text_content.size() <= kTextboxHardClampMaxCodepoints) {
        return false;
    }
    const std::string_view text(node.text_content.data(), node.text_content.size());
    std::vector<TextClampRange> clamp_ranges{};
    CollectTextboxHardLineClampRanges(text, clamp_ranges);
    if (clamp_ranges.empty()) {
        return false;
    }
    std::size_t removed_bytes = 0U;
    for (const TextClampRange& range : clamp_ranges) {
        removed_bytes += static_cast<std::size_t>(range.end - range.start);
    }
    std::string clamped_text{};
    clamped_text.reserve(node.text_content.size() - removed_bytes);
    std::size_t cursor = 0U;
    for (const TextClampRange& range : clamp_ranges) {
        clamped_text.append(text.data() + cursor, static_cast<std::size_t>(range.start) - cursor);
        cursor = static_cast<std::size_t>(range.end);
    }
    clamped_text.append(text.data() + cursor, text.size() - cursor);
    node.text_content = std::move(clamped_text);
    node.selection_start = MapClampedTextIndex(node.selection_start, clamp_ranges);
    node.selection_end = MapClampedTextIndex(node.selection_end, clamp_ranges);
    return true;
}

bool UiRuntime::WouldApplyAbsurdLineClamp(const UINode& node) const {
    if (!ShouldClampTextboxText(node) || node.text_content.size() <= kTextboxHardClampMaxCodepoints) {
        return false;
    }
    std::vector<TextClampRange> clamp_ranges{};
    CollectTextboxHardLineClampRanges(node.text_content, clamp_ranges);
    return !clamp_ranges.empty();
}

// See docs/v2/ui/TEXT_RUNTIME_OPTIMIZATIONS.md#line-start-index. The raw
// line-start index is the cheap hard-line map shared by paragraph layout and
// incremental edit patching; newline-bearing edits must explicitly bail out.
bool UiRuntime::TryApplyIncrementalTextLineStartsImpl(UINode& node, std::string_view previous_text) const {
    if (node.text_line_starts.empty()) {
        return false;
    }

    const std::string_view next_text(node.text_content.data(), node.text_content.size());
    IncrementalTextDiff diff{};
    if (!ComputeIncrementalTextDiff(previous_text, next_text, diff)) {
        return true;
    }

    const auto contains_line_break = [](std::string_view text, std::uint32_t start, std::uint32_t end) {
        if (start >= end || start >= text.size()) {
            return false;
        }
        const std::size_t clamped_end = std::min<std::size_t>(end, text.size());
        return text.find_first_of("\r\n", static_cast<std::size_t>(start)) < clamped_end;
    };
    const auto touches_line_break_boundary = [](std::string_view text, std::uint32_t index) {
        const std::size_t clamped = std::min<std::size_t>(index, text.size());
        return (clamped < text.size() && IsLineBreakByte(text[clamped])) ||
            (clamped > 0U && IsLineBreakByte(text[clamped - 1U]));
    };
    if (contains_line_break(previous_text, diff.changed_start, diff.old_changed_end) ||
        contains_line_break(next_text, diff.changed_start, diff.new_changed_end) ||
        touches_line_break_boundary(previous_text, diff.changed_start) ||
        touches_line_break_boundary(previous_text, diff.old_changed_end) ||
        touches_line_break_boundary(next_text, diff.changed_start) ||
        touches_line_break_boundary(next_text, diff.new_changed_end)) {
        return false;
    }

    const auto line_index_for_starts = [](const std::vector<std::uint32_t>& starts, std::uint32_t pos, std::uint32_t text_length) -> std::size_t {
        if (starts.empty()) {
            return 0U;
        }
        const std::uint32_t clamped = std::min(pos, text_length);
        const auto it = std::upper_bound(starts.begin(), starts.end(), clamped);
        if (it == starts.begin()) {
            return 0U;
        }
        return static_cast<std::size_t>(std::distance(starts.begin(), it) - 1);
    };

    const std::size_t old_start_line =
        line_index_for_starts(node.text_line_starts, diff.changed_start, static_cast<std::uint32_t>(previous_text.size()));
    const std::uint32_t old_probe =
        diff.old_changed_end > diff.changed_start
        ? (diff.old_changed_end - 1U)
        : diff.changed_start;
    const std::size_t old_end_line =
        line_index_for_starts(node.text_line_starts, old_probe, static_cast<std::uint32_t>(previous_text.size()));
    const std::uint32_t scan_start = GetTextLineStart(node, old_start_line);
    const std::uint32_t scan_end_old =
        old_end_line + 1U < node.text_line_starts.size()
        ? node.text_line_starts[old_end_line + 1U]
        : static_cast<std::uint32_t>(previous_text.size());
    const std::uint32_t scan_end_new = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(scan_end_old) + diff.byte_delta,
        static_cast<std::int64_t>(scan_start),
        static_cast<std::int64_t>(next_text.size())));

    // Replace only the touched hard-line start slice, then shift the untouched suffix by
    // the byte delta. This keeps the cheapest text index hot for same-line edits and
    // lets the heavier paragraph caches decide independently whether they can patch too.
    std::vector<std::uint32_t> replacement_starts =
        ComputeTextLineStartsForRange(next_text, scan_start, scan_end_new);
    std::vector<std::uint32_t> updated_starts{};
    updated_starts.reserve(
        old_start_line +
        replacement_starts.size() +
        (node.text_line_starts.size() > (old_end_line + 2U)
         ? (node.text_line_starts.size() - (old_end_line + 2U))
         : 0U));
    updated_starts.insert(
        updated_starts.end(),
        node.text_line_starts.begin(),
        node.text_line_starts.begin() + static_cast<std::ptrdiff_t>(old_start_line));
    updated_starts.insert(
        updated_starts.end(),
        replacement_starts.begin(),
        replacement_starts.end());
    const std::size_t suffix_start = std::min(node.text_line_starts.size(), old_end_line + 2U);
    for (std::size_t index = suffix_start; index < node.text_line_starts.size(); index += 1U) {
        updated_starts.push_back(static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(node.text_line_starts[index]) + diff.byte_delta,
            0,
            static_cast<std::int64_t>(next_text.size()))));
    }
    if (updated_starts.empty()) {
        updated_starts.push_back(0U);
    }
    node.text_line_starts = std::move(updated_starts);
    node.text_line_starts_dirty = false;
    return true;
}

bool UiRuntime::TryApplyIncrementalTextLineStartsImpl(UINode& node, const TextEdit& edit) const {
    if (node.text_line_starts.empty() || edit.old_text_length > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const std::string_view next_text(node.text_content.data(), node.text_content.size());
    const std::uint32_t old_length = edit.old_text_length;
    if (edit.start > edit.removed_end || edit.removed_end > old_length) {
        return false;
    }

    const auto line_index_for_starts = [](const std::vector<std::uint32_t>& starts, std::uint32_t pos, std::uint32_t text_length) -> std::size_t {
        const std::uint32_t clamped = std::min(pos, text_length);
        const auto it = std::upper_bound(starts.begin(), starts.end(), clamped);
        return it == starts.begin()
            ? 0U
            : static_cast<std::size_t>(std::distance(starts.begin(), it) - 1);
    };

    const std::size_t old_start_line =
        line_index_for_starts(node.text_line_starts, edit.start, old_length);
    const std::uint32_t old_probe = edit.removed_end > edit.start
        ? edit.removed_end - 1U
        : edit.start;
    const std::size_t old_end_line =
        line_index_for_starts(node.text_line_starts, old_probe, old_length);
    if (old_start_line >= node.text_line_starts.size() || old_end_line < old_start_line) {
        return false;
    }

    const std::uint32_t scan_start = node.text_line_starts[old_start_line];
    const std::uint32_t scan_end_old = old_end_line + 1U < node.text_line_starts.size()
        ? node.text_line_starts[old_end_line + 1U]
        : old_length;
    const std::uint32_t scan_end_new = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(scan_end_old) + edit.byte_delta(),
        static_cast<std::int64_t>(scan_start),
        static_cast<std::int64_t>(next_text.size())));

    std::vector<std::uint32_t> replacement_starts =
        ComputeTextLineStartsForRange(next_text, scan_start, scan_end_new);
    std::vector<std::uint32_t> updated_starts{};
    updated_starts.reserve(node.text_line_starts.size() + replacement_starts.size() + 1U);
    updated_starts.insert(
        updated_starts.end(),
        node.text_line_starts.begin(),
        node.text_line_starts.begin() + static_cast<std::ptrdiff_t>(old_start_line));
    updated_starts.insert(updated_starts.end(), replacement_starts.begin(), replacement_starts.end());
    const std::size_t suffix_start = std::min(node.text_line_starts.size(), old_end_line + 2U);
    for (std::size_t index = suffix_start; index < node.text_line_starts.size(); index += 1U) {
        updated_starts.push_back(static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(node.text_line_starts[index]) + edit.byte_delta(),
            0,
            static_cast<std::int64_t>(next_text.size()))));
    }
    if (updated_starts.empty()) {
        updated_starts.push_back(0U);
    }
    updated_starts.erase(std::unique(updated_starts.begin(), updated_starts.end()), updated_starts.end());
    node.text_line_starts = std::move(updated_starts);
    node.text_line_starts_dirty = false;
    return true;
}

} // namespace effindom::v2::ui
