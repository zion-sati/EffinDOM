#include "SdlUiDispatcher.h"

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

SdlUiDispatcher::SdlUiDispatcher(SDL_Window* window)
    : window_(window), event_type_(SDL_RegisterEvents(1)) {
    if (event_type_ == 0U) {
        throw std::runtime_error(std::string("SDL_RegisterEvents failed: ") + SDL_GetError());
    }
}

bool SdlUiDispatcher::Post(std::uint64_t callback_id) {
    return Enqueue(Operation::Run, callback_id);
}

bool SdlUiDispatcher::Cancel(std::uint64_t callback_id) {
    return Enqueue(Operation::Cancel, callback_id);
}

bool SdlUiDispatcher::PostTask(std::function<bool()> task) {
    if (!task) return false;
    std::uint64_t work_id = 0U;
    {
        std::lock_guard lock(mutex_);
        work_id = next_work_id_++;
        queue_.push_back(WorkItem{Operation::Task, 0U, std::move(task), work_id});
    }
    if (PushWakeEvent()) return true;
    std::lock_guard lock(mutex_);
    const auto iterator = std::find_if(queue_.begin(), queue_.end(), [=](const WorkItem& item) {
        return item.work_id == work_id;
    });
    if (iterator != queue_.end()) queue_.erase(iterator);
    return false;
}

bool SdlUiDispatcher::HandleEvent(const SDL_Event& event) {
    if (event.type != event_type_) return false;
    std::deque<WorkItem> work;
    {
        std::lock_guard lock(mutex_);
        work.swap(queue_);
    }
    bool rendered_work = false;
    for (WorkItem& item : work) {
        if (item.operation == Operation::Run) {
            rendered_work = __fui_run_ui_dispatch(item.callback_id) || rendered_work;
        } else if (item.operation == Operation::Cancel) {
            __fui_cancel_ui_dispatch(item.callback_id);
        } else {
            rendered_work = item.task() || rendered_work;
        }
    }
    return rendered_work;
}

void SdlUiDispatcher::Clear() {
    std::deque<WorkItem> discarded;
    {
        std::lock_guard lock(mutex_);
        discarded.swap(queue_);
    }
    for (const WorkItem& item : discarded) {
        if (item.operation != Operation::Task) __fui_cancel_ui_dispatch(item.callback_id);
    }
}

bool SdlUiDispatcher::Enqueue(Operation operation, std::uint64_t callback_id) {
    if (callback_id == 0U) return false;
    std::uint64_t work_id = 0U;
    {
        std::lock_guard lock(mutex_);
        work_id = next_work_id_++;
        queue_.push_back(WorkItem{operation, callback_id, {}, work_id});
    }
    if (PushWakeEvent()) return true;

    std::lock_guard lock(mutex_);
    const auto iterator = std::find_if(queue_.begin(), queue_.end(), [=](const WorkItem& item) {
        return item.work_id == work_id;
    });
    if (iterator != queue_.end()) queue_.erase(iterator);
    return false;
}

bool SdlUiDispatcher::PushWakeEvent() {
    SDL_Event event{};
    event.type = event_type_;
    event.user.windowID = SDL_GetWindowID(window_);
    return SDL_PushEvent(&event);
}

} // namespace effindom::v2::native
