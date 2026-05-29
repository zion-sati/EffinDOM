#include "UiRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <hb-ot.h>
#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/udata.h>
#include <unicode/unistr.h>
#include <woff2/decode.h>

namespace effindom::v2::ui {

namespace {

constexpr std::size_t kNonWrappingFragmentOverscanCount = 1U;

bool IsWoff2Font(std::string_view bytes) {
    return bytes.size() >= 4U &&
        bytes[0] == 'w' &&
        bytes[1] == 'O' &&
        bytes[2] == 'F' &&
        bytes[3] == '2';
}

bool DecodeWoff2Font(const std::uint8_t* bytes, std::uint32_t length, std::vector<std::uint8_t>& out) {
    std::string decoded_bytes{};
    woff2::WOFF2StringOut decoded_stream(&decoded_bytes);
    if (!woff2::ConvertWOFF2ToTTF(bytes, static_cast<std::size_t>(length), &decoded_stream)) {
        return false;
    }
    out.assign(decoded_bytes.begin(), decoded_bytes.end());
    return !out.empty();
}

bool FontFaceIsFixedPitch(hb_face_t* face) {
    if (face == nullptr) {
        return false;
    }

    hb_blob_t* post_blob = hb_face_reference_table(face, HB_TAG('p', 'o', 's', 't'));
    if (post_blob == nullptr) {
        return false;
    }

    unsigned int length = 0U;
    const char* data = hb_blob_get_data(post_blob, &length);
    bool fixed_pitch = false;
    if (data != nullptr && length >= 16U) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(data);
        const std::uint32_t is_fixed_pitch =
            (static_cast<std::uint32_t>(bytes[12]) << 24U) |
            (static_cast<std::uint32_t>(bytes[13]) << 16U) |
            (static_cast<std::uint32_t>(bytes[14]) << 8U) |
            static_cast<std::uint32_t>(bytes[15]);
        fixed_pitch = is_fixed_pitch != 0U;
    }
    hb_blob_destroy(post_blob);
    return fixed_pitch;
}

std::optional<std::uint32_t> ResolveNominalGlyph(hb_font_t* font, std::uint32_t codepoint) {
    if (font == nullptr) {
        return std::nullopt;
    }
    hb_codepoint_t glyph = 0;
    if (hb_font_get_nominal_glyph(font, codepoint, &glyph) == 0 || glyph == 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(glyph);
}

bool ReadUint16(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t& out) {
    if (offset + 2U > bytes.size()) {
        return false;
    }
    out =
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]);
    return true;
}

bool ReadInt16(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::int16_t& out) {
    std::uint16_t value = 0U;
    if (!ReadUint16(bytes, offset, value)) {
        return false;
    }
    out = static_cast<std::int16_t>(value);
    return true;
}

