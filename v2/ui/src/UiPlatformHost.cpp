#include "UiPlatformHost.h"

#include "effindom_ui.h"

namespace effindom::v2::ui {
namespace {

UiPlatformHost* g_global_platform_host = nullptr;

const std::uint8_t* Bytes(std::string_view value) {
    return value.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(value.data());
}

class AbiUiPlatformHost final : public UiPlatformHost {
public:
    void WriteClipboard(std::string_view plain_text, std::string_view rich_json) override {
        as_on_clipboard_write(
            Bytes(plain_text),
            static_cast<std::uint32_t>(plain_text.size()),
            Bytes(rich_json),
            static_cast<std::uint32_t>(rich_json.size()));
    }

    void RequestClipboardRead(std::uint64_t handle) override {
        as_on_request_clipboard_read(handle);
    }

    void RequestFontLoad(std::uint32_t font_id, std::string_view url) override {
        as_on_request_font_load(
            font_id,
            Bytes(url),
            static_cast<std::uint32_t>(url.size()));
    }

    void ReportMissingFontCoverage(
        std::uint32_t font_id,
        std::uint32_t coverage_kind,
        std::string_view sample_text) override {
        as_on_missing_font_coverage(
            font_id,
            coverage_kind,
            Bytes(sample_text),
            static_cast<std::uint32_t>(sample_text.size()));
    }

    void RequestSemanticAnnouncement(std::uint64_t handle) override {
        as_on_request_semantic_announcement(handle);
    }
};

} // namespace

UiPlatformHost& GetAbiUiPlatformHost() {
    static AbiUiPlatformHost host{};
    return host;
}

void SetGlobalUiPlatformHost(UiPlatformHost& host) {
    g_global_platform_host = &host;
}

UiPlatformHost& GetGlobalUiPlatformHost() {
    return g_global_platform_host == nullptr ? GetAbiUiPlatformHost() : *g_global_platform_host;
}

} // namespace effindom::v2::ui
