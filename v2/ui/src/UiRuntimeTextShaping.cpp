#include "UiRuntime.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <memory>

#include <hb-ot.h>

namespace effindom::v2::ui {

namespace {

constexpr std::size_t kMaxHarfBuzzRunBytes = 16U * 1024U;
constexpr std::size_t kShapingBoundarySearchBytes = 1024U;
constexpr float kTabStopColumns = 4.0f;
using ProfileClock = std::chrono::steady_clock;

double ElapsedMilliseconds(ProfileClock::time_point start, ProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool IsUtf8ContinuationByte(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

struct ShapeInputText {
    std::string expanded_text{};
    std::vector<std::uint32_t> cluster_map{};
};

ShapeInputText BuildShapeInputText(std::string_view text) {
    ShapeInputText input{};
    if (text.find('\t') == std::string_view::npos) {
        return input;
    }
    input.expanded_text.reserve(text.size());
    input.cluster_map.reserve(text.size());
    for (std::size_t index = 0U; index < text.size(); index += 1U) {
        if (text[index] == '\t') {
            for (std::size_t space = 0U; space < static_cast<std::size_t>(kTabStopColumns); space += 1U) {
                input.cluster_map.push_back(static_cast<std::uint32_t>(index));
                input.expanded_text.push_back(' ');
            }
            continue;
        }
        input.cluster_map.push_back(static_cast<std::uint32_t>(index));
        input.expanded_text.push_back(text[index]);
    }
    return input;
}

std::size_t PreviousUtf8Boundary(std::string_view text, std::size_t offset) {
    std::size_t boundary = std::min(offset, text.size());
    while (boundary > 0U && boundary < text.size() && IsUtf8ContinuationByte(static_cast<unsigned char>(text[boundary]))) {
        boundary -= 1U;
    }
    return boundary;
}

std::size_t NextUtf8Boundary(std::string_view text, std::size_t offset) {
    std::size_t boundary = std::min(offset, text.size());
    while (boundary < text.size() && IsUtf8ContinuationByte(static_cast<unsigned char>(text[boundary]))) {
        boundary += 1U;
    }
    return boundary;
}

std::size_t FindBoundedShapingSplit(std::string_view text, std::size_t start) {
    const std::size_t preferred = std::min(start + kMaxHarfBuzzRunBytes, text.size());
    if (preferred >= text.size()) {
        return text.size();
    }

    const std::size_t search_start = start + std::min(kMaxHarfBuzzRunBytes, preferred - start);
    const std::size_t lower_bound =
        preferred > kShapingBoundarySearchBytes ? preferred - kShapingBoundarySearchBytes : start + 1U;
    for (std::size_t cursor = search_start; cursor > lower_bound; cursor -= 1U) {
        const std::size_t boundary = PreviousUtf8Boundary(text, cursor);
        if (boundary <= start) {
            break;
        }
        const unsigned char previous = static_cast<unsigned char>(text[boundary - 1U]);
        if (previous < 0x80U && std::isspace(previous) != 0) {
            return boundary;
        }
        cursor = boundary;
    }

    const std::size_t utf8_boundary = PreviousUtf8Boundary(text, preferred);
    if (utf8_boundary > start) {
        return utf8_boundary;
    }
    return std::max(NextUtf8Boundary(text, preferred), start + 1U);
}

std::size_t NextUtf8Codepoint(std::string_view text, std::size_t offset, std::uint32_t* out_codepoint) {
    if (out_codepoint == nullptr || offset >= text.size()) {
        return text.size();
    }

    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80U) {
        *out_codepoint = lead;
        return offset + 1U;
    }

    const auto is_continuation = [&](std::size_t index) {
        return index < text.size() &&
               (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U;
    };

    if ((lead & 0xE0U) == 0xC0U && is_continuation(offset + 1U)) {
        *out_codepoint =
            ((static_cast<std::uint32_t>(lead & 0x1FU)) << 6U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 1U]) & 0x3FU);
        return offset + 2U;
    }
    if ((lead & 0xF0U) == 0xE0U && is_continuation(offset + 1U) && is_continuation(offset + 2U)) {
        *out_codepoint =
            ((static_cast<std::uint32_t>(lead & 0x0FU)) << 12U) |
            ((static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 1U]) & 0x3FU)) << 6U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 2U]) & 0x3FU);
        return offset + 3U;
    }
    if ((lead & 0xF8U) == 0xF0U &&
        is_continuation(offset + 1U) &&
        is_continuation(offset + 2U) &&
        is_continuation(offset + 3U)) {
        *out_codepoint =
            ((static_cast<std::uint32_t>(lead & 0x07U)) << 18U) |
            ((static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 1U]) & 0x3FU)) << 12U) |
            ((static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 2U]) & 0x3FU)) << 6U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + 3U]) & 0x3FU);
        return offset + 4U;
    }

    *out_codepoint = 0xFFFD;
    return offset + 1U;
}

bool CharsetContainsCodepoint(std::string_view charset, std::uint32_t codepoint) {
    for (std::size_t offset = 0U; offset < charset.size();) {
        std::uint32_t charset_codepoint = 0U;
        const std::size_t next = NextUtf8Codepoint(charset, offset, &charset_codepoint);
        if (charset_codepoint == codepoint) {
            return true;
        }
        offset = next;
    }
    return false;
}

struct ResolvedLineBoxMetrics {
    float ascent = 0.0f;
    float descent = 0.0f;
    float height = 0.0f;
};

