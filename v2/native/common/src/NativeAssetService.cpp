#include "NativeAssetService.h"
#include "NativeHttpClient.h"

#include "Engine.h"
#include "SvgIntrinsicSize.h"
#include "UiRuntime.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"

#include <include/codec/SkCodec.h>
#include <include/core/SkData.h>
#include <include/core/SkFontMgr.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkStream.h>
#include <include/core/SkTypeface.h>
#include <include/ports/SkFontMgr_data.h>
#include <modules/svg/include/SkSVGDOM.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <optional>

extern "C" {
void __fui_on_font_loaded(std::uint32_t font_id);
void __fui_on_svg_loaded(std::uint32_t svg_id, float width, float height);
void __fui_on_svg_failed(std::uint32_t svg_id, const std::uint8_t* error, std::uint32_t error_length);
void __fui_on_texture_loaded(std::uint32_t texture_id, float width, float height);
void __fui_on_texture_failed(std::uint32_t texture_id, const std::uint8_t* error, std::uint32_t error_length);
}

namespace effindom::v2::native {
namespace {

constexpr std::string_view kSvgDataPrefix = "data:image/svg+xml";

struct BuiltInFont {
    std::uint32_t id;
    std::string_view file_name;
};

constexpr std::array kBuiltInFonts{
    BuiltInFont{1U, "NotoSans-Regular.ttf"},
    BuiltInFont{2U, "NotoSans-Bold.ttf"},
    BuiltInFont{3U, "NotoSansSymbols2-Regular.ttf"},
    BuiltInFont{4U, "NotoEmoji-Regular.ttf"},
    BuiltInFont{5U, "NotoSans-Italic.ttf"},
    BuiltInFont{6U, "NotoSans-BoldItalic.ttf"},
    BuiltInFont{7U, "NotoSansMono-Regular.ttf"},
    BuiltInFont{8U, "NotoSansMono-Bold.ttf"},
};

constexpr std::array kBuiltInFallbackPrimaries{1U, 2U, 5U, 6U, 7U, 8U};

bool IsRemoteSource(std::string_view source) {
    return source.rfind("http://", 0U) == 0U || source.rfind("https://", 0U) == 0U;
}

std::string NormalizedContentType(std::string_view content_type) {
    const std::size_t parameters = content_type.find(';');
    content_type = content_type.substr(0U, parameters);
    while (!content_type.empty() && std::isspace(static_cast<unsigned char>(content_type.front()))) {
        content_type.remove_prefix(1U);
    }
    while (!content_type.empty() && std::isspace(static_cast<unsigned char>(content_type.back()))) {
        content_type.remove_suffix(1U);
    }
    std::string normalized(content_type);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return normalized;
}

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) return {};
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size <= 0 || static_cast<std::uint64_t>(size) > std::numeric_limits<std::size_t>::max()) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return input.good() ? bytes : std::vector<std::uint8_t>{};
}

int HexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::vector<std::uint8_t> PercentDecode(std::string_view value) {
    std::vector<std::uint8_t> result;
    result.reserve(value.size());
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2U < value.size()) {
            const int high = HexValue(value[index + 1U]);
            const int low = HexValue(value[index + 2U]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<std::uint8_t>((high << 4) | low));
                index += 2U;
                continue;
            }
        }
        result.push_back(static_cast<std::uint8_t>(value[index]));
    }
    return result;
}

std::vector<std::uint8_t> Base64Decode(std::string_view value) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> result;
    std::uint32_t buffer = 0U;
    std::uint32_t bits = 0U;
    for (const char character : value) {
        if (character == '=') break;
        const std::size_t index = alphabet.find(character);
        if (index == std::string_view::npos) {
            if (character == ' ' || character == '\t' || character == '\r' || character == '\n') continue;
            return {};
        }
        buffer = (buffer << 6U) | static_cast<std::uint32_t>(index);
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            result.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFFU));
        }
    }
    return result;
}

void ReportFailure(
    void (*callback)(std::uint32_t, const std::uint8_t*, std::uint32_t),
    std::uint32_t id,
    std::string_view message) {
    callback(id, reinterpret_cast<const std::uint8_t*>(message.data()), static_cast<std::uint32_t>(message.size()));
}

