#include "TestUiSupport.h"
#include "UiDebugTreeProjector.h"
#include "UiSemanticProjector.h"

namespace {

std::uint32_t HandleLow(std::uint64_t handle) {
    return static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
}

std::uint32_t HandleHigh(std::uint64_t handle) {
    return static_cast<std::uint32_t>(handle >> 32U);
}

std::uint64_t g_scroll_layout_mutation_handle = UI_INVALID_HANDLE;
bool g_mutate_layout_on_scroll = false;

void MutateLayoutFromScrollCallback(
    std::uint64_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height) {
    test_ui_support::RecordScrollChange(
        handle,
        offset_x,
        offset_y,
        content_width,
        content_height,
        viewport_width,
        viewport_height);
    if (!g_mutate_layout_on_scroll) return;
    g_mutate_layout_on_scroll = false;
    ui_set_width(g_scroll_layout_mutation_handle, 150.0f, UI_SIZE_UNIT_PIXEL);
}

} // namespace

TEST_CASE("v2 ui frame coordinator emits lifecycle and prepared commands before retained painting", "[v2][ui][commit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t removed = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(removed != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, removed);
    GetRuntime().frame_commit_coordinator_->Commit(-1.0);

    REQUIRE(GetRuntime().DeleteNode(removed));
    const std::uint64_t created = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(created != UI_INVALID_HANDLE);
    auto& runtime = GetRuntime();
    runtime.pending_prepare_commands_ = {
        CMD_SET_TEXT_FADE,
        HandleLow(created),
        HandleHigh(created),
        ED_FADE_NONE,
    };

    ui_commit_frame();
    const auto words = ReadCommandBuffer();
    REQUIRE(words.size() >= 10U);
    CHECK(words[0] == CMD_DELETE_NODE);
    CHECK(words[1] == HandleLow(removed));
    CHECK(words[2] == HandleHigh(removed));
    CHECK(words[3] == CMD_CREATE_NODE);
    CHECK(words[4] == HandleLow(created));
    CHECK(words[5] == HandleHigh(created));
    CHECK(words[6] == CMD_SET_TEXT_FADE);
    CHECK(words[7] == HandleLow(created));
    CHECK(words[8] == HandleHigh(created));
    CHECK(words[9] == ED_FADE_NONE);
}

TEST_CASE("v2 ui frame coordinator records bounded layout stabilization work", "[v2][ui][commit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t child = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(child != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(child, 50.0f, UI_SIZE_UNIT_PERCENT);
    ui_set_height(child, 20.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, child);

    auto& runtime = GetRuntime();
    runtime.ClearTextCommitProfile();
    runtime.frame_commit_coordinator_->Commit(0.0);
    const auto& layout_profile = runtime.last_text_commit_profile();
    CHECK(layout_profile.layout_stabilization_passes >= 1U);
    CHECK(layout_profile.layout_stabilization_passes <= 4U);
    CHECK(layout_profile.yoga_layout_ms >= 0.0);

    runtime.frame_commit_coordinator_->Commit(1000.0 / 60.0);
    const auto& idle_profile = runtime.last_text_commit_profile();
    CHECK(idle_profile.layout_stabilization_passes == 0U);
    CHECK(idle_profile.total_commit_ms >= 0.0);
}

TEST_CASE("v2 ui frame coordinator preserves layout dirtied by a scroll callback", "[v2][ui][commit]") {
    using effindom::v2::ui::GetRuntime;
    using namespace test_ui_support;

    ui_reset();
    UseRecordingInteractionCallbacks();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 100.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, content);

    auto& runtime = GetRuntime();
    g_scroll_layout_mutation_handle = content;
    g_mutate_layout_on_scroll = true;
    g_scroll_change_callback = &MutateLayoutFromScrollCallback;

    runtime.frame_commit_coordinator_->Commit(0.0);
    CHECK_FALSE(g_mutate_layout_on_scroll);
    const auto* content_node = runtime.Resolve(content);
    REQUIRE(content_node != nullptr);
    REQUIRE(content_node->yg_node != nullptr);
    CHECK(YGNodeLayoutGetWidth(content_node->yg_node) == Approx(150.0f));
    CHECK_FALSE(runtime.layout_dirty_);

    UseRecordingInteractionCallbacks();
    g_scroll_layout_mutation_handle = UI_INVALID_HANDLE;
    g_mutate_layout_on_scroll = false;
}

