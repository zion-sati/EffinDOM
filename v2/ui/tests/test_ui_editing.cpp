#include "TestUiSupport.h"

TEST_CASE("v2 ui private editing helpers cover guard paths", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const auto font_bytes = ReadFileBytes(
        std::string(EFFINDOM_SOURCE_DIR) + "/v2/fonts/DejaVuSans.ttf");
    REQUIRE(GetRuntime().RegisterFont(1U, font_bytes.data(), static_cast<std::uint32_t>(font_bytes.size())));

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    const std::uint64_t box = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(text != UI_INVALID_HANDLE);
    REQUIRE(box != UI_INVALID_HANDLE);

    auto* text_node = GetRuntime().ResolveMutable(text);
    auto* box_node = GetRuntime().ResolveMutable(box);
    REQUIRE(text_node != nullptr);
    REQUIRE(box_node != nullptr);

    GetRuntime().Selection().state().active_handle = text;
    CHECK(GetRuntime().SetSelectable(text, false, 0x40007AFFU));
    CHECK(GetRuntime().Selection().state().active_handle == UI_INVALID_HANDLE);
    CHECK_FALSE(GetRuntime().SetSelectable(box, true, 0x40007AFFU));
    CHECK_FALSE(GetRuntime().SetCaretColor(box, 0xFF000000U));

    text_node->is_text_node = true;
    text_node->is_selectable = false;
    text_node->is_editable = true;
    text_node->text_content = "abc";
    text_node->selection_start = 2U;
    text_node->selection_end = 2U;
    GetRuntime().SetFocus(UI_INVALID_HANDLE);
    GetRuntime().SetInteractionTime(91U);
    GetRuntime().HandleImeUpdate(text, nullptr, 0U, 5U);
    CHECK(text_node->text_content.empty());
    CHECK(text_node->selection_start == 0U);
    CHECK(text_node->selection_end == 0U);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    text_node->text_content = "same";
    text_node->selection_start = 2U;
    text_node->selection_end = 2U;
    ResetInteractionLogs();
    GetRuntime().HandleImeUpdate(text, reinterpret_cast<const std::uint8_t*>("same"), 4U, 2U);
    CHECK(g_text_changes.empty());
    GetRuntime().HandleImeUpdate(UI_INVALID_HANDLE, nullptr, 1U, 0U);

    text_node->text_content = "abc";
    text_node->selection_start = 1U;
    text_node->selection_end = 1U;
    GetRuntime().HandlePasteText(text, nullptr, 1U);
    CHECK(text_node->text_content == "abc");
    GetRuntime().HandlePasteText(box, reinterpret_cast<const std::uint8_t*>("x"), 1U);
    CHECK(text_node->text_content == "abc");

    text_node->is_selectable = false;
    text_node->is_editable = false;
    GetRuntime().HandlePasteText(text, reinterpret_cast<const std::uint8_t*>("x"), 1U);
    CHECK(text_node->text_content == "abc");
    CHECK_FALSE(GetRuntime().HandlePaste(*text_node));

    text_node->is_selectable = true;
    text_node->is_editable = true;
    GetRuntime().SetFocus(text);
    ResetInteractionLogs();
    GetRuntime().HandlePasteText(text, nullptr, 0U);
    CHECK(text_node->text_content == "abc");
    CHECK(g_text_changes.empty());

    text_node->selection_start = 0U;
    text_node->selection_end = 2U;
    GetRuntime().HandlePasteText(text, nullptr, 0U);
    CHECK(text_node->text_content == "c");
    CHECK(text_node->selection_start == 0U);
    CHECK(text_node->selection_end == 0U);

    effindom::v2::ui::UINode plain_node{};
    CHECK(GetRuntime().GetParagraphBoundaries(plain_node, 0U) == std::pair<std::uint32_t, std::uint32_t>{0U, 0U});
    CHECK(GetRuntime().GetStringIndexFromPoint(plain_node, 10.0f, 10.0f) == 0U);
    CHECK(GetRuntime().GetLocalPositionFromIndex(plain_node, 2U) == std::pair<float, int>{0.0f, 0});
    CHECK(GetRuntime().IndexForLineBegin(plain_node, 1U) == 0U);
    CHECK(GetRuntime().IndexForLineEnd(plain_node, 1U) == 0U);
    CHECK(GetRuntime().IndexForVerticalMove(plain_node, 1U, true) == 0U);
    CHECK(GetRuntime().BuildSelectionRects(plain_node, 0U, 0U, std::nullopt).empty());

    effindom::v2::ui::UINode helper{};
    helper.is_text_node = true;
    helper.text_content = "Hello";
    helper.font_id = 1U;
    helper.font_size = 20.0f;
    helper.layout_width = 100.0f;
    helper.line_height = 24.0f;
    helper.break_offsets = {0, 5};
    helper.visible_line_count = 0U;
    CHECK(GetRuntime().GetAlignedLineXOffset(helper, 40.0f) == Approx(0.0f));
    helper.text_align = UI_TEXT_ALIGN_CENTER;
    CHECK(GetRuntime().GetAlignedLineXOffset(helper, 40.0f) == Approx(30.0f));
    helper.text_align = UI_TEXT_ALIGN_RIGHT;
    CHECK(GetRuntime().GetAlignedLineXOffset(helper, 40.0f) == Approx(60.0f));
    helper.text_align = UI_TEXT_ALIGN_LEFT;
    CHECK(GetRuntime().GetStringIndexFromPoint(helper, -1.0f, 3.0f) == 0U);
    CHECK(GetRuntime().GetStringIndexFromPoint(helper, 500.0f, 3.0f) == 5U);
    helper.text_align = UI_TEXT_ALIGN_CENTER;
    CHECK(GetRuntime().GetStringIndexFromPoint(helper, 1.0f, 3.0f) == 0U);
    helper.font_id = 999U;
    CHECK(GetRuntime().GetStringIndexFromPoint(helper, 25.0f, 3.0f) == 0U);
    CHECK(GetRuntime().GetLocalPositionFromIndex(helper, 2U) == std::pair<float, int>{0.0f, 0});
    helper.font_id = 1U;
    helper.text_align = UI_TEXT_ALIGN_LEFT;
    CHECK(GetRuntime().GetLocalPositionFromIndex(helper, 0U).second == 0);
    CHECK(GetRuntime().GetLocalPositionFromIndex(helper, 5U).first >= 0.0f);

    effindom::v2::ui::UINode weird_breaks{};
    weird_breaks.is_text_node = true;
    weird_breaks.text_content = "abcde";
    weird_breaks.break_offsets = {0, 0, 5};
    CHECK(GetRuntime().IndexForLineBegin(weird_breaks, 0U) == 0U);
    CHECK(GetRuntime().IndexForLineEnd(weird_breaks, 0U) == 0U);
}

