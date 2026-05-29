#include "UiRuntime.h"

#include "CommandBuilder.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace effindom::v2::ui {

namespace {

struct GridStyleSnapshot {
    YGValue width{};
    YGValue height{};
};

GridStyleSnapshot CaptureGridStyle(YGNodeRef node) {
    return GridStyleSnapshot{
        YGNodeStyleGetWidth(node),
        YGNodeStyleGetHeight(node),
    };
}

void RestoreGridDimension(YGNodeRef node, bool is_width, YGValue value) {
    switch (value.unit) {
    case YGUnitPoint:
        if (is_width) {
            YGNodeStyleSetWidth(node, value.value);
        } else {
            YGNodeStyleSetHeight(node, value.value);
        }
        break;
    case YGUnitPercent:
        if (is_width) {
            YGNodeStyleSetWidthPercent(node, value.value);
        } else {
            YGNodeStyleSetHeightPercent(node, value.value);
        }
        break;
    case YGUnitAuto:
        if (is_width) {
            YGNodeStyleSetWidthAuto(node);
        } else {
            YGNodeStyleSetHeightAuto(node);
        }
        break;
    case YGUnitUndefined:
    default:
        if (is_width) {
            YGNodeStyleSetWidth(node, YGUndefined);
        } else {
            YGNodeStyleSetHeight(node, YGUndefined);
        }
        break;
    }
}

void RestoreGridStyle(YGNodeRef node, const GridStyleSnapshot& snapshot) {
    RestoreGridDimension(node, true, snapshot.width);
    RestoreGridDimension(node, false, snapshot.height);
}

void MarkGridChildDirty(UINode& node) {
    if (node.is_text_node && node.yg_node != nullptr) {
        YGNodeMarkDirty(node.yg_node);
    }
}

std::size_t ResolveGridTrackCount(const UINode& node, bool columns) {
    std::size_t track_count = columns ? node.column_types.size() : node.row_types.size();
    for (const std::uint64_t child_handle : node.children) {
        const UINode* child = GetRuntime().Resolve(child_handle);
        if (child == nullptr) {
            continue;
        }
        const std::uint32_t start = columns ? child->grid_col : child->grid_row;
        const std::uint32_t span = columns ? child->grid_col_span : child->grid_row_span;
        track_count = std::max(track_count, static_cast<std::size_t>(start + std::max(span, 1U)));
    }
    return std::max<std::size_t>(track_count, 1U);
}

void BuildTrackDefinitions(
    const std::vector<float>& source_values,
    const std::vector<std::uint8_t>& source_types,
    std::size_t track_count,
    std::vector<float>& out_values,
    std::vector<std::uint8_t>& out_types) {
    out_values.assign(track_count, 1.0f);
    out_types.assign(track_count, UI_GRID_UNIT_STAR);
    for (std::size_t index = 0; index < track_count && index < source_types.size(); index += 1U) {
        out_types[index] = source_types[index];
        out_values[index] = index < source_values.size() ? source_values[index] : 0.0f;
    }
}

void BuildSharedSizeGroupDefinitions(
    const std::vector<std::string>& source_groups,
    std::size_t track_count,
    std::vector<std::string>& out_groups) {
    out_groups.assign(track_count, std::string{});
    for (std::size_t index = 0; index < track_count && index < source_groups.size(); index += 1U) {
        out_groups[index] = source_groups[index];
    }
}

std::vector<std::uint8_t> ResolveEffectiveTrackTypes(
    const std::vector<std::uint8_t>& track_types,
    const std::vector<std::string>& shared_groups) {
    std::vector<std::uint8_t> effective = track_types;
    for (std::size_t index = 0; index < effective.size() && index < shared_groups.size(); index += 1U) {
        if (!shared_groups[index].empty() && effective[index] == UI_GRID_UNIT_STAR) {
            effective[index] = UI_GRID_UNIT_AUTO;
        }
    }
    return effective;
}

bool HasSharedSizeGroups(const std::vector<std::string>& groups) {
    return std::any_of(groups.begin(), groups.end(), [](const std::string& value) {
        return !value.empty();
    });
}

float SumTracks(const std::vector<float>& tracks, std::size_t start, std::size_t count) {
    float total = 0.0f;
    for (std::size_t index = start; index < std::min(tracks.size(), start + count); index += 1U) {
        total += tracks[index];
    }
    return total;
}

std::pair<std::size_t, std::size_t> ResolvePlacement(
    const UINode& child,
    std::size_t track_count,
    bool columns) {
    const std::size_t start = std::min<std::size_t>(columns ? child.grid_col : child.grid_row, track_count - 1U);
    const std::size_t span = std::max<std::size_t>(
        1U,
        std::min<std::size_t>(columns ? child.grid_col_span : child.grid_row_span, track_count - start));
    return {start, span};
}

void DistributeStarTracks(
    std::vector<float>& tracks,
    const std::vector<std::uint8_t>& track_types,
    const std::vector<float>& track_values,
    float available_space) {
    float star_weight = 0.0f;
    for (std::size_t index = 0; index < tracks.size(); index += 1U) {
        if (track_types[index] == UI_GRID_UNIT_STAR && track_values[index] > 0.0f) {
            star_weight += track_values[index];
        }
    }
    if (star_weight <= 0.0f || available_space <= 0.0f) {
        for (std::size_t index = 0; index < tracks.size(); index += 1U) {
            if (track_types[index] == UI_GRID_UNIT_STAR) {
                tracks[index] = 0.0f;
            }
        }
        return;
    }
    for (std::size_t index = 0; index < tracks.size(); index += 1U) {
        if (track_types[index] == UI_GRID_UNIT_STAR) {
            tracks[index] = available_space * (track_values[index] / star_weight);
        }
    }
}

void SetTrackBaseSizes(
    std::vector<float>& tracks,
    const std::vector<std::uint8_t>& track_types,
    const std::vector<float>& track_values) {
    tracks.assign(track_types.size(), 0.0f);
    for (std::size_t index = 0; index < track_types.size(); index += 1U) {
        if (track_types[index] == UI_GRID_UNIT_PIXEL) {
            tracks[index] = std::max(0.0f, track_values[index]);
        }
    }
}

void MarkAutoTracksFromMeasures(
    std::vector<float>& tracks,
    const std::vector<std::uint8_t>& track_types,
    const UINode& grid,
    bool columns,
    bool use_measured_sizes) {
    for (std::size_t child_index = 0; child_index < grid.children.size() && child_index < grid.cell_measures.size(); child_index += 1U) {
        const UINode* child = GetRuntime().Resolve(grid.children[child_index]);
        if (child == nullptr) {
            continue;
        }
        const auto [start, span] = ResolvePlacement(*child, tracks.size(), columns);
        const float contribution = use_measured_sizes
            ? (columns ? grid.cell_measures[child_index].measured_width : grid.cell_measures[child_index].measured_height)
            : (columns ? grid.cell_measures[child_index].natural_width : grid.cell_measures[child_index].natural_height);
        if (span == 1U) {
            if (track_types[start] == UI_GRID_UNIT_AUTO) {
                tracks[start] = std::max(tracks[start], contribution);
            }
            continue;
        }
        const float distributed = contribution / static_cast<float>(span);
        for (std::size_t track_index = start; track_index < start + span; track_index += 1U) {
            if (track_types[track_index] == UI_GRID_UNIT_AUTO) {
                tracks[track_index] = std::max(tracks[track_index], distributed);
            }
        }
    }
}

GridStyleSnapshot ApplyGridMeasurementStyle(
    UINode& child,
    std::optional<float> width,
    std::optional<float> height) {
    GridStyleSnapshot snapshot = CaptureGridStyle(child.yg_node);
    if (width.has_value()) {
        YGNodeStyleSetWidth(child.yg_node, *width);
    } else {
        YGNodeStyleSetWidthAuto(child.yg_node);
    }
    if (height.has_value()) {
        YGNodeStyleSetHeight(child.yg_node, *height);
    } else {
        YGNodeStyleSetHeightAuto(child.yg_node);
    }
    MarkGridChildDirty(child);
    return snapshot;
}

float ResolveGridContentExtent(const UINode& grid, bool horizontal) {
    if (grid.yg_node == nullptr) {
        return 0.0f;
    }
    const float size = horizontal ? YGNodeLayoutGetWidth(grid.yg_node) : YGNodeLayoutGetHeight(grid.yg_node);
    const YGEdge leading = horizontal ? YGEdgeLeft : YGEdgeTop;
    const YGEdge trailing = horizontal ? YGEdgeRight : YGEdgeBottom;
    const float padding =
        YGNodeLayoutGetPadding(grid.yg_node, leading) +
        YGNodeLayoutGetPadding(grid.yg_node, trailing);
    const float border =
        YGNodeLayoutGetBorder(grid.yg_node, leading) +
        YGNodeLayoutGetBorder(grid.yg_node, trailing);
    return std::max(0.0f, size - padding - border);
}

void EnsureGridNaturalMeasures(UINode& grid) {
    grid.cell_measures.assign(grid.children.size(), UINode::CellMeasure{});
    for (std::size_t child_index = 0; child_index < grid.children.size(); child_index += 1U) {
        const std::uint64_t child_handle = grid.children[child_index];
        UINode* child = const_cast<UINode*>(GetRuntime().Resolve(child_handle));
        if (child == nullptr || child->yg_node == nullptr) {
            continue;
        }
        const GridStyleSnapshot snapshot = ApplyGridMeasurementStyle(*child, std::nullopt, std::nullopt);
        YGNodeCalculateLayout(
            child->yg_node,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            YGDirectionLTR);
        grid.cell_measures[child_index].natural_width = YGNodeLayoutGetWidth(child->yg_node);
        grid.cell_measures[child_index].natural_height = YGNodeLayoutGetHeight(child->yg_node);
        RestoreGridStyle(child->yg_node, snapshot);
        MarkGridChildDirty(*child);
    }
}

void EnsureGridMeasuredSizesForColumns(UINode& grid, const std::vector<float>& column_widths) {
    if (grid.cell_measures.size() != grid.children.size()) {
        EnsureGridNaturalMeasures(grid);
    }
    for (std::size_t child_index = 0; child_index < grid.children.size(); child_index += 1U) {
        const std::uint64_t child_handle = grid.children[child_index];
        UINode* child = const_cast<UINode*>(GetRuntime().Resolve(child_handle));
        if (child == nullptr || child->yg_node == nullptr) {
            continue;
        }
        const auto [column_start, column_span] = ResolvePlacement(*child, column_widths.size(), true);
        const float constrained_width = SumTracks(column_widths, column_start, column_span);
        const GridStyleSnapshot snapshot = ApplyGridMeasurementStyle(*child, constrained_width, std::nullopt);
        YGNodeCalculateLayout(
            child->yg_node,
            constrained_width,
            std::numeric_limits<float>::quiet_NaN(),
            YGDirectionLTR);
        grid.cell_measures[child_index].measured_width = YGNodeLayoutGetWidth(child->yg_node);
        grid.cell_measures[child_index].measured_height = YGNodeLayoutGetHeight(child->yg_node);
        RestoreGridStyle(child->yg_node, snapshot);
        MarkGridChildDirty(*child);
    }
}

struct SharedSizeScopeRef {
    std::uint64_t handle = UI_INVALID_HANDLE;
    const UINode* node = nullptr;
};

SharedSizeScopeRef ResolveSharedSizeScope(const UINode& node, std::uint64_t handle) {
    if (GetRuntime().IsSharedSizeScope(handle)) {
        return {handle, &node};
    }
    for (std::uint64_t current = node.parent_handle; current != UI_INVALID_HANDLE;) {
        const UINode* parent = GetRuntime().Resolve(current);
        if (parent == nullptr) {
            break;
        }
        if (GetRuntime().IsSharedSizeScope(current)) {
            return {current, parent};
        }
        current = parent->parent_handle;
    }
    return {};
}

void ForEachGridInSharedSizeScope(
    std::uint64_t scope_handle,
    const UINode& scope,
    const std::function<void(std::uint64_t, UINode&)>& visitor) {
    if (scope.is_grid) {
        UINode* mutable_scope = const_cast<UINode*>(GetRuntime().Resolve(scope_handle));
        if (mutable_scope != nullptr) {
            visitor(scope_handle, *mutable_scope);
        }
    }
    std::vector<std::uint64_t> stack = scope.children;
    while (!stack.empty()) {
        const std::uint64_t handle = stack.back();
        stack.pop_back();
        UINode* node = const_cast<UINode*>(GetRuntime().Resolve(handle));
        if (node == nullptr) {
            continue;
        }
        if (GetRuntime().IsSharedSizeScope(handle)) {
            continue;
        }
        if (node->is_grid) {
            visitor(handle, *node);
        }
        stack.insert(stack.end(), node->children.begin(), node->children.end());
    }
}

std::vector<float> ResolveSharedColumnWidths(
    std::uint64_t handle,
    UINode& grid,
    float available_width,
    const SharedSizeScopeRef& scope_ref);

void CollectSharedTrackSizes(
    const SharedSizeScopeRef& scope_ref,
    bool columns,
    std::unordered_map<std::string, float>& out_sizes) {
    if (scope_ref.node == nullptr) {
        return;
    }
    ForEachGridInSharedSizeScope(scope_ref.handle, *scope_ref.node, [&](std::uint64_t grid_handle, UINode& grid) {
        EnsureGridNaturalMeasures(grid);
        const std::size_t track_count = ResolveGridTrackCount(grid, columns);
        std::vector<float> track_values{};
        std::vector<std::uint8_t> track_types{};
        std::vector<std::string> shared_groups{};
        BuildTrackDefinitions(
            columns ? grid.column_values : grid.row_values,
            columns ? grid.column_types : grid.row_types,
            track_count,
            track_values,
            track_types);
        BuildSharedSizeGroupDefinitions(
            columns ? GetRuntime().GridColumnSharedSizeGroups(grid_handle) : GetRuntime().GridRowSharedSizeGroups(grid_handle),
            track_count,
            shared_groups);
        if (!HasSharedSizeGroups(shared_groups)) {
            return;
        }
        const std::vector<std::uint8_t> effective_types = ResolveEffectiveTrackTypes(track_types, shared_groups);
        std::vector<float> tracks{};
        SetTrackBaseSizes(tracks, effective_types, track_values);
        if (columns) {
            MarkAutoTracksFromMeasures(tracks, effective_types, grid, true, false);
        } else {
            const std::vector<float> peer_columns = ResolveSharedColumnWidths(
                grid_handle,
                grid,
                ResolveGridContentExtent(grid, true),
                scope_ref);
            EnsureGridMeasuredSizesForColumns(grid, peer_columns);
            MarkAutoTracksFromMeasures(tracks, effective_types, grid, false, true);
        }
        for (std::size_t index = 0; index < tracks.size() && index < shared_groups.size(); index += 1U) {
            const std::string& group = shared_groups[index];
            if (group.empty()) {
                continue;
            }
            const auto it = out_sizes.find(group);
            if (it == out_sizes.end()) {
                out_sizes.emplace(group, tracks[index]);
            } else {
                it->second = std::max(it->second, tracks[index]);
            }
        }
    });
}

std::vector<float> ResolveSharedColumnWidths(
    std::uint64_t handle,
    UINode& grid,
    float available_width,
    const SharedSizeScopeRef& scope_ref) {
    EnsureGridNaturalMeasures(grid);
    const std::size_t column_count = ResolveGridTrackCount(grid, true);
    std::vector<float> column_values{};
    std::vector<std::uint8_t> column_types{};
    std::vector<std::string> shared_groups{};
    BuildTrackDefinitions(grid.column_values, grid.column_types, column_count, column_values, column_types);
    BuildSharedSizeGroupDefinitions(GetRuntime().GridColumnSharedSizeGroups(handle), column_count, shared_groups);
    const std::vector<std::uint8_t> effective_types = ResolveEffectiveTrackTypes(column_types, shared_groups);

    std::vector<float> column_widths{};
    SetTrackBaseSizes(column_widths, effective_types, column_values);
    MarkAutoTracksFromMeasures(column_widths, effective_types, grid, true, false);
    if (scope_ref.node != nullptr && HasSharedSizeGroups(shared_groups)) {
        std::unordered_map<std::string, float> shared_sizes{};
        CollectSharedTrackSizes(scope_ref, true, shared_sizes);
        for (std::size_t index = 0; index < column_widths.size() && index < shared_groups.size(); index += 1U) {
            const std::string& group = shared_groups[index];
            if (group.empty()) {
                continue;
            }
            const auto it = shared_sizes.find(group);
            if (it != shared_sizes.end()) {
                column_widths[index] = std::max(column_widths[index], it->second);
            }
        }
    }
    DistributeStarTracks(
        column_widths,
        effective_types,
        column_values,
        std::max(0.0f, available_width - SumTracks(column_widths, 0U, column_widths.size())));
    return column_widths;
}

} // namespace

