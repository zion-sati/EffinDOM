#include "TestUiSupport.h"

#include <array>

namespace {

struct AccessibilityTextInfo {
    std::uint64_t revision = 0U;
    std::uint32_t character_count = 0U;
    std::uint32_t selection_start = 0U;
    std::uint32_t selection_end = 0U;
    std::uint32_t flags = 0U;
};

AccessibilityTextInfo ReadInfo(ui_handle_t handle) {
    AccessibilityTextInfo info{};
    REQUIRE(ui_get_accessibility_text_info(
        handle,
        &info.revision,
        &info.character_count,
        &info.selection_start,
        &info.selection_end,
        &info.flags));
    return info;
}

std::string ReadRange(
    ui_handle_t handle,
    std::uint64_t revision,
    std::uint32_t start,
    std::uint32_t end) {
    std::uint32_t length = 0U;
    REQUIRE(ui_get_accessibility_text_range_utf8_length(handle, revision, start, end, &length) ==
        UI_TEXT_ACCESSIBILITY_QUERY_OK);
    std::string output(length, '\0');
    REQUIRE(ui_copy_accessibility_text_range_utf8(
        handle,
        revision,
        start,
        end,
        reinterpret_cast<std::uint8_t*>(output.data()),
        length) == UI_TEXT_ACCESSIBILITY_QUERY_OK);
    return output;
}

} // namespace

TEST_CASE("v2 accessibility text queries use Unicode character ranges", "[v2][ui][accessibility-text]") {
    ui_reset();
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    const std::string content = "A我😀e\u0301אב";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_text_selection_range(text, 1U, 8U);

    const AccessibilityTextInfo info = ReadInfo(text);
    CHECK(info.character_count == 7U);
    CHECK(info.selection_start == 1U);
    CHECK(info.selection_end == 3U);
    CHECK((info.flags & UI_TEXT_ACCESSIBILITY_FLAG_READ_ONLY) != 0U);
    CHECK((info.flags & UI_TEXT_ACCESSIBILITY_FLAG_MULTILINE) != 0U);
    CHECK(ReadRange(text, info.revision, 1U, 7U) == "我😀e\u0301אב");
}

TEST_CASE("v2 accessibility text resolves semantic owners to authoritative text documents",
    "[v2][ui][accessibility-text][semantic-owner]") {
    using effindom::v2::ui::GetRuntime;
    ui_reset();
    const ui_handle_t owner = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t wrapper = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(owner != UI_INVALID_HANDLE);
    REQUIRE(wrapper != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_node_add_child(owner, wrapper);
    ui_node_add_child(wrapper, text);
    ui_set_semantic_role(owner, UI_SEMANTIC_TEXTBOX);
    const std::string content = "A\xF0\x9F\x98\x80\xE4\xBD\xA0";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()),
        static_cast<std::uint32_t>(content.size()));
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);

    const AccessibilityTextInfo info = ReadInfo(owner);
    CHECK(info.character_count == 3U);
    CHECK(ReadRange(owner, info.revision, 1U, 3U) == "\xF0\x9F\x98\x80\xE4\xBD\xA0");
    CHECK(GetRuntime().GetAccessibilityTextOwnerHandle(text) == owner);
    REQUIRE(ui_set_accessibility_text_selection(owner, info.revision, 1U, 2U) ==
        UI_TEXT_ACCESSIBILITY_QUERY_OK);
    CHECK(ReadInfo(owner).selection_start == 1U);
}

TEST_CASE("v2 accessibility text sanitizes malformed UTF-8 without splitting valid characters", "[v2][ui][accessibility-text]") {
    ui_reset();
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    const std::array<std::uint8_t, 7U> content{'A', 0xF0U, 0x28U, 0x8CU, 0x28U, 0xC0U, 0xAFU};
    ui_set_text(text, content.data(), static_cast<std::uint32_t>(content.size()));

    const AccessibilityTextInfo info = ReadInfo(text);
    CHECK(info.character_count == 7U);
    CHECK(ReadRange(text, info.revision, 0U, 7U) == "A�(�(��");
    CHECK(ReadRange(text, info.revision, 1U, 2U) == "�");
}

