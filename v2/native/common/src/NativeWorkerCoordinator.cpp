#include "NativeWorkerCoordinator.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace effindom::v2::native {
namespace {

enum class EnvelopeKind : std::uint8_t { Progress, Complete, Error };

struct WorkerRecord final {
    std::uint64_t generation = 0U;
    std::uint64_t session_generation = 0U;
    std::atomic<bool> cancelled = false;
    std::mutex cancellation_mutex;
    std::condition_variable cancellation_changed;
    std::atomic<bool> finished = false;
    bool terminal_queued = false;
};

void CancelRecord(const std::shared_ptr<WorkerRecord>& record) {
    record->cancelled.store(true);
    record->cancellation_changed.notify_all();
}

void FinishRecord(const std::shared_ptr<WorkerRecord>& record) {
    record->finished.store(true);
    record->cancellation_changed.notify_all();
}

struct WorkerThread final {
    std::shared_ptr<WorkerRecord> record;
    std::thread thread;
};

struct Envelope final {
    EnvelopeKind kind = EnvelopeKind::Error;
    std::uint32_t worker_id = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t session_generation = 0U;
    std::string text;
};

constexpr const char* kOversizedPayloadError =
    "Worker payload exceeds the maximum UTF-8 payload size of 1048576 bytes.";
constexpr const char* kMailboxOverflowError = "Worker message queue capacity exceeded.";

} // namespace

struct NativeWorkerCoordinator::State final : std::enable_shared_from_this<State> {
    State(
        std::shared_ptr<NativeWorkerLanguageAdapter> worker_adapter,
        UiPost ui_post,
        Dispatch progress_dispatch,
        Dispatch complete_dispatch,
        Dispatch error_dispatch)
        : adapter(std::move(worker_adapter)),
          post(std::move(ui_post)),
          progress(std::move(progress_dispatch)),
          complete(std::move(complete_dispatch)),
          error(std::move(error_dispatch)) {}

    std::shared_ptr<NativeWorkerLanguageAdapter> adapter;
    UiPost post;
    Dispatch progress;
    Dispatch complete;
    Dispatch error;
    std::mutex mutex;
    std::unordered_map<std::uint32_t, std::shared_ptr<WorkerRecord>> workers;
    std::unordered_map<std::uint64_t, std::shared_ptr<WorkerRecord>> cancelled_workers;
    std::deque<Envelope> mailbox;
    std::vector<WorkerThread> threads;
    std::uint64_t session_generation = 1U;
    std::uint64_t next_generation = 1U;
    bool wake_queued = false;
    bool alive = true;
    bool clearing = false;

    std::vector<WorkerThread> TakeFinishedThreads() {
        std::vector<WorkerThread> finished;
        std::lock_guard lock(mutex);
        for (auto iterator = threads.begin(); iterator != threads.end();) {
            if (iterator->record->finished.load()) {
                if (!iterator->record->terminal_queued) {
                    cancelled_workers.erase(iterator->record->generation);
                }
                finished.push_back(std::move(*iterator));
                iterator = threads.erase(iterator);
            } else {
                ++iterator;
            }
        }
        return finished;
    }

    void RevokeAllLocked() {
        for (const auto& [worker_id, record] : workers) {
            static_cast<void>(worker_id);
            CancelRecord(record);
        }
        for (const auto& [generation, record] : cancelled_workers) {
            static_cast<void>(generation);
            CancelRecord(record);
        }
        workers.clear();
        cancelled_workers.clear();
        mailbox.clear();
        wake_queued = false;
    }

    bool IsCurrent(
        std::uint32_t worker_id,
        std::uint64_t generation,
        std::uint64_t expected_session) const {
        const auto iterator = workers.find(worker_id);
        return alive && expected_session == session_generation && iterator != workers.end() &&
            iterator->second->generation == generation &&
            iterator->second->session_generation == expected_session;
    }

    bool IsCooperativeCancellationError(
        const std::shared_ptr<WorkerRecord>& record,
        std::uint32_t worker_id,
        EnvelopeKind kind) const {
        const auto cancelled = cancelled_workers.find(record->generation);
        return alive && kind == EnvelopeKind::Error && record->cancelled.load() &&
            record->session_generation == session_generation &&
            cancelled != cancelled_workers.end() && cancelled->second == record &&
            workers.find(worker_id) == workers.end();
    }

