#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

namespace detail {

inline constexpr std::uint32_t kLinuxAccentColorFallback = 0x2563EBFFU;

std::optional<std::uint32_t> PackLinuxPortalAccentColor(
    double red,
    double green,
    double blue);

class LinuxAccentColorState final {
public:
    using ChangedHandler = std::function<void(std::uint32_t)>;

    LinuxAccentColorState(std::optional<std::uint32_t> initial, ChangedHandler);

    std::uint32_t Current() const;
    bool Apply(std::optional<std::uint32_t> color);

private:
    ChangedHandler changed_handler_;
    std::uint32_t current_ = kLinuxAccentColorFallback;
};

} // namespace detail

class LinuxSystemThemeBridge final {
public:
    using AccentChangedHandler = std::function<void(std::uint32_t)>;

    LinuxSystemThemeBridge(SDL_Window*, AccentChangedHandler);
    ~LinuxSystemThemeBridge();

    std::uint32_t AccentColor() const;
    bool HandleEvent(const SDL_Event&);

    LinuxSystemThemeBridge(const LinuxSystemThemeBridge&) = delete;
    LinuxSystemThemeBridge& operator=(const LinuxSystemThemeBridge&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