bool ReadUint32(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t& out) {
    if (offset + 4U > bytes.size()) {
        return false;
    }
    out =
        (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
    return true;
}

struct SfntTable {
    std::uint32_t offset = 0U;
    std::uint32_t length = 0U;
};

bool FindSfntTable(const std::vector<std::uint8_t>& bytes, const char tag[4], SfntTable& out) {
    std::uint16_t num_tables = 0U;
    if (!ReadUint16(bytes, 4U, num_tables)) {
        return false;
    }
    std::size_t record_offset = 12U;
    for (std::uint16_t index = 0U; index < num_tables; index += 1U, record_offset += 16U) {
        if (record_offset + 16U > bytes.size()) {
            return false;
        }
        if (bytes[record_offset] != static_cast<std::uint8_t>(tag[0]) ||
            bytes[record_offset + 1U] != static_cast<std::uint8_t>(tag[1]) ||
            bytes[record_offset + 2U] != static_cast<std::uint8_t>(tag[2]) ||
            bytes[record_offset + 3U] != static_cast<std::uint8_t>(tag[3])) {
            continue;
        }
        if (!ReadUint32(bytes, record_offset + 8U, out.offset) ||
            !ReadUint32(bytes, record_offset + 12U, out.length) ||
            out.offset + out.length > bytes.size()) {
            return false;
        }
        return true;
    }
    return false;
}

bool GlyphIndexFromBmpCmap(
    const std::vector<std::uint8_t>& bytes,
    const SfntTable& cmap_table,
    std::uint32_t codepoint,
    std::uint16_t& out_glyph) {
    out_glyph = 0U;
    if (codepoint > 0xFFFFU || cmap_table.offset + 4U > bytes.size()) {
        return false;
    }

    std::uint16_t num_subtables = 0U;
    if (!ReadUint16(bytes, cmap_table.offset + 2U, num_subtables)) {
        return false;
    }

    std::uint32_t chosen_offset = 0U;
    for (std::uint16_t index = 0U; index < num_subtables; index += 1U) {
        const std::size_t record_offset = cmap_table.offset + 4U + static_cast<std::size_t>(index) * 8U;
        std::uint16_t platform_id = 0U;
        std::uint16_t encoding_id = 0U;
        std::uint32_t subtable_offset = 0U;
        if (!ReadUint16(bytes, record_offset, platform_id) ||
            !ReadUint16(bytes, record_offset + 2U, encoding_id) ||
            !ReadUint32(bytes, record_offset + 4U, subtable_offset)) {
            return false;
        }
        const std::size_t absolute_offset = cmap_table.offset + static_cast<std::size_t>(subtable_offset);
        std::uint16_t format = 0U;
        if (!ReadUint16(bytes, absolute_offset, format)) {
            return false;
        }
        if (format == 4U && platform_id == 3U && (encoding_id == 1U || encoding_id == 0U)) {
            chosen_offset = static_cast<std::uint32_t>(absolute_offset);
            break;
        }
    }
    if (chosen_offset == 0U) {
        return false;
    }

    std::uint16_t seg_count_x2 = 0U;
    if (!ReadUint16(bytes, chosen_offset + 6U, seg_count_x2)) {
        return false;
    }
    const std::size_t seg_count = static_cast<std::size_t>(seg_count_x2 / 2U);
    const std::size_t end_codes_offset = chosen_offset + 14U;
    const std::size_t start_codes_offset = end_codes_offset + seg_count * 2U + 2U;
    const std::size_t id_deltas_offset = start_codes_offset + seg_count * 2U;
    const std::size_t id_range_offsets_offset = id_deltas_offset + seg_count * 2U;
    if (id_range_offsets_offset + seg_count * 2U > bytes.size()) {
        return false;
    }

    for (std::size_t segment = 0U; segment < seg_count; segment += 1U) {
        std::uint16_t end_code = 0U;
        std::uint16_t start_code = 0U;
        std::int16_t id_delta = 0;
        std::uint16_t id_range_offset = 0U;
        if (!ReadUint16(bytes, end_codes_offset + segment * 2U, end_code) ||
            !ReadUint16(bytes, start_codes_offset + segment * 2U, start_code) ||
            !ReadInt16(bytes, id_deltas_offset + segment * 2U, id_delta) ||
            !ReadUint16(bytes, id_range_offsets_offset + segment * 2U, id_range_offset)) {
            return false;
        }
        if (codepoint < start_code || codepoint > end_code) {
            continue;
        }
        if (id_range_offset == 0U) {
            out_glyph = static_cast<std::uint16_t>((codepoint + static_cast<std::uint32_t>(id_delta)) & 0xFFFFU);
            return out_glyph != 0U;
        }
        const std::size_t glyph_offset =
            id_range_offsets_offset +
            segment * 2U +
            id_range_offset +
            static_cast<std::size_t>(codepoint - start_code) * 2U;
        std::uint16_t glyph_index = 0U;
        if (!ReadUint16(bytes, glyph_offset, glyph_index) || glyph_index == 0U) {
            return false;
        }
        out_glyph = static_cast<std::uint16_t>((glyph_index + static_cast<std::uint32_t>(id_delta)) & 0xFFFFU);
        return out_glyph != 0U;
    }
    return false;
}

bool DetectAsciiFixedPitchFont(const std::vector<std::uint8_t>& bytes) {
    SfntTable cmap_table{};
    SfntTable hhea_table{};
    SfntTable hmtx_table{};
    SfntTable maxp_table{};
    if (!FindSfntTable(bytes, "cmap", cmap_table) ||
        !FindSfntTable(bytes, "hhea", hhea_table) ||
        !FindSfntTable(bytes, "hmtx", hmtx_table) ||
        !FindSfntTable(bytes, "maxp", maxp_table)) {
        return false;
    }

    std::uint16_t number_of_h_metrics = 0U;
    std::uint16_t num_glyphs = 0U;
    if (!ReadUint16(bytes, hhea_table.offset + 34U, number_of_h_metrics) ||
        !ReadUint16(bytes, maxp_table.offset + 4U, num_glyphs) ||
        number_of_h_metrics == 0U ||
        num_glyphs == 0U) {
        return false;
    }

    std::uint16_t first_advance = 0U;
    bool has_first_advance = false;
    for (std::uint32_t codepoint = 0x20U; codepoint <= 0x7EU; codepoint += 1U) {
        std::uint16_t glyph_index = 0U;
        if (!GlyphIndexFromBmpCmap(bytes, cmap_table, codepoint, glyph_index)) {
            return false;
        }
        const std::size_t metric_index = std::min<std::size_t>(glyph_index, number_of_h_metrics - 1U);
        std::uint16_t advance = 0U;
        if (!ReadUint16(bytes, hmtx_table.offset + metric_index * 4U, advance)) {
            return false;
        }
        if (!has_first_advance) {
            first_advance = advance;
            has_first_advance = true;
            continue;
        }
        if (advance != first_advance) {
            return false;
        }
    }
    return has_first_advance;
}

float ClusterXForIndex(
    const std::vector<TextClusterStop>& stops,
    float shaped_width,
    std::uint32_t local_index,
    std::size_t text_length) {
    float x = shaped_width;
    for (const TextClusterStop& stop : stops) {
        if (stop.index > local_index) {
            break;
        }
        x = stop.x;
    }
    if (local_index == 0U) {
        x = 0.0f;
    }
    if (local_index >= text_length) {
        x = shaped_width;
    }
    return x;
}

std::uint32_t SkipLeadingLineBreaks(std::string_view text, std::uint32_t start, std::uint32_t end) {
    const std::uint32_t text_length = static_cast<std::uint32_t>(text.size());
    std::uint32_t cursor = std::min(start, text_length);
    const std::uint32_t clamped_end = std::min(end, text_length);
    while (cursor < clamped_end && (text[cursor] == '\n' || text[cursor] == '\r')) {
        cursor += 1U;
    }
    return cursor;
}

} // namespace

