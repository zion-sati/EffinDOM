#pragma once

#include "NativeHttpClient.h"

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace effindom::v2 {
class Engine;
namespace native {

struct NativeSystemFontSource {
    std::filesystem::path path;
    std::string postscript_name;
    std::uint32_t required_scalar = 0U;
    std::uint32_t face_index = 0U;
};

struct NativeAssetEnvironment {
    std::vector<std::filesystem::path> search_roots;
    std::function<std::filesystem::path(std::string_view)> path_from_utf8;
    std::function<NativeSystemFontSource(std::string_view)> resolve_system_font;
    std::function<NativeHttpResponse(std::string_view, const std::shared_ptr<std::atomic_bool>&)>
        fetch_remote_asset;
    bool use_symbol_font_for_non_emoji_supplemental = true;
};

enum class NativePackagePlatform {
    MacOs,
    Windows,
    Linux,
};

std::vector<std::filesystem::path> BuildNativeAssetSearchRoots(
    const std::filesystem::path& executable_directory,
    NativePackagePlatform platform);

std::filesystem::path ResolveNativeAssetPath(
    const NativeAssetEnvironment& environment,
    std::string_view source);

bool IsSupportedRemoteAssetContentType(std::string_view content_type, bool svg);

class NativeAssetService final {
public:
    NativeAssetService(
        Engine& engine,
        std::function<void()> request_frame,
        NativeAssetEnvironment environment);

    bool LoadDefaultFont(std::uint32_t font_id, std::string_view name);
    bool LoadBuiltInFonts();
    bool LoadFont(std::uint32_t font_id, std::string_view source);
    bool LoadFont(std::uint32_t font_id, const std::filesystem::path& path);
    bool LoadSvg(std::uint32_t svg_id, std::string_view source);
    bool LoadTexture(std::uint32_t texture_id, std::string_view source);
    void ReleaseSvg(std::uint32_t svg_id);
    void ReleaseTexture(std::uint32_t texture_id);

    void QueueMissingFontCoverage(
        std::uint32_t primary_font_id,
        std::uint32_t coverage_kind,
        std::string_view sample_text);
    bool ProcessPendingAssets();
    std::size_t FallbackFontCountForTesting() const;

private:
    struct MissingCoverageRequest {
        std::uint32_t primary_font_id = 0U;
        std::uint32_t coverage_kind = 0U;
        std::string sample_text;
    };
    struct ResolvedCoverage {
        MissingCoverageRequest request;
        std::filesystem::path path;
        std::uint32_t face_index = 0U;
        std::vector<std::uint8_t> bytes;
    };
    enum class RemoteAssetKind { Svg, Texture };
    struct RemoteTransfer {
        std::string source;
        std::shared_ptr<std::atomic_bool> cancelled;
        std::shared_future<NativeHttpResponse> future;
    };
    struct RemoteRequest {
        RemoteAssetKind kind;
        std::uint32_t id;
        std::uint64_t generation;
        std::shared_ptr<RemoteTransfer> transfer;
    };

    static ResolvedCoverage ResolveFallbackFont(
        MissingCoverageRequest request,
        std::filesystem::path packaged_font_directory,
        std::function<NativeSystemFontSource(std::string_view)> resolve_system_font,
        bool use_symbol_font_for_non_emoji_supplemental);
    std::filesystem::path ResolvePath(std::string_view source) const;
    bool RegisterFontBytes(
        std::uint32_t font_id,
        const std::vector<std::uint8_t>& bytes,
        std::uint32_t face_index = 0U);
    bool LoadFontPath(
        std::uint32_t font_id,
        const std::filesystem::path& path,
        std::uint32_t face_index = 0U);
    bool RegisterSvgBytes(std::uint32_t svg_id, const std::vector<std::uint8_t>& bytes, std::string_view origin);
    bool RegisterTextureBytes(std::uint32_t texture_id, const std::vector<std::uint8_t>& bytes, std::string_view origin);
    bool QueueRemote(RemoteAssetKind kind, std::uint32_t id, std::string_view source, std::uint64_t generation);
    bool ProcessPendingRemoteAssets();
    void CancelUnusedTransfers();
    std::vector<std::uint8_t> ReadSource(
        std::string_view source,
        std::string_view data_mime) const;

    Engine& engine_;
    std::function<void()> request_frame_;
    NativeAssetEnvironment environment_;
    std::filesystem::path packaged_font_directory_;
    std::vector<MissingCoverageRequest> pending_coverage_;
    std::vector<std::future<ResolvedCoverage>> pending_coverage_jobs_;
    std::unordered_map<std::string, std::uint32_t> fallback_ids_by_path_;
    std::unordered_set<std::uint64_t> registered_fallbacks_;
    std::vector<RemoteRequest> pending_remote_;
    std::unordered_map<std::string, std::weak_ptr<RemoteTransfer>> inflight_remote_;
    std::unordered_map<std::string, std::vector<std::uint8_t>> remote_cache_;
    std::unordered_map<std::uint32_t, std::uint64_t> svg_generations_;
    std::unordered_map<std::uint32_t, std::uint64_t> texture_generations_;
    std::uint32_t next_fallback_font_id_ = 0x7E000100U;
};

} // namespace native
} // namespace effindom::v2
