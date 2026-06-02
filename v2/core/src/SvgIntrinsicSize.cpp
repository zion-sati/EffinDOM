#include "SvgIntrinsicSize.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace effindom::v2::detail {

namespace {

bool IsSvgAttributeBoundary(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) || ch == '<' || ch == '/' || ch == '\n' || ch == '\r' || ch == '\t';
}

std::string_view TrimAscii(std::string_view value) {
    std::size_t start = 0U;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start += 1U;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1U]))) {
        end -= 1U;
    }
    return value.substr(start, end - start);
}

std::optional<std::string_view> ReadRootSvgTag(std::string_view markup) {
    const std::size_t svg_pos = markup.find("<svg");
    if (svg_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t end_pos = markup.find('>', svg_pos);
    if (end_pos == std::string_view::npos) {
        return std::nullopt;
    }
    return markup.substr(svg_pos, (end_pos - svg_pos) + 1U);
}

std::optional<std::string_view> ReadSvgAttributeValue(std::string_view tag, std::string_view name) {
    std::size_t search_from = 0U;
    while (search_from < tag.size()) {
        const std::size_t attr_pos = tag.find(name, search_from);
        if (attr_pos == std::string_view::npos) {
            return std::nullopt;
        }
        if (attr_pos > 0U && !IsSvgAttributeBoundary(tag[attr_pos - 1U])) {
            search_from = attr_pos + 1U;
            continue;
        }
        std::size_t cursor = attr_pos + name.size();
        while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) {
            cursor += 1U;
        }
        if (cursor >= tag.size() || tag[cursor] != '=') {
            search_from = attr_pos + 1U;
            continue;
        }
        cursor += 1U;
        while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) {
            cursor += 1U;
        }
        if (cursor >= tag.size() || (tag[cursor] != '"' && tag[cursor] != '\'')) {
            return std::nullopt;
        }
        const char quote = tag[cursor];
        cursor += 1U;
        const std::size_t value_end = tag.find(quote, cursor);
        if (value_end == std::string_view::npos) {
            return std::nullopt;
        }
        return tag.substr(cursor, value_end - cursor);
    }
    return std::nullopt;
}

std::optional<float> ParseAbsoluteSvgLength(std::string_view raw_value) {
    const std::string_view trimmed = TrimAscii(raw_value);
    if (trimmed.empty() || trimmed.back() == '%') {
        return std::nullopt;
    }
    const std::string owned_value(trimmed);
    char* end_ptr = nullptr;
    const float parsed = std::strtof(owned_value.c_str(), &end_ptr);
    if (!std::isfinite(parsed) || parsed <= 0.0f || end_ptr == owned_value.c_str()) {
        return std::nullopt;
    }
    std::string_view suffix(end_ptr, static_cast<std::size_t>(owned_value.c_str() + owned_value.size() - end_ptr));
    suffix = TrimAscii(suffix);
    if (!suffix.empty() && suffix != "px") {
        return std::nullopt;
    }
    return parsed;
}

std::optional<SvgIntrinsicSize> ParseSvgViewBox(std::string_view raw_value) {
    std::string normalized;
    normalized.reserve(raw_value.size());
    for (const char ch : raw_value) {
        normalized.push_back(ch == ',' ? ' ' : ch);
    }
    std::array<float, 4> values{};
    std::size_t count = 0U;
    const char* cursor = normalized.c_str();
    while (*cursor != '\0' && count < values.size()) {
        while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) {
            cursor += 1;
        }
        if (*cursor == '\0') {
            break;
        }
        char* end_ptr = nullptr;
        const float parsed = std::strtof(cursor, &end_ptr);
        if (!std::isfinite(parsed) || end_ptr == cursor) {
            return std::nullopt;
        }
        values[count] = parsed;
        count += 1U;
        cursor = end_ptr;
    }
    if (count != 4U || values[2] <= 0.0f || values[3] <= 0.0f) {
        return std::nullopt;
    }
    return SvgIntrinsicSize{values[2], values[3]};
}

} // namespace

SvgIntrinsicSize ParseSvgIntrinsicSize(const std::uint8_t* bytes, std::uint32_t length) {
    const std::string_view markup(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(length));
    const std::optional<std::string_view> root_tag = ReadRootSvgTag(markup);
    if (!root_tag.has_value()) {
        return SvgIntrinsicSize{};
    }

    const std::optional<float> width = ParseAbsoluteSvgLength(ReadSvgAttributeValue(root_tag.value(), "width").value_or(std::string_view{}));
    const std::optional<float> height = ParseAbsoluteSvgLength(ReadSvgAttributeValue(root_tag.value(), "height").value_or(std::string_view{}));
    if (width.has_value() && height.has_value()) {
        return SvgIntrinsicSize{width.value(), height.value()};
    }

    const std::optional<SvgIntrinsicSize> view_box = ParseSvgViewBox(ReadSvgAttributeValue(root_tag.value(), "viewBox").value_or(std::string_view{}));
    if (view_box.has_value()) {
        if (width.has_value()) {
            return SvgIntrinsicSize{width.value(), width.value() * (view_box->height / view_box->width)};
        }
        if (height.has_value()) {
            return SvgIntrinsicSize{height.value() * (view_box->width / view_box->height), height.value()};
        }
        return view_box.value();
    }

    return SvgIntrinsicSize{
        width.value_or(1.0f),
        height.value_or(1.0f),
    };
}

} // namespace effindom::v2::detail