TEST_CASE("v2 ui frame coordinator profiles idle scroll and text-heavy work", "[v2][ui][commit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 60.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 180.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 220.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    constexpr std::string_view kText = "Commit profiling text with enough content to shape and paint.";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kText.data()), static_cast<std::uint32_t>(kText.size()));
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    auto& runtime = GetRuntime();
    runtime.frame_commit_coordinator_->Commit(0.0);
    runtime.frame_commit_coordinator_->Commit(1000.0 / 60.0);
    const auto idle_profile = runtime.last_text_commit_profile();
    CHECK(idle_profile.layout_stabilization_passes == 0U);

    auto* scroll_node = const_cast<effindom::v2::ui::UINode*>(runtime.Resolve(scroll));
    REQUIRE(scroll_node != nullptr);
    scroll_node->scroll_velocity_y = 600.0f;
    runtime.frame_commit_coordinator_->Commit(2.0 * 1000.0 / 60.0);
    const auto scroll_profile = runtime.last_text_commit_profile();
    CHECK(scroll_node->scroll_offset_y > 0.0f);
    CHECK(scroll_profile.total_commit_ms >= 0.0);

    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(kText.data()), static_cast<std::uint32_t>(kText.size()));
    runtime.frame_commit_coordinator_->Commit(3.0 * 1000.0 / 60.0);
    const auto text_profile = runtime.last_text_commit_profile();
    CHECK(text_profile.layout_paragraph_total_ms >= 0.0);
    CHECK(text_profile.total_commit_ms >= 0.0);
}

TEST_CASE("v2 ui measures viewport-bounded Tier 2 glyph traffic by mutation cause", "[v2][ui][commit][text-traffic]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    RegisterMonoTestFont();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(scroll != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(scroll, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(scroll, 120.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 8000.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_line_height(text, 20.0f);
    ui_set_selectable(text, true, 0x007aff40U);
    std::string document{};
    for (std::uint32_t line = 0U; line < 400U; line += 1U) {
        document += "Viewport bounded text traffic line " + std::to_string(line) + "\n";
    }
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(document.data()), static_cast<std::uint32_t>(document.size()));
    ui_node_add_child(root, scroll);
    ui_node_add_child(scroll, text);

    auto& runtime = GetRuntime();
    runtime.CommitFrame(0.0);
    const auto initial = runtime.last_text_commit_profile();
    REQUIRE(initial.glyph_run_commands == 1U);
    CHECK(initial.glyph_run_plain_commands == 1U);
    CHECK(initial.glyphs_emitted > 0U);
    CHECK(initial.glyphs_emitted < document.size() / 10U);
    CHECK(initial.glyph_run_encoded_words == 7U + (initial.glyphs_emitted * 4U));

    runtime.CommitFrame(16.0);
    const auto idle = runtime.last_text_commit_profile();
    CHECK(idle.glyph_run_commands == 0U);
    CHECK(idle.glyphs_emitted == 0U);

    ui_set_text_selection_range(text, 2U, 18U);
    runtime.CommitFrame(32.0);
    const auto selection = runtime.last_text_commit_profile();
    CHECK(selection.glyph_run_commands == 0U);
    CHECK(selection.glyphs_emitted == 0U);

    ui_set_scroll_offset(scroll, 0.0f, 1200.0f);
    runtime.CommitFrame(48.0);
    const auto scrolled = runtime.last_text_commit_profile();
    CHECK(scrolled.glyph_run_commands == 1U);
    CHECK(scrolled.glyphs_emitted > 0U);
    CHECK(scrolled.glyphs_emitted < document.size() / 10U);

    document += "edited\n";
    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(document.data()), static_cast<std::uint32_t>(document.size()));
    runtime.CommitFrame(64.0);
    const auto edited = runtime.last_text_commit_profile();
    CHECK(edited.glyph_run_commands == 1U);

    ui_set_text(text, reinterpret_cast<const std::uint8_t*>(document.data()), static_cast<std::uint32_t>(document.size()));
    runtime.CommitFrame(80.0);
    const auto identical = runtime.last_text_commit_profile();
    CHECK(identical.glyph_run_commands == 0U);
    CHECK(identical.glyphs_emitted == 0U);

    ui_set_text_color(text, 0x3355aaffU);
    runtime.CommitFrame(96.0);
    const auto recolored = runtime.last_text_commit_profile();
    CHECK(recolored.glyph_run_commands == 1U);
    CHECK(recolored.glyph_run_plain_commands == 1U);

    ui_set_font(text, 5U, 16.0f);
    runtime.CommitFrame(112.0);
    const auto refonted = runtime.last_text_commit_profile();
    CHECK(refonted.glyph_run_commands == 1U);

    INFO("initial glyphs=" << initial.glyphs_emitted << " words=" << initial.glyph_run_encoded_words);
    INFO("scrolled glyphs=" << scrolled.glyphs_emitted << " words=" << scrolled.glyph_run_encoded_words);
    INFO("edited glyphs=" << edited.glyphs_emitted << " words=" << edited.glyph_run_encoded_words);
    INFO("identical glyphs=" << identical.glyphs_emitted << " words=" << identical.glyph_run_encoded_words);
    INFO("initial commit ms=" << initial.total_commit_ms);
    CHECK(initial.total_commit_ms >= 0.0);
    INFO("scroll commit ms=" << scrolled.total_commit_ms);
    CHECK(scrolled.total_commit_ms >= 0.0);
    INFO("edit commit ms=" << edited.total_commit_ms);
    CHECK(edited.total_commit_ms >= 0.0);
    INFO("identical replacement commit ms=" << identical.total_commit_ms);
    CHECK(identical.total_commit_ms >= 0.0);
}

