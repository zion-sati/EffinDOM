#include "EngineInternal.h"

#include <algorithm>
#include <cstring>

namespace effindom::v2 {

bool Rect::Contains(float px, float py) const {
    return px >= x && py >= y && px <= (x + width) && py <= (y + height);
}

namespace detail {

void DisplayNode::ResetForCreate(std::uint32_t next_generation) {
    alive = true;
    generation = next_generation;
    visual_bounds = {};
    hit_bounds = {};
    clip_bounds = {};
    interactive = false;
    clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL;
    has_box_style = false;
    bg_color = 0;
    corner_radii = {};
    has_border = false;
    border_width = 0.0f;
    border_color = 0;
    border_style = ED_BORDER_SOLID;
    border_dash_on = 0.0f;
    border_dash_off = 0.0f;
    has_gradient = false;
    gradient_start_x = 0.0f;
    gradient_start_y = 0.0f;
    gradient_end_x = 0.0f;
    gradient_end_y = 0.0f;
    gradient_stops.clear();
    has_layer_effect = false;
    opacity = 1.0f;
    blur_sigma = 0.0f;
    background_blur_sigma = 0.0f;
    drop_shadow_color = 0;
    drop_shadow_offset_x = 0.0f;
    drop_shadow_offset_y = 0.0f;
    drop_shadow_blur_sigma = 0.0f;
    drop_shadow_spread = 0.0f;
    blend_mode = ED_BLEND_SRC_OVER;
    has_image = false;
    texture_id = 0;
    object_fit = ED_OBJECT_FIT_FILL;
    has_image_nine = false;
    image_nine_texture_id = 0;
    image_nine_insets = {};
    has_svg = false;
    svg_id = 0;
    svg_tint_color = 0;
    has_path = false;
    path_fill_color = 0;
    path_stroke_color = 0;
    path_stroke_width = 0.0f;
    path.clear();
    has_glyph_run = false;
    glyphs_have_per_color = false;
    font_id = 0;
    font_size = 16.0f;
    glyph_color = 0;
    glyphs.clear();
    cached_glyph_blob.reset();
    glyph_blob_version = 0;
    cached_glyph_blob_version = 0;
    glyph_blob_build_count = 0;
    glyph_blob_last_used_generation = 0;
    glyph_blob_estimated_bytes = 0;
    glyph_blob_prev_index = kInvalidGlyphBlobCacheIndex;
    glyph_blob_next_index = kInvalidGlyphBlobCacheIndex;
    glyph_blob_in_lru = false;
    fade_edge = ED_FADE_NONE;
    has_caret = false;
    caret_x = 0.0f;
    caret_y = 0.0f;
    caret_height = 0.0f;
    caret_color = 0;
    caret_last_interaction_ms = 0;
    highlight_color = 0;
    highlights.clear();
    colored_highlights.clear();
}

void DisplayNode::ResetForDelete() {
    alive = false;
    visual_bounds = {};
    hit_bounds = {};
    clip_bounds = {};
    interactive = false;
    clip_mode = ED_CLIP_MODE_RASTER_SAFE_VISUAL;
    has_box_style = false;
    bg_color = 0;
    corner_radii = {};
    has_border = false;
    border_width = 0.0f;
    border_color = 0;
    border_style = ED_BORDER_SOLID;
    border_dash_on = 0.0f;
    border_dash_off = 0.0f;
    has_gradient = false;
    gradient_start_x = 0.0f;
    gradient_start_y = 0.0f;
    gradient_end_x = 0.0f;
    gradient_end_y = 0.0f;
    gradient_stops.clear();
    gradient_stops.shrink_to_fit();
    has_layer_effect = false;
    opacity = 1.0f;
    blur_sigma = 0.0f;
    background_blur_sigma = 0.0f;
    drop_shadow_color = 0;
    drop_shadow_offset_x = 0.0f;
    drop_shadow_offset_y = 0.0f;
    drop_shadow_blur_sigma = 0.0f;
    drop_shadow_spread = 0.0f;
    blend_mode = ED_BLEND_SRC_OVER;
    has_image = false;
    texture_id = 0;
    object_fit = ED_OBJECT_FIT_FILL;
    has_image_nine = false;
    image_nine_texture_id = 0;
    image_nine_insets = {};
    has_svg = false;
    svg_id = 0;
    svg_tint_color = 0;
    has_path = false;
    path_fill_color = 0;
    path_stroke_color = 0;
    path_stroke_width = 0.0f;
    path.clear();
    path.shrink_to_fit();
    has_glyph_run = false;
    glyphs_have_per_color = false;
    font_id = 0;
    font_size = 16.0f;
    glyph_color = 0;
    glyphs.clear();
    glyphs.shrink_to_fit();
    cached_glyph_blob.reset();
    glyph_blob_version = 0;
    cached_glyph_blob_version = 0;
    glyph_blob_build_count = 0;
    glyph_blob_last_used_generation = 0;
    glyph_blob_estimated_bytes = 0;
    glyph_blob_prev_index = kInvalidGlyphBlobCacheIndex;
    glyph_blob_next_index = kInvalidGlyphBlobCacheIndex;
    glyph_blob_in_lru = false;
    fade_edge = ED_FADE_NONE;
    has_caret = false;
    caret_x = 0.0f;
    caret_y = 0.0f;
    caret_height = 0.0f;
    caret_color = 0;
    caret_last_interaction_ms = 0;
    highlight_color = 0;
    highlights.clear();
    highlights.shrink_to_fit();
    colored_highlights.clear();
    colored_highlights.shrink_to_fit();
}

HandleParts DecodeHandle(std::uint64_t handle) {
    return HandleParts{
        static_cast<std::uint32_t>(handle & 0xffffffffULL),
        static_cast<std::uint32_t>(handle >> 32),
    };
}

std::uint64_t DecodeHandleWords(std::uint32_t low, std::uint32_t high) {
    return (static_cast<std::uint64_t>(high) << 32) | static_cast<std::uint64_t>(low);
}

float ReadFloat(std::uint32_t word) {
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(word), "float size mismatch");
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

float ClampNonNegative(float value) {
    return value < 0.0f ? 0.0f : value;
}

float ClampOpacity(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::uint32_t VerbArgCount(std::uint32_t verb) {
    switch (verb) {
    case ED_PATH_MOVE_TO:
    case ED_PATH_LINE_TO:
        return 2;
    case ED_PATH_QUAD_TO:
        return 4;
    case ED_PATH_CUBIC_TO:
        return 6;
    case ED_PATH_CLOSE:
    default:
        return 0;
    }
}

} // namespace detail

namespace {

std::uint32_t IndexForNode(
    const std::vector<detail::DisplayNode>& nodes,
    const detail::DisplayNode& node) {
    return static_cast<std::uint32_t>(&node - nodes.data());
}

std::size_t EstimateGlyphBlobBytes(const detail::DisplayNode& node) {
    constexpr std::size_t kGlyphBlobOverheadBytes = 128U;
    constexpr std::size_t kGlyphBlobBytesPerGlyph = 16U;
    return kGlyphBlobOverheadBytes + (node.glyphs.size() * kGlyphBlobBytesPerGlyph);
}

} // namespace

detail::DisplayNode* Engine::Impl::ResolveMutable(std::uint64_t handle) {
    const detail::HandleParts parts = detail::DecodeHandle(handle);
    if (handle == ED_INVALID_HANDLE || parts.generation == 0 || parts.index >= nodes.size()) {
        return nullptr;
    }
    detail::DisplayNode& node = nodes[parts.index];
    if (!node.alive || node.generation != parts.generation) {
        return nullptr;
    }
    return &node;
}

const detail::DisplayNode* Engine::Impl::Resolve(std::uint64_t handle) const {
    const detail::HandleParts parts = detail::DecodeHandle(handle);
    if (handle == ED_INVALID_HANDLE || parts.generation == 0 || parts.index >= nodes.size()) {
        return nullptr;
    }
    const detail::DisplayNode& node = nodes[parts.index];
    if (!node.alive || node.generation != parts.generation) {
        return nullptr;
    }
    return &node;
}

bool Engine::Impl::CreateNode(std::uint64_t handle) {
    const detail::HandleParts parts = detail::DecodeHandle(handle);
    if (handle == ED_INVALID_HANDLE || parts.generation == 0 || parts.index >= nodes.size()) {
        return false;
    }
    detail::DisplayNode& node = nodes[parts.index];
    if (node.alive && node.generation == parts.generation) {
        return true;
    }
    ReleaseGlyphBlobCache(node);
    node.ResetForCreate(parts.generation);
    return true;
}

bool Engine::Impl::DeleteNode(std::uint64_t handle) {
    detail::DisplayNode* node = ResolveMutable(handle);
    if (node == nullptr) {
        return false;
    }
    ReleaseGlyphBlobCache(*node);
    node->ResetForDelete();
    return true;
}

void Engine::Impl::ReleaseGlyphBlobCache(detail::DisplayNode& node) {
    if (node.glyph_blob_in_lru) {
        const std::uint32_t index = IndexForNode(nodes, node);
        if (node.glyph_blob_prev_index != detail::kInvalidGlyphBlobCacheIndex) {
            nodes[node.glyph_blob_prev_index].glyph_blob_next_index = node.glyph_blob_next_index;
        } else {
            glyph_blob_lru_head = node.glyph_blob_next_index;
        }
        if (node.glyph_blob_next_index != detail::kInvalidGlyphBlobCacheIndex) {
            nodes[node.glyph_blob_next_index].glyph_blob_prev_index = node.glyph_blob_prev_index;
        } else {
            glyph_blob_lru_tail = node.glyph_blob_prev_index;
        }
        (void)index;
        cached_glyph_blob_bytes =
            cached_glyph_blob_bytes >= node.glyph_blob_estimated_bytes
            ? (cached_glyph_blob_bytes - node.glyph_blob_estimated_bytes)
            : 0U;
    }
    node.glyph_blob_prev_index = detail::kInvalidGlyphBlobCacheIndex;
    node.glyph_blob_next_index = detail::kInvalidGlyphBlobCacheIndex;
    node.glyph_blob_in_lru = false;
    node.cached_glyph_blob.reset();
    node.cached_glyph_blob_version = 0;
    node.glyph_blob_last_used_generation = 0;
    node.glyph_blob_estimated_bytes = 0;
}

void Engine::Impl::StoreGlyphBlobCache(detail::DisplayNode& node, sk_sp<SkTextBlob> blob) {
    ReleaseGlyphBlobCache(node);
    if (!blob) {
        return;
    }
    node.cached_glyph_blob = std::move(blob);
    node.cached_glyph_blob_version = node.glyph_blob_version;
    node.glyph_blob_build_count += 1U;
    node.glyph_blob_estimated_bytes = EstimateGlyphBlobBytes(node);
    TouchGlyphBlobCache(node);
}

void Engine::Impl::TouchGlyphBlobCache(detail::DisplayNode& node) {
    if (node.cached_glyph_blob == nullptr) {
        return;
    }
    const std::uint32_t index = IndexForNode(nodes, node);
    node.glyph_blob_last_used_generation = render_generation;
    if (node.glyph_blob_in_lru && glyph_blob_lru_head == index) {
        return;
    }
    if (node.glyph_blob_in_lru) {
        if (node.glyph_blob_prev_index != detail::kInvalidGlyphBlobCacheIndex) {
            nodes[node.glyph_blob_prev_index].glyph_blob_next_index = node.glyph_blob_next_index;
        } else {
            glyph_blob_lru_head = node.glyph_blob_next_index;
        }
        if (node.glyph_blob_next_index != detail::kInvalidGlyphBlobCacheIndex) {
            nodes[node.glyph_blob_next_index].glyph_blob_prev_index = node.glyph_blob_prev_index;
        } else {
            glyph_blob_lru_tail = node.glyph_blob_prev_index;
        }
    } else {
        node.glyph_blob_estimated_bytes = std::max(node.glyph_blob_estimated_bytes, EstimateGlyphBlobBytes(node));
        cached_glyph_blob_bytes += node.glyph_blob_estimated_bytes;
        node.glyph_blob_in_lru = true;
    }
    node.glyph_blob_prev_index = detail::kInvalidGlyphBlobCacheIndex;
    node.glyph_blob_next_index = glyph_blob_lru_head;
    if (glyph_blob_lru_head != detail::kInvalidGlyphBlobCacheIndex) {
        nodes[glyph_blob_lru_head].glyph_blob_prev_index = index;
    } else {
        glyph_blob_lru_tail = index;
    }
    glyph_blob_lru_head = index;
}

void Engine::Impl::EvictGlyphBlobCaches() {
    while (glyph_blob_lru_tail != detail::kInvalidGlyphBlobCacheIndex) {
        detail::DisplayNode& node = nodes[glyph_blob_lru_tail];
        const bool over_budget =
            cached_glyph_blob_bytes > detail::kGlyphBlobBudgetBytes &&
            glyph_blob_lru_tail != glyph_blob_lru_head;
        const bool over_age =
            node.glyph_blob_last_used_generation > 0U &&
            render_generation > node.glyph_blob_last_used_generation &&
            (render_generation - node.glyph_blob_last_used_generation) > detail::kGlyphBlobMaxUnusedGenerations;
        if (!over_budget && !over_age) {
            break;
        }
        ReleaseGlyphBlobCache(node);
    }
}

} // namespace effindom::v2
