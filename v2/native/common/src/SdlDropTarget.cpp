#include "SdlDropTarget.h"

#include "Engine.h"
#include "effindom_ui.h"
#include "SDL3/SDL.h"

#include <cstring>
#include <filesystem>
#include <string_view>

extern "C" std::uint32_t __fui_on_external_drag_event(
    std::uint32_t event_type,
    std::uint64_t handle,
    float x,
    float y,
    std::uint32_t modifiers,
    const std::uint8_t* payload,
    std::uint32_t payload_length);

namespace effindom::v2::native {
namespace {

constexpr std::uint32_t kExternalDragEnter = 1U;
constexpr std::uint32_t kExternalDragOver = 2U;
constexpr std::uint32_t kExternalDragLeave = 3U;
constexpr std::uint32_t kExternalDrop = 4U;
constexpr std::uint32_t kExternalFile = 1U;
constexpr std::uint32_t kExternalText = 2U;
constexpr std::uint32_t kExternalUri = 3U;

std::uint32_t Modifiers(SDL_Keymod modifiers) {
    std::uint32_t result = 0U;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) result |= UI_KEY_MOD_SHIFT;
    if ((modifiers & SDL_KMOD_CTRL) != 0) result |= UI_KEY_MOD_CTRL;
    if ((modifiers & SDL_KMOD_ALT) != 0) result |= UI_KEY_MOD_ALT;
    if ((modifiers & SDL_KMOD_GUI) != 0) result |= UI_KEY_MOD_META;
    return result;
}

bool IsUri(std::string_view value) {
    const std::size_t separator = value.find("://");
    if (separator == std::string_view::npos || separator == 0U) return false;
    for (std::size_t index = 0U; index < separator; ++index) {
        const char value_char = value[index];
        const bool valid = (value_char >= 'a' && value_char <= 'z') ||
            (value_char >= 'A' && value_char <= 'Z') ||
            (index > 0U && ((value_char >= '0' && value_char <= '9') || value_char == '+' ||
                            value_char == '-' || value_char == '.'));
        if (!valid) return false;
    }
    return true;
}

std::string MimeTypeForPath(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    if (extension == ".txt" || extension == ".md" || extension == ".log") return "text/plain";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".json") return "application/json";
    if (extension == ".pdf") return "application/pdf";
    return {};
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void AppendF64(std::vector<std::uint8_t>& bytes, double value) {
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFU));
    }
}

void AppendString(std::vector<std::uint8_t>& bytes, const std::string& value) {
    AppendU32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

} // namespace

SdlDropTarget::SdlDropTarget(SDL_Window* window, Engine& engine) : window_(window), engine_(engine) {}

bool SdlDropTarget::HandleEvent(const SDL_Event& event) {
    if (event.type >= SDL_EVENT_DROP_FILE && event.type <= SDL_EVENT_DROP_POSITION &&
        window_ != nullptr && event.drop.windowID != 0U && event.drop.windowID != SDL_GetWindowID(window_)) {
        return false;
    }
    switch (event.type) {
        case SDL_EVENT_DROP_BEGIN: {
            float x = 0.0f;
            float y = 0.0f;
            SDL_GetMouseState(&x, &y);
            Begin(x, y);
            return true;
        }
        case SDL_EVENT_DROP_POSITION:
            if (!active_) Begin(event.drop.x, event.drop.y);
            x_ = event.drop.x;
            y_ = event.drop.y;
            if (!entered_) {
                Dispatch(kExternalDragEnter);
                entered_ = true;
            }
            Dispatch(kExternalDragOver);
            return true;
        case SDL_EVENT_DROP_FILE:
            if (!active_) Begin(event.drop.x, event.drop.y);
            x_ = event.drop.x;
            y_ = event.drop.y;
            AddFile(event.drop.data);
            return true;
        case SDL_EVENT_DROP_TEXT:
            if (!active_) Begin(event.drop.x, event.drop.y);
            x_ = event.drop.x;
            y_ = event.drop.y;
            AddText(event.drop.data);
            return true;
        case SDL_EVENT_DROP_COMPLETE:
            if (!active_) return true;
            if (!entered_) {
                Dispatch(kExternalDragEnter);
                Dispatch(kExternalDragOver);
            }
            Dispatch(items_.empty() ? kExternalDragLeave : kExternalDrop);
            Clear();
            return true;
        default:
            return false;
    }
}

void SdlDropTarget::Clear() {
    active_ = false;
    entered_ = false;
    items_.clear();
}

void SdlDropTarget::Begin(float x, float y) {
    Clear();
    active_ = true;
    x_ = x;
    y_ = y;
}

void SdlDropTarget::AddFile(const char* path_value) {
    if (path_value == nullptr || *path_value == '\0') return;
    const std::filesystem::path path(path_value);
    std::error_code error;
    const auto size = std::filesystem::is_regular_file(path, error)
        ? std::filesystem::file_size(path, error)
        : 0U;
    items_.push_back(Item{
        kExternalFile,
        error ? 0.0 : static_cast<double>(size),
        path.string(),
        path.filename().string(),
        MimeTypeForPath(path),
    });
}

void SdlDropTarget::AddText(const char* text_value) {
    if (text_value == nullptr) return;
    const std::string value(text_value);
    items_.push_back(Item{
        IsUri(value) ? kExternalUri : kExternalText,
        static_cast<double>(value.size()),
        value,
        value,
        IsUri(value) ? "text/uri-list" : "text/plain",
    });
}

std::uint32_t SdlDropTarget::Dispatch(std::uint32_t event_type) const {
    const std::vector<std::uint8_t> payload = EncodePayload();
    const std::uint64_t handle = engine_.HitTest(x_, y_);
    return __fui_on_external_drag_event(
        event_type,
        handle,
        x_,
        y_,
        Modifiers(SDL_GetModState()),
        payload.empty() ? nullptr : payload.data(),
        static_cast<std::uint32_t>(payload.size()));
}

std::vector<std::uint8_t> SdlDropTarget::EncodePayload() const {
    std::vector<std::uint8_t> payload;
    if (items_.empty()) return payload;
    AppendU32(payload, static_cast<std::uint32_t>(items_.size()));
    for (const Item& item : items_) {
        AppendU32(payload, item.kind);
        AppendF64(payload, item.size_bytes);
        AppendString(payload, item.id);
        AppendString(payload, item.name);
        AppendString(payload, item.mime_type);
    }
    return payload;
}

} // namespace effindom::v2::native
