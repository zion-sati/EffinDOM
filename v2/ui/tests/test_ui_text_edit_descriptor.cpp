#include "UiTextEdit.h"
#include "TestUiSupport.h"

#include <catch2/catch_test_macros.hpp>

using effindom::v2::ui::PreviousTextView;
using effindom::v2::ui::TextEdit;

TEST_CASE("v2 text edit descriptor captures only removed UTF-8 bytes", "[v2][ui][text-edit]") {
    const std::string old_text = "prefix-\xE4\xB8\x96\xE7\x95\x8C-suffix";
    const auto edit = TextEdit::Create(old_text, 7U, 13U, "world");
    REQUIRE(edit.has_value());
    CHECK(edit->start == 7U);
    CHECK(edit->removed_end == 13U);
    CHECK(edit->removed_text == "\xE4\xB8\x96\xE7\x95\x8C");
    CHECK(edit->inserted_text == "world");
    CHECK(edit->byte_delta() == -1);
}

TEST_CASE("v2 previous text view reconstructs old ranges without an old document copy", "[v2][ui][text-edit]") {
    const std::string old_text = "alpha beta gamma";
    const auto edit = TextEdit::Create(old_text, 6U, 10U, "B");
    REQUIRE(edit.has_value());
    std::string edited_text = old_text;
    edited_text.replace(edit->start, edit->removed_end - edit->start, edit->inserted_text);
    const PreviousTextView previous(edited_text, *edit);
    CHECK(previous.size() == old_text.size());
    CHECK(previous.materialize(0U, previous.size()) == old_text);
    CHECK(previous.materialize(4U, 12U) == old_text.substr(4U, 8U));
}

TEST_CASE("v2 previous text view reconstructs every bounded range around an edit", "[v2][ui][text-edit]") {
    const std::string old_text = "zero-\xE4\xB8\x96\xE7\x95\x8C-middle-\xF0\x9F\x8C\x8D-end";
    const std::uint32_t start = 5U;
    const std::uint32_t end = 11U;
    const auto edit = TextEdit::Create(old_text, start, end, "replacement");
    REQUIRE(edit.has_value());
    std::string edited_text = old_text;
    edited_text.replace(edit->start, edit->removed_end - edit->start, edit->inserted_text);
    const PreviousTextView previous(edited_text, *edit);

    for (std::size_t range_start = 0U; range_start <= old_text.size(); range_start += 1U) {
        for (std::size_t range_end = range_start; range_end <= old_text.size(); range_end += 1U) {
            CHECK(previous.materialize(range_start, range_end) ==
                  old_text.substr(range_start, range_end - range_start));
        }
    }
}

TEST_CASE("v2 text edit descriptor rejects split or malformed UTF-8", "[v2][ui][text-edit]") {
    const std::string old_text = "a\xF0\x9F\x8C\x8D" "b";
    CHECK_FALSE(TextEdit::Create(old_text, 2U, 5U, "x").has_value());
    CHECK_FALSE(TextEdit::Create(old_text, 1U, 4U, "x").has_value());
    CHECK_FALSE(TextEdit::Create(old_text, 1U, 5U, "\xC0\xAF").has_value());
    CHECK(TextEdit::Create(old_text, 1U, 5U, "\xE4\xB8\x96").has_value());
}

TEST_CASE("v2 full replacement fallback expands byte diffs to UTF-8 boundaries", "[v2][ui][text-edit]") {
    const std::string old_text = "A\xF0\x9F\x8C\x8D" "B";
    const std::string new_text = "A\xF0\x9F\x8C\x8E" "B";
    const auto edit = effindom::v2::ui::CreateTextEditForFullReplacement(old_text, new_text);
    REQUIRE(edit.has_value());
    CHECK(edit->start == 1U);
    CHECK(edit->removed_end == 5U);
    CHECK(edit->removed_text == "\xF0\x9F\x8C\x8D");
    CHECK(edit->inserted_text == "\xF0\x9F\x8C\x8E");
}

TEST_CASE("v2 exact text edits splice hard-line starts across newline and CRLF changes", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::UINode;

    const auto verify = [](std::string old_text, std::uint32_t start, std::uint32_t end, std::string_view inserted) {
        UINode node{};
        node.is_text_node = true;
        node.text_content = old_text;
        GetRuntime().RebuildTextLineStarts(node);
        const auto edit = TextEdit::Create(old_text, start, end, inserted);
        REQUIRE(edit.has_value());
        node.text_content.replace(start, end - start, inserted);
        REQUIRE(GetRuntime().TryApplyIncrementalTextLineStarts(node, *edit));

        UINode fresh{};
        fresh.is_text_node = true;
        fresh.text_content = node.text_content;
        GetRuntime().RebuildTextLineStarts(fresh);
        CHECK(node.text_line_starts == fresh.text_line_starts);
    };

    verify("alpha\nbeta\ngamma", 2U, 2U, "!");
    verify("alpha\nbeta\ngamma", 5U, 5U, "\n");
    verify("alpha\nbeta\ngamma", 5U, 6U, "");
    verify("alpha\r\nbeta\r\ngamma", 5U, 7U, "\n");
    verify("alpha\r\nbeta\r\ngamma", 12U, 12U, "\r\nnew");
}

TEST_CASE("v2 ordinary exact edits do not compare or materialize the complete document", "[v2][ui][text-edit]") {
    using effindom::v2::ui::GetRuntime;
    using effindom::v2::ui::UINode;

    UINode node{};
    node.is_text_node = true;
    node.is_text_editor = true;
    node.is_editable = true;
    node.text_content = std::string(8000U, 'a') + "\n" + std::string(8000U, 'b');
    node.selection_start = 4000U;
    node.selection_end = 4000U;
    GetRuntime().RebuildTextLineStarts(node);
    const auto edit = TextEdit::Create(node.text_content, 4000U, 4000U, "!");
    REQUIRE(edit.has_value());

    GetRuntime().ClearTextCommitProfile();
    REQUIRE(GetRuntime().ApplyTextEdit(1U, node, *edit, 4001U, 4001U));
    GetRuntime().CommitFrame();
    const auto& profile = GetRuntime().last_text_commit_profile();
    CHECK(profile.exact_text_edit_applications == 1U);
    CHECK(profile.exact_text_edit_inserted_bytes == 1U);
    CHECK(profile.exact_text_edit_removed_bytes == 0U);
    CHECK(profile.previous_text_materialized_bytes == 0U);
    CHECK(profile.full_text_replacement_fallbacks == 0U);
    CHECK(profile.full_text_replacement_compared_bytes == 0U);
}