    void Queue(
        const std::shared_ptr<WorkerRecord>& record,
        std::uint32_t worker_id,
        EnvelopeKind kind,
        std::string text) {
        bool should_post = false;
        {
            std::lock_guard lock(mutex);
            const bool current =
                IsCurrent(worker_id, record->generation, record->session_generation);
            const bool cancellation_error =
                IsCooperativeCancellationError(record, worker_id, kind);
            if (!current && !cancellation_error) return;
            if (record->terminal_queued || (record->cancelled && kind == EnvelopeKind::Progress)) {
                return;
            }
            if (text.size() > kNativeWorkerMaxPayloadBytes) {
                kind = EnvelopeKind::Error;
                text = kOversizedPayloadError;
            }
            if (mailbox.size() >= kNativeWorkerMailboxCapacity) {
                kind = EnvelopeKind::Error;
                text = kMailboxOverflowError;
                mailbox.pop_back();
            }
            if (kind != EnvelopeKind::Progress) record->terminal_queued = true;
            mailbox.push_back(Envelope{
                kind,
                worker_id,
                record->generation,
                record->session_generation,
                std::move(text),
            });
            if (!wake_queued) {
                wake_queued = true;
                should_post = true;
            }
        }
        if (!should_post) return;
        const std::weak_ptr<State> weak_state = shared_from_this();
        if (!post([weak_state] {
                const auto state = weak_state.lock();
                if (state != nullptr) state->Drain();
                return false;
            })) {
            std::lock_guard lock(mutex);
            wake_queued = false;
            mailbox.clear();
        }
    }

    void Drain() {
        std::deque<Envelope> pending;
        {
            std::lock_guard lock(mutex);
            if (!alive) return;
            pending.swap(mailbox);
            wake_queued = false;
        }
        for (const Envelope& envelope : pending) {
            Dispatch dispatch;
            {
                std::lock_guard lock(mutex);
                const bool current = IsCurrent(
                    envelope.worker_id,
                    envelope.generation,
                    envelope.session_generation);
                const auto cancelled = cancelled_workers.find(envelope.generation);
                const bool cancellation_error =
                    envelope.kind == EnvelopeKind::Error &&
                    envelope.session_generation == session_generation &&
                    cancelled != cancelled_workers.end() &&
                    cancelled->second->cancelled.load() &&
                    workers.find(envelope.worker_id) == workers.end();
                if (!current && !cancellation_error) {
                    continue;
                }
                const auto record = current ? workers.at(envelope.worker_id) : cancelled->second;
                if (record->cancelled && envelope.kind == EnvelopeKind::Progress) continue;
                switch (envelope.kind) {
                    case EnvelopeKind::Progress: dispatch = progress; break;
                    case EnvelopeKind::Complete: dispatch = complete; break;
                    case EnvelopeKind::Error: dispatch = error; break;
                }
                if (envelope.kind != EnvelopeKind::Progress) {
                    if (current) {
                        workers.erase(envelope.worker_id);
                    } else {
                        cancelled_workers.erase(envelope.generation);
                    }
                }
            }
            dispatch(envelope.worker_id, envelope.text);
        }
    }
};

void JoinFinishedThreads(std::vector<WorkerThread> threads) {
    for (auto& worker : threads) {
        if (worker.thread.joinable()) worker.thread.join();
    }
}

void DisposeWorkerThreads(std::vector<WorkerThread> threads) {
    const auto deadline = std::chrono::steady_clock::now() + kNativeWorkerShutdownTimeout;
    std::size_t detached = 0U;
    for (auto& worker : threads) {
        if (!worker.record->finished.load()) {
            std::unique_lock lock(worker.record->cancellation_mutex);
            worker.record->cancellation_changed.wait_until(lock, deadline, [&] {
                return worker.record->finished.load();
            });
        }
        if (!worker.thread.joinable()) continue;
        if (worker.record->finished.load()) {
            worker.thread.join();
        } else {
            worker.thread.detach();
            ++detached;
        }
    }
    if (detached != 0U) {
        std::fprintf(
            stderr,
            "EffinDOM detached %zu non-cooperative native Worker thread(s) after capability revocation.\n",
            detached);
    }
}

class NativeWorkerReporterImpl final : public NativeWorkerReporter {
public:
    NativeWorkerReporterImpl(
        std::weak_ptr<NativeWorkerCoordinator::State> state,
        std::shared_ptr<WorkerRecord> record,
        std::uint32_t worker_id)
        : state_(std::move(state)), record_(std::move(record)), worker_id_(worker_id) {}

    bool IsCancelled() const override {
        if (record_->cancelled.load()) return true;
        const auto state = state_.lock();
        if (state == nullptr) return true;
        std::lock_guard lock(state->mutex);
        return !state->IsCurrent(
            worker_id_, record_->generation, record_->session_generation);
    }

    bool WaitForCancellation(std::chrono::milliseconds timeout) override {
        std::unique_lock lock(record_->cancellation_mutex);
        return record_->cancellation_changed.wait_for(lock, timeout, [&] {
            return record_->cancelled.load();
        });
    }

