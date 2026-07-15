#include "UiPlatformHost.h"
#include "UiRuntime.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>
#include <vector>

namespace {

using effindom::v2::ui::UiPlatformHost;
using effindom::v2::ui::UiRuntime;

class RecordingPlatformHost final : public UiPlatformHost {
public:
    void WriteClipboard(std::string_view plain_text, std::string_view rich_json) override {
        clipboard_plain.assign(plain_text);
        clipboard_rich.assign(rich_json);
    }

    void RequestClipboardRead(std::uint64_t handle) override {
        clipboard_read_handles.push_back(handle);
    }

    void RequestFontLoad(std::uint32_t font_id, std::string_view url) override {
        requested_font_id = font_id;
        requested_font_url.assign(url);
    }

    void ReportMissingFontCoverage(
        std::uint32_t font_id,
        std::uint32_t coverage_kind,
        std::string_view sample_text) override {
        missing_font_id = font_id;
        missing_coverage_kind = coverage_kind;
        missing_sample.assign(sample_text);
    }

    void RequestSemanticAnnouncement(std::uint64_t handle) override {
        semantic_announcement_handles.push_back(handle);
    }

    std::string clipboard_plain{};
    std::string clipboard_rich{};
    std::vector<std::uint64_t> clipboard_read_handles{};
    std::uint32_t requested_font_id = 0U;
    std::string requested_font_url{};
    std::uint32_t missing_font_id = 0U;
    std::uint32_t missing_coverage_kind = 0U;
    std::string missing_sample{};
    std::vector<std::uint64_t> semantic_announcement_handles{};
};

} // namespace

TEST_CASE("UiRuntime routes platform side effects through its injected host", "[v2][ui][platform-host]") {
    RecordingPlatformHost host{};
    auto runtime = std::make_unique<UiRuntime>(host);

    const std::uint64_t text = runtime->CreateNode(UI_NODE_TEXT);
    REQUIRE(text != UI_INVALID_HANDLE);
    REQUIRE(runtime->SetText(text, reinterpret_cast<const std::uint8_t*>("copy me"), 7U));
    REQUIRE(runtime->SetSelectable(text, true, 0x3366FFFFU));
    REQUIRE(runtime->SelectAllText(text));
    REQUIRE(runtime->CopyTextSelection(text));
    CHECK(host.clipboard_plain == "copy me");

    REQUIRE(runtime->RequestSemanticAnnouncement(text));
    REQUIRE(host.semantic_announcement_handles.size() == 1U);
    CHECK(host.semantic_announcement_handles.front() == text);
}

TEST_CASE("platform host contract preserves UTF-8 payloads and request identity", "[v2][ui][platform-host]") {
    RecordingPlatformHost host{};
    constexpr std::uint64_t handle = 0x0000000200000001ULL;

    host.RequestClipboardRead(handle);
    host.RequestFontLoad(17U, "fonts/body.ttf");
    host.ReportMissingFontCoverage(17U, UI_MISSING_FONT_COVERAGE_CJK, u8"你好");

    REQUIRE(host.clipboard_read_handles.size() == 1U);
    CHECK(host.clipboard_read_handles.front() == handle);
    CHECK(host.requested_font_id == 17U);
    CHECK(host.requested_font_url == "fonts/body.ttf");
    CHECK(host.missing_font_id == 17U);
    CHECK(host.missing_coverage_kind == UI_MISSING_FONT_COVERAGE_CJK);
    CHECK(host.missing_sample == u8"你好");
}
