#include "NativeFuiUnsupportedCapabilities.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
void fui_fetch_start(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t);
void fui_fetch_cancel(std::uint32_t);
std::uint32_t fui_file_capabilities();
void fui_file_pick(std::uint32_t, std::uintptr_t, std::uint32_t, bool);
void fui_file_read_chunk(std::uint32_t, std::uintptr_t, std::uint32_t, std::uint64_t, std::uint32_t);
void fui_file_save_text(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t);
void fui_file_save_bytes(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t);
void fui_file_create_writer(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t);
void fui_file_writer_write_text(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t);
void fui_file_writer_write_bytes(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t);
void fui_file_writer_finish(std::uint32_t, std::uintptr_t, std::uint32_t);
void fui_file_process_worker_start(
    std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t, bool);
void fui_file_process_worker_cancel(std::uint32_t);
void fui_set_persisted_scroll_offset(std::uintptr_t, std::uint32_t, float, float);
bool fui_try_get_persisted_scroll_offset(std::uintptr_t, std::uint32_t, std::uintptr_t, std::uintptr_t);
void fui_set_persisted_state(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t, std::uint32_t,
    std::uintptr_t, std::uint32_t);
std::int32_t fui_copy_persisted_state(
    std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t,
    std::uintptr_t, std::uintptr_t, std::uint32_t);
void fui_reload_page();
bool fui_can_navigate_back();
bool fui_can_navigate_forward();
void fui_navigate_back();
void fui_navigate_forward();
void fui_show_url_preview(std::uintptr_t, std::uint32_t);
void fui_hide_url_preview();
}

namespace {

std::vector<std::string> g_diagnostics;
std::uint32_t g_fetch_errors = 0U;
std::uint32_t g_file_errors = 0U;

void CaptureDiagnostic(const char* symbol, const char*) { g_diagnostics.emplace_back(symbol); }
void FetchError(std::uint32_t, const std::uint8_t*, std::uint32_t) { ++g_fetch_errors; }
void FileError(std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t) { ++g_file_errors; }
void FileReadError(
    std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
    const std::uint8_t*, std::uint32_t) { ++g_file_errors; }
void FileSaveError(
    std::uint32_t, std::uint32_t, std::uint64_t, const std::uint8_t*, std::uint32_t) {
    ++g_file_errors;
}
void FileWriteError(
    std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
    const std::uint8_t*, std::uint32_t) { ++g_file_errors; }

} // namespace

TEST_CASE("unsupported native FUI service APIs reject predictably and force-link their complete ABI") {
    using namespace effindom::v2::native;
    g_diagnostics.clear();
    g_fetch_errors = 0U;
    g_file_errors = 0U;
    SetUnsupportedCapabilityDiagnosticForTesting(CaptureDiagnostic);
    SetUnsupportedFuiCallbacksForTesting({
        FetchError,
        FileError,
        FileReadError,
        FileSaveError,
        FileError,
        FileWriteError,
        FileSaveError,
        FileError,
    });

    fui_fetch_start(1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U);
    fui_fetch_start(2U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U);
    fui_fetch_cancel(1U);
    REQUIRE(g_fetch_errors == 2U);

    REQUIRE(fui_file_capabilities() == 0U);
    fui_file_pick(4U, 0U, 0U, false);
    fui_file_read_chunk(5U, 0U, 0U, 0U, 0U);
    fui_file_save_text(6U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U);
    fui_file_save_bytes(7U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U);
    fui_file_create_writer(8U, 0U, 0U, 0U, 0U, 0U, 0U);
    fui_file_writer_write_text(9U, 0U, 0U, 0U, 0U);
    fui_file_writer_write_bytes(10U, 0U, 0U, 0U, 0U);
    fui_file_writer_finish(11U, 0U, 0U);
    fui_file_process_worker_start(12U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, false);
    fui_file_process_worker_cancel(12U);
    REQUIRE(g_file_errors == 9U);

    float x = 7.0F;
    float y = 8.0F;
    const std::size_t diagnostics_before_persistence = g_diagnostics.size();
    fui_set_persisted_scroll_offset(0U, 0U, x, y);
    REQUIRE_FALSE(fui_try_get_persisted_scroll_offset(
        0U, 0U, reinterpret_cast<std::uintptr_t>(&x), reinterpret_cast<std::uintptr_t>(&y)));
    REQUIRE(x == 7.0F);
    REQUIRE(y == 8.0F);
    fui_set_persisted_state(0U, 0U, 0U, 0U, 0U, 0U, 0U);
    REQUIRE(fui_copy_persisted_state(0U, 0U, 0U, 0U, 0U, 0U, 0U) == -1);
    REQUIRE(g_diagnostics.size() == diagnostics_before_persistence);

    REQUIRE_FALSE(fui_can_navigate_back());
    REQUIRE_FALSE(fui_can_navigate_forward());
    fui_reload_page();
    fui_navigate_back();
    fui_navigate_forward();
    fui_show_url_preview(0U, 0U);
    fui_hide_url_preview();

    REQUIRE(g_diagnostics.size() == 7U);
    ResetUnsupportedFuiCapabilitiesForTesting();
}