TEST_CASE("v2 accessibility text rejects stale invalid and obscured reads atomically", "[v2][ui][accessibility-text]") {
    ui_reset();
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    constexpr std::string_view initial = "secret text";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(initial.data()), static_cast<std::uint32_t>(initial.size()));
    const AccessibilityTextInfo first = ReadInfo(text);

    std::array<std::uint8_t, 16U> output{};
    output.fill(0xA5U);
    CHECK(ui_copy_accessibility_text_range_utf8(
        text, first.revision, 5U, 2U, output.data(), static_cast<std::uint32_t>(output.size())) ==
        UI_TEXT_ACCESSIBILITY_QUERY_INVALID_RANGE);
    CHECK(output.front() == 0xA5U);

    constexpr std::string_view changed = "changed";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(changed.data()), static_cast<std::uint32_t>(changed.size()));
    CHECK(ui_copy_accessibility_text_range_utf8(
        text, first.revision, 0U, 1U, output.data(), static_cast<std::uint32_t>(output.size())) ==
        UI_TEXT_ACCESSIBILITY_QUERY_STALE_REVISION);
    CHECK(output.front() == 0xA5U);

    ui_set_text_obscured(text, true);
    const AccessibilityTextInfo obscured = ReadInfo(text);
    CHECK((obscured.flags & UI_TEXT_ACCESSIBILITY_FLAG_OBSCURED) != 0U);
    CHECK(ui_copy_accessibility_text_range_utf8(
        text, obscured.revision, 0U, 1U, output.data(), static_cast<std::uint32_t>(output.size())) ==
        UI_TEXT_ACCESSIBILITY_QUERY_OBSCURED);
    CHECK(output.front() == 0xA5U);
}

TEST_CASE("v2 accessibility text range geometry maps character indices to retained byte offsets", "[v2][ui][accessibility-text]") {
    ui_reset();
    const auto font_bytes = ReadFileBytes(std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    REQUIRE(ui_register_font(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size())));
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    const std::string content = "A我B";
    ui_set_root(text);
    ui_resize_window(200.0f, 60.0f);
    ui_set_font(text, 1U, 20.0f);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_commit_frame();
    const AccessibilityTextInfo info = ReadInfo(text);

    std::uint32_t rect_count = 0U;
    REQUIRE(ui_get_accessibility_text_range_rect_count(text, info.revision, 1U, 2U, &rect_count) ==
        UI_TEXT_ACCESSIBILITY_QUERY_OK);
    REQUIRE(rect_count > 0U);
    std::vector<float> words(static_cast<std::size_t>(rect_count) * 4U);
    std::uint32_t copied = 0U;
    REQUIRE(ui_copy_accessibility_text_range_rects(
        text, info.revision, 1U, 2U, words.data(), rect_count, &copied) ==
        UI_TEXT_ACCESSIBILITY_QUERY_OK);
    CHECK(copied == rect_count);
    CHECK(words[2] > 0.0f);
    CHECK(words[3] > 0.0f);
}

TEST_CASE("v2 accessibility text reveal resolves semantic owners and revision-checked character ranges",
    "[v2][ui][accessibility-text][reveal]") {
    ui_reset();
    const ui_handle_t owner = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t scroll = ui_create_node(UI_NODE_FLEX_BOX);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(owner != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_node_add_child(owner, scroll);
    ui_node_add_child(scroll, text);
    ui_set_semantic_role(owner, UI_SEMANTIC_TEXTBOX);
    ui_set_root(owner);
    ui_resize_window(200.0f, 60.0f);
    const std::string content = "A我😀B";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()),
        static_cast<std::uint32_t>(content.size()));
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);
    ui_commit_frame();

    const AccessibilityTextInfo info = ReadInfo(owner);
    CHECK(ui_reveal_accessibility_text_range(owner, info.revision, 1U, 3U) ==
        UI_TEXT_ACCESSIBILITY_QUERY_OK);
    CHECK(ui_reveal_accessibility_text_range(owner, info.revision, 3U, 1U) ==
        UI_TEXT_ACCESSIBILITY_QUERY_INVALID_RANGE);
    constexpr std::string_view changed = "changed";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(changed.data()),
        static_cast<std::uint32_t>(changed.size()));
    CHECK(ui_reveal_accessibility_text_range(owner, info.revision, 1U, 3U) ==
        UI_TEXT_ACCESSIBILITY_QUERY_STALE_REVISION);
}