TEST_CASE("v2 ui programmatic text selection emits the requested range without forcing focus", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_set_root(root);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("Melbourne"), 9U);
    ui_set_selectable(text, true, kDefaultSelectionColor);
    ui_set_focusable(text, true, 0);
    ui_node_add_child(root, text);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->selection_start = 0U;
    node->selection_end = 0U;
    ResetInteractionLogs();

    REQUIRE(GetRuntime().SetTextSelectionRange(text, 9U, 9U));

    CHECK(node->selection_start == 9U);
    CHECK(node->selection_end == 9U);
    REQUIRE_FALSE(g_selection_changes.empty());
    CHECK(g_selection_changes.front().handle == text);
    CHECK(g_selection_changes.front().start == 9U);
    CHECK(g_selection_changes.front().end == 9U);
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
}

TEST_CASE("v2 ui private editable mutation helpers cover undo and delete branches", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::UINode;

    ui_reset();
    UseRecordingInteractionCallbacks();

    UINode plain_node{};
    GetRuntime().BeginUndoGroup(plain_node);
    CHECK(plain_node.undo_stack.empty());
    CHECK_FALSE(GetRuntime().UndoTextEdit(1U, plain_node));

    UINode undo_node{};
    undo_node.is_editable = true;
    undo_node.text_content = "after";
    undo_node.selection_start = 5U;
    undo_node.selection_end = 5U;
    undo_node.undo_stack.push_back(UINode::UndoEntry{"before", 0U, 0U, 0U});
    undo_node.redo_stack.resize(UINode::kMaxUndo);
    CHECK(GetRuntime().UndoTextEdit(2U, undo_node));
    CHECK(undo_node.text_content == "before");
    CHECK(undo_node.redo_stack.size() == UINode::kMaxUndo);

    UINode redo_node{};
    redo_node.is_editable = true;
    redo_node.text_content = "before";
    redo_node.selection_start = 0U;
    redo_node.selection_end = 0U;
    redo_node.redo_stack.push_back(UINode::UndoEntry{"after", 5U, 5U, 5U});
    redo_node.undo_stack.resize(UINode::kMaxUndo);
    CHECK(GetRuntime().RedoTextEdit(3U, redo_node));
    CHECK(redo_node.text_content == "after");
    CHECK(redo_node.undo_stack.size() == UINode::kMaxUndo);

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->is_text_node = true;
    node->is_selectable = true;
    node->is_editable = true;
    GetRuntime().SetFocus(text);

    node->text_content = "abc";
    node->selection_start = 0U;
    node->selection_end = 0U;
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(*node, "Backspace", 0U));

    node->selection_start = 3U;
    node->selection_end = 3U;
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(*node, "Delete", 0U));

    node->text_content = "abc";
    node->selection_start = 2U;
    node->selection_end = 2U;
    CHECK(GetRuntime().HandleTextEditingKey(*node, "Backspace", 0U));
    CHECK(node->text_content == "ac");

    node->text_content = "abc";
    node->selection_start = 1U;
    node->selection_end = 1U;
    CHECK(GetRuntime().HandleTextEditingKey(*node, "Delete", 0U));
    CHECK(node->text_content == "ac");

    node->text_content = "abc";
    node->selection_start = 1U;
    node->selection_end = 1U;
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(*node, "Up", 0U));
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(*node, "Insert", 0U));
    CHECK(node->text_content == "abc");

    node->text_content = "abcd";
    node->selection_start = 1U;
    node->selection_end = 3U;
    CHECK(GetRuntime().HandleTextEditingKey(*node, "Delete", 0U));
    CHECK(node->text_content == "ad");

    const std::string unicode_text = u8"Hello World! こんにちは世界 🌍✨";
    const std::size_t globe_pos = unicode_text.find(u8"🌍");
    REQUIRE(globe_pos != std::string::npos);
    const std::size_t sparkle_pos = unicode_text.find(u8"✨");
    REQUIRE(sparkle_pos != std::string::npos);

    node->text_content = unicode_text;
    node->selection_start = static_cast<std::uint32_t>(unicode_text.size());
    node->selection_end = static_cast<std::uint32_t>(unicode_text.size());
    CHECK(GetRuntime().HandleTextEditingKey(*node, "Backspace", 0U));
    CHECK(node->text_content == u8"Hello World! こんにちは世界 🌍");

    node->text_content = unicode_text;
    node->selection_start = static_cast<std::uint32_t>(globe_pos);
    node->selection_end = static_cast<std::uint32_t>(globe_pos);
    CHECK(GetRuntime().HandleTextEditingKey(*node, "Delete", 0U));
    CHECK(node->text_content == u8"Hello World! こんにちは世界 ✨");
}

