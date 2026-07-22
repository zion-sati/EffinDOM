#pragma once

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CommandBuilder.h"
#define private public
#include "UiRuntime.h"
#undef private
#include "effindom_ui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <cmath>
#include <limits>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using Catch::Approx;

#ifndef EFFINDOM_SOURCE_DIR
#define EFFINDOM_SOURCE_DIR "."
#endif

namespace test_ui_support {

inline constexpr std::uint32_t kDefaultTextColor = EF_RGB(0x00U, 0x00U, 0x00U);
inline constexpr std::uint32_t kDefaultSelectionColor = EF_RGBA(0x00U, 0x7AU, 0xFFU, 0x40U);
inline constexpr std::size_t kTextboxHardClampMaxCodepoints = effindom::v2::ui::kTextboxHardClampMaxCodepoints;
inline constexpr std::size_t kTextboxHardClampOverflowCodepoints = kTextboxHardClampMaxCodepoints + 1U;
inline std::uint32_t HandleIndex(std::uint64_t handle) {
    return static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
}

inline std::uint32_t HandleGeneration(std::uint64_t handle) {
    return static_cast<std::uint32_t>(handle >> 32U);
}

inline std::vector<std::uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.is_open());
    stream.seekg(0, std::ios::end);
    const std::streamsize size = stream.tellg();
    REQUIRE(size >= 0);
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        REQUIRE(stream.good());
    }
    return bytes;
}

struct WrappedTextFixtureTargets {
    std::uint32_t reverse_selection_start = 0U;
    std::uint32_t block_start = 0U;
    std::uint32_t block_end = 0U;
};