void UiRuntime::InvalidateTextLayoutCache(UINode& node, bool preserve_logical_line_shapes) {
    if (!node.is_text_node) {
        return;
    }
    node.text_layout_cache_valid = false;
    node.text_layout_cache_width_limit = -1.0f;
    node.text_layout_cache_max_line_width = 0.0f;
    node.line_heights.clear();
    node.line_ascents.clear();
    node.line_y_offsets.clear();
    node.text_glyphs_dirty = true;
    node.text_selection_visuals_dirty = false;
    node.visual_line_shape_cache_valid = false;
    node.visual_line_shapes.clear();
    if (!preserve_logical_line_shapes) {
        node.logical_line_shape_cache_valid = false;
        node.logical_line_shapes.clear();
    }
    node.nonwrap_fragment_cache_valid = false;
    node.nonwrap_fragment_line_offsets.clear();
    node.nonwrap_fragments.clear();
    node.cached_nonwrap_geometry_slices.clear();
    node.nonwrap_render_fragment_window_valid = false;
    node.nonwrap_render_fragment_start = 0U;
    node.nonwrap_render_fragment_end = 0U;
    node.text_render_window_valid = false;
    node.text_render_line_start = 0U;
    node.text_render_line_end = 0U;
}

bool UiRuntime::SetText(std::uint64_t handle, const std::uint8_t* utf8_str, std::uint32_t len) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || (utf8_str == nullptr && len > 0U)) {
        return false;
    }
    const bool should_emit_text_state =
        node->is_editable || node->semantic_role == UI_SEMANTIC_TEXTBOX;
    if (len == 0U) {
        node->text_content.clear();
    } else {
        node->text_content.assign(reinterpret_cast<const char*>(utf8_str), reinterpret_cast<const char*>(utf8_str) + len);
    }
    (void)ApplyAbsurdLineClamp(*node);
    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    node->selection_start = std::min(node->selection_start, text_length);
    node->selection_end = std::min(node->selection_end, text_length);
    node->text_line_starts_dirty = true;
    RebuildTextLineStarts(*node);
    const std::uint32_t normalized_length = static_cast<std::uint32_t>(node->text_content.size());
    if (node->has_text_style_runs) {
        for (TextStyleRun& run : node->text_style_runs) {
            run.start = std::min(run.start, normalized_length);
            run.end = std::min(std::max(run.end, run.start), normalized_length);
        }
    }
    node->selection_start = std::min(node->selection_start, normalized_length);
    node->selection_end = std::min(node->selection_end, normalized_length);
    if (node->is_editable) {
        ClearUndoHistory(*node);
    }
    InvalidateTextLayoutCache(*node);
    if (node->yg_node != nullptr) {
        YGNodeMarkDirty(node->yg_node);
    }
    node->is_dirty = true;
    layout_dirty_ = true;
    if (should_emit_text_state) {
        const auto* text_ptr = node->text_content.empty()
            ? nullptr
            : reinterpret_cast<const std::uint8_t*>(node->text_content.data());
        as_on_text_changed(handle, text_ptr, static_cast<std::uint32_t>(node->text_content.size()));
        as_on_selection_changed(handle, node->selection_start, node->selection_end);
    }
    return true;
}