TEST_CASE("v2 ui private editing helpers preserve multiline delete boundary semantics", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->is_text_node = true;
    node->is_selectable = true;
    node->is_editable = true;
    GetRuntime().SetFocus(text);

    struct DeleteCase {
        const char* name;
        std::string text;
        std::uint32_t selection_start;
        std::uint32_t selection_end;
        const char* key;
        std::string expected_text;
        std::uint32_t expected_caret;
    };

    const std::vector<DeleteCase> cases{
        {
            "delete middle line text only keeps the blank line",
            "alpha\nbeta\ngamma",
            6U,
            10U,
            "Delete",
            "alpha\n\ngamma",
            6U,
        },
        {
            "reverse selected middle line text only keeps the blank line",
            "alpha\nbeta\ngamma",
            10U,
            6U,
            "Backspace",
            "alpha\n\ngamma",
            6U,
        },
        {
            "delete middle line plus trailing newline joins the following line",
            "alpha\nbeta\ngamma",
            6U,
            11U,
            "Delete",
            "alpha\ngamma",
            6U,
        },
        {
            "reverse selected middle line plus trailing newline joins the following line",
            "alpha\nbeta\ngamma",
            11U,
            6U,
            "Backspace",
            "alpha\ngamma",
            6U,
        },
        {
            "deleting only the newline merges adjacent lines",
            "alpha\nbeta\ngamma",
            5U,
            6U,
            "Delete",
            "alphabeta\ngamma",
            5U,
        },
        {
            "deleting a CRLF boundary merges the surrounding lines once",
            "a\r\nb\r\nc",
            1U,
            3U,
            "Backspace",
            "ab\r\nc",
            1U,
        },
    };

    for (const DeleteCase& test_case : cases) {
        DYNAMIC_SECTION(test_case.name) {
            node->text_content = test_case.text;
            node->selection_start = test_case.selection_start;
            node->selection_end = test_case.selection_end;

            REQUIRE(GetRuntime().HandleTextEditingKey(*node, test_case.key, 0U));
            CHECK(node->text_content == test_case.expected_text);
            CHECK(node->selection_start == test_case.expected_caret);
            CHECK(node->selection_end == test_case.expected_caret);
        }
    }
}