inline std::string ReadWrappedTextFixture() {
    const auto bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/ui/tests/fixtures/wrapped_large_document.txt");
    std::string text(bytes.begin(), bytes.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

inline WrappedTextFixtureTargets FindWrappedTextFixtureTargets(std::string_view text) {
    static constexpr std::string_view kReverseSelectionMarker = "\n    for (";
    static constexpr std::string_view kReverseSelectionFallback = "for (";
    static constexpr std::string_view kBlockStartMarker = "if (ch == '\\r'";
    static constexpr std::string_view kBlockEndMarker = "segment_start = ";

    const std::size_t reverse_selection_start_pos = text.rfind(kReverseSelectionMarker) != std::string_view::npos
        ? text.rfind(kReverseSelectionMarker) + 1U
        : text.rfind(kReverseSelectionFallback);
    REQUIRE(reverse_selection_start_pos != std::string_view::npos);
    REQUIRE(reverse_selection_start_pos > 5000U);

    const std::size_t block_start_pos = text.rfind(kBlockStartMarker);
    REQUIRE(block_start_pos != std::string_view::npos);
    REQUIRE(block_start_pos > 5000U);

    const std::size_t block_end_pos = text.find(kBlockEndMarker, block_start_pos);
    REQUIRE(block_end_pos != std::string_view::npos);
    REQUIRE(block_end_pos > block_start_pos);

    return WrappedTextFixtureTargets{
        static_cast<std::uint32_t>(reverse_selection_start_pos),
        static_cast<std::uint32_t>(block_start_pos),
        static_cast<std::uint32_t>(block_end_pos),
    };
}

inline void RegisterTestFont(std::uint32_t font_id = 1U) {
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    ui_register_font(font_id, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
}

inline void RegisterBridgeBodyTestFont(std::uint32_t font_id = 1U) {
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/NotoSans-Regular.ttf");
    ui_register_font(font_id, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
}

inline void RegisterMonoTestFont(std::uint32_t font_id = 5U) {
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/NotoSansMono-Regular.ttf");
    ui_register_font(font_id, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
}

inline void RegisterEmojiTestFont(std::uint32_t font_id = 4U) {
    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/NotoColorEmoji.ttf");
    ui_register_font(font_id, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size()));
}

inline void UiTestPointerEvent(
    std::uint32_t event_enum,
    ui_handle_t handle,
    float logical_x,
    float logical_y,
    std::int32_t pointer_id = -1,
    std::uint32_t pointer_type = UI_POINTER_TYPE_MOUSE,
    std::int32_t button = 0,
    std::uint32_t buttons = 0,
    float pressure = 0.0f,
    float width = 0.0f,
    float height = 0.0f,
    std::int32_t click_count = 0,
    std::uint32_t modifiers = 0) {
    ui_on_pointer_event(
        static_cast<UiEvent>(event_enum),
        handle,
        logical_x,
        logical_y,
        pointer_id,
        static_cast<UiPointerType>(pointer_type),
        button,
        buttons,
        pressure,
        width,
        height,
        click_count,
        modifiers);
}

inline std::vector<std::uint32_t> ReadCommandBuffer() {
    std::uint32_t word_count = 0;
    const std::uint32_t* words = ui_get_command_buffer(&word_count);
    if (words == nullptr || word_count == 0) {
        return {};
    }
    return std::vector<std::uint32_t>(words, words + word_count);
}

inline std::vector<std::uint32_t> ReadSemanticBuffer() {
    std::uint32_t word_count = 0;
    const std::uint32_t* words = ui_get_semantic_buffer(&word_count);
    if (words == nullptr || word_count == 0) {
        return {};
    }
    return std::vector<std::uint32_t>(words, words + word_count);
}

inline std::vector<std::uint32_t> ReadDebugTreeBuffer() {
    std::uint32_t word_count = 0;
    const std::uint32_t* words = ui_get_debug_tree_buffer(&word_count);
    if (words == nullptr || word_count == 0) {
        return {};
    }
    return std::vector<std::uint32_t>(words, words + word_count);
}

struct Bounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct SemanticRecord {
    std::uint32_t role = 0U;
    std::uint64_t handle = UI_INVALID_HANDLE;
    Bounds bounds{};
    std::uint32_t state_flags = 0U;
    std::uint32_t checked_state = 0U;
    std::uint32_t orientation = 0U;
    float value_now = 0.0f;
    float value_min = 0.0f;
    float value_max = 0.0f;
    std::string label{};
};

inline constexpr std::uint32_t kSemanticHasReadonly = 1U << 7U;
inline constexpr std::uint32_t kSemanticIsReadonly = 1U << 8U;
inline constexpr std::uint32_t kSemanticHasMultiline = 1U << 9U;
inline constexpr std::uint32_t kSemanticIsMultiline = 1U << 10U;

inline constexpr std::uint32_t kDebugTreeMagic = 0x44544231U;
inline constexpr std::uint32_t kDebugTreeVersion = 1U;
inline constexpr std::uint32_t kDebugTreeFixedRecordWords = 52U;
inline constexpr std::uint32_t kDebugTreeFlagVisibleNormal = 1U << 1U;
inline constexpr std::uint32_t kDebugTreeFlagClippedOrEmpty = 1U << 3U;
inline constexpr std::uint32_t kDebugTreeFlagHasNodeId = 1U << 4U;
inline constexpr std::uint32_t kDebugTreeFlagHasSemanticLabel = 1U << 5U;
inline constexpr std::uint32_t kDebugTreeFlagHasBoxStyle = 1U << 6U;
inline constexpr std::uint32_t kDebugTreeBehaviorInteractive = 1U << 0U;
inline constexpr std::uint32_t kDebugTreeBehaviorFocusable = 1U << 1U;
inline constexpr std::uint32_t kDebugTreeBehaviorScrollView = 1U << 5U;
inline constexpr std::uint32_t kDebugTreeBehaviorScrollEnabledX = 1U << 10U;
inline constexpr std::uint32_t kDebugTreeBehaviorScrollEnabledY = 1U << 11U;
inline constexpr std::uint32_t kDebugTreeBehaviorText = 1U << 13U;

struct DebugTreeRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
    std::uint64_t parent = UI_INVALID_HANDLE;
    std::uint32_t node_type = 0U;
    std::uint32_t flags = 0U;
    std::uint32_t behavior_flags = 0U;
    std::uint32_t semantic_role = 0U;
    Bounds bounds{};
    Bounds visible_bounds{};
    Bounds padding{};
    Bounds margin{};
    Bounds border{};
    std::uint32_t bg_color = 0U;
    std::uint32_t border_color = 0U;
    std::uint32_t border_style = 0U;
    float radius_tl = 0.0f;
    float radius_tr = 0.0f;
    float radius_br = 0.0f;
    float radius_bl = 0.0f;
    float opacity = 1.0f;
    std::uint32_t font_id = 0U;
    float font_size = 0.0f;
    std::uint32_t text_color = 0U;
    std::uint64_t nearest_scroll_ancestor = UI_INVALID_HANDLE;
    float scroll_offset_x = 0.0f;
    float scroll_offset_y = 0.0f;
    float scroll_content_width = 0.0f;
    float scroll_content_height = 0.0f;
    float scroll_viewport_width = 0.0f;
    float scroll_viewport_height = 0.0f;
    std::uint64_t scroll_proxy_target = UI_INVALID_HANDLE;
    std::uint32_t text_align = 0U;
    std::uint32_t text_vertical_align = 0U;
    std::uint32_t visibility = 0U;
    std::string node_id{};
    std::string semantic_label{};
};

struct GlyphRunInfo {
    std::uint32_t font_id = 0;
    float font_size = 0.0f;
    std::uint32_t color = 0;
    std::vector<effindom::v2::ui::GlyphPlacement> glyphs{};
};

struct CaretInfo {
    float x = 0.0f;
    float y = 0.0f;
    float height = 0.0f;
    std::uint32_t color = 0U;
    std::uint32_t last_interaction_ms = 0U;
};

struct HighlightInfo {
    std::uint32_t color = 0U;
    std::vector<effindom::v2::ui::Rect> rects{};
};

struct PointerEventRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
    std::uint32_t event = 0U;
};

struct FocusEventRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
    bool is_focused = false;
};

struct TextChangeRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
    std::string text{};
};

