#include "UiRuntime.h"

#include <algorithm>
#include <string_view>

namespace effindom::v2::ui {

namespace {

struct Utf8Character {
    std::size_t width = 1U;
    bool valid = false;
};

Utf8Character DecodeUtf8Character(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) {
        return {};
    }
    const auto byte = [&](std::size_t index) {
        return static_cast<unsigned char>(text[index]);
    };
    const auto continuation = [&](std::size_t index) {
        return index < text.size() && (byte(index) & 0xC0U) == 0x80U;
    };
    const unsigned char lead = byte(offset);
    if (lead < 0x80U) {
        return {1U, true};
    }
    if (lead >= 0xC2U && lead <= 0xDFU && continuation(offset + 1U)) {
        return {2U, true};
    }
    if (lead >= 0xE0U && lead <= 0xEFU && continuation(offset + 1U) && continuation(offset + 2U)) {
        const unsigned char second = byte(offset + 1U);
        if ((lead != 0xE0U || second >= 0xA0U) && (lead != 0xEDU || second < 0xA0U)) {
            return {3U, true};
        }
    }
    if (lead >= 0xF0U && lead <= 0xF4U && continuation(offset + 1U) &&
        continuation(offset + 2U) && continuation(offset + 3U)) {
        const unsigned char second = byte(offset + 1U);
        if ((lead != 0xF0U || second >= 0x90U) && (lead != 0xF4U || second < 0x90U)) {
            return {4U, true};
        }
    }
    return {1U, false};
}

std::uint32_t CharacterIndexForByteOffset(std::string_view text, std::uint32_t byte_offset) {
    const std::size_t limit = std::min<std::size_t>(byte_offset, text.size());
    std::size_t offset = 0U;
    std::uint32_t character = 0U;
    while (offset < limit) {
        const Utf8Character decoded = DecodeUtf8Character(text, offset);
        if (offset + decoded.width > limit) {
            break;
        }
        offset += decoded.width;
        character += 1U;
    }
    return character;
}

bool ResolveCharacterRange(
    std::string_view text,
    std::uint32_t start_character,
    std::uint32_t end_character,
    std::size_t& out_start_byte,
    std::size_t& out_end_byte,
    std::uint32_t* out_character_count = nullptr) {
    if (start_character > end_character) {
        return false;
    }
    std::size_t offset = 0U;
    std::uint32_t character = 0U;
    bool found_start = start_character == 0U;
    bool found_end = end_character == 0U;
    out_start_byte = 0U;
    out_end_byte = 0U;
    while (offset < text.size()) {
        if (character == start_character) {
            out_start_byte = offset;
            found_start = true;
        }
        if (character == end_character) {
            out_end_byte = offset;
            found_end = true;
        }
        const Utf8Character decoded = DecodeUtf8Character(text, offset);
        offset += decoded.width;
        character += 1U;
    }
    if (character == start_character) {
        out_start_byte = offset;
        found_start = true;
    }
    if (character == end_character) {
        out_end_byte = offset;
        found_end = true;
    }
    if (out_character_count != nullptr) {
        *out_character_count = character;
    }
    return found_start && found_end;
}

void AppendSanitizedUtf8(std::string_view text, std::size_t start, std::size_t end, std::string& output) {
    output.clear();
    output.reserve(end - start);
    std::size_t offset = start;
    while (offset < end) {
        const Utf8Character decoded = DecodeUtf8Character(text, offset);
        if (decoded.valid) {
            output.append(text.data() + offset, decoded.width);
        } else {
            output.append("\xEF\xBF\xBD", 3U);
        }
        offset += decoded.width;
    }
}

UiTextAccessibilityQueryStatus ResolveTextRange(
    const UINode* node,
    std::uint64_t revision,
    std::uint32_t start_character,
    std::uint32_t end_character,
    std::size_t& out_start_byte,
    std::size_t& out_end_byte) {
    if (node == nullptr || !node->is_text_node) {
        return UI_TEXT_ACCESSIBILITY_QUERY_NOT_TEXT;
    }
    if (node->is_obscured) {
        return UI_TEXT_ACCESSIBILITY_QUERY_OBSCURED;
    }
    if (node->text_accessibility_revision != revision) {
        return UI_TEXT_ACCESSIBILITY_QUERY_STALE_REVISION;
    }
    if (!ResolveCharacterRange(
            node->text_content, start_character, end_character, out_start_byte, out_end_byte)) {
        return UI_TEXT_ACCESSIBILITY_QUERY_INVALID_RANGE;
    }
    return UI_TEXT_ACCESSIBILITY_QUERY_OK;
}

} // namespace