std::uint32_t FirstScalar(std::string_view text) {
    if (text.empty()) return 0U;
    const auto first = static_cast<std::uint8_t>(text[0]);
    if ((first & 0xF8U) == 0xF0U && text.size() >= 4U) {
        return ((first & 0x07U) << 18U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 12U) |
            ((static_cast<std::uint8_t>(text[2]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[3]) & 0x3FU);
    }
    if ((first & 0xF0U) == 0xE0U && text.size() >= 3U) {
        return ((first & 0x0FU) << 12U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[2]) & 0x3FU);
    }
    if ((first & 0xE0U) == 0xC0U && text.size() >= 2U) {
        return ((first & 0x1FU) << 6U) | (static_cast<std::uint8_t>(text[1]) & 0x3FU);
    }
    return first;
}

bool ContainsEmoji(std::string_view text) {
    for (std::size_t index = 0U; index < text.size();) {
        const std::string_view remainder = text.substr(index);
        const std::uint32_t scalar = FirstScalar(remainder);
        std::size_t length = 1U;
        const auto first = static_cast<std::uint8_t>(text[index]);
        if ((first & 0xF8U) == 0xF0U) length = 4U;
        else if ((first & 0xF0U) == 0xE0U) length = 3U;
        else if ((first & 0xE0U) == 0xC0U) length = 2U;
        if ((scalar >= 0x1F000U && scalar <= 0x1FAFFU) ||
            (scalar >= 0x2600U && scalar <= 0x27BFU)) return true;
        index += std::min(length, text.size() - index);
    }
    return false;
}

std::uint32_t ResolveCollectionFaceIndex(
    const std::vector<std::uint8_t>& bytes,
    const NativeSystemFontSource& source) {
    if (bytes.empty()) return 0U;
    sk_sp<SkData> data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    std::array<sk_sp<SkData>, 1> font_data = {data};
    sk_sp<SkFontMgr> manager = SkFontMgr_New_Custom_Data(
        SkSpan<sk_sp<SkData>>(font_data.data(), font_data.size()));
    if (manager == nullptr) return 0U;
    for (std::uint32_t index = 0U; index < 128U; ++index) {
        sk_sp<SkTypeface> face = manager->makeFromData(data, static_cast<int>(index));
        if (face == nullptr) break;
        if (!source.postscript_name.empty()) {
            SkString name;
            if (face->getPostScriptName(&name) && source.postscript_name == name.c_str()) return index;
        } else if (source.required_scalar != 0U &&
                   face->unicharToGlyph(static_cast<SkUnichar>(source.required_scalar)) != 0U) {
            return index;
        }
    }
    return 0U;
}

} // namespace

bool IsSupportedRemoteAssetContentType(std::string_view content_type, bool svg) {
    const std::string normalized = NormalizedContentType(content_type);
    if (normalized.empty() || normalized == "application/octet-stream") return true;
    if (svg) {
        return normalized == "image/svg+xml" ||
            normalized == "application/svg+xml" ||
            normalized == "application/xml" ||
            normalized == "text/xml";
    }
    return normalized.rfind("image/", 0U) == 0U && normalized != "image/svg+xml";
}

std::vector<std::filesystem::path> BuildNativeAssetSearchRoots(
    const std::filesystem::path& executable_directory,
    NativePackagePlatform platform) {
    if (executable_directory.empty()) return {};
    std::vector<std::filesystem::path> roots{executable_directory};
    switch (platform) {
        case NativePackagePlatform::MacOs:
            roots.push_back(executable_directory / "../Resources");
            roots.push_back(executable_directory / "../Resources/effindom");
            break;
        case NativePackagePlatform::Windows:
            roots.push_back(executable_directory / "assets");
            roots.push_back(executable_directory / "assets/effindom");
            break;
        case NativePackagePlatform::Linux:
            roots.push_back(executable_directory / "../share");
            roots.push_back(executable_directory / "../share/effindom");
            break;
    }
    roots.push_back(executable_directory / "../resources");
    roots.push_back(executable_directory / "../resources/effindom");
    for (std::filesystem::path& root : roots) root = root.lexically_normal();
    return roots;
}