struct SelectionChangeRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
};

struct CrossSelectionChangeRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
    std::string text{};
};

struct ClipboardWriteRecord {
    std::string text{};
    std::string rich_json{};
};

struct ClipboardReadRequestRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
};

struct ScrollChangeRecord {
    std::uint64_t handle = UI_INVALID_HANDLE;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float content_width = 0.0f;
    float content_height = 0.0f;
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
};

struct MissingFontCoverageRecord {
    std::uint32_t font_id = 0U;
    std::uint32_t coverage_kind = UI_MISSING_FONT_COVERAGE_UNKNOWN;
    std::string sample_text{};
};

using PointerEventCallback = void (*)(std::uint64_t, std::uint32_t);
using FocusEventCallback = void (*)(std::uint64_t, bool);
using TextChangeCallback = void (*)(std::uint64_t, const std::string&);
using SelectionChangeCallback = void (*)(std::uint64_t, std::uint32_t, std::uint32_t);
using ScrollChangeCallback = void (*)(std::uint64_t, float, float, float, float, float, float);

void RecordPointerEvent(std::uint64_t handle, std::uint32_t event);
void RecordFocusEvent(std::uint64_t handle, bool is_focused);
void RecordTextChange(std::uint64_t handle, const std::string& text);
void RecordSelectionChange(std::uint64_t handle, std::uint32_t start, std::uint32_t end);
void RecordScrollChange(std::uint64_t handle, float offset_x, float offset_y, float content_width, float content_height, float viewport_width, float viewport_height);

extern std::vector<PointerEventRecord> g_pointer_events;
extern std::vector<FocusEventRecord> g_focus_events;
extern std::vector<TextChangeRecord> g_text_changes;
extern std::vector<SelectionChangeRecord> g_selection_changes;
extern std::vector<CrossSelectionChangeRecord> g_cross_selection_changes;
extern std::vector<ClipboardWriteRecord> g_clipboard_writes;
extern std::vector<ClipboardReadRequestRecord> g_clipboard_read_requests;
extern std::vector<ScrollChangeRecord> g_scroll_changes;
extern std::vector<MissingFontCoverageRecord> g_missing_font_coverage_requests;
extern PointerEventCallback g_pointer_event_callback;
extern FocusEventCallback g_focus_event_callback;
extern TextChangeCallback g_text_change_callback;
extern SelectionChangeCallback g_selection_change_callback;
extern ScrollChangeCallback g_scroll_change_callback;

void RecordPointerEvent(std::uint64_t handle, std::uint32_t event);
void RecordFocusEvent(std::uint64_t handle, bool is_focused);
void RecordTextChange(std::uint64_t handle, const std::string& text);
void RecordSelectionChange(std::uint64_t handle, std::uint32_t start, std::uint32_t end);

inline void ResetInteractionLogs() {
    g_pointer_events.clear();
    g_focus_events.clear();
    g_text_changes.clear();
    g_selection_changes.clear();
    g_cross_selection_changes.clear();
    g_clipboard_writes.clear();
    g_clipboard_read_requests.clear();
    g_scroll_changes.clear();
    g_missing_font_coverage_requests.clear();
}

