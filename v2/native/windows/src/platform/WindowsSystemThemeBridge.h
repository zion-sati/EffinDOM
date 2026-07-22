#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

namespace detail {

inline constexpr std::uint32_t kWindowsAccentColorFallback = 0x0A84FFFFU;

inline constexpr std::uint32_t PackWindowsColorizationColor(std::uint32_t argb) {
    return ((argb & 0x00FFFFFFU) << 8U) | 0xFFU;
}

std::optional<std::uint32_t> ReadWindowsAccentColor();

class WindowsAccentColorState final {
public:
    using Reader = std::function<std::optional<std::uint32_t>()>;
    using ChangedHandler = std::function<void(std::uint32_t)>;

    WindowsAccentColorState(Reader, ChangedHandler);

    std::uint32_t Current() const;
    bool Refresh();

private:
    Reader reader_;
    ChangedHandler changed_handler_;
    std::uint32_t current_ = kWindowsAccentColorFallback;
};

} // namespace detail

class WindowsSystemThemeBridge final {
public:
    using AccentChangedHandler = std::function<void(std::uint32_t)>;

    WindowsSystemThemeBridge(SDL_Window*, AccentChangedHandler);
    ~WindowsSystemThemeBridge();

    std::uint32_t AccentColor() const;
    bool HandleEvent(const SDL_Event&);

    WindowsSystemThemeBridge(const WindowsSystemThemeBridge&) = delete;
    WindowsSystemThemeBridge& operator=(const WindowsSystemThemeBridge&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