std::pair<const UINode*, std::uint64_t> UiRuntime::ResolveAccessibilityTextNode(
    std::uint64_t handle) const {
    const UINode* root = Resolve(handle);
    if (root == nullptr) return {nullptr, UI_INVALID_HANDLE};
    if (root->is_text_node) return {root, handle};
    if (root->semantic_role != UI_SEMANTIC_TEXTBOX &&
        root->semantic_role != UI_SEMANTIC_COMBOBOX &&
        root->semantic_role != UI_SEMANTIC_STATIC_TEXT &&
        root->semantic_role != UI_SEMANTIC_HEADING) return {nullptr, UI_INVALID_HANDLE};

    std::vector<std::uint64_t> pending(root->children.rbegin(), root->children.rend());
    const UINode* first_text = nullptr;
    std::uint64_t first_text_handle = UI_INVALID_HANDLE;
    while (!pending.empty()) {
        const std::uint64_t candidate_handle = pending.back();
        pending.pop_back();
        const UINode* candidate = Resolve(candidate_handle);
        if (candidate == nullptr) continue;
        if (candidate->is_text_node) {
            if (candidate->is_editable) return {candidate, candidate_handle};
            if (first_text == nullptr) {
                first_text = candidate;
                first_text_handle = candidate_handle;
            }
            continue;
        }
        if (candidate->semantic_role != UI_SEMANTIC_NONE) continue;
        pending.insert(pending.end(), candidate->children.rbegin(), candidate->children.rend());
    }
    return {first_text, first_text_handle};
}

std::uint64_t UiRuntime::GetAccessibilityTextOwnerHandle(std::uint64_t handle) const {
    const UINode* node = Resolve(handle);
    if (node == nullptr) return UI_INVALID_HANDLE;
    std::uint64_t current_handle = handle;
    while (node != nullptr) {
        if (node->semantic_role == UI_SEMANTIC_TEXTBOX ||
            node->semantic_role == UI_SEMANTIC_COMBOBOX ||
            node->semantic_role == UI_SEMANTIC_STATIC_TEXT ||
            node->semantic_role == UI_SEMANTIC_HEADING) return current_handle;
        current_handle = node->parent_handle;
        node = Resolve(current_handle);
    }
    return handle;
}

bool UiRuntime::GetAccessibilityTextInfo(std::uint64_t handle, TextAccessibilityInfo& out) const {
    accessibility_text_profile_.metadata_queries += 1U;
    const auto [node, text_handle] = ResolveAccessibilityTextNode(handle);
    (void)text_handle;
    if (node == nullptr) {
        return false;
    }
    std::size_t start_byte = 0U;
    std::size_t end_byte = 0U;
    std::uint32_t character_count = 0U;
    (void)ResolveCharacterRange(
        node->text_content, 0U, 0U, start_byte, end_byte, &character_count);
    out.revision = node->text_accessibility_revision;
    out.character_count = character_count;
    out.selection_start = CharacterIndexForByteOffset(node->text_content, node->selection_start);
    out.selection_end = CharacterIndexForByteOffset(node->text_content, node->selection_end);
    out.flags = 0U;
    if (!node->is_editable) {
        out.flags |= UI_TEXT_ACCESSIBILITY_FLAG_READ_ONLY;
    }
    if (node->max_lines != 1) {
        out.flags |= UI_TEXT_ACCESSIBILITY_FLAG_MULTILINE;
    }
    if (node->is_obscured) {
        out.flags |= UI_TEXT_ACCESSIBILITY_FLAG_OBSCURED;
    }
    return true;
}

UiTextAccessibilityQueryStatus UiRuntime::GetAccessibilityTextRange(
    std::uint64_t handle,
    std::uint64_t revision,
    std::uint32_t start_character,
    std::uint32_t end_character,
    std::string& out_utf8) const {
    accessibility_text_profile_.range_queries += 1U;
    if (end_character >= start_character) {
        accessibility_text_profile_.requested_characters += end_character - start_character;
    }
    const auto [node, text_handle] = ResolveAccessibilityTextNode(handle);
    (void)text_handle;
    std::size_t start_byte = 0U;
    std::size_t end_byte = 0U;
    const auto status = ResolveTextRange(
        node, revision, start_character, end_character, start_byte, end_byte);
    if (status != UI_TEXT_ACCESSIBILITY_QUERY_OK) {
        return status;
    }
    AppendSanitizedUtf8(node->text_content, start_byte, end_byte, out_utf8);
    accessibility_text_profile_.materialized_utf8_bytes += out_utf8.size();
    return UI_TEXT_ACCESSIBILITY_QUERY_OK;
}

