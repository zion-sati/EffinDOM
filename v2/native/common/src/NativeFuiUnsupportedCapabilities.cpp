#include "NativeFuiUnsupportedCapabilities.h"

#include <SDL3/SDL_log.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

extern "C" {
void __fui_on_fetch_error(std::uint32_t, const std::uint8_t*, std::uint32_t);
void __fui_on_file_pick_result(std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t);
void __fui_on_file_read_result(
    std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
    const std::uint8_t*, std::uint32_t);
void __fui_on_file_save_result(
    std::uint32_t, std::uint32_t, std::uint64_t, const std::uint8_t*, std::uint32_t);
void __fui_on_file_writer_created(
    std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t);
void __fui_on_file_write_result(
    std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
    const std::uint8_t*, std::uint32_t);
void __fui_on_file_finish_result(
    std::uint32_t, std::uint32_t, std::uint64_t, const std::uint8_t*, std::uint32_t);
void __fui_on_file_worker_process_error(
    std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t);
}

namespace effindom::v2::native {
namespace {

constexpr std::uint32_t kUnsupportedFileStatus = 0U;
constexpr char kFetchError[] =
    "FUI Fetch is a browser host capability. Use a native Rust networking library on desktop.";
constexpr char kFileError[] =
    "FUI browser file APIs are unavailable on desktop. Use native Rust file APIs or native file dialogs.";

UnsupportedFuiCallbacks DefaultCallbacks() {
    return {
        __fui_on_fetch_error,
        __fui_on_file_pick_result,
        __fui_on_file_read_result,
        __fui_on_file_save_result,
        __fui_on_file_writer_created,
        __fui_on_file_write_result,
        __fui_on_file_finish_result,
        __fui_on_file_worker_process_error,
    };
}

std::mutex g_mutex;
std::unordered_set<std::string> g_reported_symbols;
UnsupportedCapabilityDiagnostic g_diagnostic = nullptr;
UnsupportedFuiCallbacks g_callbacks = DefaultCallbacks();

void DefaultDiagnostic(const char* symbol, const char* guidance) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unsupported native FUI capability %s: %s", symbol, guidance);
}

std::uint32_t Length(const char* value) {
    return static_cast<std::uint32_t>(std::strlen(value));
}

} // namespace

void ReportUnsupportedFuiCapability(const char* symbol, const char* guidance) {
    UnsupportedCapabilityDiagnostic diagnostic = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_reported_symbols.emplace(symbol).second) {
            return;
        }
        diagnostic = g_diagnostic;
    }
    (diagnostic == nullptr ? DefaultDiagnostic : diagnostic)(symbol, guidance);
}

void RejectUnsupportedFetch(std::uint32_t request_id) {
    ReportUnsupportedFuiCapability("fui_fetch_start", kFetchError);
    UnsupportedFuiCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        callbacks = g_callbacks;
    }
    callbacks.fetch_error(request_id, reinterpret_cast<const std::uint8_t*>(kFetchError), Length(kFetchError));
}

void RejectUnsupportedFile(UnsupportedFileOperation operation, std::uint32_t request_id) {
    ReportUnsupportedFuiCapability("fui_file_api", kFileError);
    UnsupportedFuiCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        callbacks = g_callbacks;
    }
    const auto* message = reinterpret_cast<const std::uint8_t*>(kFileError);
    const std::uint32_t length = Length(kFileError);
    switch (operation) {
        case UnsupportedFileOperation::Pick:
            callbacks.file_pick_error(request_id, kUnsupportedFileStatus, message, length);
            break;
        case UnsupportedFileOperation::Read:
            callbacks.file_read_error(request_id, kUnsupportedFileStatus, 0U, 0U, message, length);
            break;
        case UnsupportedFileOperation::Save:
            callbacks.file_save_error(request_id, kUnsupportedFileStatus, 0U, message, length);
            break;
        case UnsupportedFileOperation::CreateWriter:
            callbacks.file_writer_create_error(request_id, kUnsupportedFileStatus, message, length);
            break;
        case UnsupportedFileOperation::Write:
            callbacks.file_write_error(request_id, kUnsupportedFileStatus, 0U, 0U, message, length);
            break;
        case UnsupportedFileOperation::Finish:
            callbacks.file_finish_error(request_id, kUnsupportedFileStatus, 0U, message, length);
            break;
        case UnsupportedFileOperation::ProcessInWorker:
            callbacks.file_process_error(request_id, kUnsupportedFileStatus, message, length);
            break;
    }
}

void SetUnsupportedCapabilityDiagnosticForTesting(UnsupportedCapabilityDiagnostic diagnostic) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_diagnostic = diagnostic;
    g_reported_symbols.clear();
}

void SetUnsupportedFuiCallbacksForTesting(const UnsupportedFuiCallbacks& callbacks) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_callbacks = callbacks;
}

void ResetUnsupportedFuiCapabilitiesForTesting() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_diagnostic = nullptr;
    g_callbacks = DefaultCallbacks();
    g_reported_symbols.clear();
}

} // namespace effindom::v2::native