inline void UseRecordingInteractionCallbacks() {
    g_pointer_event_callback = &RecordPointerEvent;
    g_focus_event_callback = &RecordFocusEvent;
    g_text_change_callback = &RecordTextChange;
    g_selection_change_callback = &RecordSelectionChange;
    g_scroll_change_callback = &RecordScrollChange;
    ResetInteractionLogs();
}

inline std::unordered_map<std::uint64_t, bool> ReadInteractiveFlags(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, bool> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            result[handle] = (words[i + 15U] & ED_BOUNDS_FLAG_INTERACTIVE) != 0U;
            i += 16U;
            break;
        }
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::unordered_map<std::uint64_t, std::uint32_t> ReadClipModes(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, std::uint32_t> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            result[handle] = (words[i + 15U] & ED_BOUNDS_CLIP_MODE_MASK) >> ED_BOUNDS_CLIP_MODE_SHIFT;
            i += 16U;
            break;
        }
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::unordered_map<std::uint64_t, std::uint32_t> ReadTextFades(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, std::uint32_t> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            result[(static_cast<std::uint64_t>(words[i + 2U]) << 32U) | static_cast<std::uint64_t>(words[i + 1U])] =
                words[i + 3U];
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::unordered_map<std::uint64_t, Bounds> ReadBounds(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, Bounds> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            result[handle] = Bounds{
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 3U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 4U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 5U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 6U]),
            };
            i += 16U;
            break;
        }
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::unordered_map<std::uint64_t, Bounds> ReadHitBounds(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, Bounds> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            result[handle] = Bounds{
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 7U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 8U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 9U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 10U]),
            };
            i += 16U;
            break;
        }
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::unordered_map<std::uint64_t, Bounds> ReadClipBounds(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, Bounds> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            result[handle] = Bounds{
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 11U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 12U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 13U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 14U]),
            };
            i += 16U;
            break;
        }
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::vector<SemanticRecord> ReadSemanticRecords(const std::vector<std::uint32_t>& words) {
    if (words.empty()) {
        return {};
    }

    const std::uint32_t record_count = words.front();
    std::size_t index = 1U;
    std::vector<SemanticRecord> records{};
    records.reserve(record_count);

    for (std::uint32_t record_index = 0; record_index < record_count; record_index += 1U) {
        REQUIRE(index + 14U <= words.size());

        SemanticRecord record{};
        record.role = words[index];
        record.handle =
            (static_cast<std::uint64_t>(words[index + 2U]) << 32U) |
            static_cast<std::uint64_t>(words[index + 1U]);
        record.bounds = Bounds{
            effindom::v2::ui::CommandBuilder::WordToFloat(words[index + 3U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[index + 4U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[index + 5U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[index + 6U]),
        };
        record.state_flags = words[index + 7U];
        record.checked_state = words[index + 8U];
        record.orientation = words[index + 9U];
        record.value_now = effindom::v2::ui::CommandBuilder::WordToFloat(words[index + 10U]);
        record.value_min = effindom::v2::ui::CommandBuilder::WordToFloat(words[index + 11U]);
        record.value_max = effindom::v2::ui::CommandBuilder::WordToFloat(words[index + 12U]);
        const std::uint32_t label_length = words[index + 13U];
        index += 14U;

        const std::size_t label_word_count = (static_cast<std::size_t>(label_length) + 3U) / 4U;
        REQUIRE(index + label_word_count <= words.size());
        record.label.assign(static_cast<std::size_t>(label_length), '\0');
        if (label_length > 0U) {
            std::memcpy(record.label.data(), words.data() + index, label_length);
        }
        index += label_word_count;
        records.push_back(std::move(record));
    }

    return records;
}

inline std::uint64_t ReadDebugTreeHandle(const std::vector<std::uint32_t>& words, std::size_t index) {
    return (static_cast<std::uint64_t>(words[index + 1U]) << 32U) | static_cast<std::uint64_t>(words[index]);
}

inline std::string ReadDebugTreeString(const std::vector<std::uint32_t>& words, std::size_t& index) {
    REQUIRE(index < words.size());
    const std::uint32_t byte_length = words[index];
    index += 1U;
    const std::size_t word_count = (static_cast<std::size_t>(byte_length) + 3U) / 4U;
    REQUIRE(index + word_count <= words.size());
    std::string value(static_cast<std::size_t>(byte_length), '\0');
    if (byte_length > 0U) {
        std::memcpy(value.data(), words.data() + index, byte_length);
    }
    index += word_count;
    return value;
}

inline std::vector<DebugTreeRecord> ReadDebugTreeRecords(const std::vector<std::uint32_t>& words) {
    if (words.empty()) {
        return {};
    }

    REQUIRE(words.size() >= 4U);
    REQUIRE(words[0] == kDebugTreeMagic);
    REQUIRE(words[1] == kDebugTreeVersion);
    REQUIRE(words[2] == kDebugTreeFixedRecordWords);
    const std::uint32_t record_count = words[3];
    std::size_t index = 4U;
    std::vector<DebugTreeRecord> records{};
    records.reserve(record_count);

    for (std::uint32_t record_index = 0; record_index < record_count; record_index += 1U) {
        REQUIRE(index + kDebugTreeFixedRecordWords <= words.size());
        const std::size_t base = index;
        DebugTreeRecord record{};
        record.handle = ReadDebugTreeHandle(words, base);
        record.parent = ReadDebugTreeHandle(words, base + 2U);
        record.node_type = words[base + 4U];
        record.flags = words[base + 5U];
        record.behavior_flags = words[base + 6U];
        record.semantic_role = words[base + 7U];
        record.bounds = Bounds{
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 8U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 9U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 10U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 11U]),
        };
        record.visible_bounds = Bounds{
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 12U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 13U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 14U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 15U]),
        };
        record.padding = Bounds{
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 16U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 17U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 18U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 19U]),
        };
        record.margin = Bounds{
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 20U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 21U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 22U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 23U]),
        };
        record.border = Bounds{
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 24U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 25U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 26U]),
            effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 27U]),
        };
        record.bg_color = words[base + 28U];
        record.border_color = words[base + 29U];
        record.border_style = words[base + 30U];
        record.radius_tl = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 31U]);
        record.radius_tr = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 32U]);
        record.radius_br = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 33U]);
        record.radius_bl = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 34U]);
        record.opacity = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 35U]);
        record.font_id = words[base + 36U];
        record.font_size = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 37U]);
        record.text_color = words[base + 38U];
        record.nearest_scroll_ancestor = ReadDebugTreeHandle(words, base + 39U);
        record.scroll_offset_x = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 41U]);
        record.scroll_offset_y = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 42U]);
        record.scroll_content_width = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 43U]);
        record.scroll_content_height = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 44U]);
        record.scroll_viewport_width = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 45U]);
        record.scroll_viewport_height = effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 46U]);
        record.scroll_proxy_target = ReadDebugTreeHandle(words, base + 47U);
        record.text_align = words[base + 49U];
        record.text_vertical_align = words[base + 50U];
        record.visibility = words[base + 51U];
        index += kDebugTreeFixedRecordWords;
        record.node_id = ReadDebugTreeString(words, index);
        record.semantic_label = ReadDebugTreeString(words, index);
        records.push_back(std::move(record));
    }

    return records;
}