    void Progress(std::string text) override { Queue(EnvelopeKind::Progress, std::move(text)); }
    void Complete(std::string text) override { Queue(EnvelopeKind::Complete, std::move(text)); }
    void Error(std::string text) override { Queue(EnvelopeKind::Error, std::move(text)); }

private:
    void Queue(EnvelopeKind kind, std::string text) {
        const auto state = state_.lock();
        if (state != nullptr) state->Queue(record_, worker_id_, kind, std::move(text));
    }

    std::weak_ptr<NativeWorkerCoordinator::State> state_;
    std::shared_ptr<WorkerRecord> record_;
    std::uint32_t worker_id_;
};

NativeWorkerCoordinator::NativeWorkerCoordinator(
    std::shared_ptr<NativeWorkerLanguageAdapter> adapter,
    UiPost post_to_ui,
    Dispatch progress,
    Dispatch complete,
    Dispatch error)
    : state_(std::make_shared<State>(
          std::move(adapter),
          std::move(post_to_ui),
          std::move(progress),
          std::move(complete),
          std::move(error))) {}

NativeWorkerCoordinator::~NativeWorkerCoordinator() {
    std::vector<WorkerThread> threads;
    {
        std::lock_guard lock(state_->mutex);
        state_->alive = false;
        state_->clearing = true;
        state_->RevokeAllLocked();
        threads.swap(state_->threads);
    }
    DisposeWorkerThreads(std::move(threads));
}

void NativeWorkerCoordinator::SetSessionGeneration(std::uint64_t session_generation) {
    Clear();
    std::lock_guard lock(state_->mutex);
    if (state_->alive) state_->session_generation = session_generation;
}

void NativeWorkerCoordinator::Start(
    std::uint32_t worker_id,
    std::string artifact,
    std::string entry,
    std::string input) {
    std::shared_ptr<WorkerRecord> record;
    NativeWorkerStartRequest request;
    JoinFinishedThreads(state_->TakeFinishedThreads());
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->alive || state_->clearing) return;
        if (state_->workers.find(worker_id) != state_->workers.end()) {
            record = state_->workers.at(worker_id);
        } else {
            record = std::make_shared<WorkerRecord>();
            record->generation = state_->next_generation++;
            record->session_generation = state_->session_generation;
            state_->workers.emplace(worker_id, record);
            request = NativeWorkerStartRequest{
                worker_id,
                record->generation,
                record->session_generation,
                std::move(artifact),
                std::move(entry),
                std::move(input),
            };
        }
    }
    if (request.generation == 0U) {
        state_->Queue(record, worker_id, EnvelopeKind::Error, "Worker already started.");
        return;
    }
    if (request.input.size() > kNativeWorkerMaxPayloadBytes) {
        state_->Queue(record, worker_id, EnvelopeKind::Error, kOversizedPayloadError);
        return;
    }
    const std::weak_ptr<State> weak_state = state_;
    auto adapter = state_->adapter;
    std::thread thread([weak_state, adapter, record, request = std::move(request)] {
        NativeWorkerReporterImpl reporter(weak_state, record, request.worker_id);
        try {
            adapter->Invoke(request, reporter);
        } catch (const std::exception& exception) {
            reporter.Error(exception.what());
        } catch (...) {
            reporter.Error("Worker entry failed with an unknown native exception.");
        }
        FinishRecord(record);
    });
    std::lock_guard lock(state_->mutex);
    state_->threads.push_back(WorkerThread{record, std::move(thread)});
}

void NativeWorkerCoordinator::Cancel(std::uint32_t worker_id) {
    {
        std::lock_guard lock(state_->mutex);
        const auto iterator = state_->workers.find(worker_id);
        if (iterator == state_->workers.end()) return;
        const auto record = iterator->second;
        CancelRecord(record);
        state_->workers.erase(iterator);
        state_->cancelled_workers.emplace(record->generation, record);
        state_->mailbox.erase(
            std::remove_if(
                state_->mailbox.begin(),
                state_->mailbox.end(),
                [&](const Envelope& envelope) { return envelope.worker_id == worker_id; }),
            state_->mailbox.end());
        if (state_->mailbox.empty()) state_->wake_queued = false;
    }
    JoinFinishedThreads(state_->TakeFinishedThreads());
}

void NativeWorkerCoordinator::Clear() {
    std::vector<WorkerThread> threads;
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->alive || state_->clearing) return;
        state_->clearing = true;
        state_->RevokeAllLocked();
        threads.swap(state_->threads);
    }
    DisposeWorkerThreads(std::move(threads));
    std::lock_guard lock(state_->mutex);
    if (state_->alive) state_->clearing = false;
}

} // namespace effindom::v2::native
