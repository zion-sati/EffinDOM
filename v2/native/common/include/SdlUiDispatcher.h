#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

class SdlUiDispatcher final {
public:
    explicit SdlUiDispatcher(SDL_Window* window);

    bool Post(std::uint64_t callback_id);
    bool Cancel(std::uint64_t callback_id);
    bool PostTask(std::function<bool()> task);
    bool HandleEvent(const SDL_Event& event);
    void Clear();

private:
    enum class Operation : std::uint8_t { Run, Cancel, Task };
    struct WorkItem {
        Operation operation;
        std::uint64_t callback_id = 0U;
        std::function<bool()> task;
        std::uint64_t work_id = 0U;
    };

    bool Enqueue(Operation operation, std::uint64_t callback_id);
    bool PushWakeEvent();

    SDL_Window* window_;
    std::uint32_t event_type_;
    std::mutex mutex_;
    std::deque<WorkItem> queue_;
    std::uint64_t next_work_id_ = 1U;
};

} // namespace effindom::v2::native