inline std::unordered_map<std::uint64_t, GlyphRunInfo> ReadGlyphRuns(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, GlyphRunInfo> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_HIGHLIGHTS_COLORED:
            i += 4U + (static_cast<std::size_t>(words[i + 3U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            GlyphRunInfo run{};
            run.font_id = words[i + 3U];
            run.font_size = effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 4U]);
            run.color = words[i + 5U];
            const std::uint32_t glyph_count = words[i + 6U];
            run.glyphs.reserve(glyph_count);
            for (std::uint32_t glyph_index = 0; glyph_index < glyph_count; glyph_index += 1U) {
                const std::size_t base = i + 7U + (static_cast<std::size_t>(glyph_index) * 4U);
                run.glyphs.push_back(effindom::v2::ui::GlyphPlacement{
                    words[base],
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 1U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 2U]),
                    0U,
                    words[base + 3U],
                    run.color,
                    run.font_size,
                });
            }
            result[handle] = run;
            i += 7U + (static_cast<std::size_t>(glyph_count) * 4U);
            break;
        }
        case CMD_SET_GLYPH_RUN_COLORED: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            GlyphRunInfo run{};
            run.font_id = words[i + 3U];
            run.font_size = effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 4U]);
            const std::uint32_t glyph_count = words[i + 5U];
            run.glyphs.reserve(glyph_count);
            for (std::uint32_t glyph_index = 0U; glyph_index < glyph_count; glyph_index += 1U) {
                const std::size_t base = i + 6U + (static_cast<std::size_t>(glyph_index) * 5U);
                run.glyphs.push_back(effindom::v2::ui::GlyphPlacement{
                    words[base],
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 1U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 2U]),
                    0U,
                    words[base + 3U],
                    words[base + 4U],
                    run.font_size,
                });
            }
            result[handle] = run;
            i += 6U + (static_cast<std::size_t>(glyph_count) * 5U);
            break;
        }
        case CMD_SET_GLYPH_RUN_STYLED: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            GlyphRunInfo run{};
            run.font_id = words[i + 3U];
            run.font_size = effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 4U]);
            const std::uint32_t glyph_count = words[i + 5U];
            run.glyphs.reserve(glyph_count);
            for (std::uint32_t glyph_index = 0U; glyph_index < glyph_count; glyph_index += 1U) {
                const std::size_t base = i + 6U + (static_cast<std::size_t>(glyph_index) * 6U);
                run.glyphs.push_back(effindom::v2::ui::GlyphPlacement{
                    words[base],
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 1U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 2U]),
                    0U,
                    words[base + 3U],
                    words[base + 4U],
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 5U]),
                });
            }
            result[handle] = run;
            i += 6U + (static_cast<std::size_t>(glyph_count) * 6U);
            break;
        }
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::unordered_map<std::uint64_t, CaretInfo> ReadCarets(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, CaretInfo> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            CaretInfo info{
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 3U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 4U]),
                effindom::v2::ui::CommandBuilder::WordToFloat(words[i + 5U]),
                words[i + 6U],
                words[i + 7U],
            };
            if (info.height > 0.0f && info.color != 0U) {
                result[handle] = info;
            } else {
                result.erase(handle);
            }
            i += 8U;
            break;
        }
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::unordered_map<std::uint64_t, HighlightInfo> ReadHighlights(const std::vector<std::uint32_t>& words) {
    std::unordered_map<std::uint64_t, HighlightInfo> result{};

    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            HighlightInfo info{};
            info.color = words[i + 3U];
            const std::uint32_t rect_count = words[i + 4U];
            info.rects.reserve(rect_count);
            for (std::uint32_t rect_index = 0; rect_index < rect_count; rect_index += 1U) {
                const std::size_t base = i + 5U + (static_cast<std::size_t>(rect_index) * 4U);
                info.rects.push_back(effindom::v2::ui::Rect{
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 1U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 2U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 3U]),
                });
            }
            if (!info.rects.empty()) {
                result[handle] = std::move(info);
            } else {
                result.erase(handle);
            }
            i += 5U + (static_cast<std::size_t>(rect_count) * 4U);
            break;
        }
        case CMD_SET_HIGHLIGHTS_COLORED: {
            const std::uint64_t handle =
                (static_cast<std::uint64_t>(words[i + 2U]) << 32U) |
                static_cast<std::uint64_t>(words[i + 1U]);
            HighlightInfo info{};
            const std::uint32_t rect_count = words[i + 3U];
            info.rects.reserve(rect_count);
            for (std::uint32_t rect_index = 0; rect_index < rect_count; rect_index += 1U) {
                const std::size_t base = i + 4U + (static_cast<std::size_t>(rect_index) * 5U);
                info.rects.push_back(effindom::v2::ui::Rect{
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 1U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 2U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 3U]),
                });
                if (rect_index == 0U) {
                    info.color = words[base + 4U];
                }
            }
            if (!info.rects.empty()) {
                result[handle] = std::move(info);
            } else {
                result.erase(handle);
            }
            i += 4U + (static_cast<std::size_t>(rect_count) * 5U);
            break;
        }
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }

    return result;
}

