#include "NativeWorkerHost.h"

#include <utility>

namespace effindom::v2::native {
namespace {

NativeWorkerHost* active_worker_host = nullptr;

} // namespace

NativeWorkerHost::NativeWorkerHost(
    const NativeFuiRsWorkerRegistryEntry* entries,
    std::size_t entry_count,
    NativeWorkerCoordinator::UiPost post_to_ui,
    NativeWorkerHostCallbacks callbacks)
    : adapter_(std::make_shared<NativeFuiRsWorkerAdapter>(entries, entry_count)),
      coordinator_(std::make_unique<NativeWorkerCoordinator>(
          adapter_,
          std::move(post_to_ui),
          std::move(callbacks.progress),
          std::move(callbacks.complete),
          std::move(callbacks.error))) {}

NativeWorkerHost::~NativeWorkerHost() = default;

void NativeWorkerHost::SetSessionGeneration(std::uint64_t session_generation) {
    coordinator_->SetSessionGeneration(session_generation);
}

void NativeWorkerHost::Start(
    std::uint32_t worker_id,
    std::string artifact,
    std::string entry,
    std::string input) {
    coordinator_->Start(
        worker_id, std::move(artifact), std::move(entry), std::move(input));
}

void NativeWorkerHost::Cancel(std::uint32_t worker_id) { coordinator_->Cancel(worker_id); }
void NativeWorkerHost::Clear() { coordinator_->Clear(); }

void SetActiveNativeWorkerHost(NativeWorkerHost* host) { active_worker_host = host; }
NativeWorkerHost* ActiveNativeWorkerHost() { return active_worker_host; }

} // namespace effindom::v2::native
