#include "SdlFileDialogs.h"

#include "SDL3/SDL.h"

#include <stdexcept>

namespace effindom::v2::native {

struct SdlFileDialogs::Request {
    std::shared_ptr<SharedState> state;
    std::uint64_t generation = 0U;
    NativeFileDialogKind kind = NativeFileDialogKind::OpenFile;
    std::uint64_t request_id = 0U;
    std::vector<std::string> filter_names;
    std::vector<std::string> filter_patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string default_location;
    bool allow_multiple = false;
};

struct SdlFileDialogs::SharedState {
    SDL_Window* window = nullptr;
    std::uint32_t event_type = 0U;
    std::mutex mutex;
    std::uint64_t generation = 1U;
    bool owner_alive = true;
    std::unordered_set<std::uint64_t> active_requests;
    std::deque<NativeFileDialogCompletion> completions;
};

namespace {

std::vector<std::string> SplitNullSeparated(std::string_view encoded) {
    std::vector<std::string> values;
    std::size_t start = 0U;
    while (start < encoded.size()) {
        const std::size_t end = encoded.find('\0', start);
        const std::size_t length = (end == std::string_view::npos ? encoded.size() : end) - start;
        values.emplace_back(encoded.substr(start, length));
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return values;
}

} // namespace

SdlFileDialogs::SdlFileDialogs(
    SDL_Window* window,
    bool show_native_dialogs,
    CompletionSink completion_sink)
    : window_(window),
      show_native_dialogs_(show_native_dialogs),
      completion_sink_(std::move(completion_sink)),
      event_type_(SDL_RegisterEvents(1)),
      state_(std::make_shared<SharedState>()) {
    if (event_type_ == 0U) {
        throw std::runtime_error(std::string("SDL_RegisterEvents failed: ") + SDL_GetError());
    }
    state_->window = window_;
    state_->event_type = event_type_;
}

SdlFileDialogs::~SdlFileDialogs() {
    Clear();
    std::lock_guard lock(state_->mutex);
    state_->owner_alive = false;
}

bool SdlFileDialogs::Show(
    NativeFileDialogKind kind,
    std::uint64_t request_id,
    std::string_view encoded_filters,
    std::string_view default_location,
    bool allow_multiple) {
    if (request_id == 0U) return false;
    auto request = std::make_unique<Request>();
    request->state = state_;
    request->kind = kind;
    request->request_id = request_id;
    request->default_location = default_location;
    request->allow_multiple = allow_multiple;

    const auto filter_values = SplitNullSeparated(encoded_filters);
    if (filter_values.size() % 2U != 0U) return false;
    for (std::size_t index = 0U; index < filter_values.size(); index += 2U) {
        if (filter_values[index].empty() || filter_values[index + 1U].empty()) return false;
        request->filter_names.push_back(filter_values[index]);
        request->filter_patterns.push_back(filter_values[index + 1U]);
    }
    request->filters.reserve(request->filter_names.size());
    for (std::size_t index = 0U; index < request->filter_names.size(); ++index) {
        request->filters.push_back(SDL_DialogFileFilter{
            request->filter_names[index].c_str(),
            request->filter_patterns[index].c_str(),
        });
    }

    {
        std::lock_guard lock(state_->mutex);
        if (!state_->owner_alive || state_->active_requests.find(request_id) != state_->active_requests.end()) {
            return false;
        }
        request->generation = state_->generation;
        state_->active_requests.insert(request_id);
    }
    Request* request_ptr = request.release();
    if (!show_native_dialogs_) {
        delete request_ptr;
        return true;
    }

    const SDL_DialogFileFilter* filters = request_ptr->filters.empty() ? nullptr : request_ptr->filters.data();
    const int filter_count = static_cast<int>(request_ptr->filters.size());
    const char* location = request_ptr->default_location.empty() ? nullptr : request_ptr->default_location.c_str();
    switch (kind) {
        case NativeFileDialogKind::OpenFile:
            SDL_ShowOpenFileDialog(DialogCompleted, request_ptr, window_, filters, filter_count, location, allow_multiple);
            break;
        case NativeFileDialogKind::SaveFile:
            SDL_ShowSaveFileDialog(DialogCompleted, request_ptr, window_, filters, filter_count, location);
            break;
        case NativeFileDialogKind::OpenFolder:
            SDL_ShowOpenFolderDialog(DialogCompleted, request_ptr, window_, location, allow_multiple);
            break;
    }
    return true;
}

bool SdlFileDialogs::HandleEvent(const SDL_Event& event) {
    if (event.type != event_type_) return false;
    std::deque<NativeFileDialogCompletion> completions;
    {
        std::lock_guard lock(state_->mutex);
        completions.swap(state_->completions);
    }
    for (const auto& completion : completions) completion_sink_(completion);
    return !completions.empty();
}

void SdlFileDialogs::Clear() {
    std::lock_guard lock(state_->mutex);
    ++state_->generation;
    state_->active_requests.clear();
    state_->completions.clear();
}

void SdlFileDialogs::CompleteForTesting(NativeFileDialogCompletion completion) {
    std::uint64_t generation = 0U;
    {
        std::lock_guard lock(state_->mutex);
        generation = state_->generation;
    }
    QueueCompletion(state_, generation, std::move(completion));
}

void SdlFileDialogs::DialogCompleted(void* userdata, const char* const* file_list, int filter) {
    const std::unique_ptr<Request> request(static_cast<Request*>(userdata));
    NativeFileDialogCompletion completion{};
    completion.request_id = request->request_id;
    completion.selected_filter = filter;
    if (file_list == nullptr) {
        completion.status = NativeFileDialogStatus::Error;
        completion.error = SDL_GetError();
    } else if (file_list[0] == nullptr) {
        completion.status = NativeFileDialogStatus::Cancelled;
    } else {
        completion.status = NativeFileDialogStatus::Selected;
        for (std::size_t index = 0U; file_list[index] != nullptr; ++index) {
            completion.paths.emplace_back(file_list[index]);
        }
    }
    QueueCompletion(request->state, request->generation, std::move(completion));
}

void SdlFileDialogs::QueueCompletion(
    const std::shared_ptr<SharedState>& state,
    std::uint64_t generation,
    NativeFileDialogCompletion completion) {
    SDL_Window* window = nullptr;
    std::uint32_t event_type = 0U;
    {
        std::lock_guard lock(state->mutex);
        if (!state->owner_alive || generation != state->generation ||
            state->active_requests.erase(completion.request_id) == 0U) {
            return;
        }
        state->completions.push_back(std::move(completion));
        window = state->window;
        event_type = state->event_type;
    }
    SDL_Event event{};
    event.type = event_type;
    event.user.windowID = window == nullptr ? 0U : SDL_GetWindowID(window);
    SDL_PushEvent(&event);
}

} // namespace effindom::v2::native