bool UiRuntime::SetTextStyleRuns(std::uint64_t handle, std::uint32_t run_count, const std::uint32_t* runs_words) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || (run_count > 0U && runs_words == nullptr)) {
        return false;
    }
    node->text_style_runs.clear();
    node->has_text_style_runs = run_count > 0U;
    const std::uint32_t text_length = static_cast<std::uint32_t>(node->text_content.size());
    node->text_style_runs.reserve(run_count);
    for (std::uint32_t index = 0U; index < run_count; index += 1U) {
        const std::size_t base = static_cast<std::size_t>(index) * 7U;
        float run_font_size = 16.0f;
        std::memcpy(&run_font_size, &runs_words[base + 3U], sizeof(float));
        TextStyleRun run{
            std::min(runs_words[base + 0U], text_length),
            std::min(std::max(runs_words[base + 1U], runs_words[base + 0U]), text_length),
            runs_words[base + 2U],
            std::max(run_font_size, 1.0f),
            runs_words[base + 4U],
            runs_words[base + 5U],
            runs_words[base + 6U],
        };
        if (run.start >= run.end) {
            continue;
        }
        node->text_style_runs.push_back(run);
    }
    std::sort(
        node->text_style_runs.begin(),
        node->text_style_runs.end(),
        [](const TextStyleRun& lhs, const TextStyleRun& rhs) {
            if (lhs.start != rhs.start) {
                return lhs.start < rhs.start;
            }
            return lhs.end < rhs.end;
        });
    InvalidateTextLayoutCache(*node);
    if (node->yg_node != nullptr) {
        YGNodeMarkDirty(node->yg_node);
    }
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetFont(std::uint64_t handle, std::uint32_t font_id, float size) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    node->font_id = font_id;
    node->font_size = std::max(size, 1.0f);
    InvalidateTextLayoutCache(*node);
    if (node->yg_node != nullptr) {
        YGNodeMarkDirty(node->yg_node);
    }
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetLineHeight(std::uint64_t handle, float line_height) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    const float normalized_line_height = line_height > 0.0f ? line_height : 0.0f;
    if (std::abs(node->authored_line_height - normalized_line_height) < 0.001f) {
        return true;
    }
    node->authored_line_height = normalized_line_height;
    InvalidateTextLayoutCache(*node);
    if (node->yg_node != nullptr) {
        YGNodeMarkDirty(node->yg_node);
    }
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetTextColor(std::uint64_t handle, std::uint32_t color) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    if (node->text_color == color) {
        return true;
    }
    node->text_color = color;
    InvalidateTextLayoutCache(*node);
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetTextAlign(std::uint64_t handle, std::uint32_t align_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node ||
        (align_enum != ALIGN_LEFT && align_enum != ALIGN_CENTER && align_enum != ALIGN_RIGHT)) {
        return false;
    }
    if (node->text_align == align_enum) {
        return true;
    }
    node->text_align = align_enum;
    InvalidateTextLayoutCache(*node, true);
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetTextVerticalAlign(std::uint64_t handle, std::uint32_t align_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node ||
        (align_enum != VERTICAL_ALIGN_TOP &&
         align_enum != VERTICAL_ALIGN_CENTER &&
         align_enum != VERTICAL_ALIGN_BOTTOM)) {
        return false;
    }
    if (node->text_vertical_align == align_enum) {
        return true;
    }
    node->text_vertical_align = align_enum;
    InvalidateTextLayoutCache(*node, true);
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetTextLimits(std::uint64_t handle, std::int32_t max_chars, std::int32_t max_lines) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node || max_chars < 0 || max_lines < 0) {
        return false;
    }
    node->max_chars = max_chars;
    node->max_lines = max_lines;
    InvalidateTextLayoutCache(*node);
    if (node->yg_node != nullptr) {
        YGNodeMarkDirty(node->yg_node);
    }
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetTextWrapping(std::uint64_t handle, bool wrap) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    node->text_wrap = wrap;
    InvalidateTextLayoutCache(*node, true);
    if (node->yg_node != nullptr) {
        YGNodeMarkDirty(node->yg_node);
    }
    node->is_dirty = true;
    layout_dirty_ = true;
    return true;
}

bool UiRuntime::SetTextOverflow(std::uint64_t handle, std::uint32_t overflow_enum) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node ||
        (overflow_enum != OVERFLOW_CLIP && overflow_enum != OVERFLOW_ELLIPSIS && overflow_enum != OVERFLOW_FADE)) {
        return false;
    }
    if (node->text_overflow == overflow_enum) {
        return true;
    }
    node->text_overflow = overflow_enum;
    InvalidateTextLayoutCache(*node, true);
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetTextOverflowFade(std::uint64_t handle, bool horizontal, bool vertical) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    if (node->text_overflow_fade_horizontal == horizontal &&
        node->text_overflow_fade_vertical == vertical) {
        return true;
    }
    node->text_overflow_fade_horizontal = horizontal;
    node->text_overflow_fade_vertical = vertical;
    node->is_dirty = true;
    return true;
}

bool UiRuntime::SetTextObscured(std::uint64_t handle, bool is_password) {
    UINode* node = ResolveMutable(handle);
    if (node == nullptr || !node->is_text_node) {
        return false;
    }
    node->is_obscured = is_password;
    InvalidateTextLayoutCache(*node);
    node->is_dirty = true;
    return true;
}