TEST_CASE("v2 ui identical text mutation preserves retained state and editor hydration callbacks", "[v2][ui][commit][text-traffic]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    UseRecordingInteractionCallbacks();
    const std::uint64_t root = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t text = ui_create_node(UI_NODE_TEXT);
    REQUIRE(root != UI_INVALID_HANDLE);
    REQUIRE(text != UI_INVALID_HANDLE);
    ui_set_root(root);
    ui_set_width(root, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(root, 80.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(text, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(text, 40.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(text, 1U, 16.0f);
    ui_set_editable(text, true);
    static constexpr std::string_view kInitial = "identical retained text";
    ui_set_text(
        text,
        reinterpret_cast<const std::uint8_t*>(kInitial.data()),
        static_cast<std::uint32_t>(kInitial.size()));
    ui_node_add_child(root, text);

    auto& runtime = GetRuntime();
    runtime.CommitFrame(0.0);
    REQUIRE(runtime.SetTextSelectionRange(text, 3U, 9U));
    runtime.CommitFrame(16.0);
    auto* node = runtime.ResolveMutable(text);
    REQUIRE(node != nullptr);
    const bool layout_cache_valid = node->text_layout_cache_valid;
    const bool logical_shape_cache_valid = node->logical_line_shape_cache_valid;
    const std::size_t line_start_count = node->text_line_starts.size();
    CHECK_FALSE(runtime.layout_dirty_);
    CHECK_FALSE(node->is_dirty);
    ResetInteractionLogs();

    REQUIRE(runtime.SetText(
        text,
        reinterpret_cast<const std::uint8_t*>(kInitial.data()),
        static_cast<std::uint32_t>(kInitial.size())));
    CHECK_FALSE(runtime.layout_dirty_);
    CHECK_FALSE(node->is_dirty);
    CHECK(node->text_layout_cache_valid == layout_cache_valid);
    CHECK(node->logical_line_shape_cache_valid == logical_shape_cache_valid);
    CHECK(node->text_line_starts.size() == line_start_count);
    CHECK(node->selection_start == 3U);
    CHECK(node->selection_end == 9U);
    REQUIRE(g_text_changes.size() == 1U);
    CHECK(g_text_changes.front().handle == text);
    CHECK(g_text_changes.front().text == kInitial);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes.front().handle == text);
    CHECK(g_selection_changes.front().start == 3U);
    CHECK(g_selection_changes.front().end == 9U);

    runtime.CommitFrame(32.0);
    const auto identical = runtime.last_text_commit_profile();
    CHECK(identical.glyph_run_commands == 0U);
    CHECK(identical.glyphs_emitted == 0U);

    ResetInteractionLogs();
    static constexpr std::string_view kChanged = "distinct retained text";
    REQUIRE(runtime.SetText(
        text,
        reinterpret_cast<const std::uint8_t*>(kChanged.data()),
        static_cast<std::uint32_t>(kChanged.size())));
    CHECK(runtime.layout_dirty_);
    CHECK(node->is_dirty);
    REQUIRE(g_text_changes.size() == 1U);
    CHECK(g_text_changes.front().handle == text);
    CHECK(g_text_changes.front().text == kChanged);
    REQUIRE(g_selection_changes.size() == 1U);
    CHECK(g_selection_changes.front().handle == text);
}

TEST_CASE("v2 ui retains multiline textbox work across ancestor viewport re-entry", "[v2][ui][commit][text-traffic]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    RegisterTestFont();
    const std::uint64_t outer_scroll = ui_create_node(UI_NODE_SCROLLVIEW);
    const std::uint64_t content = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t spacer = ui_create_node(UI_NODE_FLEX_BOX);
    const std::uint64_t textbox = ui_create_node(UI_NODE_TEXT);
    REQUIRE(outer_scroll != UI_INVALID_HANDLE);
    REQUIRE(content != UI_INVALID_HANDLE);
    REQUIRE(spacer != UI_INVALID_HANDLE);
    REQUIRE(textbox != UI_INVALID_HANDLE);

    ui_set_root(outer_scroll);
    ui_set_width(outer_scroll, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(outer_scroll, 240.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(content, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(content, 1600.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_flex_direction(content, UI_FLEX_DIRECTION_COLUMN);
    ui_set_width(spacer, 320.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(spacer, 700.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_width(textbox, 300.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_height(textbox, 200.0f, UI_SIZE_UNIT_PIXEL);
    ui_set_font(textbox, 1U, 16.0f);
    ui_set_line_height(textbox, 20.0f);
    ui_set_semantic_role(textbox, UI_SEMANTIC_TEXTBOX);
    ui_set_text_limits(textbox, 1000000, 0);
    ui_set_text_wrapping(textbox, true);

    std::string document{};
    for (std::uint32_t word = 0U; word < 1000U; word += 1U) {
        document += "large-retained-wrapped-textbox-word-" + std::to_string(word) + " ";
    }
    ui_set_text(
        textbox,
        reinterpret_cast<const std::uint8_t*>(document.data()),
        static_cast<std::uint32_t>(document.size()));
    ui_node_add_child(outer_scroll, content);
    ui_node_add_child(content, spacer);
    ui_node_add_child(content, textbox);

    auto& runtime = GetRuntime();
    runtime.CommitFrame(0.0);
    const auto offscreen = runtime.last_text_commit_profile();
    CHECK(offscreen.glyph_run_commands == 0U);
    CHECK(offscreen.layout_paragraph_calls == 0U);
    CHECK(offscreen.shape_text_calls == 0U);

    ui_set_scroll_offset(outer_scroll, 0.0f, 650.0f);
    runtime.CommitFrame(16.0);
    const auto first_entry = runtime.last_text_commit_profile();
    CHECK(first_entry.glyph_run_commands == 1U);
    CHECK(first_entry.glyphs_emitted > 0U);
    CHECK(first_entry.glyphs_emitted < document.size() / 20U);
    CHECK(first_entry.layout_paragraph_calls > 0U);
    const auto* retained_textbox = runtime.Resolve(textbox);
    REQUIRE(retained_textbox != nullptr);
    const auto cached_layout = runtime.LayoutParagraph(*retained_textbox, 300.0f);
    CHECK(cached_layout.break_offsets.data() == retained_textbox->break_offsets.data());
    CHECK(cached_layout.line_widths.data() == retained_textbox->line_widths.data());
    CHECK(cached_layout.line_heights.data() == retained_textbox->line_heights.data());
    CHECK(cached_layout.line_ascents.data() == retained_textbox->line_ascents.data());
    CHECK(cached_layout.line_y_offsets.data() == retained_textbox->line_y_offsets.data());

    ui_set_scroll_offset(outer_scroll, 0.0f, 0.0f);
    runtime.CommitFrame(32.0);
    const auto exit = runtime.last_text_commit_profile();
    CHECK(exit.glyph_run_commands == 0U);

    ui_set_scroll_offset(outer_scroll, 0.0f, 650.0f);
    runtime.CommitFrame(48.0);
    const auto reentry = runtime.last_text_commit_profile();
    CHECK(reentry.glyph_run_commands == 1U);
    CHECK(reentry.glyphs_emitted == first_entry.glyphs_emitted);

    INFO("offscreen commit ms=" << offscreen.total_commit_ms);
    CHECK(offscreen.total_commit_ms >= 0.0);
    INFO("first ancestor viewport entry ms=" << first_entry.total_commit_ms);
    CHECK(first_entry.total_commit_ms >= 0.0);
    INFO("ancestor viewport exit ms=" << exit.total_commit_ms);
    CHECK(exit.total_commit_ms >= 0.0);
    INFO("ancestor viewport re-entry ms=" << reentry.total_commit_ms);
    CHECK(reentry.total_commit_ms >= 0.0);
    INFO("first entry logical line shape builds=" << first_entry.logical_line_shape_cache_builds);
    CHECK(first_entry.logical_line_shape_cache_builds <= 1U);
    INFO("re-entry logical line shape builds=" << reentry.logical_line_shape_cache_builds);
    CHECK(reentry.logical_line_shape_cache_builds == 0U);
}

TEST_CASE("v2 ui frame coordinator commits pending lifecycle commands for an invalid root", "[v2][ui][commit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    const std::uint64_t created = ui_create_node(UI_NODE_FLEX_BOX);
    REQUIRE(created != UI_INVALID_HANDLE);

    auto& runtime = GetRuntime();
    runtime.frame_commit_coordinator_->Commit(0.0);

    const auto words = ReadCommandBuffer();
    REQUIRE(words.size() == 7U);
    CHECK(words[0] == CMD_CREATE_NODE);
    CHECK(words[1] == HandleLow(created));
    CHECK(words[2] == HandleHigh(created));
    CHECK(words[3] == CMD_COMMIT_PAINT_ORDER);
    CHECK(words[4] == 0U);
    CHECK(words[5] == CMD_COMMIT_SCENE);
    CHECK(words[6] == 0U);
    CHECK_FALSE(runtime.layout_dirty_);
    CHECK(runtime.last_text_commit_profile().total_commit_ms >= 0.0);
}

TEST_CASE("v2 ui frame coordinator clears transient projections for an empty invalid-root commit", "[v2][ui][commit]") {
    using effindom::v2::ui::GetRuntime;

    ui_reset();
    auto& runtime = GetRuntime();
    runtime.semantic_buffer_ = {1U, 2U, 3U};
    runtime.debug_tree_buffer_ = {4U, 5U, 6U};
    runtime.pending_text_scroll_metric_handles_.insert(123U);

    std::vector<std::uint32_t> cleared_semantics{};
    std::vector<std::uint32_t> cleared_debug_tree{};
    effindom::v2::ui::SemanticProjector::ClearOutput(cleared_semantics);
    effindom::v2::ui::DebugTreeProjector::ClearOutput(cleared_debug_tree);

    runtime.frame_commit_coordinator_->Commit(0.0);

    CHECK(runtime.command_buffer_.empty());
    CHECK(runtime.semantic_buffer_ == cleared_semantics);
    CHECK(runtime.debug_tree_buffer_ == cleared_debug_tree);
    CHECK(runtime.pending_text_scroll_metric_handles_.empty());
    CHECK_FALSE(runtime.layout_dirty_);
    CHECK(runtime.last_text_commit_profile().total_commit_ms >= 0.0);
}
