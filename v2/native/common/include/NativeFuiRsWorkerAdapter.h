#pragma once

#include "NativeWorkerCoordinator.h"

#include <cstddef>
#include <cstdint>

namespace effindom::v2::native {

using NativeFuiRsWorkerEntry = void (*)(std::uintptr_t input, std::uint32_t length);

struct NativeFuiRsWorkerHostServiceEntry final {
    const std::uint8_t* name = nullptr;
};

struct NativeFuiRsWorkerRegistryEntry final {
    const std::uint8_t* artifact = nullptr;
    const std::uint8_t* entry = nullptr;
    const NativeFuiRsWorkerHostServiceEntry* host_services = nullptr;
    std::size_t host_service_count = 0U;
    NativeFuiRsWorkerEntry invoke = nullptr;
};

class NativeFuiRsWorkerAdapter final : public NativeWorkerLanguageAdapter {
public:
    NativeFuiRsWorkerAdapter(
        const NativeFuiRsWorkerRegistryEntry* entries,
        std::size_t entry_count);

    void Invoke(
        const NativeWorkerStartRequest& request,
        NativeWorkerReporter& reporter) override;

private:
    const NativeFuiRsWorkerRegistryEntry* entries_;
    std::size_t entry_count_;
};

} // namespace effindom::v2::native

extern "C" {
const effindom::v2::native::NativeFuiRsWorkerRegistryEntry* fui_native_worker_registry(
    std::size_t* count);
void fui_native_worker_report_progress(const std::uint8_t* text, std::uint32_t length);
void fui_native_worker_complete_string(const std::uint8_t* text, std::uint32_t length);
void fui_native_worker_fail(const std::uint8_t* text, std::uint32_t length);
bool fui_native_worker_is_cancelled();
void fui_native_worker_request_yield(std::int32_t delay_ms);
bool fui_native_worker_host_service_is_allowed(const std::uint8_t* name, std::uint32_t length);
}