std::filesystem::path ResolveNativeAssetPath(
    const NativeAssetEnvironment& environment,
    std::string_view source) {
    if (source.empty() || IsRemoteSource(source) || source.rfind("data:", 0U) == 0U) return {};
    std::string decoded_source(source);
    if (source.rfind("file://", 0U) == 0U) {
        const auto decoded = PercentDecode(source.substr(7U));
        decoded_source.assign(decoded.begin(), decoded.end());
    }
    std::filesystem::path path = environment.path_from_utf8
        ? environment.path_from_utf8(decoded_source)
        : std::filesystem::path(decoded_source);
    std::error_code error;
    const bool browser_root_source = decoded_source.front() == '/' &&
        decoded_source.rfind("//", 0U) != 0U;
    if (path.is_absolute()) {
        if (std::filesystem::is_regular_file(path, error)) return path;
        error.clear();
        if (!browser_root_source) return {};
    }
    if (browser_root_source) path = path.relative_path();
    for (const auto& component : path) {
        if (component == "..") return {};
    }
    for (const std::filesystem::path& root : environment.search_roots) {
        const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, error);
        if (error) {
            error.clear();
            continue;
        }
        std::vector<std::filesystem::path> candidates{
            canonical_root / path,
            canonical_root / "app" / path,
            canonical_root / "fonts" / path,
        };
        std::filesystem::path packaged_font_path;
        bool found_font_namespace = false;
        for (const auto& component : path) {
            if (found_font_namespace) {
                packaged_font_path /= component;
            } else if (component == "fonts") {
                found_font_namespace = true;
                packaged_font_path = component;
            }
        }
        if (found_font_namespace) candidates.push_back(canonical_root / packaged_font_path);
        for (const std::filesystem::path& unresolved : candidates) {
            const std::filesystem::path candidate = std::filesystem::weakly_canonical(unresolved, error);
            const std::filesystem::path relative = candidate.lexically_relative(canonical_root);
            const bool contained = !relative.empty() && relative != "." &&
                relative.begin() != relative.end() && *relative.begin() != "..";
            if (!error && contained && std::filesystem::is_regular_file(candidate, error)) return candidate;
            error.clear();
        }
    }
    return {};
}

NativeAssetService::NativeAssetService(
    Engine& engine,
    std::function<void()> request_frame,
    NativeAssetEnvironment environment)
    : engine_(engine),
      request_frame_(std::move(request_frame)),
      environment_(std::move(environment)) {
    const std::filesystem::path default_font = ResolvePath("fonts/NotoSans-Regular.ttf");
    if (!default_font.empty()) packaged_font_directory_ = default_font.parent_path();
}

bool NativeAssetService::LoadDefaultFont(std::uint32_t font_id, std::string_view name) {
    const std::filesystem::path path = ResolvePath(std::string("fonts/") + std::string(name));
    return !path.empty() && LoadFontPath(font_id, path);
}

bool NativeAssetService::LoadBuiltInFonts() {
    for (const BuiltInFont& font : kBuiltInFonts) {
        if (!LoadDefaultFont(font.id, font.file_name)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "EffinDOM built-in font %u could not be loaded: %.*s",
                font.id,
                static_cast<int>(font.file_name.size()),
                font.file_name.data());
            return false;
        }
    }
    for (const std::uint32_t primary : kBuiltInFallbackPrimaries) {
        ui_register_font_fallback(primary, 4U);
        ui_register_font_fallback(primary, 3U);
    }
    return true;
}

bool NativeAssetService::LoadFont(std::uint32_t font_id, std::string_view source) {
    const std::filesystem::path path = ResolvePath(source);
    if (path.empty()) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "EffinDOM authored font source could not be resolved: %.*s",
            static_cast<int>(source.size()),
            source.data());
        return false;
    }
    if (LoadFontPath(font_id, path)) return true;
    SDL_LogError(
        SDL_LOG_CATEGORY_APPLICATION,
        "EffinDOM authored font failed to register: %s",
        path.u8string().c_str());
    return false;
}

bool NativeAssetService::LoadFont(std::uint32_t font_id, const std::filesystem::path& path) {
    return LoadFontPath(font_id, path);
}

bool NativeAssetService::LoadSvg(std::uint32_t svg_id, std::string_view source) {
    const std::uint64_t generation = ++svg_generations_[svg_id];
    if (IsRemoteSource(source)) {
        const bool queued = QueueRemote(RemoteAssetKind::Svg, svg_id, source, generation);
        CancelUnusedTransfers();
        return queued;
    }
    const std::vector<std::uint8_t> bytes = ReadSource(source, kSvgDataPrefix);
    if (bytes.empty()) {
        ReportFailure(__fui_on_svg_failed, svg_id, "Local SVG source was not found or is not supported.");
        return false;
    }
    return RegisterSvgBytes(svg_id, bytes, "Local SVG");
}