TEST_CASE("v2 ui private unicode editing helpers cover ICU and backward navigation", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    effindom::v2::ui::UINode unicode_node{};
    unicode_node.is_text_node = true;
    unicode_node.text_content = u8"éclair 漢字";
    unicode_node.break_offsets = {0, static_cast<std::int32_t>(unicode_node.text_content.size())};
    unicode_node.line_widths = {80.0f};
    unicode_node.line_height = 20.0f;
    unicode_node.visible_line_count = 1U;
    unicode_node.layout_width = 120.0f;

    const auto word = GetRuntime().GetWordBoundaries(unicode_node, 1U);
    CHECK(word.first <= word.second);
    CHECK(word.second <= unicode_node.text_content.size());
    const auto trailing_word = GetRuntime().GetWordBoundaries(
        unicode_node,
        static_cast<std::uint32_t>(unicode_node.text_content.size()));
    CHECK(trailing_word.first <= trailing_word.second);
    CHECK(trailing_word.second == unicode_node.text_content.size());
    CHECK(GetRuntime().NextWordIndex(unicode_node, 0U, true) <= unicode_node.text_content.size());
    CHECK(
        GetRuntime().NextWordIndex(
            unicode_node,
            static_cast<std::uint32_t>(unicode_node.text_content.size()),
            false) <= unicode_node.text_content.size());

    effindom::v2::ui::UINode unicode_padded{};
    unicode_padded.is_text_node = true;
    unicode_padded.text_content = u8" !éclair";
    CHECK(GetRuntime().NextWordIndex(unicode_padded, 0U, true) == 2U);
    CHECK(GetRuntime().NextWordIndex(unicode_padded, 3U, true) == unicode_padded.text_content.size());
    CHECK(GetRuntime().NextWordIndex(unicode_padded, 2U, true) == unicode_padded.text_content.size());
    CHECK(GetRuntime().NextWordIndex(unicode_padded, 3U, false) == 2U);
    CHECK(GetRuntime().NextWordIndex(unicode_padded, static_cast<std::uint32_t>(unicode_padded.text_content.size()), false) == 2U);

    effindom::v2::ui::UINode unicode_trailing_punct{};
    unicode_trailing_punct.is_text_node = true;
    unicode_trailing_punct.text_content = u8"éclair !";
    CHECK(
        GetRuntime().NextWordIndex(
            unicode_trailing_punct,
            static_cast<std::uint32_t>(unicode_trailing_punct.text_content.size()),
            false) == 0U);

    effindom::v2::ui::UINode unicode_only_punct{};
    unicode_only_punct.is_text_node = true;
    unicode_only_punct.text_content = u8" !";
    CHECK(
        GetRuntime().NextWordIndex(
            unicode_only_punct,
            static_cast<std::uint32_t>(unicode_only_punct.text_content.size()),
            false) == 0U);
    effindom::v2::ui::UINode empty_like{};
    CHECK(GetRuntime().GetWordBoundaries(empty_like, 0U) == std::pair<std::uint32_t, std::uint32_t>{0U, 0U});

    const std::string utf8 = u8"é";
    CHECK(GetRuntime().NextCharacterIndex(utf8, 0U, true) == utf8.size());
    CHECK(GetRuntime().NextCharacterIndex(utf8, static_cast<std::uint32_t>(utf8.size()), false) == 0U);
}