inline std::size_t CountCommand(const std::vector<std::uint32_t>& words, std::uint32_t opcode) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < words.size();) {
        const std::uint32_t current = words[i];
        if (current == opcode) {
            count += 1U;
        }
        switch (current) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_HIGHLIGHTS_COLORED:
            i += 4U + (static_cast<std::size_t>(words[i + 3U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            i = words.size();
            break;
        }
    }
    return count;
}

inline std::vector<std::uint64_t> ReadPaintOrder(const std::vector<std::uint32_t>& words) {
    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER: {
            const std::size_t handle_count = static_cast<std::size_t>(words[i + 1U]);
            std::vector<std::uint64_t> result{};
            result.reserve(handle_count);
            for (std::size_t handle_index = 0; handle_index < handle_count; handle_index += 1U) {
                const std::size_t base = i + 2U + (handle_index * 2U);
                result.push_back(
                    (static_cast<std::uint64_t>(words[base + 1U]) << 32U) |
                    static_cast<std::uint64_t>(words[base]));
            }
            return result;
        }
        case CMD_COMMIT_SCENE:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 5U);
            break;
        default:
            return {};
        }
    }
    return {};
}

inline std::vector<effindom::v2::ui::SceneInstruction> ReadScene(const std::vector<std::uint32_t>& words) {
    for (std::size_t i = 0; i < words.size();) {
        switch (words[i]) {
        case CMD_CREATE_NODE:
        case CMD_DELETE_NODE:
            i += 3U;
            break;
        case CMD_SET_BOUNDS:
            i += 16U;
            break;
        case CMD_SET_BOX_STYLE:
            i += 13U;
            break;
        case CMD_SET_LAYER_EFFECT:
            i += 6U;
            break;
        case CMD_SET_BACKGROUND_BLUR:
            i += 4U;
            break;
        case CMD_SET_DROP_SHADOW:
            i += 8U;
            break;
        case CMD_SET_LINEAR_GRADIENT:
            i += 8U + (static_cast<std::size_t>(words[i + 7U]) * 2U);
            break;
        case CMD_SET_IMAGE:
        case CMD_SET_SVG:
            i += 7U;
            break;
        case CMD_SET_IMAGE_NINE:
            i += 10U;
            break;
        case CMD_SET_TEXT_FADE:
            i += 4U;
            break;
        case CMD_SET_CARET:
            i += 8U;
            break;
        case CMD_SET_HIGHLIGHTS:
            i += 5U + (static_cast<std::size_t>(words[i + 4U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN:
            i += 7U + (static_cast<std::size_t>(words[i + 6U]) * 4U);
            break;
        case CMD_SET_GLYPH_RUN_COLORED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 5U);
            break;
        case CMD_SET_GLYPH_RUN_STYLED:
            i += 6U + (static_cast<std::size_t>(words[i + 5U]) * 6U);
            break;
        case CMD_COMMIT_PAINT_ORDER:
            i += 2U + (static_cast<std::size_t>(words[i + 1U]) * 2U);
            break;
        case CMD_COMMIT_SCENE: {
            const std::size_t instruction_count = static_cast<std::size_t>(words[i + 1U]);
            std::vector<effindom::v2::ui::SceneInstruction> result{};
            result.reserve(instruction_count);
            for (std::size_t instruction_index = 0; instruction_index < instruction_count; instruction_index += 1U) {
                const std::size_t base = i + 2U + (instruction_index * 5U);
                result.push_back(effindom::v2::ui::SceneInstruction{
                    words[base],
                    (static_cast<std::uint64_t>(words[base + 2U]) << 32U) | static_cast<std::uint64_t>(words[base + 1U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 3U]),
                    effindom::v2::ui::CommandBuilder::WordToFloat(words[base + 4U]),
                });
            }
            return result;
        }
        default:
            return {};
        }
    }
    return {};
}

} // namespace test_ui_support

using namespace test_ui_support;

extern "C" bool as_on_pointer_event(ui_handle_t handle, UiEvent event_enum);
extern "C" void as_on_focus_changed(ui_handle_t handle, bool is_focused);
extern "C" void as_on_text_changed(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len);
extern "C" void as_on_selection_changed(ui_handle_t handle, uint32_t start_idx, uint32_t end_idx);
extern "C" void as_on_scroll(
    ui_handle_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height);
extern "C" void as_on_cross_selection_changed(ui_handle_t area_handle, const uint8_t* utf8_str, uint32_t len);
extern "C" void as_on_clipboard_write(
    const uint8_t* utf8_plain_text,
    uint32_t plain_text_len,
    const uint8_t* utf8_rich_json,
    uint32_t rich_json_len);
extern "C" void as_on_request_clipboard_read(ui_handle_t handle);
extern "C" void as_on_missing_font_coverage(uint32_t font_id, uint32_t coverage_kind, const uint8_t* utf8_sample, uint32_t len);