bool NativeAssetService::RegisterSvgBytes(
    std::uint32_t svg_id,
    const std::vector<std::uint8_t>& bytes,
    std::string_view origin) {
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        ReportFailure(__fui_on_svg_failed, svg_id, std::string(origin) + " data is too large.");
        return false;
    }
    SkMemoryStream stream(bytes.data(), bytes.size(), true);
    if (SkSVGDOM::MakeFromStream(stream) == nullptr) {
        ReportFailure(__fui_on_svg_failed, svg_id, std::string(origin) + " data is malformed.");
        return false;
    }
    const auto byte_length = static_cast<std::uint32_t>(bytes.size());
    const detail::SvgIntrinsicSize size = detail::ParseSvgIntrinsicSize(bytes.data(), byte_length);
    engine_.RegisterSvg(svg_id, bytes.data(), byte_length);
    __fui_on_svg_loaded(svg_id, std::max(size.width, 1.0f), std::max(size.height, 1.0f));
    request_frame_();
    return true;
}

bool NativeAssetService::LoadTexture(std::uint32_t texture_id, std::string_view source) {
    const std::uint64_t generation = ++texture_generations_[texture_id];
    if (IsRemoteSource(source)) {
        const bool queued = QueueRemote(RemoteAssetKind::Texture, texture_id, source, generation);
        CancelUnusedTransfers();
        return queued;
    }
    const std::vector<std::uint8_t> bytes = ReadSource(source, "data:image/");
    if (bytes.empty()) {
        ReportFailure(__fui_on_texture_failed, texture_id, "Local image source was not found or is not supported.");
        return false;
    }
    return RegisterTextureBytes(texture_id, bytes, "Local image");
}