bool UiRuntime::RegisterFont(std::uint32_t font_id, const std::uint8_t* bytes, std::uint32_t length) {
    if (font_id == 0U || bytes == nullptr || length == 0U) {
        return false;
    }

    RegisteredFont font{};
    const std::string_view raw_font_bytes(
        reinterpret_cast<const char*>(bytes),
        static_cast<std::size_t>(length));
    if (IsWoff2Font(raw_font_bytes)) {
        // Google-hosted incremental shards arrive as WOFF2, but HarfBuzz in our
        // pinned text stack shapes SFNT input. Expand once at registration time.
        if (!DecodeWoff2Font(bytes, length, font.bytes)) {
            return false;
        }
    } else {
        font.bytes.assign(bytes, bytes + length);
    }
    font.blob = hb_blob_create(
        reinterpret_cast<const char*>(font.bytes.data()),
        static_cast<unsigned int>(font.bytes.size()),
        HB_MEMORY_MODE_READONLY,
        nullptr,
        nullptr);
    if (font.blob == nullptr) return false;

    font.face = hb_face_create(font.blob, 0);
    if (font.face == nullptr || hb_face_get_glyph_count(font.face) == 0U) { DestroyRegisteredFont(font); return false; }

    font.upem = hb_face_get_upem(font.face);
    if (font.upem == 0U) font.upem = 2048U;

    font.font = hb_font_create(font.face);
    if (font.font == nullptr) { DestroyRegisteredFont(font); return false; }
    hb_ot_font_set_funcs(font.font);
    font.has_extents = hb_font_get_h_extents(font.font, &font.extents);
    font.is_fixed_pitch = FontFaceIsFixedPitch(font.face);
    font.is_ascii_fixed_pitch = DetectAsciiFixedPitchFont(font.bytes);

    hb_buffer_t* bullet_buffer = hb_buffer_create();
    if (bullet_buffer != nullptr) {
        static constexpr char kBulletUtf8[] = "\xE2\x80\xA2";
        hb_buffer_add_utf8(bullet_buffer, kBulletUtf8, 3, 0, 3);
        hb_buffer_guess_segment_properties(bullet_buffer);
        hb_shape(font.font, bullet_buffer, nullptr, 0U);
        unsigned int glyph_count = 0U;
        const hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(bullet_buffer, &glyph_count);
        if (glyphs != nullptr && glyph_count > 0U) {
            font.bullet_glyph_id = glyphs[0].codepoint;
        }
        hb_buffer_destroy(bullet_buffer);
    }
    font.tofu_glyph_id = ResolveNominalGlyph(font.font, 0x25A1U);
    if (!font.tofu_glyph_id.has_value()) {
        font.tofu_glyph_id = ResolveNominalGlyph(font.font, 0xFFFDU);
    }

    auto existing = font_registry_.find(font_id);
    if (existing != font_registry_.end()) {
        DestroyRegisteredFont(existing->second);
        existing->second = std::move(font);
        reported_missing_font_coverage_keys_.clear();
    } else {
        font_registry_.emplace(font_id, std::move(font));
    }
    InvalidateAllTextLayoutForFontChange();
    return true;
}

bool UiRuntime::UnregisterFont(std::uint32_t font_id) {
    if (font_id == 0U) {
        return false;
    }
    const auto existing = font_registry_.find(font_id);
    if (existing == font_registry_.end()) {
        return false;
    }
    DestroyRegisteredFont(existing->second);
    font_registry_.erase(existing);
    font_fallbacks_.erase(font_id);
    for (auto& [primary_font_id, fallbacks] : font_fallbacks_) {
        (void)primary_font_id;
        fallbacks.erase(
            std::remove(fallbacks.begin(), fallbacks.end(), font_id),
            fallbacks.end());
    }
    reported_missing_font_coverage_keys_.clear();
    InvalidateAllTextLayoutForFontChange();
    return true;
}

bool UiRuntime::UnregisterFontFallback(std::uint32_t font_id, std::uint32_t fallback_font_id) {
    if (font_id == 0U || fallback_font_id == 0U || font_id == fallback_font_id) {
        return false;
    }
    const auto found = font_fallbacks_.find(font_id);
    if (found == font_fallbacks_.end()) {
        return false;
    }
    std::vector<std::uint32_t>& fallbacks = found->second;
    const auto fallback_it = std::find(fallbacks.begin(), fallbacks.end(), fallback_font_id);
    if (fallback_it == fallbacks.end()) {
        return false;
    }
    fallbacks.erase(fallback_it);
    if (fallbacks.empty()) {
        font_fallbacks_.erase(found);
    }
    reported_missing_font_coverage_keys_.clear();
    InvalidateAllTextLayoutForFontChange();
    return true;
}

void UiRuntime::InvalidateAllTextLayoutForFontChange() {
    for (UINode& node : node_pool_) {
        if (!node.is_active || !node.is_text_node) {
            continue;
        }
        InvalidateTextLayoutCache(node);
        node.is_dirty = true;
        if (node.yg_node != nullptr) {
            YGNodeMarkDirty(node.yg_node);
        }
    }
    layout_dirty_ = true;
}

bool UiRuntime::RegisterFontFallback(std::uint32_t font_id, std::uint32_t fallback_font_id) {
    if (font_id == 0U || fallback_font_id == 0U || font_id == fallback_font_id) {
        return false;
    }

    std::vector<std::uint32_t>& fallbacks = font_fallbacks_[font_id];
    if (std::find(fallbacks.begin(), fallbacks.end(), fallback_font_id) == fallbacks.end()) {
        fallbacks.push_back(fallback_font_id);
        reported_missing_font_coverage_keys_.clear();
        InvalidateAllTextLayoutForFontChange();
    }
    return true;
}

bool UiRuntime::RegisterIcuData(const std::uint8_t* bytes, std::uint32_t length) {
    if (bytes == nullptr || length == 0U) {
        return false;
    }
    if (icu_data_registered_) {
        return true;
    }

    icu_data_bytes_.assign(bytes, bytes + length);
    UErrorCode status = U_ZERO_ERROR;
    udata_setCommonData(icu_data_bytes_.data(), &status);
    if (U_FAILURE(status)) {
        icu_data_bytes_.clear();
        icu_data_bytes_.shrink_to_fit();
        return false;
    }

    icu_data_registered_ = true;
    for (UINode& node : node_pool_) {
        if (!node.is_active || !node.is_text_node) {
            continue;
        }
        InvalidateTextLayoutCache(node);
        node.is_dirty = true;
        if (node.yg_node != nullptr) {
            YGNodeMarkDirty(node.yg_node);
        }
    }
    layout_dirty_ = true;
    return true;
}

