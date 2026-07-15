#pragma once

#include <cstdint>
#include <string_view>

namespace effindom::v2::ui {

// Platform-service boundary owned by the application host. Retained UI state
// stays inside UiRuntime; hosts only fulfill side effects requested by it.
class UiPlatformHost {
public:
    virtual ~UiPlatformHost() = default;

    virtual void WriteClipboard(std::string_view plain_text, std::string_view rich_json) = 0;
    virtual void RequestClipboardRead(std::uint64_t handle) = 0;
    virtual void RequestFontLoad(std::uint32_t font_id, std::string_view url) = 0;
    virtual void ReportMissingFontCoverage(
        std::uint32_t font_id,
        std::uint32_t coverage_kind,
        std::string_view sample_text) = 0;
    virtual void RequestSemanticAnnouncement(std::uint64_t handle) = 0;
};

// Existing C ABI adapter. In browser builds the ABI callbacks are implemented
// by Emscripten glue; native applications may inject their own UiPlatformHost.
UiPlatformHost& GetAbiUiPlatformHost();

// Selects the host used by the process-global C ABI runtime. This must be
// called before the first GetRuntime() access. The caller owns the host and
// must keep it alive until process shutdown.
void SetGlobalUiPlatformHost(UiPlatformHost& host);
UiPlatformHost& GetGlobalUiPlatformHost();

} // namespace effindom::v2::ui