TEST_CASE("v2 ui private editing helpers cover alignment and boundary cases", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    effindom::v2::ui::UINode helper{};
    helper.is_text_node = true;
    helper.text_content = "alpha beta";
    helper.break_offsets = {0, 6, 10};
    helper.line_widths = {40.0f, 30.0f};
    helper.line_height = 14.0f;
    helper.visible_line_count = 2U;
    helper.layout_width = 100.0f;

    CHECK(GetRuntime().NextCharacterIndex("abc", 3U, true) == 3U);
    CHECK(GetRuntime().NextCharacterIndex("abc", 0U, false) == 0U);
    CHECK(GetRuntime().NextWordIndex(helper, 5U, true) == 6U);
    CHECK(GetRuntime().NextWordIndex(helper, 5U, false) == 0U);
    CHECK(
        GetRuntime().GetWordBoundaries(helper, static_cast<std::uint32_t>(helper.text_content.size())) ==
        std::pair<std::uint32_t, std::uint32_t>{6U, 10U});
    effindom::v2::ui::UINode paragraph_helper{};
    paragraph_helper.is_text_node = true;
    paragraph_helper.text_content = "alpha\nbeta\ngamma";
    CHECK(GetRuntime().GetParagraphBoundaries(paragraph_helper, 0U) == std::pair<std::uint32_t, std::uint32_t>{0U, 5U});
    CHECK(GetRuntime().GetParagraphBoundaries(paragraph_helper, 5U) == std::pair<std::uint32_t, std::uint32_t>{0U, 5U});
    CHECK(GetRuntime().GetParagraphBoundaries(paragraph_helper, 6U) == std::pair<std::uint32_t, std::uint32_t>{6U, 10U});
    CHECK(GetRuntime().GetParagraphBoundaries(paragraph_helper, static_cast<std::uint32_t>(paragraph_helper.text_content.size())) ==
        std::pair<std::uint32_t, std::uint32_t>{11U, 16U});
    CHECK(GetRuntime().IndexForVerticalMove(helper, 2U, false) == 2U);
    CHECK(GetRuntime().IndexForVerticalMove(helper, 9U, true) == 9U);
}