void UiRuntime::LayoutGrid(
    std::uint64_t handle,
    UINode& node,
    float abs_x,
    float abs_y,
    float scene_x,
    float scene_y,
    bool inherited_scroll_dirty,
    CommandBuilder& builder,
    std::vector<std::uint64_t>& paint_order,
    std::vector<SceneInstruction>& scene,
    std::vector<std::uint64_t>& deferred_portal_roots) {
    const std::size_t column_count = ResolveGridTrackCount(node, true);
    const std::size_t row_count = ResolveGridTrackCount(node, false);
    const Rect content_abs_bounds = ComputeContentBounds(node, abs_x, abs_y);
    const Rect content_scene_bounds = ComputeContentBounds(node, scene_x, scene_y);
    const float content_width = content_abs_bounds.width;
    const float content_height = content_abs_bounds.height;

    std::vector<float> column_values{};
    std::vector<std::uint8_t> column_types{};
    std::vector<float> row_values{};
    std::vector<std::uint8_t> row_types{};
    BuildTrackDefinitions(node.column_values, node.column_types, column_count, column_values, column_types);
    BuildTrackDefinitions(node.row_values, node.row_types, row_count, row_values, row_types);
    const SharedSizeScopeRef scope_ref = ResolveSharedSizeScope(node, handle);
    std::vector<std::string> row_shared_groups{};
    BuildSharedSizeGroupDefinitions(GridRowSharedSizeGroups(handle), row_count, row_shared_groups);
    const std::vector<std::uint8_t> effective_row_types = ResolveEffectiveTrackTypes(row_types, row_shared_groups);

    std::vector<float> column_widths = ResolveSharedColumnWidths(handle, node, content_width, scope_ref);
    EnsureGridMeasuredSizesForColumns(node, column_widths);

    std::vector<float> row_heights{};
    SetTrackBaseSizes(row_heights, effective_row_types, row_values);
    MarkAutoTracksFromMeasures(row_heights, effective_row_types, node, false, true);
    if (scope_ref.node != nullptr && HasSharedSizeGroups(row_shared_groups)) {
        std::unordered_map<std::string, float> shared_sizes{};
        CollectSharedTrackSizes(scope_ref, false, shared_sizes);
        for (std::size_t index = 0; index < row_heights.size() && index < row_shared_groups.size(); index += 1U) {
            const std::string& group = row_shared_groups[index];
            if (group.empty()) {
                continue;
            }
            const auto it = shared_sizes.find(group);
            if (it != shared_sizes.end()) {
                row_heights[index] = std::max(row_heights[index], it->second);
            }
        }
    }
    DistributeStarTracks(
        row_heights,
        effective_row_types,
        row_values,
        std::max(0.0f, content_height - SumTracks(row_heights, 0U, row_heights.size())));

    for (std::size_t child_index = 0; child_index < node.children.size(); child_index += 1U) {
        const std::uint64_t child_handle = node.children[child_index];
        UINode* child = ResolveMutable(child_handle);
        if (child == nullptr || child->yg_node == nullptr) {
            continue;
        }

        const auto [column_start, column_span] = ResolvePlacement(*child, column_widths.size(), true);
        const auto [row_start, row_span] = ResolvePlacement(*child, row_heights.size(), false);
        const float child_abs_x = content_abs_bounds.x + SumTracks(column_widths, 0U, column_start);
        const float child_abs_y = content_abs_bounds.y + SumTracks(row_heights, 0U, row_start);
        const float child_scene_x = content_scene_bounds.x + SumTracks(column_widths, 0U, column_start);
        const float child_scene_y = content_scene_bounds.y + SumTracks(row_heights, 0U, row_start);
        const float alloc_width = SumTracks(column_widths, column_start, column_span);
        const float alloc_height = SumTracks(row_heights, row_start, row_span);

        const GridStyleSnapshot snapshot = ApplyGridMeasurementStyle(*child, alloc_width, alloc_height);
        YGNodeCalculateLayout(child->yg_node, alloc_width, alloc_height, YGDirectionLTR);
        const float origin_x = child_abs_x - YGNodeLayoutGetLeft(child->yg_node);
        const float origin_y = child_abs_y - YGNodeLayoutGetTop(child->yg_node);
        const float scene_origin_x = child_scene_x - YGNodeLayoutGetLeft(child->yg_node);
        const float scene_origin_y = child_scene_y - YGNodeLayoutGetTop(child->yg_node);
        WalkTree(
            child_handle,
            origin_x,
            origin_y,
            scene_origin_x,
            scene_origin_y,
            inherited_scroll_dirty,
            builder,
            paint_order,
            scene,
            deferred_portal_roots);
        RestoreGridStyle(child->yg_node, snapshot);
        MarkGridChildDirty(*child);
    }
}

} // namespace effindom::v2::ui