bool NativeAssetService::RegisterTextureBytes(
    std::uint32_t texture_id,
    const std::vector<std::uint8_t>& bytes,
    std::string_view origin) {
    sk_sp<SkData> data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(std::move(data));
    if (codec == nullptr) {
        ReportFailure(__fui_on_texture_failed, texture_id, std::string(origin) + " data is malformed or unsupported.");
        return false;
    }
    const SkImageInfo source_info = codec->getInfo();
    if (source_info.width() <= 0 || source_info.height() <= 0) {
        ReportFailure(__fui_on_texture_failed, texture_id, std::string(origin) + " dimensions are invalid.");
        return false;
    }
    const SkImageInfo target_info = SkImageInfo::Make(
        source_info.width(), source_info.height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    std::vector<std::uint8_t> pixels(target_info.computeMinByteSize());
    if (codec->getPixels(target_info, pixels.data(), target_info.minRowBytes()) != SkCodec::kSuccess) {
        ReportFailure(__fui_on_texture_failed, texture_id, std::string(origin) + " pixels could not be decoded.");
        return false;
    }
    engine_.RegisterTextureRgba(
        texture_id,
        pixels.data(),
        static_cast<std::uint32_t>(target_info.width()),
        static_cast<std::uint32_t>(target_info.height()),
        pixels.size());
    __fui_on_texture_loaded(texture_id, static_cast<float>(target_info.width()), static_cast<float>(target_info.height()));
    request_frame_();
    return true;
}

void NativeAssetService::ReleaseSvg(std::uint32_t svg_id) {
    ++svg_generations_[svg_id];
    engine_.UnregisterSvg(svg_id);
    CancelUnusedTransfers();
}
void NativeAssetService::ReleaseTexture(std::uint32_t texture_id) {
    ++texture_generations_[texture_id];
    engine_.UnregisterTexture(texture_id);
    CancelUnusedTransfers();
}

bool NativeAssetService::QueueRemote(
    RemoteAssetKind kind,
    std::uint32_t id,
    std::string_view source,
    std::uint64_t generation) {
    const std::string key(source);
    const auto cached = remote_cache_.find(key);
    if (cached != remote_cache_.end()) {
        return kind == RemoteAssetKind::Svg
            ? RegisterSvgBytes(id, cached->second, "Remote SVG")
            : RegisterTextureBytes(id, cached->second, "Remote image");
    }
    std::shared_ptr<RemoteTransfer> transfer;
    const auto existing = inflight_remote_.find(key);
    if (existing != inflight_remote_.end()) transfer = existing->second.lock();
    if (transfer == nullptr) {
        transfer = std::make_shared<RemoteTransfer>();
        transfer->source = key;
        transfer->cancelled = std::make_shared<std::atomic_bool>(false);
        auto fetch = environment_.fetch_remote_asset;
        transfer->future = std::async(
            std::launch::async,
            [url = key, cancelled = transfer->cancelled, fetch = std::move(fetch)] {
                return fetch ? fetch(url, cancelled) : NativeHttpClient::Get(url, cancelled);
            }).share();
        inflight_remote_[key] = transfer;
    }
    pending_remote_.push_back({kind, id, generation, transfer});
    request_frame_();
    return true;
}

bool NativeAssetService::ProcessPendingRemoteAssets() {
    for (auto request = pending_remote_.begin(); request != pending_remote_.end();) {
        if (request->transfer->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++request;
            continue;
        }
        const NativeHttpResponse& response = request->transfer->future.get();
        const bool current = request->kind == RemoteAssetKind::Svg
            ? svg_generations_[request->id] == request->generation
            : texture_generations_[request->id] == request->generation;
        if (current) {
            if (response.cancelled) {
                // A released or replaced request has no observable completion.
            } else if (response.too_large || response.status < 200 || response.status >= 300 || response.bytes.empty()) {
                std::string message = response.error.empty()
                    ? "Remote asset request failed with HTTP status " + std::to_string(response.status) + "."
                    : response.error;
                ReportFailure(
                    request->kind == RemoteAssetKind::Svg ? __fui_on_svg_failed : __fui_on_texture_failed,
                    request->id,
                    message);
            } else if (!IsSupportedRemoteAssetContentType(
                           response.content_type,
                           request->kind == RemoteAssetKind::Svg)) {
                ReportFailure(
                    request->kind == RemoteAssetKind::Svg ? __fui_on_svg_failed : __fui_on_texture_failed,
                    request->id,
                    "Remote asset response has unsupported content type '" + response.content_type + "'.");
            } else {
                remote_cache_.try_emplace(request->transfer->source, response.bytes);
                if (request->kind == RemoteAssetKind::Svg) {
                    RegisterSvgBytes(request->id, response.bytes, "Remote SVG");
                } else {
                    RegisterTextureBytes(request->id, response.bytes, "Remote image");
                }
            }
        }
        const std::string source = request->transfer->source;
        request = pending_remote_.erase(request);
        bool still_pending = false;
        for (const RemoteRequest& other : pending_remote_) {
            if (other.transfer->source == source) {
                still_pending = true;
                break;
            }
        }
        if (!still_pending) inflight_remote_.erase(source);
    }
    return !pending_remote_.empty();
}

void NativeAssetService::CancelUnusedTransfers() {
    for (const RemoteRequest& request : pending_remote_) {
        bool current = request.kind == RemoteAssetKind::Svg
            ? svg_generations_[request.id] == request.generation
            : texture_generations_[request.id] == request.generation;
        if (current) continue;
        bool has_current_consumer = false;
        for (const RemoteRequest& other : pending_remote_) {
            if (other.transfer != request.transfer) continue;
            has_current_consumer = other.kind == RemoteAssetKind::Svg
                ? svg_generations_[other.id] == other.generation
                : texture_generations_[other.id] == other.generation;
            if (has_current_consumer) break;
        }
        if (!has_current_consumer) request.transfer->cancelled->store(true);
    }
}

void NativeAssetService::QueueMissingFontCoverage(
    std::uint32_t primary_font_id,
    std::uint32_t coverage_kind,
    std::string_view sample_text) {
    if (primary_font_id == 0U || coverage_kind == UI_MISSING_FONT_COVERAGE_UNKNOWN || sample_text.empty()) return;
    pending_coverage_.push_back({primary_font_id, coverage_kind, std::string(sample_text)});
    request_frame_();
}

bool NativeAssetService::ProcessPendingAssets() {
    const bool has_remote = ProcessPendingRemoteAssets();
    for (MissingCoverageRequest& request : pending_coverage_) {
        pending_coverage_jobs_.push_back(std::async(
            std::launch::async,
            [request = std::move(request),
             fonts = packaged_font_directory_,
             resolver = environment_.resolve_system_font,
             use_symbols = environment_.use_symbol_font_for_non_emoji_supplemental]() mutable {
                return ResolveFallbackFont(std::move(request), std::move(fonts), std::move(resolver), use_symbols);
            }));
    }
    pending_coverage_.clear();
    for (auto job = pending_coverage_jobs_.begin(); job != pending_coverage_jobs_.end();) {
        if (job->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++job;
            continue;
        }
        ResolvedCoverage resolved = job->get();
        job = pending_coverage_jobs_.erase(job);
        if (resolved.path.empty() || resolved.bytes.empty()) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "EffinDOM fallback font resolution failed for coverage %u and sample '%s'",
                resolved.request.coverage_kind,
                resolved.request.sample_text.c_str());
            continue;
        }
        const std::string key = resolved.path.u8string() + "#" + std::to_string(resolved.face_index);
        auto existing = fallback_ids_by_path_.find(key);
        std::uint32_t fallback_id = 0U;
        if (existing != fallback_ids_by_path_.end()) {
            fallback_id = existing->second;
        } else {
            fallback_id = next_fallback_font_id_++;
            if (!RegisterFontBytes(fallback_id, resolved.bytes, resolved.face_index)) continue;
            fallback_ids_by_path_.emplace(key, fallback_id);
        }
        const std::uint64_t binding =
            (static_cast<std::uint64_t>(resolved.request.primary_font_id) << 32U) | fallback_id;
        if (registered_fallbacks_.insert(binding).second) {
            ui_register_font_fallback(resolved.request.primary_font_id, fallback_id);
            request_frame_();
        }
    }
    return has_remote || !pending_coverage_jobs_.empty() || !pending_coverage_.empty();
}