void UiRuntime::FontLoaded(std::uint32_t font_id) {
    if (font_id == 0U) {
        return;
    }
    for (UINode& node : node_pool_) {
        if (!node.is_active || !node.is_text_node) {
            continue;
        }
        const std::vector<std::uint32_t> font_chain = ResolveFontChain(node.font_id);
        if (std::find(font_chain.begin(), font_chain.end(), font_id) == font_chain.end()) {
            continue;
        }
        InvalidateTextLayoutCache(node);
        if (node.yg_node != nullptr) {
            YGNodeMarkDirty(node.yg_node);
        }
        node.is_dirty = true;
    }
    layout_dirty_ = true;
}

bool UiRuntime::BuildCachedLogicalLineShape(
    const UINode& node,
    std::string_view source_text,
    std::uint32_t raw_start,
    std::uint32_t raw_end,
    CachedLogicalLineShape& out_shape) const {
    return BuildCachedLogicalLineShapeImpl(node, source_text, raw_start, raw_end, out_shape);
}

UiRuntime::ParagraphLayout UiRuntime::LayoutParagraph(const UINode& node, std::optional<float> max_width) const {
    return LayoutParagraphImpl(node, max_width);
}

std::vector<NonWrappingTextFragment> UiRuntime::BuildNonWrappingFragmentsForLine(
    std::size_t line_index,
    std::string_view line_text,
    const ShapedTextRun& shaped) const {
    return BuildNonWrappingFragmentsForLineImpl(line_index, line_text, shaped);
}

std::uint32_t UiRuntime::GetNonWrapVisibleLineStart(const UINode& node, std::size_t line_index) const {
    if (node.break_offsets.size() < 2U) {
        return 0U;
    }
    const std::size_t clamped_line_index =
        std::min<std::size_t>(line_index, node.break_offsets.size() - 2U);
    const std::uint32_t raw_start =
        static_cast<std::uint32_t>(std::max(node.break_offsets[clamped_line_index], 0));
    const std::uint32_t raw_end =
        static_cast<std::uint32_t>(std::max(node.break_offsets[clamped_line_index + 1U], 0));
    return SkipLeadingLineBreaks(node.text_content, raw_start, raw_end);
}

std::uint32_t UiRuntime::GetNonWrapFragmentAbsoluteStart(
    const UINode& node,
    std::size_t line_index,
    const NonWrappingTextFragment& fragment) const {
    return GetNonWrapVisibleLineStart(node, line_index) + fragment.local_byte_start;
}

std::uint32_t UiRuntime::GetNonWrapFragmentAbsoluteEnd(
    const UINode& node,
    std::size_t line_index,
    const NonWrappingTextFragment& fragment) const {
    return GetNonWrapVisibleLineStart(node, line_index) + fragment.local_byte_end;
}

UiRuntime::NonWrappingFragmentWindow UiRuntime::ResolveNonWrappingFragmentWindow(
    const UINode& node,
    std::size_t line_index,
    float visible_left,
    float visible_right) const {
    return ResolveNonWrappingFragmentWindowImpl(node, line_index, visible_left, visible_right);
}

std::vector<TextClusterStop> UiRuntime::BuildTextClusterStops(
    const std::vector<GlyphPlacement>& glyphs,
    float shaped_width,
    std::size_t text_length) const {
    return BuildTextClusterStopsImpl(glyphs, shaped_width, text_length);
}

float UiRuntime::ClusterXForIndex(
    const std::vector<TextClusterStop>& stops,
    float shaped_width,
    std::uint32_t local_index,
    std::size_t text_length) const {
    return ::effindom::v2::ui::ClusterXForIndex(stops, shaped_width, local_index, text_length);
}

bool UiRuntime::TryBuildFragmentGeometrySliceFromLogicalLineShape(
    const UINode& node,
    std::size_t line_index,
    std::uint32_t slice_start,
    std::uint32_t slice_end,
    FragmentGeometrySlice& out) const {
    return TryBuildFragmentGeometrySliceFromLogicalLineShapeImpl(node, line_index, slice_start, slice_end, out);
}

bool UiRuntime::TryGetCachedNonWrapGeometrySliceForIndex(
    const UINode& node,
    std::size_t line_index,
    std::uint32_t byte_index,
    FragmentGeometrySlice& out) const {
    for (const CachedNonWrapGeometrySlice& cached : node.cached_nonwrap_geometry_slices) {
        if (cached.line_index != line_index ||
            byte_index < cached.slice_start ||
            byte_index > cached.slice_end) {
            continue;
        }
        out = FragmentGeometrySlice{};
        out.line_start = cached.line_start;
        out.line_end = cached.line_end;
        out.slice_start = cached.slice_start;
        out.slice_end = cached.slice_end;
        out.slice_x = cached.slice_x;
        out.full_line_width = cached.full_line_width;
        out.shaped.width = cached.shaped_width;
        out.shaped.height = cached.shaped_height;
        out.shaped.baseline = cached.shaped_baseline;
        out.shaped.ascent = cached.shaped_ascent;
        out.shaped.descent = cached.shaped_descent;
        out.shaped.glyphs = cached.glyphs;
        out.cluster_stops = cached.cluster_stops;
        return true;
    }
    return false;
}

