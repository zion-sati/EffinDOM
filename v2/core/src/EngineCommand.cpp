#include "EngineInternal.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace effindom::v2 {

namespace {

namespace {

std::uint32_t DecodeClipMode(std::uint32_t bounds_flags) {
    const std::uint32_t clip_mode = (bounds_flags & ED_BOUNDS_CLIP_MODE_MASK) >> ED_BOUNDS_CLIP_MODE_SHIFT;
    return clip_mode == ED_CLIP_MODE_STRICT_CONTENT ? clip_mode : ED_CLIP_MODE_RASTER_SAFE_VISUAL;
}

} // namespace

class CommandReader {
public:
    CommandReader(const std::uint32_t* buffer, std::uint32_t length)
        : buffer_(buffer)
        , length_(length) {}

    std::uint32_t remaining() const {
        return length_ - cursor_;
    }

    std::uint32_t cursor() const {
        return cursor_;
    }

    void set_cursor(std::uint32_t cursor) {
        cursor_ = cursor;
    }

    bool Require(std::uint32_t word_count, CommandBufferStats& stats) const {
        if (remaining() < word_count) {
            stats.truncated_buffers += 1;
            return false;
        }
        return true;
    }

    std::uint32_t ReadWord() {
        return buffer_[cursor_++];
    }

    std::uint64_t ReadHandle() {
        const std::uint64_t handle = detail::DecodeHandleWords(buffer_[cursor_], buffer_[cursor_ + 1]);
        cursor_ += 2;
        return handle;
    }

    float ReadFloatWord() {
        return detail::ReadFloat(ReadWord());
    }

    void Skip(std::uint32_t word_count) {
        cursor_ += word_count;
    }

    const std::uint32_t* data() const {
        return buffer_;
    }

private:
    const std::uint32_t* buffer_ = nullptr;
    std::uint32_t length_ = 0;
    std::uint32_t cursor_ = 0;
};

} // namespace

