#pragma once

#include "NativeFuiRsWorkerAdapter.h"
#include "NativeWorkerCoordinator.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace effindom::v2::native {

struct NativeWorkerHostCallbacks final {
    NativeWorkerCoordinator::Dispatch progress;
    NativeWorkerCoordinator::Dispatch complete;
    NativeWorkerCoordinator::Dispatch error;
};

class NativeWorkerHost final {
public:
    NativeWorkerHost(
        const NativeFuiRsWorkerRegistryEntry* entries,
        std::size_t entry_count,
        NativeWorkerCoordinator::UiPost post_to_ui,
        NativeWorkerHostCallbacks callbacks);
    ~NativeWorkerHost();

    NativeWorkerHost(const NativeWorkerHost&) = delete;
    NativeWorkerHost& operator=(const NativeWorkerHost&) = delete;

    void SetSessionGeneration(std::uint64_t session_generation);
    void Start(
        std::uint32_t worker_id,
        std::string artifact,
        std::string entry,
        std::string input);
    void Cancel(std::uint32_t worker_id);
    void Clear();

private:
    std::shared_ptr<NativeFuiRsWorkerAdapter> adapter_;
    std::unique_ptr<NativeWorkerCoordinator> coordinator_;
};

void SetActiveNativeWorkerHost(NativeWorkerHost* host);
NativeWorkerHost* ActiveNativeWorkerHost();

} // namespace effindom::v2::native