bool UiRuntime::TryGetCachedNonWrapGeometrySliceForX(
    const UINode& node,
    std::size_t line_index,
    float aligned_x,
    FragmentGeometrySlice& out) const {
    for (const CachedNonWrapGeometrySlice& cached : node.cached_nonwrap_geometry_slices) {
        const float cached_right = cached.slice_x + cached.shaped_width;
        if (cached.line_index != line_index ||
            aligned_x < cached.slice_x ||
            aligned_x > cached_right) {
            continue;
        }
        out = FragmentGeometrySlice{};
        out.line_start = cached.line_start;
        out.line_end = cached.line_end;
        out.slice_start = cached.slice_start;
        out.slice_end = cached.slice_end;
        out.slice_x = cached.slice_x;
        out.full_line_width = cached.full_line_width;
        out.shaped.width = cached.shaped_width;
        out.shaped.height = cached.shaped_height;
        out.shaped.baseline = cached.shaped_baseline;
        out.shaped.ascent = cached.shaped_ascent;
        out.shaped.descent = cached.shaped_descent;
        out.shaped.glyphs = cached.glyphs;
        out.cluster_stops = cached.cluster_stops;
        return true;
    }
    return false;
}

void UiRuntime::StoreCachedNonWrapGeometrySlice(UINode& node, std::size_t line_index, const FragmentGeometrySlice& slice) const {
    CachedNonWrapGeometrySlice cached{};
    cached.line_index = line_index;
    cached.line_start = slice.line_start;
    cached.line_end = slice.line_end;
    cached.slice_start = slice.slice_start;
    cached.slice_end = slice.slice_end;
    cached.slice_x = slice.slice_x;
    cached.full_line_width = slice.full_line_width;
    cached.shaped_width = slice.shaped.width;
    cached.shaped_height = slice.shaped.height;
    cached.shaped_baseline = slice.shaped.baseline;
    cached.shaped_ascent = slice.shaped.ascent;
    cached.shaped_descent = slice.shaped.descent;
    cached.glyphs = slice.shaped.glyphs;
    cached.cluster_stops = slice.cluster_stops;

    auto existing = std::find_if(
        node.cached_nonwrap_geometry_slices.begin(),
        node.cached_nonwrap_geometry_slices.end(),
        [line_index](const CachedNonWrapGeometrySlice& entry) {
            return entry.line_index == line_index;
        });
    if (existing != node.cached_nonwrap_geometry_slices.end()) {
        *existing = std::move(cached);
        return;
    }
    node.cached_nonwrap_geometry_slices.push_back(std::move(cached));
}

bool UiRuntime::TryShapeFragmentGeometrySliceForIndex(
    const UINode& node,
    std::size_t line_index,
    std::uint32_t byte_index,
    FragmentGeometrySlice& out) const {
    out = FragmentGeometrySlice{};
    if (!node.nonwrap_fragment_cache_valid ||
        line_index + 1U >= node.nonwrap_fragment_line_offsets.size()) {
        return false;
    }

    const std::size_t line_fragment_start = node.nonwrap_fragment_line_offsets[line_index];
    const std::size_t line_fragment_end = node.nonwrap_fragment_line_offsets[line_index + 1U];
    if (line_fragment_start >= line_fragment_end) {
        return false;
    }

    const auto line_range_start = static_cast<std::uint32_t>(std::max(node.break_offsets[line_index], 0));
    const auto line_range_end = static_cast<std::uint32_t>(std::max(node.break_offsets[line_index + 1U], 0));
    const std::uint32_t line_visible_start = GetNonWrapVisibleLineStart(node, line_index);
    const std::uint32_t clamped_index = std::clamp(byte_index, line_range_start, line_range_end);
    if (TryGetCachedNonWrapGeometrySliceForIndex(node, line_index, clamped_index, out)) {
        return true;
    }
    const std::uint32_t local_index =
        clamped_index > line_visible_start ? (clamped_index - line_visible_start) : 0U;

    const auto fragment_it = std::lower_bound(
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start),
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end),
        local_index,
        [](const NonWrappingTextFragment& fragment, std::uint32_t index) {
            return fragment.local_byte_end <= index;
        });
    const std::size_t focus_fragment =
        fragment_it == node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end)
        ? (line_fragment_end - 1U)
        : static_cast<std::size_t>(std::distance(node.nonwrap_fragments.begin(), fragment_it));
    const std::size_t window_start =
        std::max(line_fragment_start, focus_fragment - std::min(focus_fragment - line_fragment_start, kNonWrappingFragmentOverscanCount));
    const std::size_t window_end =
        std::min(line_fragment_end, focus_fragment + 1U + kNonWrappingFragmentOverscanCount);
    if (window_start >= window_end) {
        return false;
    }

    const NonWrappingTextFragment& first_fragment = node.nonwrap_fragments[window_start];
    const NonWrappingTextFragment& last_fragment = node.nonwrap_fragments[window_end - 1U];
    const std::uint32_t slice_start = line_visible_start + first_fragment.local_byte_start;
    const std::uint32_t slice_end = line_visible_start + last_fragment.local_byte_end;
    if (slice_end <= slice_start) {
        return false;
    }

    out.line_start = line_range_start;
    out.line_end = line_range_end;
    out.slice_start = slice_start;
    out.slice_end = slice_end;
    if (!TryBuildFragmentGeometrySliceFromLogicalLineShape(node, line_index, slice_start, slice_end, out)) {
        out.slice_x = first_fragment.x;
        out.full_line_width = line_index < node.line_widths.size() ? node.line_widths[line_index] : 0.0f;
        if (!ShapeText(
                std::string_view(node.text_content).substr(
                    static_cast<std::size_t>(out.slice_start),
                    static_cast<std::size_t>(out.slice_end - out.slice_start)),
                node.font_id,
                node.font_size,
                out.shaped,
                node.is_obscured)) {
            return false;
        }
        out.cluster_stops = BuildTextClusterStops(out.shaped.glyphs, out.shaped.width, out.slice_end - out.slice_start);
    }
    StoreCachedNonWrapGeometrySlice(const_cast<UINode&>(node), line_index, out);
    return true;
}

