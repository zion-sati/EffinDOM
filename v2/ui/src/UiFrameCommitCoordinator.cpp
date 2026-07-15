#include "UiFrameCommitCoordinator.h"

#include "CommandBuilder.h"
#include "UiDebugTreeProjector.h"
#include "UiScrollCoordinator.h"
#include "UiSelectionCoordinator.h"
#include "UiSemanticProjector.h"
#include "UiTreePainter.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace effindom::v2::ui {

namespace {

using ProfileClock = std::chrono::steady_clock;
constexpr double kNominalFrameMs = 1000.0 / 60.0;
constexpr double kMaxFrameDeltaMs = 100.0;

double ElapsedMilliseconds(ProfileClock::time_point start, ProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool IsValidTimestamp(double timestamp_ms) {
    return std::isfinite(timestamp_ms) && timestamp_ms >= 0.0;
}

double ClampFrameDeltaMs(double delta_ms) {
    if (!std::isfinite(delta_ms) || delta_ms < 0.0) {
        return 0.0;
    }
    return std::min(delta_ms, kMaxFrameDeltaMs);
}

} // namespace

FrameCommitCoordinator::FrameCommitCoordinator(
    NodeStore& nodes,
    std::vector<std::uint32_t>& command_buffer,
    std::vector<std::uint32_t>& pending_prepare_commands,
    std::vector<std::uint32_t>& semantic_buffer,
    std::vector<std::uint32_t>& debug_tree_buffer,
    std::unordered_set<std::uint64_t>& pending_text_scroll_metric_handles,
    float& window_width,
    float& window_height,
    bool& layout_dirty,
    LayoutCoordinator& layout,
    ScrollCoordinator& scrolling,
    SelectionCoordinator& selection,
    TreePainter& painter,
    FrameCommitHost& host)
    : nodes_(nodes),
      command_buffer_(command_buffer),
      pending_prepare_commands_(pending_prepare_commands),
      semantic_buffer_(semantic_buffer),
      debug_tree_buffer_(debug_tree_buffer),
      pending_text_scroll_metric_handles_(pending_text_scroll_metric_handles),
      window_width_(window_width),
      window_height_(window_height),
      layout_dirty_(layout_dirty),
      layout_(layout),
      scrolling_(scrolling),
      selection_(selection),
      painter_(painter),
      host_(host) {}

void FrameCommitCoordinator::ResetClock() {
    has_last_commit_timestamp_ = false;
    last_commit_timestamp_ms_ = 0.0;
}

double FrameCommitCoordinator::AdvanceClock(double timestamp_ms) {
    double scroll_delta_ms = kNominalFrameMs;
    if (IsValidTimestamp(timestamp_ms)) {
        if (has_last_commit_timestamp_) {
            scroll_delta_ms = ClampFrameDeltaMs(timestamp_ms - last_commit_timestamp_ms_);
        }
        last_commit_timestamp_ms_ = timestamp_ms;
        has_last_commit_timestamp_ = true;
    } else if (has_last_commit_timestamp_) {
        last_commit_timestamp_ms_ += kNominalFrameMs;
    }
    return scroll_delta_ms;
}

void FrameCommitCoordinator::EmitPendingCommands(CommandBuilder& builder) {
    nodes_.EmitLifecycleCommands(builder);
    if (!pending_prepare_commands_.empty()) {
        command_buffer_.insert(
            command_buffer_.end(),
            pending_prepare_commands_.begin(),
            pending_prepare_commands_.end());
        pending_prepare_commands_.clear();
    }
}

void FrameCommitCoordinator::Commit(double timestamp_ms) {
    const ProfileClock::time_point commit_start = ProfileClock::now();
    const double scroll_delta_ms = AdvanceClock(timestamp_ms);
    host_.BeginCommitProfile();
    command_buffer_.clear();
    host_.ResetCommitFrameArena();
    SemanticProjector::ClearOutput(semantic_buffer_);
    DebugTreeProjector::ClearOutput(debug_tree_buffer_);

    CommandBuilder builder(command_buffer_);
    EmitPendingCommands(builder);

    UINode* root = nodes_.ResolveMutable(nodes_.RootHandle());
    if (root == nullptr || root->yg_node == nullptr) {
        if (!command_buffer_.empty()) {
            builder.CommitPaintOrder({});
            builder.CommitScene({});
        }
        layout_dirty_ = false;
        pending_text_scroll_metric_handles_.clear();
        host_.FinishCommitProfile(ElapsedMilliseconds(commit_start, ProfileClock::now()));
        return;
    }

    const LayoutResult layout_result = layout_.Update(
        nodes_.RootHandle(), window_width_, window_height_, layout_dirty_);
    host_.RecordCommitLayoutProfile(layout_result);

    scrolling_.AdvanceAutoScroll();
    scrolling_.Advance(scroll_delta_ms);
    host_.UpdateCommitAutoScrollSelection();
    host_.ResolveCommitCaretVisibility();
    selection_.ClearHitRects();

    const bool needs_follow_up_layout = layout_dirty_;
    layout_dirty_ = layout_result.emitted_layout_updates;

    const ProfileClock::time_point paint_start = ProfileClock::now();
    const PaintFrameOutput painted = painter_.Paint(nodes_.RootHandle(), builder);
    host_.RecordCommitPaintProfile(ElapsedMilliseconds(paint_start, ProfileClock::now()));

    host_.BuildCommitSemantics(painted.paint_order);
    host_.BuildCommitDebugTree();
    builder.CommitPaintOrder(painted.paint_order);
    builder.CommitScene(painted.scene);
    layout_dirty_ = needs_follow_up_layout;
    host_.FinishCommitProfile(ElapsedMilliseconds(commit_start, ProfileClock::now()));
}

} // namespace effindom::v2::ui
