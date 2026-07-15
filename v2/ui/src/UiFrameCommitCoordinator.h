#pragma once

#include "UiLayoutCoordinator.h"
#include "UiNodeStore.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace effindom::v2::ui {

class ScrollCoordinator;
class SelectionCoordinator;
class TreePainter;
class CommandBuilder;

class FrameCommitHost {
public:
    virtual ~FrameCommitHost() = default;

    virtual void ResetCommitFrameArena() = 0;
    virtual void BeginCommitProfile() = 0;
    virtual void FinishCommitProfile(double total_commit_ms) = 0;
    virtual void RecordCommitLayoutProfile(const LayoutResult& result) = 0;
    virtual void RecordCommitPaintProfile(double walk_tree_ms) = 0;
    virtual void UpdateCommitAutoScrollSelection() = 0;
    virtual void ResolveCommitCaretVisibility() = 0;
    virtual void BuildCommitSemantics(const std::vector<std::uint64_t>& paint_order) = 0;
    virtual void BuildCommitDebugTree() = 0;
};

class FrameCommitCoordinator {
public:
    FrameCommitCoordinator(
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
        FrameCommitHost& host);

    void Commit(double timestamp_ms);
    void ResetClock();

private:
    double AdvanceClock(double timestamp_ms);
    void EmitPendingCommands(CommandBuilder& builder);

    NodeStore& nodes_;
    std::vector<std::uint32_t>& command_buffer_;
    std::vector<std::uint32_t>& pending_prepare_commands_;
    std::vector<std::uint32_t>& semantic_buffer_;
    std::vector<std::uint32_t>& debug_tree_buffer_;
    std::unordered_set<std::uint64_t>& pending_text_scroll_metric_handles_;
    float& window_width_;
    float& window_height_;
    bool& layout_dirty_;
    LayoutCoordinator& layout_;
    ScrollCoordinator& scrolling_;
    SelectionCoordinator& selection_;
    TreePainter& painter_;
    FrameCommitHost& host_;
    double last_commit_timestamp_ms_ = 0.0;
    bool has_last_commit_timestamp_ = false;
};

} // namespace effindom::v2::ui