std::size_t NativeAssetService::FallbackFontCountForTesting() const { return fallback_ids_by_path_.size(); }

NativeAssetService::ResolvedCoverage NativeAssetService::ResolveFallbackFont(
    MissingCoverageRequest request,
    std::filesystem::path packaged_font_directory,
    std::function<NativeSystemFontSource(std::string_view)> resolve_system_font,
    bool use_symbol_font_for_non_emoji_supplemental) {
    ResolvedCoverage result{};
    result.request = std::move(request);
    NativeSystemFontSource source;
    if (result.request.coverage_kind == UI_MISSING_FONT_COVERAGE_ARABIC) {
        result.path = packaged_font_directory / "NotoNaskhArabic-Variable.ttf";
    } else if (result.request.coverage_kind == UI_MISSING_FONT_COVERAGE_THAI) {
        result.path = packaged_font_directory / "NotoSansThai-Regular.ttf";
    } else if (result.request.coverage_kind == UI_MISSING_FONT_COVERAGE_SUPPLEMENTAL) {
        result.path = packaged_font_directory /
            (use_symbol_font_for_non_emoji_supplemental && !ContainsEmoji(result.request.sample_text)
                ? "NotoSansSymbols2-Regular.ttf"
                : "NotoColorEmoji.ttf");
    } else if (resolve_system_font) {
        source = resolve_system_font(result.request.sample_text);
        result.path = source.path;
        result.face_index = source.face_index;
    }
    result.bytes = ReadBytes(result.path);
    if (result.face_index == 0U &&
        (!source.postscript_name.empty() || source.required_scalar != 0U)) {
        result.face_index = ResolveCollectionFaceIndex(result.bytes, source);
    }
    return result;
}

std::filesystem::path NativeAssetService::ResolvePath(std::string_view source) const {
    return ResolveNativeAssetPath(environment_, source);
}

bool NativeAssetService::RegisterFontBytes(
    std::uint32_t font_id,
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t face_index) {
    if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        !ui::GetRuntime().RegisterFont(
            font_id, bytes.data(), static_cast<std::uint32_t>(bytes.size()), face_index)) return false;
    engine_.RegisterFont(font_id, bytes.data(), static_cast<std::uint32_t>(bytes.size()), face_index);
    __fui_on_font_loaded(font_id);
    request_frame_();
    return true;
}

bool NativeAssetService::LoadFontPath(
    std::uint32_t font_id,
    const std::filesystem::path& path,
    std::uint32_t face_index) {
    return RegisterFontBytes(font_id, ReadBytes(path), face_index);
}

std::vector<std::uint8_t> NativeAssetService::ReadSource(
    std::string_view source,
    std::string_view data_mime) const {
    if (source.rfind("data:", 0U) == 0U) {
        const std::size_t comma = source.find(',');
        if (comma == std::string_view::npos || source.substr(0U, comma).rfind(data_mime, 0U) != 0U) return {};
        const std::string_view metadata = source.substr(0U, comma);
        const std::string_view payload = source.substr(comma + 1U);
        return metadata.find(";base64") != std::string_view::npos
            ? Base64Decode(payload)
            : PercentDecode(payload);
    }
    const std::filesystem::path path = ResolvePath(source);
    return path.empty() ? std::vector<std::uint8_t>{} : ReadBytes(path);
}

} // namespace effindom::v2::native