ResolvedLineBoxMetrics BuildLineBoxMetrics(float raw_ascent, float raw_descent, float line_spacing, bool clamp_components) {
    const float resolved_spacing = std::max(line_spacing, 1.0f);
    const float total_leading = resolved_spacing - (raw_ascent + raw_descent);
    const float ascent_leading = std::floor(total_leading * 0.5f);
    const float descent_leading = total_leading - ascent_leading;
    const float ascent = clamp_components
        ? std::max(raw_ascent + ascent_leading, 0.0f)
        : (raw_ascent + ascent_leading);
    const float descent = clamp_components
        ? std::max(raw_descent + descent_leading, 0.0f)
        : (resolved_spacing - ascent);
    return ResolvedLineBoxMetrics{
        ascent,
        descent,
        clamp_components ? std::max(ascent + descent, 1.0f) : resolved_spacing,
    };
}

} // namespace

void UiRuntime::MeasureText(
    const std::uint8_t* utf8_str,
    std::uint32_t len,
    std::uint32_t font_id,
    float size,
    float max_width,
    float* out_width,
    float* out_height) const {
    if (out_width != nullptr) {
        *out_width = 0.0f;
    }
    if (out_height != nullptr) {
        *out_height = 0.0f;
    }
    if (utf8_str == nullptr || len == 0U) {
        return;
    }

    UINode measure_node{};
    measure_node.is_text_node = true;
    measure_node.text_content.assign(reinterpret_cast<const char*>(utf8_str), reinterpret_cast<const char*>(utf8_str) + len);
    measure_node.font_id = font_id;
    measure_node.font_size = std::max(size, 1.0f);
    const std::optional<float> constraint =
        (std::isfinite(max_width) && max_width > 0.0f) ? std::optional<float>(max_width) : std::nullopt;
    const ParagraphLayout paragraph = LayoutParagraph(measure_node, constraint);
    if (out_width != nullptr) {
        *out_width = paragraph.width;
    }
    if (out_height != nullptr) {
        *out_height = paragraph.height;
    }
}



void UiRuntime::DestroyRegisteredFont(RegisteredFont& font) {
    if (font.font != nullptr) {
        hb_font_destroy(font.font);
        font.font = nullptr;
    }
    if (font.face != nullptr) {
        hb_face_destroy(font.face);
        font.face = nullptr;
    }
    if (font.blob != nullptr) {
        hb_blob_destroy(font.blob);
        font.blob = nullptr;
    }
    font.bytes.clear();
    font.bytes.shrink_to_fit();
    font.upem = 0U;
    font.extents = hb_font_extents_t{};
    font.has_extents = false;
    font.bullet_glyph_id.reset();
}



const UiRuntime::RegisteredFont* UiRuntime::LookupFont(std::uint32_t font_id) const {
    const auto found = font_registry_.find(font_id);
    return found == font_registry_.end() ? nullptr : &found->second;
}



UiRuntime::FontMetrics UiRuntime::GetFontMetrics(const RegisteredFont& font, float font_size) const {
    const float scale = font.upem != 0U ? (font_size / static_cast<float>(font.upem)) : 1.0f;
    const float raw_ascent = (font.has_extents && font.upem != 0U)
        ? std::max(0.0f, static_cast<float>(font.extents.ascender) * scale)
        : font_size;
    const float raw_descent = (font.has_extents && font.upem != 0U)
        ? std::max(0.0f, -static_cast<float>(font.extents.descender) * scale)
        : 0.0f;
    const float raw_height = std::max(raw_ascent + raw_descent, 1.0f);
    const float line_gap = (font.has_extents && font.upem != 0U)
        ? static_cast<float>(font.extents.line_gap) * scale
        : 0.0f;
    const ResolvedLineBoxMetrics metrics = BuildLineBoxMetrics(raw_ascent, raw_descent, raw_height + line_gap, true);
    return FontMetrics{metrics.ascent, metrics.descent, metrics.height};
}

float UiRuntime::GetFontLineHeight(std::uint32_t font_id, float font_size) const {
    const RegisteredFont* font = LookupFont(font_id);
    if (font == nullptr || font->font == nullptr) {
        return 0.0f;
    }
    return GetFontMetrics(*font, font_size).height;
}

UiRuntime::FontMetrics UiRuntime::ResolvePrimaryLineBoxMetrics(const UINode& node) const {
    const RegisteredFont* font = LookupFont(node.font_id);
    if (node.authored_line_height > 0.0f) {
        float raw_ascent = std::max(node.font_size, 1.0f);
        float raw_descent = 0.0f;
        if (font != nullptr && font->font != nullptr) {
            const float scale = font->upem != 0U ? (node.font_size / static_cast<float>(font->upem)) : 1.0f;
            raw_ascent = (font->has_extents && font->upem != 0U)
                ? std::max(0.0f, static_cast<float>(font->extents.ascender) * scale)
                : std::max(node.font_size, 1.0f);
            raw_descent = (font->has_extents && font->upem != 0U)
                ? std::max(0.0f, -static_cast<float>(font->extents.descender) * scale)
                : 0.0f;
        }
        const ResolvedLineBoxMetrics metrics = BuildLineBoxMetrics(raw_ascent, raw_descent, node.authored_line_height, false);
        return FontMetrics{metrics.ascent, metrics.descent, metrics.height};
    }
    if (font != nullptr && font->font != nullptr) {
        return GetFontMetrics(*font, node.font_size);
    }
    return FontMetrics{std::max(node.font_size, 1.0f), 0.0f, std::max(node.font_size, 1.0f)};
}