TEST_CASE("v2 ui private text editing key helpers keep readonly shift-extension gated to an existing selection", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.is_selectable = true;
    node.text_content = "Hello brave world";
    node.break_offsets = {0, static_cast<std::int32_t>(node.text_content.size())};
    node.line_height = 20.0f;
    node.visible_line_count = 1U;

    const std::uint64_t focus_handle = ui_create_node(UI_NODE_TEXT);
    REQUIRE(focus_handle != UI_INVALID_HANDLE);
    GetRuntime().SetFocus(focus_handle);
    GetRuntime().Selection().state().anchor_handle = 77U;
    GetRuntime().Selection().state().anchor_index = 3U;

    CHECK_FALSE(GetRuntime().HandleTextEditingKey(node, std::string_view{}, 0U));
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(node, "Q", 0U));
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowUp", 0U));
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowDown", 0U));

    node.selection_start = 2U;
    node.selection_end = 6U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowLeft", 0U));
    CHECK(node.selection_start == 2U);
    CHECK(node.selection_end == 2U);

    node.selection_start = 2U;
    node.selection_end = 6U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowRight", 0U));
    CHECK(node.selection_start == 6U);
    CHECK(node.selection_end == 6U);

    node.selection_start = 4U;
    node.selection_end = 4U;
    GetRuntime().Selection().state().anchor_handle = 12U;
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(node, "ArrowRight", UI_KEY_MOD_SHIFT));
    CHECK(GetRuntime().Selection().state().anchor_handle == 12U);
    CHECK(node.selection_start == 4U);
    CHECK(node.selection_end == 4U);

    node.selection_start = 4U;
    node.selection_end = 4U;
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(node, "ArrowLeft", UI_KEY_MOD_SHIFT));
    CHECK(node.selection_start == 4U);
    CHECK(node.selection_end == 4U);
    CHECK_FALSE(GetRuntime().HandleTextEditingKey(node, "ArrowDown", UI_KEY_MOD_SHIFT));
    CHECK(node.selection_start == 4U);
    CHECK(node.selection_end == 4U);

    node.selection_start = 10U;
    node.selection_end = 12U;
    GetRuntime().Selection().state().anchor_handle = 77U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowLeft", UI_KEY_MOD_SHIFT));
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowLeft", UI_KEY_MOD_SHIFT));
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowLeft", UI_KEY_MOD_SHIFT));
    CHECK(GetRuntime().Selection().state().anchor_handle == focus_handle);
    CHECK(GetRuntime().Selection().state().anchor_index == 10U);
    CHECK(node.selection_start == 10U);
    CHECK(node.selection_end == 9U);

    node.selection_start = 5U;
    node.selection_end = 5U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowLeft", 0U));
    CHECK(node.selection_start == 4U);
    CHECK(node.selection_end == 4U);

    node.selection_start = 6U;
    node.selection_end = 6U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowLeft", UI_KEY_MOD_CTRL));
    CHECK(node.selection_start == 0U);
    CHECK(node.selection_end == 0U);
}

TEST_CASE("v2 ui private text editing key helpers honor platform word modifiers", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::PlatformFamily;

    ui_reset();
    UseRecordingInteractionCallbacks();

    effindom::v2::ui::UINode node{};
    node.is_text_node = true;
    node.is_selectable = true;
    node.is_editable = true;
    node.text_content = "Hello brave world";
    node.break_offsets = {0, static_cast<std::int32_t>(node.text_content.size())};
    node.line_height = 20.0f;
    node.visible_line_count = 1U;

    const std::uint32_t expected_forward = GetRuntime().NextWordIndex(node, 6U, true);

    GetRuntime().SetPlatformFamily(static_cast<std::uint32_t>(PlatformFamily::Windows));
    node.selection_start = 6U;
    node.selection_end = 6U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowRight", UI_KEY_MOD_CTRL));
    CHECK(node.selection_start == expected_forward);
    CHECK(node.selection_end == expected_forward);

    GetRuntime().SetPlatformFamily(static_cast<std::uint32_t>(PlatformFamily::Apple));
    node.selection_start = 6U;
    node.selection_end = 6U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowRight", UI_KEY_MOD_ALT));
    CHECK(node.selection_start == expected_forward);
    CHECK(node.selection_end == expected_forward);

    node.selection_start = 6U;
    node.selection_end = 6U;
    CHECK(GetRuntime().HandleTextEditingKey(node, "ArrowRight", UI_KEY_MOD_CTRL));
    CHECK(node.selection_start < expected_forward);
    CHECK(node.selection_end < expected_forward);
}

