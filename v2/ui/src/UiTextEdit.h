#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace effindom::v2::ui {

inline bool IsUtf8Boundary(std::string_view text, std::size_t offset) {
    return offset <= text.size() &&
        (offset == 0U || offset == text.size() ||
         (static_cast<unsigned char>(text[offset]) & 0xC0U) != 0x80U);
}

inline bool IsValidUtf8(std::string_view text) {
    for (std::size_t offset = 0U; offset < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        std::size_t length = 0U;
        std::uint32_t minimum = 0U;
        std::uint32_t codepoint = 0U;
        if (lead <= 0x7FU) {
            length = 1U;
            codepoint = lead;
        } else if ((lead & 0xE0U) == 0xC0U) {
            length = 2U;
            minimum = 0x80U;
            codepoint = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3U;
            minimum = 0x800U;
            codepoint = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4U;
            minimum = 0x10000U;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (offset + length > text.size()) {
            return false;
        }
        for (std::size_t index = 1U; index < length; index += 1U) {
            const unsigned char continuation = static_cast<unsigned char>(text[offset + index]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        if ((length > 1U && codepoint < minimum) ||
            codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }
        offset += length;
    }
    return true;
}

struct TextEdit {
    std::uint32_t start = 0U;
    std::uint32_t removed_end = 0U;
    std::uint32_t old_text_length = 0U;
    std::string inserted_text{};
    std::string removed_text{};

    static std::optional<TextEdit> Create(
        std::string_view old_text,
        std::uint32_t start,
        std::uint32_t removed_end,
        std::string_view inserted_text) {
        constexpr std::size_t kMaxOffset = std::numeric_limits<std::uint32_t>::max();
        if (old_text.size() > kMaxOffset || inserted_text.size() > kMaxOffset) {
            return std::nullopt;
        }
        const std::size_t clamped_start = std::min<std::size_t>(start, old_text.size());
        const std::size_t clamped_end = std::min<std::size_t>(std::max(start, removed_end), old_text.size());
        const std::size_t next_size = old_text.size() - (clamped_end - clamped_start) + inserted_text.size();
        if (next_size > kMaxOffset) {
            return std::nullopt;
        }
        if (!IsUtf8Boundary(old_text, clamped_start) ||
            !IsUtf8Boundary(old_text, clamped_end) ||
            !IsValidUtf8(inserted_text)) {
            return std::nullopt;
        }
        TextEdit edit{};
        edit.start = static_cast<std::uint32_t>(clamped_start);
        edit.removed_end = static_cast<std::uint32_t>(clamped_end);
        edit.old_text_length = static_cast<std::uint32_t>(old_text.size());
        edit.inserted_text.assign(inserted_text);
        edit.removed_text.assign(old_text.substr(clamped_start, clamped_end - clamped_start));
        return edit;
    }

    std::uint32_t inserted_end() const {
        return start + static_cast<std::uint32_t>(inserted_text.size());
    }

    std::int64_t byte_delta() const {
        return static_cast<std::int64_t>(inserted_text.size()) -
            static_cast<std::int64_t>(removed_end - start);
    }
};

inline std::optional<TextEdit> CreateTextEditForFullReplacement(
    std::string_view old_text,
    std::string_view new_text,
    std::uint64_t* compared_bytes = nullptr) {
    if (old_text == new_text) {
        return std::nullopt;
    }
    std::size_t prefix = 0U;
    const std::size_t prefix_limit = std::min(old_text.size(), new_text.size());
    while (prefix < prefix_limit && old_text[prefix] == new_text[prefix]) {
        if (compared_bytes != nullptr) *compared_bytes += 1U;
        prefix += 1U;
    }
    if (compared_bytes != nullptr && prefix < prefix_limit) *compared_bytes += 1U;
    while (prefix > 0U && (!IsUtf8Boundary(old_text, prefix) || !IsUtf8Boundary(new_text, prefix))) {
        prefix -= 1U;
    }
    std::size_t suffix = 0U;
    while (suffix < old_text.size() - prefix &&
           suffix < new_text.size() - prefix &&
           old_text[old_text.size() - suffix - 1U] == new_text[new_text.size() - suffix - 1U]) {
        if (compared_bytes != nullptr) *compared_bytes += 1U;
        suffix += 1U;
    }
    if (compared_bytes != nullptr &&
        suffix < old_text.size() - prefix && suffix < new_text.size() - prefix) {
        *compared_bytes += 1U;
    }
    while (suffix > 0U &&
           (!IsUtf8Boundary(old_text, old_text.size() - suffix) ||
            !IsUtf8Boundary(new_text, new_text.size() - suffix))) {
        suffix -= 1U;
    }
    return TextEdit::Create(
        old_text,
        static_cast<std::uint32_t>(prefix),
        static_cast<std::uint32_t>(old_text.size() - suffix),
        new_text.substr(prefix, new_text.size() - prefix - suffix));
}

class PreviousTextView {
public:
    PreviousTextView(std::string_view edited_text, const TextEdit& edit)
        : edited_text_(edited_text), edit_(edit) {}

    std::size_t size() const { return edit_.old_text_length; }
    bool empty() const { return size() == 0U; }

    char operator[](std::size_t index) const {
        if (index < edit_.start) {
            return edited_text_[index];
        }
        if (index < edit_.removed_end) {
            return edit_.removed_text[index - edit_.start];
        }
        const std::size_t edited_index =
            static_cast<std::size_t>(edit_.inserted_end()) + (index - edit_.removed_end);
        return edited_text_[edited_index];
    }

    std::string materialize(std::size_t start, std::size_t end) const {
        const std::size_t clamped_start = std::min(start, size());
        const std::size_t clamped_end = std::min(std::max(start, end), size());
        std::string result{};
        result.reserve(clamped_end - clamped_start);
        for (std::size_t index = clamped_start; index < clamped_end; index += 1U) {
            result.push_back((*this)[index]);
        }
        return result;
    }

private:
    std::string_view edited_text_;
    const TextEdit& edit_;
};

} // namespace effindom::v2::ui
