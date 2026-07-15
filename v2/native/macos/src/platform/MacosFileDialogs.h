#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

enum class NativeFileDialogKind : std::uint32_t { OpenFile = 0U, SaveFile = 1U, OpenFolder = 2U };
enum class NativeFileDialogStatus : std::uint32_t { Selected = 0U, Cancelled = 1U, Error = 2U };

struct NativeFileDialogCompletion {
    std::uint64_t request_id = 0U;
    NativeFileDialogStatus status = NativeFileDialogStatus::Cancelled;
    std::vector<std::string> paths;
    std::string error;
    std::int32_t selected_filter = -1;
};

class MacosFileDialogs final {
public:
    using CompletionSink = std::function<void(const NativeFileDialogCompletion&)>;

    MacosFileDialogs(SDL_Window* window, bool show_native_dialogs, CompletionSink completion_sink);
    ~MacosFileDialogs();

    bool Show(
        NativeFileDialogKind kind,
        std::uint64_t request_id,
        std::string_view encoded_filters,
        std::string_view default_location,
        bool allow_multiple);
    bool HandleEvent(const SDL_Event& event);
    void Clear();

    void CompleteForTesting(NativeFileDialogCompletion completion);

private:
    struct Request;
    struct SharedState;
    static void DialogCompleted(void* userdata, const char* const* file_list, int filter);
    static void QueueCompletion(
        const std::shared_ptr<SharedState>& state,
        std::uint64_t generation,
        NativeFileDialogCompletion completion);

    SDL_Window* window_;
    bool show_native_dialogs_;
    CompletionSink completion_sink_;
    std::uint32_t event_type_;
    std::shared_ptr<SharedState> state_;
};

} // namespace effindom::v2::native
