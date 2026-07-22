#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace effindom::v2::native {

namespace detail {

inline constexpr std::uint32_t kMacosAccentColorFallback = 0x0A84FFFFU;

std::optional<std::uint32_t> PackMacosAccentColor(
    double red,
    double green,
    double blue);
std::optional<std::uint32_t> ReadMacosAccentColor();

class MacosAccentColorState final {
public:
    using Reader = std::function<std::optional<std::uint32_t>()>;
    using ChangedHandler = std::function<void(std::uint32_t)>;

    MacosAccentColorState(Reader, ChangedHandler);

    std::uint32_t Current() const;
    bool Refresh();

private:
    Reader reader_;
    ChangedHandler changed_handler_;
    std::uint32_t current_ = kMacosAccentColorFallback;
};

} // namespace detail

class MacosSystemThemeBridge final {
public:
    using AccentChangedHandler = std::function<void(std::uint32_t)>;

    explicit MacosSystemThemeBridge(AccentChangedHandler);
    ~MacosSystemThemeBridge();

    std::uint32_t AccentColor() const;

    MacosSystemThemeBridge(const MacosSystemThemeBridge&) = delete;
    MacosSystemThemeBridge& operator=(const MacosSystemThemeBridge&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
