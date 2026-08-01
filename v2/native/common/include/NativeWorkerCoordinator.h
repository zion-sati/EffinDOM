#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace effindom::v2::native {

constexpr std::size_t kNativeWorkerMaxPayloadBytes = 1024U * 1024U;
constexpr std::size_t kNativeWorkerMailboxCapacity = 256U;
constexpr auto kNativeWorkerShutdownTimeout = std::chrono::milliseconds(250);

struct NativeWorkerStartRequest final {
    std::uint32_t worker_id = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t session_generation = 0U;
    std::string artifact;
    std::string entry;
    std::string input;
};

class NativeWorkerReporter {
public:
    virtual ~NativeWorkerReporter() = default;
    virtual bool IsCancelled() const = 0;
    virtual bool WaitForCancellation(std::chrono::milliseconds timeout) = 0;
    virtual void Progress(std::string text) = 0;
    virtual void Complete(std::string text) = 0;
    virtual void Error(std::string text) = 0;
};

class NativeWorkerLanguageAdapter {
public:
    virtual ~NativeWorkerLanguageAdapter() = default;
    virtual void Invoke(const NativeWorkerStartRequest& request, NativeWorkerReporter& reporter) = 0;
};

class NativeWorkerReporterImpl;

class NativeWorkerCoordinator final {
public:
    using UiTask = std::function<bool()>;
    using UiPost = std::function<bool(UiTask)>;
    using Dispatch = std::function<void(std::uint32_t, const std::string&)>;

    NativeWorkerCoordinator(
        std::shared_ptr<NativeWorkerLanguageAdapter> adapter,
        UiPost post_to_ui,
        Dispatch progress,
        Dispatch complete,
        Dispatch error);
    ~NativeWorkerCoordinator();

    NativeWorkerCoordinator(const NativeWorkerCoordinator&) = delete;
    NativeWorkerCoordinator& operator=(const NativeWorkerCoordinator&) = delete;

    void SetSessionGeneration(std::uint64_t session_generation);
    void Start(
        std::uint32_t worker_id,
        std::string artifact,
        std::string entry,
        std::string input);
    void Cancel(std::uint32_t worker_id);
    void Clear();

private:
    friend class NativeWorkerReporterImpl;
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace effindom::v2::native
