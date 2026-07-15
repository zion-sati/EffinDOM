#include "MacosUiDispatcher.h"

#include "SDL3/SDL.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>

extern "C" {
bool __fui_run_ui_dispatch(std::uint64_t callback_id);
void __fui_cancel_ui_dispatch(std::uint64_t callback_id);
}

namespace effindom::v2::native {

MacosUiDispatcher::MacosUiDispatcher(SDL_Window* window)
    : window_(window), event_type_(SDL_RegisterEvents(1)) {
    if (event_type_ == 0U) {
        throw std::runtime_error(std::string("SDL_RegisterEvents failed: ") + SDL_GetError());
    }
}

bool MacosUiDispatcher::Post(std::uint64_t callback_id) {
    return Enqueue(Operation::Run, callback_id);
}

bool MacosUiDispatcher::Cancel(std::uint64_t callback_id) {
    return Enqueue(Operation::Cancel, callback_id);
}

bool MacosUiDispatcher::HandleEvent(const SDL_Event& event) {
    if (event.type != event_type_) return false;
    std::deque<WorkItem> work;
    {
        std::lock_guard lock(mutex_);
        work.swap(queue_);
    }
    bool rendered_work = false;
    for (const WorkItem& item : work) {
        if (item.operation == Operation::Run) {
            rendered_work = __fui_run_ui_dispatch(item.callback_id) || rendered_work;
        } else {
            __fui_cancel_ui_dispatch(item.callback_id);
        }
    }
    return rendered_work;
}

void MacosUiDispatcher::Clear() {
    std::deque<WorkItem> discarded;
    {
        std::lock_guard lock(mutex_);
        discarded.swap(queue_);
    }
    for (const WorkItem& item : discarded) {
        __fui_cancel_ui_dispatch(item.callback_id);
    }
}

bool MacosUiDispatcher::Enqueue(Operation operation, std::uint64_t callback_id) {
    if (callback_id == 0U) return false;
    {
        std::lock_guard lock(mutex_);
        queue_.push_back(WorkItem{operation, callback_id});
    }
    SDL_Event event{};
    event.type = event_type_;
    event.user.windowID = SDL_GetWindowID(window_);
    if (SDL_PushEvent(&event)) return true;

    std::lock_guard lock(mutex_);
    const auto iterator = std::find_if(queue_.rbegin(), queue_.rend(), [=](const WorkItem& item) {
        return item.operation == operation && item.callback_id == callback_id;
    });
    if (iterator != queue_.rend()) queue_.erase(std::next(iterator).base());
    return false;
}

} // namespace effindom::v2::native
