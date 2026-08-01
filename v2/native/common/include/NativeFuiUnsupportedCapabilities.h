#pragma once

#include <cstdint>

namespace effindom::v2::native {

enum class UnsupportedFileOperation {
    Pick,
    Read,
    Save,
    CreateWriter,
    Write,
    Finish,
    ProcessInWorker,
};

using UnsupportedCapabilityDiagnostic = void (*)(const char* symbol, const char* guidance);

struct UnsupportedFuiCallbacks {
    void (*fetch_error)(std::uint32_t, const std::uint8_t*, std::uint32_t);
    void (*file_pick_error)(std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t);
    void (*file_read_error)(
        std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
        const std::uint8_t*, std::uint32_t);
    void (*file_save_error)(
        std::uint32_t, std::uint32_t, std::uint64_t, const std::uint8_t*, std::uint32_t);
    void (*file_writer_create_error)(
        std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t);
    void (*file_write_error)(
        std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t,
        const std::uint8_t*, std::uint32_t);
    void (*file_finish_error)(
        std::uint32_t, std::uint32_t, std::uint64_t, const std::uint8_t*, std::uint32_t);
    void (*file_process_error)(
        std::uint32_t, std::uint32_t, const std::uint8_t*, std::uint32_t);
};

void ReportUnsupportedFuiCapability(const char* symbol, const char* guidance);
void RejectUnsupportedFetch(std::uint32_t request_id);
void RejectUnsupportedFile(UnsupportedFileOperation operation, std::uint32_t request_id);

void SetUnsupportedCapabilityDiagnosticForTesting(UnsupportedCapabilityDiagnostic diagnostic);
void SetUnsupportedFuiCallbacksForTesting(const UnsupportedFuiCallbacks& callbacks);
void ResetUnsupportedFuiCapabilitiesForTesting();

} // namespace effindom::v2::native