TEST_CASE("v2 ui private input and node helpers cover cleanup branches", "[v2][ui][unit][input]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    UseRecordingInteractionCallbacks();

    CHECK(GetRuntime().GetNextFocusable(UI_INVALID_HANDLE, true) == UI_INVALID_HANDLE);

    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);

    ui_node_add_child(root, child);
    ui_node_add_child(child, text);
    CHECK(GetRuntime().SubtreeContains(root, text));
    CHECK_FALSE(GetRuntime().SubtreeContains(effindom::v2::ui::PackHandle(123U, 1U), text));

    GetRuntime().Input().state().last_hovered_handle = text;
    GetRuntime().Selection().state().active_handle = text;
    GetRuntime().SetFocus(text);
    CHECK(GetRuntime().DeleteNode(root));
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    CHECK(GetRuntime().Input().state().last_hovered_handle == UI_INVALID_HANDLE);
    CHECK(GetRuntime().Selection().state().active_handle == UI_INVALID_HANDLE);

    const std::uint64_t root2 = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child2 = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text2 = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root2 != UI_INVALID_HANDLE);
    REQUIRE(child2 != UI_INVALID_HANDLE);
    REQUIRE(text2 != UI_INVALID_HANDLE);
    ui_node_add_child(root2, child2);
    ui_node_add_child(child2, text2);
    GetRuntime().Input().state().last_hovered_handle = text2;
    GetRuntime().Selection().state().active_handle = text2;
    CHECK(GetRuntime().RemoveChild(root2, child2));
    CHECK(GetRuntime().Input().state().last_hovered_handle == UI_INVALID_HANDLE);
    CHECK(GetRuntime().Selection().state().active_handle == UI_INVALID_HANDLE);

    auto* text_node = GetRuntime().ResolveMutable(text2);
    REQUIRE(text_node != nullptr);
    text_node->is_text_node = true;
    text_node->is_selectable = true;
    text_node->is_dirty = false;
    GetRuntime().SetFocus(text2);
    GetRuntime().SetFocus(effindom::v2::ui::PackHandle(999U, 1U));
    CHECK(GetRuntime().Focus().FocusedHandle() == UI_INVALID_HANDLE);
    CHECK(text_node->is_dirty);

    GetRuntime().HandleKeyEvent(UI_KEY_EVENT_UP, reinterpret_cast<const std::uint8_t*>("a"), 1U, 0U);
    GetRuntime().HandleKeyEvent(UI_KEY_EVENT_DOWN, nullptr, 1U, 0U);
}

TEST_CASE("v2 ui private text runtime helpers cover edge branches", "[v2][ui][unit][text]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    CHECK_FALSE(GetRuntime().RegisterFont(0U, nullptr, 0U));

    effindom::v2::ui::UiRuntime::RegisteredFont font{};
    GetRuntime().DestroyRegisteredFont(font);

    effindom::v2::ui::UINode node{};
    CHECK(GetRuntime().LayoutParagraph(node, std::nullopt).height == Approx(0.0f));
    CHECK(GetRuntime().MeasureTextNode(node, 10.0f, YGMeasureModeUndefined).height == Approx(0.0f));

    const auto candidates = GetRuntime().ComputeBreakCandidates("line one\nline two");
    CHECK(candidates.size() >= 3U);

}

TEST_CASE("v2 ui private keyboard selection helpers cover remaining branches", "[v2][ui][unit][editing]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();

    const auto text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>("alpha beta gamma"), 16U);
    ui_set_selectable(text, true, 0x40007AFFU);

    auto* node = GetRuntime().ResolveMutable(text);
    REQUIRE(node != nullptr);
    node->is_text_node = true;
    node->semantic_role = UI_SEMANTIC_TEXTBOX;
    node->break_offsets = {0, 6, 16};
    node->line_widths = {40.0f, 60.0f};
    node->line_height = 14.0f;
    node->visible_line_count = 2U;
    node->total_line_count = 2U;
    node->selection_start = 6U;
    node->selection_end = 10U;

    GetRuntime().SetFocus(text);
    GetRuntime().HandleKeyEvent(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowLeft"), 9U, UI_KEY_MOD_CTRL);
    CHECK(node->selection_start == 6U);
    CHECK(node->selection_end == 6U);

    node->selection_start = 8U;
    node->selection_end = 8U;
    GetRuntime().HandleKeyEvent(UI_KEY_EVENT_DOWN, reinterpret_cast<const std::uint8_t*>("ArrowUp"), 7U, 0U);
    CHECK(node->selection_start == 0U);
    CHECK(node->selection_end == 0U);
}
