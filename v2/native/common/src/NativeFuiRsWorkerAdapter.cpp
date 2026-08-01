#include "NativeFuiRsWorkerAdapter.h"

#include "NativeUtf8.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

namespace effindom::v2::native {
namespace {

constexpr char kUnknownEntryError[] = "Native Worker bundle or entry is not registered.";
constexpr char kNoTerminalOrYieldError[] =
    "Worker exited without calling Worker.complete(...), Worker.fail(...), or Worker.yield(...).";

class ExecutionContext final {
public:
    ExecutionContext(
        NativeWorkerReporter& reporter,
        const NativeFuiRsWorkerHostServiceEntry* host_services,
        std::size_t host_service_count)
        : reporter_(reporter), host_services_(host_services), host_service_count_(host_service_count) {}

    void ResetInvocation() {
        yield_requested_ = false;
        yield_delay_ms_ = 0;
    }

    void Progress(const std::uint8_t* text, std::uint32_t length) {
        if (!terminal_) reporter_.Progress(Utf8(text, length));
    }

    void Complete(const std::uint8_t* text, std::uint32_t length) {
        if (terminal_) return;
        terminal_ = true;
        reporter_.Complete(Utf8(text, length));
    }

    void Fail(const std::uint8_t* text, std::uint32_t length) {
        if (terminal_) return;
        terminal_ = true;
        reporter_.Error(Utf8(text, length));
    }

    bool IsCancelled() const { return reporter_.IsCancelled(); }

    void RequestYield(std::int32_t delay_ms) {
        if (terminal_) return;
        yield_requested_ = true;
        yield_delay_ms_ = std::max(delay_ms, 0);
    }

    bool Terminal() const { return terminal_; }
    bool YieldRequested() const { return yield_requested_; }
    std::int32_t YieldDelayMs() const { return yield_delay_ms_; }
    bool WaitForCancellation(std::chrono::milliseconds timeout) {
        return reporter_.WaitForCancellation(timeout);
    }

    bool HostServiceAllowed(const std::uint8_t* name, std::uint32_t length) const {
        if (reporter_.IsCancelled() || name == nullptr || length == 0U) return false;
        for (std::size_t index = 0; index < host_service_count_; ++index) {
            const auto* allowed = host_services_[index].name;
            if (allowed == nullptr) continue;
            const auto allowed_length = std::strlen(reinterpret_cast<const char*>(allowed));
            if (allowed_length == length && std::memcmp(allowed, name, length) == 0) return true;
        }
        return false;
    }

private:
    NativeWorkerReporter& reporter_;
    const NativeFuiRsWorkerHostServiceEntry* host_services_;
    std::size_t host_service_count_;
    bool terminal_ = false;
    bool yield_requested_ = false;
    std::int32_t yield_delay_ms_ = 0;
};

thread_local ExecutionContext* active_context = nullptr;

class ScopedExecutionContext final {
public:
    explicit ScopedExecutionContext(ExecutionContext& context) : previous_(active_context) {
        active_context = &context;
    }
    ~ScopedExecutionContext() { active_context = previous_; }

private:
    ExecutionContext* previous_;
};

bool Matches(const std::uint8_t* value, const std::string& expected) {
    return value != nullptr && std::strcmp(reinterpret_cast<const char*>(value), expected.c_str()) == 0;
}

ExecutionContext* ActiveContext() { return active_context; }

} // namespace

NativeFuiRsWorkerAdapter::NativeFuiRsWorkerAdapter(
    const NativeFuiRsWorkerRegistryEntry* entries,
    std::size_t entry_count)
    : entries_(entries), entry_count_(entry_count) {}

void NativeFuiRsWorkerAdapter::Invoke(
    const NativeWorkerStartRequest& request,
    NativeWorkerReporter& reporter) {
    const NativeFuiRsWorkerRegistryEntry* selected = nullptr;
    for (std::size_t index = 0; index < entry_count_; ++index) {
        const auto& candidate = entries_[index];
        if (Matches(candidate.artifact, request.artifact) &&
            Matches(candidate.entry, request.entry)) {
            selected = &candidate;
            break;
        }
    }
    if (selected == nullptr || selected->invoke == nullptr) {
        reporter.Error(kUnknownEntryError);
        return;
    }

    ExecutionContext context(reporter, selected->host_services, selected->host_service_count);
    ScopedExecutionContext active(context);
    bool first_invocation = true;
    bool cancellation_invoked = false;
    while (!context.Terminal()) {
        const bool cancelled_before_invocation = context.IsCancelled();
        if (cancelled_before_invocation) {
            if (cancellation_invoked) return;
            cancellation_invoked = true;
        }
        context.ResetInvocation();
        const auto* input = reinterpret_cast<const std::uint8_t*>(request.input.data());
        selected->invoke(
            first_invocation && !request.input.empty()
                ? reinterpret_cast<std::uintptr_t>(input)
                : 0U,
            first_invocation ? static_cast<std::uint32_t>(request.input.size()) : 0U);
        first_invocation = false;
        if (context.Terminal()) return;
        if (context.IsCancelled()) {
            if (cancelled_before_invocation) return;
            continue;
        }
        if (!context.YieldRequested()) {
            reporter.Error(kNoTerminalOrYieldError);
            return;
        }
        const auto delay = std::chrono::milliseconds(
            context.YieldDelayMs() > 0 ? context.YieldDelayMs() : 1);
        context.WaitForCancellation(delay);
    }
}

} // namespace effindom::v2::native

extern "C" {

void fui_native_worker_report_progress(const std::uint8_t* text, std::uint32_t length) {
    if (auto* context = effindom::v2::native::ActiveContext(); context != nullptr) {
        context->Progress(text, length);
    }
}

void fui_native_worker_complete_string(const std::uint8_t* text, std::uint32_t length) {
    if (auto* context = effindom::v2::native::ActiveContext(); context != nullptr) {
        context->Complete(text, length);
    }
}

void fui_native_worker_fail(const std::uint8_t* text, std::uint32_t length) {
    if (auto* context = effindom::v2::native::ActiveContext(); context != nullptr) {
        context->Fail(text, length);
    }
}

bool fui_native_worker_is_cancelled() {
    const auto* context = effindom::v2::native::ActiveContext();
    return context == nullptr || context->IsCancelled();
}

void fui_native_worker_request_yield(std::int32_t delay_ms) {
    if (auto* context = effindom::v2::native::ActiveContext(); context != nullptr) {
        context->RequestYield(delay_ms);
    }
}

bool fui_native_worker_host_service_is_allowed(const std::uint8_t* name, std::uint32_t length) {
    const auto* context = effindom::v2::native::ActiveContext();
    return context != nullptr && context->HostServiceAllowed(name, length);
}

}