UiRuntime::FontMetrics UiRuntime::ResolveLineMetrics(
    const UINode& node,
    const FontMetrics& primary_line_box_metrics,
    float content_ascent,
    float content_descent) const {
    if (node.authored_line_height > 0.0f) {
        return primary_line_box_metrics;
    }
    const float line_ascent = std::max(primary_line_box_metrics.ascent, content_ascent);
    const float line_descent = std::max(primary_line_box_metrics.descent, content_descent);
    return FontMetrics{
        line_ascent,
        line_descent,
        std::max(line_ascent + line_descent, 1.0f),
    };
}



bool UiRuntime::FontHasGlyph(const RegisteredFont& font, std::uint32_t codepoint) const {
    if (font.font == nullptr) {
        return false;
    }
    hb_codepoint_t glyph = 0;
    return hb_font_get_nominal_glyph(font.font, codepoint, &glyph) != 0 && glyph != 0;
}



bool UiRuntime::ShapeTextWithFont(
    std::string_view text,
    const RegisteredFont& font,
    std::uint32_t font_id,
    float font_size,
    ShapedTextRun& out) const {
    out = ShapedTextRun{};
    if (font.font == nullptr) {
        return false;
    }

    const FontMetrics metrics = GetFontMetrics(font, font_size);
    out.font_id = font_id;
    out.ascent = metrics.ascent;
    out.descent = metrics.descent;
    out.height = out.ascent + out.descent;
    out.baseline = out.ascent;
    if (text.empty()) {
        return true;
    }

    if (text.size() > kMaxHarfBuzzRunBytes) {
        float cursor_x = 0.0f;
        std::size_t start = 0U;
        out.glyphs.reserve(text.size());
        while (start < text.size()) {
            const std::size_t end = FindBoundedShapingSplit(text, start);
            if (end <= start || end > text.size()) {
                return false;
            }
            ShapedTextRun shard{};
            if (!ShapeTextWithFont(text.substr(start, end - start), font, font_id, font_size, shard)) {
                return false;
            }
            out.ascent = std::max(out.ascent, shard.ascent);
            out.descent = std::max(out.descent, shard.descent);
            out.height = out.ascent + out.descent;
            out.baseline = out.ascent;
            for (GlyphPlacement glyph : shard.glyphs) {
                glyph.x += cursor_x;
                glyph.cluster += static_cast<std::uint32_t>(start);
                out.glyphs.push_back(glyph);
            }
            cursor_x += shard.width;
            start = end;
        }
        out.width = cursor_x;
        return true;
    }

    hb_font_t* sized_font = hb_font_create_sub_font(font.font);
    if (sized_font == nullptr) {
        return false;
    }

    const unsigned int ppem = std::max(1U, static_cast<unsigned int>(std::lround(std::max(font_size, 1.0f))));
    const int scaled_size = std::max(64, static_cast<int>(std::lround(std::max(font_size, 1.0f) * 64.0f)));
    hb_font_set_scale(sized_font, scaled_size, scaled_size);
    hb_font_set_ppem(sized_font, ppem, ppem);

    hb_buffer_t* buffer = hb_buffer_create();
    if (buffer == nullptr) {
        hb_font_destroy(sized_font);
        return false;
    }

    const ShapeInputText shaped_input = BuildShapeInputText(text);
    const std::string_view harfbuzz_text =
        shaped_input.expanded_text.empty()
            ? text
            : std::string_view(shaped_input.expanded_text.data(), shaped_input.expanded_text.size());
    hb_buffer_add_utf8(
        buffer,
        harfbuzz_text.data(),
        static_cast<int>(harfbuzz_text.size()),
        0,
        static_cast<int>(harfbuzz_text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(sized_font, buffer, nullptr, 0);

    unsigned int glyph_count = 0U;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);

    float cursor_x = 0.0f;
    float min_x = 0.0f;
    float max_x = 0.0f;
    out.glyphs.reserve(glyph_count);

    for (unsigned int index = 0; index < glyph_count; index += 1U) {
        const float x = cursor_x + static_cast<float>(positions[index].x_offset) / 64.0f;
        const float y = static_cast<float>(positions[index].y_offset) / 64.0f;
        const float advance = static_cast<float>(positions[index].x_advance) / 64.0f;
        const float next_cursor = cursor_x + advance;

        out.glyphs.push_back(GlyphPlacement{
            infos[index].codepoint,
            x,
            y,
            shaped_input.cluster_map.empty()
                ? infos[index].cluster
                : shaped_input.cluster_map[std::min<std::size_t>(infos[index].cluster, shaped_input.cluster_map.size() - 1U)],
            font_id,
            0U,
            font_size,
        });
        min_x = std::min({min_x, x, next_cursor});
        max_x = std::max({max_x, x, next_cursor});
        cursor_x = next_cursor;
    }

    out.width = std::max(0.0f, max_x - min_x);
    for (GlyphPlacement& glyph : out.glyphs) {
        glyph.x -= min_x;
    }

    hb_buffer_destroy(buffer);
    hb_font_destroy(sized_font);
    return true;
}

bool UiRuntime::ShapeMissingTextWithFont(
    std::string_view text,
    const RegisteredFont& font,
    std::uint32_t font_id,
    float font_size,
    ShapedTextRun& out) const {
    out = ShapedTextRun{};
    if (font.font == nullptr) {
        return false;
    }

    const FontMetrics metrics = GetFontMetrics(font, font_size);
    out.font_id = font_id;
    out.ascent = metrics.ascent;
    out.descent = metrics.descent;
    out.height = out.ascent + out.descent;
    out.baseline = out.ascent;
    if (text.empty()) {
        return true;
    }

    hb_font_t* sized_font = hb_font_create_sub_font(font.font);
    if (sized_font == nullptr) {
        return false;
    }

    const unsigned int ppem = std::max(1U, static_cast<unsigned int>(std::lround(std::max(font_size, 1.0f))));
    const int scaled_size = std::max(64, static_cast<int>(std::lround(std::max(font_size, 1.0f) * 64.0f)));
    hb_font_set_scale(sized_font, scaled_size, scaled_size);
    hb_font_set_ppem(sized_font, ppem, ppem);

    const std::uint32_t placeholder_glyph_id =
        font.tofu_glyph_id.has_value()
            ? *font.tofu_glyph_id
            : (font.bullet_glyph_id.has_value() ? *font.bullet_glyph_id : 0U);
    const hb_codepoint_t placeholder_glyph = static_cast<hb_codepoint_t>(placeholder_glyph_id);
    const float fallback_advance = std::max(std::max(font_size, metrics.height) * 0.6f, 1.0f);

    float cursor_x = 0.0f;
    out.glyphs.reserve(text.size());
    for (std::size_t offset = 0U; offset < text.size();) {
        std::uint32_t ignored_codepoint = 0U;
        const std::size_t next = NextUtf8Codepoint(text, offset, &ignored_codepoint);
        const hb_position_t raw_advance = hb_font_get_glyph_h_advance(sized_font, placeholder_glyph);
        const float advance = raw_advance != 0 ? static_cast<float>(raw_advance) / 64.0f : fallback_advance;
        out.glyphs.push_back(GlyphPlacement{
            static_cast<std::uint32_t>(placeholder_glyph),
            cursor_x,
            0.0f,
            static_cast<std::uint32_t>(offset),
            font_id,
            0U,
            font_size,
        });
        cursor_x += advance;
        offset = next;
    }
    out.width = cursor_x;
    hb_font_destroy(sized_font);
    return true;
}



std::vector<std::uint32_t> UiRuntime::ResolveFontChain(std::uint32_t font_id) const {
    if (font_id == 0U) {
        return {};
    }

    std::vector<std::uint32_t> chain{font_id};
    for (std::size_t index = 0; index < chain.size(); index += 1U) {
        const auto found = font_fallbacks_.find(chain[index]);
        if (found == font_fallbacks_.end()) {
            continue;
        }
        for (const std::uint32_t fallback_font_id : found->second) {
            if (fallback_font_id == 0U ||
                std::find(chain.begin(), chain.end(), fallback_font_id) != chain.end()) {
                continue;
            }
            chain.push_back(fallback_font_id);
        }
    }
    return chain;
}

std::uint32_t UiRuntime::ClassifyMissingFontCoverage(std::uint32_t codepoint) {
    if ((codepoint >= 0x0590U && codepoint <= 0x05FFU) ||
        (codepoint >= 0x0530U && codepoint <= 0x058FU) ||
        (codepoint >= 0x10A0U && codepoint <= 0x10FFU) ||
        (codepoint >= 0x2D00U && codepoint <= 0x2D2FU) ||
        (codepoint >= 0x0900U && codepoint <= 0x097FU) ||
        (codepoint >= 0x0980U && codepoint <= 0x09FFU) ||
        (codepoint >= 0x0A00U && codepoint <= 0x0A7FU) ||
        (codepoint >= 0x0A80U && codepoint <= 0x0AFFU) ||
        (codepoint >= 0x0B00U && codepoint <= 0x0B7FU) ||
        (codepoint >= 0x0B80U && codepoint <= 0x0BFFU) ||
        (codepoint >= 0x0C00U && codepoint <= 0x0C7FU) ||
        (codepoint >= 0x0C80U && codepoint <= 0x0CFFU) ||
        (codepoint >= 0x0D00U && codepoint <= 0x0D7FU) ||
        (codepoint >= 0x0D80U && codepoint <= 0x0DFFU) ||
        (codepoint >= 0x0E80U && codepoint <= 0x0EFFU) ||
        (codepoint >= 0x1000U && codepoint <= 0x109FU) ||
        (codepoint >= 0x1780U && codepoint <= 0x17FFU) ||
        (codepoint >= 0x1200U && codepoint <= 0x137FU) ||
        (codepoint >= 0x1380U && codepoint <= 0x139FU) ||
        (codepoint >= 0x2D80U && codepoint <= 0x2DDFU)) {
        return UI_MISSING_FONT_COVERAGE_SUPPLEMENTAL;
    }
    if ((codepoint >= 0x0600U && codepoint <= 0x06FFU) ||
        (codepoint >= 0x0750U && codepoint <= 0x077FU) ||
        (codepoint >= 0x08A0U && codepoint <= 0x08FFU) ||
        (codepoint >= 0xFB50U && codepoint <= 0xFDFFU) ||
        (codepoint >= 0xFE70U && codepoint <= 0xFEFFU)) {
        return UI_MISSING_FONT_COVERAGE_ARABIC;
    }
    if (codepoint >= 0x0E00U && codepoint <= 0x0E7FU) {
        return UI_MISSING_FONT_COVERAGE_THAI;
    }
    if ((codepoint >= 0x3040U && codepoint <= 0x30FFU) ||
        (codepoint >= 0x3000U && codepoint <= 0x303FU) ||
        (codepoint >= 0x3400U && codepoint <= 0x4DBFU) ||
        (codepoint >= 0x4E00U && codepoint <= 0x9FFFU) ||
        (codepoint >= 0xFF01U && codepoint <= 0xFF0FU) ||
        (codepoint >= 0xFF1AU && codepoint <= 0xFF20U) ||
        (codepoint >= 0xFF3BU && codepoint <= 0xFF40U) ||
        (codepoint >= 0xFF5BU && codepoint <= 0xFF65U) ||
        (codepoint >= 0xAC00U && codepoint <= 0xD7AFU) ||
        (codepoint >= 0xF900U && codepoint <= 0xFAFFU)) {
        return UI_MISSING_FONT_COVERAGE_CJK;
    }
    return UI_MISSING_FONT_COVERAGE_UNKNOWN;
}

void UiRuntime::ReportMissingFontCoverage(
    const std::vector<std::uint32_t>& font_chain,
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    std::string_view sample_text) const {
    if (primary_font_id == 0U || coverage_kind == UI_MISSING_FONT_COVERAGE_UNKNOWN || sample_text.empty()) {
        return;
    }
    std::string key = std::to_string(coverage_kind);
    key.push_back(':');
    for (const std::uint32_t font_id : font_chain) {
        key.append(std::to_string(font_id));
        key.push_back('|');
    }
    key.append(sample_text.data(), sample_text.size());
    if (!reported_missing_font_coverage_keys_.insert(key).second) {
        return;
    }
    as_on_missing_font_coverage(
        primary_font_id,
        coverage_kind,
        reinterpret_cast<const std::uint8_t*>(sample_text.data()),
        static_cast<std::uint32_t>(sample_text.size()));
}



YGSize UiRuntime::MeasureTextNode(const UINode& node, float width, YGMeasureMode width_mode) const {
    if (!node.is_text_node) {
        return YGSize{0.0f, 0.0f};
    }
    if (node.text_content.empty() &&
        !IsEditorTextNode(node)) {
        return YGSize{0.0f, 0.0f};
    }

    const std::optional<float> constraint =
        width_mode == YGMeasureModeUndefined ? std::nullopt : std::optional<float>(std::max(width, 0.0f));
    const ParagraphLayout paragraph = LayoutParagraph(node, constraint);
    const float measured_width = width_mode == YGMeasureModeExactly ? std::max(width, 0.0f) : paragraph.width;
    return YGSize{measured_width, paragraph.height};
}



float UiRuntime::MeasureSingleLineWidth(std::string_view text, std::uint32_t font_id, float font_size, bool obscured) const {
    const bool profile_active = text_commit_profile_active_;
    const ProfileClock::time_point width_start = profile_active ? ProfileClock::now() : ProfileClock::time_point{};
    ShapedTextRun shaped{};
    const float width = ShapeText(text, font_id, font_size, shaped, obscured) ? shaped.width : 0.0f;
    if (profile_active) {
        current_text_commit_profile_.measure_single_line_width_calls += 1U;
        current_text_commit_profile_.measure_single_line_width_bytes += text.size();
        current_text_commit_profile_.measure_single_line_width_ms +=
            ElapsedMilliseconds(width_start, ProfileClock::now());
    }
    return width;
}



bool UiRuntime::ShapeObscuredText(std::string_view text, std::uint32_t font_id, float font_size, ShapedTextRun& out) const {
    out = ShapedTextRun{};
    const RegisteredFont* primary_font = LookupFont(font_id);
    if (primary_font == nullptr || primary_font->font == nullptr) {
        return false;
    }

    const FontMetrics primary_metrics = GetFontMetrics(*primary_font, font_size);
    out.font_id = font_id;
    out.ascent = primary_metrics.ascent;
    out.descent = primary_metrics.descent;
    out.height = out.ascent + out.descent;
    out.baseline = out.ascent;

    if (text.empty()) {
        return true;
    }

    static constexpr char kBulletUtf8[] = "\xE2\x80\xA2";
    ShapedTextRun bullet{};
    if (!ShapeTextWithFont(std::string_view(kBulletUtf8, 3U), *primary_font, font_id, font_size, bullet) || bullet.glyphs.empty()) {
        return ShapeTextWithFont(text, *primary_font, font_id, font_size, out);
    }

    out.ascent = std::max(out.ascent, bullet.ascent);
    out.descent = std::max(out.descent, bullet.descent);
    out.height = out.ascent + out.descent;
    out.baseline = out.ascent;
    out.glyphs.reserve(text.size());
    const GlyphPlacement template_glyph = bullet.glyphs.front();
    const float bullet_advance = bullet.width;
    float cursor_x = 0.0f;
    for (std::size_t offset = 0U; offset < text.size();) {
        std::uint32_t codepoint = 0U;
        const std::size_t next = NextUtf8Codepoint(text, offset, &codepoint);
        (void)codepoint;
        GlyphPlacement glyph = template_glyph;
        glyph.x += cursor_x;
        glyph.cluster = static_cast<std::uint32_t>(offset);
        glyph.font_id = font_id;
        glyph.color = 0U;
        glyph.font_size = font_size;
        out.glyphs.push_back(glyph);
        cursor_x += bullet_advance;
        offset = next;
    }
    out.width = cursor_x;
    return true;
}



bool UiRuntime::ShapeText(std::string_view text, std::uint32_t font_id, float font_size, ShapedTextRun& out, bool obscured) const {
    const bool profile_active = text_commit_profile_active_;
    const ProfileClock::time_point shape_start = profile_active ? ProfileClock::now() : ProfileClock::time_point{};
    if (obscured) {
        const bool shaped = ShapeObscuredText(text, font_id, font_size, out);
        if (profile_active) {
            current_text_commit_profile_.shape_text_calls += 1U;
            current_text_commit_profile_.shape_text_bytes += text.size();
            current_text_commit_profile_.shape_text_ms +=
                ElapsedMilliseconds(shape_start, ProfileClock::now());
        }
        return shaped;
    }
    out = ShapedTextRun{};
    const RegisteredFont* primary_font = LookupFont(font_id);
    if (primary_font == nullptr || primary_font->font == nullptr) {
        return false;
    }

    const FontMetrics primary_metrics = GetFontMetrics(*primary_font, font_size);
    out.font_id = font_id;
    out.ascent = primary_metrics.ascent;
    out.descent = primary_metrics.descent;
    out.height = out.ascent + out.descent;
    out.baseline = out.ascent;

    if (text.empty()) {
        if (profile_active) {
            current_text_commit_profile_.shape_text_calls += 1U;
            current_text_commit_profile_.shape_text_bytes += text.size();
            current_text_commit_profile_.shape_text_ms +=
                ElapsedMilliseconds(shape_start, ProfileClock::now());
        }
        return true;
    }

    const std::vector<std::uint32_t> font_chain = ResolveFontChain(font_id);
    struct FontSegment {
        std::size_t start = 0U;
        std::size_t end = 0U;
        std::uint32_t font_id = 0U;
        bool is_missing = false;
        std::uint32_t coverage_kind = UI_MISSING_FONT_COVERAGE_UNKNOWN;
    };
    std::vector<FontSegment> segments{};
    std::size_t segment_start = 0U;
    std::uint32_t segment_font_id = 0U;
    bool segment_missing = false;
    std::uint32_t segment_coverage_kind = UI_MISSING_FONT_COVERAGE_UNKNOWN;
    bool has_segment = false;

    for (std::size_t offset = 0U; offset < text.size();) {
        std::uint32_t codepoint = 0U;
        const std::size_t next = NextUtf8Codepoint(text, offset, &codepoint);
        std::uint32_t resolved_font_id = 0U;
        bool missing_coverage = false;
        std::uint32_t coverage_kind = UI_MISSING_FONT_COVERAGE_UNKNOWN;
        for (const std::uint32_t candidate_font_id : font_chain) {
            const RegisteredFont* candidate_font = LookupFont(candidate_font_id);
            if (candidate_font != nullptr &&
                candidate_font->font != nullptr &&
                FontHasGlyph(*candidate_font, codepoint)) {
                resolved_font_id = candidate_font_id;
                break;
            }
        }
        if (resolved_font_id == 0U) {
            coverage_kind = ClassifyMissingFontCoverage(codepoint);
            resolved_font_id = font_id;
            missing_coverage = true;
        }

        if (!has_segment) {
            segment_start = offset;
            segment_font_id = resolved_font_id;
            segment_missing = missing_coverage;
            segment_coverage_kind = coverage_kind;
            has_segment = true;
        } else if (resolved_font_id != segment_font_id || missing_coverage != segment_missing || coverage_kind != segment_coverage_kind) {
            segments.push_back(FontSegment{segment_start, offset, segment_font_id, segment_missing, segment_coverage_kind});
            segment_start = offset;
            segment_font_id = resolved_font_id;
            segment_missing = missing_coverage;
            segment_coverage_kind = coverage_kind;
        }
        offset = next;
    }
    if (has_segment) {
        segments.push_back(FontSegment{segment_start, text.size(), segment_font_id, segment_missing, segment_coverage_kind});
    }

    float cursor_x = 0.0f;
    out.glyphs.reserve(text.size());
    for (const FontSegment& segment : segments) {
        const RegisteredFont* segment_font = LookupFont(segment.font_id);
        const std::uint32_t effective_font_id =
            segment_font != nullptr && segment_font->font != nullptr ? segment.font_id : font_id;
        if (segment_font == nullptr || segment_font->font == nullptr) {
            segment_font = primary_font;
        }

ShapedTextRun shaped_segment{};
        const std::string_view segment_text(text.data() + segment.start, segment.end - segment.start);
if (segment.is_missing) {
    ReportMissingFontCoverage(font_chain, font_id, segment.coverage_kind, segment_text);
}
const bool shaped =
    segment.is_missing
        ? ShapeMissingTextWithFont(
                      segment_text,
                      *segment_font,
                      effective_font_id,
                      font_size,
                      shaped_segment)
                : ShapeTextWithFont(
                      segment_text,
                      *segment_font,
                      effective_font_id,
                      font_size,
                      shaped_segment);
        if (!shaped) {
            return false;
        }

        float seg_ascent = shaped_segment.ascent;
        float seg_descent = shaped_segment.descent;

        // Symmetrically balance the fallback font's metrics relative to the primary font's line box
        if (segment_font != primary_font) {
            const float seg_height = seg_ascent + seg_descent;
            if (seg_height > primary_metrics.height) {
                const float excess = seg_height - primary_metrics.height;
                const float half_excess = excess * 0.5f;
                seg_ascent = primary_metrics.ascent + half_excess;
                seg_descent = primary_metrics.descent + half_excess;
            } else {
                seg_ascent = primary_metrics.ascent;
                seg_descent = primary_metrics.descent;
            }
        }

        out.ascent = std::max(out.ascent, seg_ascent);
        out.descent = std::max(out.descent, seg_descent);
        out.height = out.ascent + out.descent;
        out.baseline = out.ascent;
        
        for (GlyphPlacement glyph : shaped_segment.glyphs) {
            glyph.x += cursor_x;
            glyph.cluster += static_cast<std::uint32_t>(segment.start);
            glyph.color = 0U;
            glyph.font_size = font_size;
            out.glyphs.push_back(glyph);
        }
        cursor_x += shaped_segment.width;
    }
    out.width = cursor_x;
    return true;
}

bool UiRuntime::ShapeDynamicTextFastPath(const UINode& node, std::string_view text, ShapedTextRun& out) const {
    out = ShapedTextRun{};
    if (!node.dynamic_text_fast_path_enabled ||
        node.dynamic_text_charset.empty() ||
        node.has_text_style_runs ||
        node.is_obscured ||
        text.empty() ||
        text.find_first_of("\r\n") != std::string_view::npos) {
        return false;
    }

    if (dynamic_text_prepare_profile_active_) {
        current_dynamic_text_prepare_profile_.fast_path_attempts += 1U;
        current_dynamic_text_prepare_profile_.composed_bytes += text.size();
    }

    UINode& mutable_node = const_cast<UINode&>(node);
    float cursor_x = 0.0f;
    std::size_t local_byte_offset = 0U;
    out.glyphs.reserve(text.size());

    for (std::size_t offset = 0U; offset < text.size();) {
        std::uint32_t codepoint = 0U;
        const std::size_t next = NextUtf8Codepoint(text, offset, &codepoint);
        if (!CharsetContainsCodepoint(node.dynamic_text_charset, codepoint)) {
            RecordDynamicTextFastPathFallback();
            return false;
        }

        auto cached = std::find_if(
            mutable_node.dynamic_text_glyph_cache.begin(),
            mutable_node.dynamic_text_glyph_cache.end(),
            [codepoint](const DynamicTextGlyphCacheEntry& entry) {
                return entry.codepoint == codepoint;
            });

        if (cached == mutable_node.dynamic_text_glyph_cache.end()) {
            ShapedTextRun shaped_codepoint{};
            const std::string_view codepoint_text(text.data() + offset, next - offset);
            if (!ShapeText(codepoint_text, node.font_id, node.font_size, shaped_codepoint, false)) {
                RecordDynamicTextFastPathFallback();
                return false;
            }

            DynamicTextGlyphCacheEntry entry{};
            entry.codepoint = codepoint;
            entry.width = shaped_codepoint.width;
            entry.height = shaped_codepoint.height;
            entry.baseline = shaped_codepoint.baseline;
            entry.ascent = shaped_codepoint.ascent;
            entry.descent = shaped_codepoint.descent;
            entry.glyphs = shaped_codepoint.glyphs;
            mutable_node.dynamic_text_glyph_cache.push_back(std::move(entry));
            cached = mutable_node.dynamic_text_glyph_cache.end() - 1;
            if (dynamic_text_prepare_profile_active_) {
                current_dynamic_text_prepare_profile_.cache_misses += 1U;
            }
        } else if (dynamic_text_prepare_profile_active_) {
            current_dynamic_text_prepare_profile_.cache_hits += 1U;
        }

        out.ascent = std::max(out.ascent, cached->ascent);
        out.descent = std::max(out.descent, cached->descent);
        out.height = std::max(out.height, cached->height);
        out.baseline = std::max(out.baseline, cached->baseline);

        for (GlyphPlacement glyph : cached->glyphs) {
            glyph.x += cursor_x;
            glyph.cluster += static_cast<std::uint32_t>(local_byte_offset);
            glyph.color = node.text_color;
            glyph.font_size = node.font_size;
            out.glyphs.push_back(glyph);
        }

        cursor_x += cached->width;
        local_byte_offset += (next - offset);
        offset = next;
    }

    out.width = cursor_x;
    out.height = std::max(out.height, out.ascent + out.descent);
    out.baseline = std::max(out.baseline, out.ascent);
    if (dynamic_text_prepare_profile_active_) {
        current_dynamic_text_prepare_profile_.fast_path_successes += 1U;
        current_dynamic_text_prepare_profile_.composed_glyphs += static_cast<std::uint32_t>(out.glyphs.size());
    }
    return true;
}

bool UiRuntime::ShapeTextStyledRange(const UINode& node, std::uint32_t start, std::uint32_t end, ShapedTextRun& out) const {
    out = ShapedTextRun{};
    const std::uint32_t text_length = static_cast<std::uint32_t>(node.text_content.size());
    const std::uint32_t clamped_start = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(std::max(end, clamped_start), text_length);
    if (clamped_start == clamped_end) {
        return ShapeText(std::string_view{}, node.font_id, node.font_size, out, node.is_obscured);
    }
    if (!node.has_text_style_runs || node.text_style_runs.empty()) {
        const std::string_view plain_text(
            node.text_content.data() + clamped_start,
            static_cast<std::size_t>(clamped_end - clamped_start));
        bool shaped = ShapeDynamicTextFastPath(node, plain_text, out);
        if (!shaped) {
            shaped = ShapeText(
                plain_text,
                node.font_id,
                node.font_size,
                out,
                node.is_obscured);
        }
        if (shaped) {
            for (GlyphPlacement& glyph : out.glyphs) {
                glyph.color = node.text_color;
                glyph.font_size = node.font_size;
            }
        }
        return shaped;
    }

    struct StyledSpan {
        std::uint32_t start = 0U;
        std::uint32_t end = 0U;
        std::uint32_t font_id = 0U;
        float font_size = 16.0f;
        std::uint32_t color = 0U;
    };
    std::vector<StyledSpan> spans{};
    std::uint32_t cursor = clamped_start;
    for (const TextStyleRun& run : node.text_style_runs) {
        if (run.end <= clamped_start || run.start >= clamped_end) {
            continue;
        }
        const std::uint32_t run_start = std::max(run.start, clamped_start);
        const std::uint32_t run_end = std::min(run.end, clamped_end);
        if (cursor < run_start) {
            spans.push_back(StyledSpan{cursor, run_start, node.font_id, node.font_size, node.text_color});
        }
        spans.push_back(StyledSpan{run_start, run_end, run.font_id != 0U ? run.font_id : node.font_id, run.font_size, run.color});
        cursor = run_end;
    }
    if (cursor < clamped_end) {
        spans.push_back(StyledSpan{cursor, clamped_end, node.font_id, node.font_size, node.text_color});
    }
    if (spans.empty()) {
        spans.push_back(StyledSpan{clamped_start, clamped_end, node.font_id, node.font_size, node.text_color});
    }

    struct ShapeSegment {
        std::uint32_t start = 0U;
        std::uint32_t end = 0U;
        std::uint32_t font_id = 0U;
        float font_size = 16.0f;
        std::size_t first_span = 0U;
        std::size_t last_span = 0U;
    };
    std::vector<ShapeSegment> shape_segments{};
    for (std::size_t index = 0U; index < spans.size(); index += 1U) {
        const StyledSpan& span = spans[index];
        if (shape_segments.empty()) {
            shape_segments.push_back(ShapeSegment{span.start, span.end, span.font_id, span.font_size, index, index});
            continue;
        }
        ShapeSegment& segment = shape_segments.back();
        if (segment.end == span.start &&
            segment.font_id == span.font_id &&
            std::abs(segment.font_size - span.font_size) < 0.001f) {
            segment.end = span.end;
            segment.last_span = index;
            continue;
        }
        shape_segments.push_back(ShapeSegment{span.start, span.end, span.font_id, span.font_size, index, index});
    }

    float cursor_x = 0.0f;
    out.glyphs.reserve(static_cast<std::size_t>(clamped_end - clamped_start));
    for (const ShapeSegment& segment : shape_segments) {
        ShapedTextRun shaped_segment{};
        if (!ShapeText(
                std::string_view(
                    node.text_content.data() + segment.start,
                    static_cast<std::size_t>(segment.end - segment.start)),
                segment.font_id,
                segment.font_size,
                shaped_segment,
                node.is_obscured)) {
            if (segment.font_id == node.font_id) {
                return false;
            }
            if (!ShapeText(
                    std::string_view(
                        node.text_content.data() + segment.start,
                        static_cast<std::size_t>(segment.end - segment.start)),
                    node.font_id,
                    segment.font_size,
                    shaped_segment,
                    node.is_obscured)) {
                return false;
            }
        }
        out.ascent = std::max(out.ascent, shaped_segment.ascent);
        out.descent = std::max(out.descent, shaped_segment.descent);
        out.height = out.ascent + out.descent;
        out.baseline = out.ascent;

        std::size_t color_span_index = segment.first_span;
        for (GlyphPlacement glyph : shaped_segment.glyphs) {
            const std::uint32_t absolute_cluster = segment.start + glyph.cluster;
            while (color_span_index < spans.size() &&
                   absolute_cluster >= spans[color_span_index].end &&
                   color_span_index < segment.last_span) {
                color_span_index += 1U;
            }
            const StyledSpan& color_span = spans[std::min(color_span_index, segment.last_span)];
            glyph.x += cursor_x;
            glyph.cluster = absolute_cluster - clamped_start;
            glyph.color = color_span.color;
            glyph.font_size = segment.font_size;
            out.glyphs.push_back(glyph);
        }
        cursor_x += shaped_segment.width;
    }
    out.width = cursor_x;
    return true;
}


void UiRuntime::RebuildLiveFallbackFontBuffer() const {
    live_fallback_font_buffer_.clear();
    for (const UINode& node : node_pool_) {
        if (!node.is_active || !node.is_text_node || node.text_content.empty()) {
            continue;
        }
        ShapedTextRun shaped{};
        if (!ShapeTextStyledRange(
                node,
                0U,
                static_cast<std::uint32_t>(node.text_content.size()),
                shaped)) {
            continue;
        }
        for (const GlyphPlacement& glyph : shaped.glyphs) {
            if (glyph.font_id == 0U || glyph.font_id == node.font_id) {
                continue;
            }
            if (std::find(
                    live_fallback_font_buffer_.begin(),
                    live_fallback_font_buffer_.end(),
                    glyph.font_id) == live_fallback_font_buffer_.end()) {
                live_fallback_font_buffer_.push_back(glyph.font_id);
            }
        }
    }
}


} // namespace effindom::v2::ui