TEST_CASE("v2 accessibility text serves bounded slices from very large documents", "[v2][ui][accessibility-text]") {
    ui_reset();
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    std::string content(1'000'000U, 'x');
    content.replace(500'000U, 3U, "我");
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    const AccessibilityTextInfo info = ReadInfo(text);
    CHECK(info.character_count == 999'998U);
    CHECK(ReadRange(text, info.revision, 499'999U, 500'002U) == "x我x");
}

TEST_CASE("v2 huge textbox commits and parent scrolling stay accessibility-lazy",
    "[v2][ui][accessibility-text][performance]") {
    using effindom::v2::ui::GetRuntime;
    ui_reset();
    const ui_handle_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_root(scroll);
    ui_resize_window(320.0f, 80.0f);
    ui_set_width(scroll, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_scroll_enabled(scroll, false, true);
    ui_set_scroll_content_size(scroll, 320.0f, 1000.0f);
    ui_set_width(text, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_text_limits(text, 1'000'000, 1);
    ui_set_semantic_role(text, UI_SEMANTIC_TEXTBOX);
    ui_node_add_child(scroll, text);

    std::string document(1'000'000U, 'x');
    document.replace(500'000U, 3U, "我");
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(document.data()),
        static_cast<std::uint32_t>(document.size()));
    ui_commit_frame();

    GetRuntime().ClearAccessibilityTextProfile();
    ui_set_scroll_offset(scroll, 0.0f, 120.0f);
    ui_commit_frame();
    const auto inactive = GetRuntime().accessibility_text_profile();
    CHECK(inactive.metadata_queries == 0U);
    CHECK(inactive.range_queries == 0U);
    CHECK(inactive.materialized_utf8_bytes == 0U);
    CHECK(inactive.geometry_queries == 0U);

    const AccessibilityTextInfo info = ReadInfo(text);
    CHECK(ReadRange(text, info.revision, 499'999U, 500'002U) == "x我x");
    const auto active = GetRuntime().accessibility_text_profile();
    CHECK(active.metadata_queries == 1U);
    CHECK(active.range_queries == 2U);
    CHECK(active.requested_characters == 6U);
    CHECK(active.materialized_utf8_bytes == 10U);
    CHECK(active.geometry_queries == 0U);
}

TEST_CASE("v2 accessibility text edits and selections use revision-checked character ranges", "[v2][ui][accessibility-text]") {
    using effindom::v2::ui::GetRuntime;
    using EventKind = effindom::v2::ui::UiEventSink::AccessibilityTextEventKind;

    ui_reset();
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    const std::string content = "A我😀B";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);
    std::vector<EventKind> events{};
    GetRuntime().SetAccessibilityTextEventCallback(
        [&](EventKind kind, std::uint64_t handle) {
            CHECK(handle == text);
            events.push_back(kind);
        });

    const AccessibilityTextInfo first = ReadInfo(text);
    REQUIRE(ui_set_accessibility_text_selection(text, first.revision, 1U, 3U) ==
        UI_TEXT_ACCESSIBILITY_QUERY_OK);
    const AccessibilityTextInfo selected = ReadInfo(text);
    CHECK(selected.selection_start == 1U);
    CHECK(selected.selection_end == 3U);
    REQUIRE(events == std::vector<EventKind>{EventKind::SelectionChanged});

    constexpr std::string_view replacement = "é";
    std::uint64_t next_revision = 0U;
    REQUIRE(ui_replace_accessibility_text_range(
        text,
        selected.revision,
        1U,
        3U,
        reinterpret_cast<const std::uint8_t*>(replacement.data()),
        static_cast<std::uint32_t>(replacement.size()),
        &next_revision) == UI_TEXT_ACCESSIBILITY_QUERY_OK);
    CHECK(next_revision != first.revision);
    const AccessibilityTextInfo replaced = ReadInfo(text);
    CHECK(ReadRange(text, replaced.revision, 0U, replaced.character_count) == "AéB");
    CHECK(events == std::vector<EventKind>{
        EventKind::SelectionChanged,
        EventKind::SelectionChanged,
        EventKind::DocumentChanged,
        EventKind::SelectionChanged,
    });
    CHECK(ui_set_accessibility_text_selection(text, first.revision, 0U, 1U) ==
        UI_TEXT_ACCESSIBILITY_QUERY_STALE_REVISION);
    GetRuntime().SetAccessibilityTextEventCallback({});
}

TEST_CASE("v2 accessibility text edits reject read-only and malformed replacements atomically", "[v2][ui][accessibility-text]") {
    ui_reset();
    const ui_handle_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    constexpr std::string_view content = "unchanged";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(content.data()), static_cast<std::uint32_t>(content.size()));
    const AccessibilityTextInfo read_only = ReadInfo(text);
    std::uint64_t revision = 0U;
    constexpr std::string_view replacement = "changed";
    CHECK(ui_replace_accessibility_text_range(
        text,
        read_only.revision,
        0U,
        read_only.character_count,
        reinterpret_cast<const std::uint8_t*>(replacement.data()),
        static_cast<std::uint32_t>(replacement.size()),
        &revision) == UI_TEXT_ACCESSIBILITY_QUERY_READ_ONLY);
    CHECK(ReadRange(text, read_only.revision, 0U, read_only.character_count) == content);

    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_editable(text, true);
    const AccessibilityTextInfo editable = ReadInfo(text);
    const std::array<std::uint8_t, 2U> malformed{0xC0U, 0xAFU};
    CHECK(ui_replace_accessibility_text_range(
        text,
        editable.revision,
        0U,
        1U,
        malformed.data(),
        static_cast<std::uint32_t>(malformed.size()),
        &revision) == UI_TEXT_ACCESSIBILITY_QUERY_INVALID_TEXT);
    CHECK(ReadRange(text, editable.revision, 0U, editable.character_count) == content);
}