UiTextAccessibilityQueryStatus UiRuntime::GetAccessibilityTextRangeRects(
    std::uint64_t handle,
    std::uint64_t revision,
    std::uint32_t start_character,
    std::uint32_t end_character,
    std::vector<Rect>& out_rects) const {
    accessibility_text_profile_.geometry_queries += 1U;
    const auto [node, text_handle] = ResolveAccessibilityTextNode(handle);
    std::size_t start_byte = 0U;
    std::size_t end_byte = 0U;
    const auto status = ResolveTextRange(
        node, revision, start_character, end_character, start_byte, end_byte);
    if (status != UI_TEXT_ACCESSIBILITY_QUERY_OK) {
        return status;
    }
    out_rects = GetTextRangeSceneRects(
        text_handle, static_cast<std::uint32_t>(start_byte), static_cast<std::uint32_t>(end_byte));
    return UI_TEXT_ACCESSIBILITY_QUERY_OK;
}

UiTextAccessibilityQueryStatus UiRuntime::SetAccessibilityTextSelection(
    std::uint64_t handle,
    std::uint64_t revision,
    std::uint32_t start_character,
    std::uint32_t end_character) {
    accessibility_text_profile_.selection_mutations += 1U;
    const auto [node, text_handle] = ResolveAccessibilityTextNode(handle);
    std::size_t start_byte = 0U;
    std::size_t end_byte = 0U;
    const auto status = ResolveTextRange(
        node, revision, start_character, end_character, start_byte, end_byte);
    if (status != UI_TEXT_ACCESSIBILITY_QUERY_OK) {
        return status;
    }
    return SetTextSelectionRange(
        text_handle, static_cast<std::uint32_t>(start_byte), static_cast<std::uint32_t>(end_byte))
        ? UI_TEXT_ACCESSIBILITY_QUERY_OK
        : UI_TEXT_ACCESSIBILITY_QUERY_NOT_TEXT;
}

UiTextAccessibilityQueryStatus UiRuntime::RevealAccessibilityTextRange(
    std::uint64_t handle,
    std::uint64_t revision,
    std::uint32_t start_character,
    std::uint32_t end_character) {
    accessibility_text_profile_.reveal_requests += 1U;
    const auto [node, text_handle] = ResolveAccessibilityTextNode(handle);
    std::size_t start_byte = 0U;
    std::size_t end_byte = 0U;
    const auto status = ResolveTextRange(
        node, revision, start_character, end_character, start_byte, end_byte);
    if (status != UI_TEXT_ACCESSIBILITY_QUERY_OK) {
        return status;
    }
    return RevealTextRange(
        text_handle,
        static_cast<std::uint32_t>(start_byte),
        static_cast<std::uint32_t>(end_byte))
        ? UI_TEXT_ACCESSIBILITY_QUERY_OK
        : UI_TEXT_ACCESSIBILITY_QUERY_NOT_TEXT;
}

UiTextAccessibilityQueryStatus UiRuntime::ReplaceAccessibilityTextRange(
    std::uint64_t handle,
    std::uint64_t revision,
    std::uint32_t start_character,
    std::uint32_t end_character,
    std::string_view replacement_utf8,
    std::uint64_t& out_revision) {
    accessibility_text_profile_.replacement_mutations += 1U;
    const auto [node, text_handle] = ResolveAccessibilityTextNode(handle);
    std::size_t start_byte = 0U;
    std::size_t end_byte = 0U;
    const auto status = ResolveTextRange(
        node, revision, start_character, end_character, start_byte, end_byte);
    if (status != UI_TEXT_ACCESSIBILITY_QUERY_OK) {
        return status;
    }
    if (!node->is_editable) {
        return UI_TEXT_ACCESSIBILITY_QUERY_READ_ONLY;
    }
    if (!IsValidUtf8(replacement_utf8)) {
        return UI_TEXT_ACCESSIBILITY_QUERY_INVALID_TEXT;
    }
    const std::uint32_t caret = static_cast<std::uint32_t>(start_byte + replacement_utf8.size());
    HandleTextReplaceRange(
        text_handle,
        static_cast<std::uint32_t>(start_byte),
        static_cast<std::uint32_t>(end_byte),
        replacement_utf8.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(replacement_utf8.data()),
        static_cast<std::uint32_t>(replacement_utf8.size()),
        caret);
    const UINode* updated = Resolve(text_handle);
    if (updated == nullptr) {
        return UI_TEXT_ACCESSIBILITY_QUERY_NOT_TEXT;
    }
    out_revision = updated->text_accessibility_revision;
    return UI_TEXT_ACCESSIBILITY_QUERY_OK;
}

} // namespace effindom::v2::ui
