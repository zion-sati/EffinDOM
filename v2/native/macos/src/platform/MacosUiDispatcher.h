#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

class MacosUiDispatcher final {
public:
    explicit MacosUiDispatcher(SDL_Window* window);

    bool Post(std::uint64_t callback_id);
    bool Cancel(std::uint64_t callback_id);
    bool HandleEvent(const SDL_Event& event);
    void Clear();

private:
    enum class Operation : std::uint8_t { Run, Cancel };
    struct WorkItem {
        Operation operation;
        std::uint64_t callback_id;
    };

    bool Enqueue(Operation operation, std::uint64_t callback_id);

    SDL_Window* window_;
    std::uint32_t event_type_;
    std::mutex mutex_;
    std::deque<WorkItem> queue_;
};

} // namespace effindom::v2::native