CommandBufferStats Engine::ExecuteCommandBuffer(const std::uint32_t* buffer, std::uint32_t length) {
    CommandBufferStats stats{};
    if (buffer == nullptr || length == 0) {
        return stats;
    }

    class CommandExecutor {
    public:
        CommandExecutor(Impl& impl, const std::uint32_t* source, std::uint32_t source_length, CommandBufferStats& command_stats)
            : impl_(impl)
            , reader_(source, source_length)
            , stats_(command_stats) {}

        CommandBufferStats Run() {
            while (reader_.remaining() > 0) {
                const std::uint32_t command = reader_.ReadWord();
                if (!Dispatch(command)) {
                    return stats_;
                }
            }
            return stats_;
        }

    private:
        bool Dispatch(std::uint32_t command) {
            switch (command) {
            case CMD_CREATE_NODE:
                return HandleCreateNode();
            case CMD_DELETE_NODE:
                return HandleDeleteNode();
            case CMD_SET_BOUNDS:
                return HandleSetBounds();
            case CMD_SET_BOX_STYLE:
                return HandleSetBoxStyle();
            case CMD_SET_LINEAR_GRADIENT:
                return HandleSetLinearGradient();
            case CMD_SET_LAYER_EFFECT:
                return HandleSetLayerEffect();
            case CMD_SET_BACKGROUND_BLUR:
                return HandleSetBackgroundBlur();
            case CMD_SET_DROP_SHADOW:
                return HandleSetDropShadow();
            case CMD_SET_IMAGE:
                return HandleSetImage();
            case CMD_SET_IMAGE_NINE:
                return HandleSetImageNine();
            case CMD_SET_PATH:
                return HandleSetPath();
            case CMD_SET_SVG:
                return HandleSetSvg();
            case CMD_SET_GLYPH_RUN:
                return HandleSetGlyphRun();
            case CMD_SET_TEXT_FADE:
                return HandleSetTextFade();
            case CMD_SET_CARET:
                return HandleSetCaret();
            case CMD_SET_HIGHLIGHTS:
                return HandleSetHighlights();
            case CMD_SET_GLYPH_RUN_COLORED:
                return HandleSetGlyphRunColored();
            case CMD_SET_HIGHLIGHTS_COLORED:
                return HandleSetHighlightsColored();
            case CMD_COMMIT_PAINT_ORDER:
                return HandleCommitPaintOrder();
            case CMD_COMMIT_SCENE:
                return HandleCommitScene();
            default:
                stats_.unknown_commands += 1;
                return false;
            }
        }

        bool HandleCreateNode() {
            if (!reader_.Require(2, stats_)) {
                return false;
            }
            if (!impl_.CreateNode(reader_.ReadHandle())) {
                stats_.ignored_commands += 1;
            } else {
                stats_.parsed_commands += 1;
            }
            return true;
        }

        bool HandleDeleteNode() {
            if (!reader_.Require(2, stats_)) {
                return false;
            }
            if (!impl_.DeleteNode(reader_.ReadHandle())) {
                stats_.ignored_commands += 1;
            } else {
                stats_.parsed_commands += 1;
            }
            return true;
        }

        bool HandleSetBounds() {
            if (!reader_.Require(15, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(13);
                stats_.ignored_commands += 1;
                return true;
            }

            node->visual_bounds = Rect{
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
            };
            node->hit_bounds = Rect{
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
            };
            node->clip_bounds = Rect{
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
                reader_.ReadFloatWord(),
            };
            const std::uint32_t bounds_flags = reader_.ReadWord();
            node->interactive = (bounds_flags & ED_BOUNDS_FLAG_INTERACTIVE) != 0U;
            node->clip_mode = DecodeClipMode(bounds_flags);
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetBoxStyle() {
            if (!reader_.Require(12, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(10);
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_box_style = true;
            node->bg_color = reader_.ReadWord();
            node->corner_radii = {
                detail::ClampNonNegative(reader_.ReadFloatWord()),
                detail::ClampNonNegative(reader_.ReadFloatWord()),
                detail::ClampNonNegative(reader_.ReadFloatWord()),
                detail::ClampNonNegative(reader_.ReadFloatWord()),
            };
            node->border_width = detail::ClampNonNegative(reader_.ReadFloatWord());
            node->border_color = reader_.ReadWord();
            node->border_style = reader_.ReadWord();
            node->border_dash_on = detail::ClampNonNegative(reader_.ReadFloatWord());
            node->border_dash_off = detail::ClampNonNegative(reader_.ReadFloatWord());
            node->has_border = node->border_width > 0.0f && (node->border_color & 0xffU) != 0;
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetLinearGradient() {
            if (!reader_.Require(7, stats_)) {
                return false;
            }

            const std::uint32_t command_start = reader_.cursor();
            const std::uint64_t handle = detail::DecodeHandleWords(reader_.data()[command_start], reader_.data()[command_start + 1]);
            detail::DisplayNode* node = impl_.ResolveMutable(handle);
            const std::uint32_t stop_count = reader_.data()[command_start + 6];
            if (!reader_.Require(7 + (stop_count * 2U), stats_)) {
                return false;
            }

            reader_.ReadHandle();
            if (node == nullptr) {
                reader_.Skip(5 + (stop_count * 2U));
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_gradient = true;
            node->gradient_start_x = reader_.ReadFloatWord();
            node->gradient_start_y = reader_.ReadFloatWord();
            node->gradient_end_x = reader_.ReadFloatWord();
            node->gradient_end_y = reader_.ReadFloatWord();
            node->gradient_stops.clear();
            node->gradient_stops.reserve(stop_count);
            reader_.ReadWord();
            for (std::uint32_t index = 0; index < stop_count; index += 1) {
                node->gradient_stops.push_back(GradientStop{
                    std::clamp(reader_.ReadFloatWord(), 0.0f, 1.0f),
                    reader_.ReadWord(),
                });
            }
            std::sort(
                node->gradient_stops.begin(),
                node->gradient_stops.end(),
                [](const GradientStop& lhs, const GradientStop& rhs) {
                    return lhs.offset < rhs.offset;
                });
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetLayerEffect() {
            if (!reader_.Require(5, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(3);
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_layer_effect = true;
            node->opacity = detail::ClampOpacity(reader_.ReadFloatWord());
            node->blur_sigma = detail::ClampNonNegative(reader_.ReadFloatWord());
            node->blend_mode = reader_.ReadWord();
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetBackgroundBlur() {
            if (!reader_.Require(3, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(1);
                stats_.ignored_commands += 1;
                return true;
            }

            node->background_blur_sigma = detail::ClampNonNegative(reader_.ReadFloatWord());
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetDropShadow() {
            if (!reader_.Require(7, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(5);
                stats_.ignored_commands += 1;
                return true;
            }

            node->drop_shadow_color = reader_.ReadWord();
            node->drop_shadow_offset_x = reader_.ReadFloatWord();
            node->drop_shadow_offset_y = reader_.ReadFloatWord();
            node->drop_shadow_blur_sigma = detail::ClampNonNegative(reader_.ReadFloatWord());
            node->drop_shadow_spread = detail::ClampNonNegative(reader_.ReadFloatWord());
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetImage() {
            if (!reader_.Require(4, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(2);
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_image = true;
            node->has_image_nine = false;
            node->has_svg = false;
            node->texture_id = reader_.ReadWord();
            node->object_fit = reader_.ReadWord();
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetImageNine() {
            if (!reader_.Require(7, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(5);
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_image_nine = true;
            node->has_image = false;
            node->has_svg = false;
            node->image_nine_texture_id = reader_.ReadWord();
            node->image_nine_insets = Insets{
                detail::ClampNonNegative(reader_.ReadFloatWord()),
                detail::ClampNonNegative(reader_.ReadFloatWord()),
                detail::ClampNonNegative(reader_.ReadFloatWord()),
                detail::ClampNonNegative(reader_.ReadFloatWord()),
            };
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetPath() {
            if (!reader_.Require(6, stats_)) {
                return false;
            }

            const std::uint32_t command_start = reader_.cursor();
            const std::uint64_t handle = detail::DecodeHandleWords(reader_.data()[command_start], reader_.data()[command_start + 1]);
            detail::DisplayNode* node = impl_.ResolveMutable(handle);
            const std::uint32_t verb_count = reader_.data()[command_start + 5];
            const std::uint32_t buffer_length = command_start + reader_.remaining();
            std::uint32_t path_cursor = command_start + 6;
            std::vector<PathVerbRecord> decoded_path{};
            decoded_path.reserve(verb_count);
            for (std::uint32_t index = 0; index < verb_count; index += 1) {
                if (path_cursor >= buffer_length) {
                    stats_.truncated_buffers += 1;
                    return false;
                }
                const std::uint32_t verb = reader_.data()[path_cursor++];
                const std::uint32_t arg_count = detail::VerbArgCount(verb);
                if (buffer_length - path_cursor < arg_count) {
                    stats_.truncated_buffers += 1;
                    return false;
                }

                PathVerbRecord record{};
                record.verb = verb;
                record.arg_count = arg_count;
                for (std::uint32_t arg = 0; arg < arg_count; arg += 1) {
                    record.args[arg] = detail::ReadFloat(reader_.data()[path_cursor + arg]);
                }
                decoded_path.push_back(record);
                path_cursor += arg_count;
            }

            reader_.set_cursor(command_start);
            reader_.ReadHandle();
            if (node == nullptr) {
                reader_.set_cursor(path_cursor);
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_path = true;
            node->path_fill_color = reader_.ReadWord();
            node->path_stroke_color = reader_.ReadWord();
            node->path_stroke_width = detail::ClampNonNegative(reader_.ReadFloatWord());
            reader_.ReadWord();
            node->path = std::move(decoded_path);
            reader_.set_cursor(path_cursor);
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetSvg() {
            if (!reader_.Require(4, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(2);
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_svg = true;
            node->has_image = false;
            node->has_image_nine = false;
            node->svg_id = reader_.ReadWord();
            node->svg_tint_color = reader_.ReadWord();
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetGlyphRun() {
            if (!reader_.Require(6, stats_)) {
                return false;
            }

            const std::uint32_t command_start = reader_.cursor();
            const std::uint64_t handle = detail::DecodeHandleWords(reader_.data()[command_start], reader_.data()[command_start + 1]);
            detail::DisplayNode* node = impl_.ResolveMutable(handle);
            const std::uint32_t glyph_count = reader_.data()[command_start + 5];
            if (!reader_.Require(6 + (glyph_count * 4U), stats_)) {
                return false;
            }

            reader_.ReadHandle();
            if (node == nullptr) {
                reader_.Skip(4 + (glyph_count * 4U));
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_glyph_run = true;
            node->glyphs_have_per_color = false;
            node->font_id = reader_.ReadWord();
            node->font_size = std::max(reader_.ReadFloatWord(), 1.0f);
            node->glyph_color = reader_.ReadWord();
            const std::uint32_t decoded_glyph_count = reader_.ReadWord();
            node->glyphs.clear();
            node->glyphs.reserve(decoded_glyph_count);
            for (std::uint32_t index = 0; index < decoded_glyph_count; index += 1) {
                node->glyphs.push_back(GlyphPlacement{
                    reader_.ReadWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadWord(),
                    node->glyph_color,
                });
            }
            impl_.ReleaseGlyphBlobCache(*node);
            node->glyph_blob_version += 1U;
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetGlyphRunColored() {
            if (!reader_.Require(5, stats_)) {
                return false;
            }
            const std::uint32_t command_start = reader_.cursor();
            const std::uint64_t handle = detail::DecodeHandleWords(reader_.data()[command_start], reader_.data()[command_start + 1]);
            detail::DisplayNode* node = impl_.ResolveMutable(handle);
            const std::uint32_t glyph_count = reader_.data()[command_start + 4];
            if (!reader_.Require(5 + (glyph_count * 5U), stats_)) {
                return false;
            }
            reader_.ReadHandle();
            if (node == nullptr) {
                reader_.Skip(3 + (glyph_count * 5U));
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_glyph_run = true;
            node->glyphs_have_per_color = true;
            node->font_id = reader_.ReadWord();
            node->font_size = std::max(reader_.ReadFloatWord(), 1.0f);
            const std::uint32_t decoded_glyph_count = reader_.ReadWord();
            node->glyphs.clear();
            node->glyphs.reserve(decoded_glyph_count);
            for (std::uint32_t index = 0; index < decoded_glyph_count; index += 1U) {
                node->glyphs.push_back(GlyphPlacement{
                    reader_.ReadWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadWord(),
                    reader_.ReadWord(),
                });
            }
            impl_.ReleaseGlyphBlobCache(*node);
            node->glyph_blob_version += 1U;
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetTextFade() {
            if (!reader_.Require(3, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(1);
                stats_.ignored_commands += 1;
                return true;
            }

            node->fade_edge = reader_.ReadWord();
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetCaret() {
            if (!reader_.Require(7, stats_)) {
                return false;
            }
            detail::DisplayNode* node = impl_.ResolveMutable(reader_.ReadHandle());
            if (node == nullptr) {
                reader_.Skip(5);
                stats_.ignored_commands += 1;
                return true;
            }

            node->has_caret = true;
            node->caret_x = reader_.ReadFloatWord();
            node->caret_y = reader_.ReadFloatWord();
            node->caret_height = detail::ClampNonNegative(reader_.ReadFloatWord());
            node->caret_color = reader_.ReadWord();
            node->caret_last_interaction_ms = reader_.ReadWord();
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetHighlights() {
            if (!reader_.Require(4, stats_)) {
                return false;
            }

            const std::uint32_t command_start = reader_.cursor();
            const std::uint64_t handle = detail::DecodeHandleWords(reader_.data()[command_start], reader_.data()[command_start + 1]);
            detail::DisplayNode* node = impl_.ResolveMutable(handle);
            const std::uint32_t rect_count = reader_.data()[command_start + 3];
            if (!reader_.Require(4 + (rect_count * 4U), stats_)) {
                return false;
            }

            reader_.ReadHandle();
            if (node == nullptr) {
                reader_.Skip(2 + (rect_count * 4U));
                stats_.ignored_commands += 1;
                return true;
            }

            node->highlight_color = reader_.ReadWord();
            const std::uint32_t decoded_rect_count = reader_.ReadWord();
            node->highlights.clear();
            node->colored_highlights.clear();
            node->highlights.reserve(decoded_rect_count);
            for (std::uint32_t index = 0; index < decoded_rect_count; index += 1) {
                node->highlights.push_back(Rect{
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                });
            }
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleSetHighlightsColored() {
            if (!reader_.Require(3, stats_)) {
                return false;
            }

            const std::uint32_t command_start = reader_.cursor();
            const std::uint64_t handle = detail::DecodeHandleWords(reader_.data()[command_start], reader_.data()[command_start + 1]);
            detail::DisplayNode* node = impl_.ResolveMutable(handle);
            const std::uint32_t rect_count = reader_.data()[command_start + 2];
            if (!reader_.Require(3 + (rect_count * 5U), stats_)) {
                return false;
            }

            reader_.ReadHandle();
            if (node == nullptr) {
                reader_.Skip(1 + (rect_count * 5U));
                stats_.ignored_commands += 1;
                return true;
            }

            const std::uint32_t decoded_rect_count = reader_.ReadWord();
            node->highlight_color = 0U;
            node->highlights.clear();
            node->colored_highlights.clear();
            node->colored_highlights.reserve(decoded_rect_count);
            for (std::uint32_t index = 0; index < decoded_rect_count; index += 1U) {
                const Rect rect{
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                };
                node->colored_highlights.push_back(ColoredRect{rect, reader_.ReadWord()});
            }
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleCommitPaintOrder() {
            if (!reader_.Require(1, stats_)) {
                return false;
            }
            const std::uint32_t handle_count = reader_.ReadWord();
            if (!reader_.Require(handle_count * 2U, stats_)) {
                return false;
            }

            impl_.paint_order.clear();
            impl_.paint_order.reserve(handle_count);
            for (std::uint32_t index = 0; index < handle_count; index += 1) {
                const std::uint64_t handle = reader_.ReadHandle();
                if (impl_.Resolve(handle) != nullptr) {
                    impl_.paint_order.push_back(handle);
                }
            }
            stats_.parsed_commands += 1;
            return true;
        }

        bool HandleCommitScene() {
            if (!reader_.Require(1, stats_)) {
                return false;
            }
            const std::uint32_t instruction_count = reader_.ReadWord();
            if (!reader_.Require(instruction_count * 5U, stats_)) {
                return false;
            }

            impl_.scene_instructions.clear();
            impl_.scene_instructions.reserve(instruction_count);
            for (std::uint32_t index = 0; index < instruction_count; index += 1) {
                impl_.scene_instructions.push_back(detail::SceneInstruction{
                    reader_.ReadWord(),
                    reader_.ReadHandle(),
                    reader_.ReadFloatWord(),
                    reader_.ReadFloatWord(),
                });
            }
            stats_.parsed_commands += 1;
            return true;
        }

        Impl& impl_;
        CommandReader reader_;
        CommandBufferStats& stats_;
    };

    CommandExecutor executor(*impl_, buffer, length, stats);
    return executor.Run();
}

} // namespace effindom::v2