bool UiRuntime::TryShapeFragmentGeometrySliceForX(
    const UINode& node,
    std::size_t line_index,
    float aligned_x,
    FragmentGeometrySlice& out) const {
    out = FragmentGeometrySlice{};
    if (!node.nonwrap_fragment_cache_valid ||
        line_index + 1U >= node.nonwrap_fragment_line_offsets.size()) {
        return false;
    }

    const std::size_t line_fragment_start = node.nonwrap_fragment_line_offsets[line_index];
    const std::size_t line_fragment_end = node.nonwrap_fragment_line_offsets[line_index + 1U];
    if (line_fragment_start >= line_fragment_end) {
        return false;
    }

    const float clamped_x = std::max(aligned_x, 0.0f);
    if (TryGetCachedNonWrapGeometrySliceForX(node, line_index, clamped_x, out)) {
        return true;
    }
    const auto fragment_it = std::lower_bound(
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_start),
        node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end),
        clamped_x,
        [](const NonWrappingTextFragment& fragment, float x) {
            return (fragment.x + fragment.width) <= x;
        });
    const std::size_t focus_fragment =
        fragment_it == node.nonwrap_fragments.begin() + static_cast<std::ptrdiff_t>(line_fragment_end)
        ? (line_fragment_end - 1U)
        : static_cast<std::size_t>(std::distance(node.nonwrap_fragments.begin(), fragment_it));
    const std::size_t window_start =
        std::max(line_fragment_start, focus_fragment - std::min(focus_fragment - line_fragment_start, kNonWrappingFragmentOverscanCount));
    const std::size_t window_end =
        std::min(line_fragment_end, focus_fragment + 1U + kNonWrappingFragmentOverscanCount);
    if (window_start >= window_end) {
        return false;
    }

    const auto line_range_start = static_cast<std::uint32_t>(std::max(node.break_offsets[line_index], 0));
    const auto line_range_end = static_cast<std::uint32_t>(std::max(node.break_offsets[line_index + 1U], 0));
    const std::uint32_t line_visible_start = GetNonWrapVisibleLineStart(node, line_index);
    const NonWrappingTextFragment& first_fragment = node.nonwrap_fragments[window_start];
    const NonWrappingTextFragment& last_fragment = node.nonwrap_fragments[window_end - 1U];
    const std::uint32_t slice_start = line_visible_start + first_fragment.local_byte_start;
    const std::uint32_t slice_end = line_visible_start + last_fragment.local_byte_end;
    if (slice_end <= slice_start) {
        return false;
    }

    out.line_start = line_range_start;
    out.line_end = line_range_end;
    out.slice_start = slice_start;
    out.slice_end = slice_end;
    if (!TryBuildFragmentGeometrySliceFromLogicalLineShape(node, line_index, slice_start, slice_end, out)) {
        out.slice_x = first_fragment.x;
        out.full_line_width = line_index < node.line_widths.size() ? node.line_widths[line_index] : 0.0f;
        if (!ShapeText(
                std::string_view(node.text_content).substr(
                    static_cast<std::size_t>(out.slice_start),
                    static_cast<std::size_t>(out.slice_end - out.slice_start)),
                node.font_id,
                node.font_size,
                out.shaped,
                node.is_obscured)) {
            return false;
        }
        out.cluster_stops = BuildTextClusterStops(out.shaped.glyphs, out.shaped.width, out.slice_end - out.slice_start);
    }
    StoreCachedNonWrapGeometrySlice(const_cast<UINode&>(node), line_index, out);
    return true;
}

bool UiRuntime::TryApplyIncrementalNonWrapLayoutCache(UINode& node, std::string_view previous_text) const {
    return TryApplyIncrementalNonWrapLayoutCacheImpl(node, previous_text);
}

std::vector<std::int32_t> UiRuntime::ComputeBreakCandidates(std::string_view utf8) const {
    return ComputeBreakCandidatesImpl(utf8);
}

std::vector<std::int32_t> UiRuntime::ComputeLineBreaks(
    std::string_view utf8,
    float max_width,
    std::uint32_t font_id,
    float font_size,
    bool obscured) const {
    return ComputeLineBreaksImpl(utf8, max_width, font_id, font_size, obscured);
}

} // namespace effindom::v2::ui
