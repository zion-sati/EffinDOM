#pragma once

#include "NativeHostState.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace effindom::v2::native {

template <typename Host, typename = void>
struct IsNativeHost : std::false_type {};

template <typename Host>
struct IsNativeHost<Host, std::void_t<
    decltype(Host(false)),
    decltype(std::declval<Host&>().MountApplication()),
    decltype(std::declval<Host&>().Unmount()),
    decltype(std::declval<Host&>().RequestFrame()),
    decltype(std::declval<Host&>().RunNextFrame()),
    decltype(std::declval<Host&>().DrainFrames(std::uint32_t{})),
    decltype(std::declval<Host&>().PumpEvent(bool{})),
    decltype(std::declval<const Host&>().ShouldPresentAfterLastEvent()),
    decltype(std::declval<Host&>().Resize(std::uint32_t{}, std::uint32_t{})),
    decltype(std::declval<Host&>().RecreateGraphicsSurface()),
    decltype(std::declval<Host&>().DispatchPointer(
        float{}, float{}, bool{}, std::int32_t{}, std::uint32_t{}, std::int32_t{})),
    decltype(std::declval<Host&>().DispatchPointerMove(float{}, float{}, std::uint32_t{})),
    decltype(std::declval<Host&>().DispatchWheel(float{}, float{})),
    decltype(std::declval<Host&>().DispatchKey(
        std::declval<const std::string&>(), bool{}, std::uint32_t{})),
    decltype(std::declval<Host&>().SetClipboardText(std::declval<const std::string&>())),
    decltype(std::declval<const Host&>().ClipboardText()),
    decltype(std::declval<const Host&>().OpenExternalUrl(std::declval<const std::string&>())),
    decltype(std::declval<const Host&>().OpenFile(std::declval<const std::filesystem::path&>())),
    decltype(std::declval<const Host&>().RevealFile(std::declval<const std::filesystem::path&>())),
    decltype(std::declval<const Host&>().HitTest(float{}, float{})),
    decltype(std::declval<const Host&>().State()),
    decltype(std::declval<const Host&>().SnapshotRgba()),
    decltype(std::declval<const Host&>().WriteScreenshot(
        std::declval<const std::filesystem::path&>(),
        std::declval<std::string&>())),
    decltype(std::declval<const Host&>().IsIdle()),
    decltype(std::declval<const Host&>().IsRunning())>>
    : std::bool_constant<
          std::is_same_v<decltype(std::declval<const Host&>().State()), NativeHostState> &&
          std::is_same_v<decltype(std::declval<Host&>().RunNextFrame()), bool> &&
          std::is_same_v<decltype(std::declval<Host&>().PumpEvent(bool{})), bool> &&
          std::is_same_v<decltype(std::declval<const Host&>().ShouldPresentAfterLastEvent()), bool> &&
          std::is_same_v<decltype(std::declval<const Host&>().SnapshotRgba()), std::vector<std::uint8_t>> &&
          std::is_same_v<decltype(std::declval<const Host&>().IsIdle()), bool> &&
          std::is_same_v<decltype(std::declval<const Host&>().IsRunning()), bool>> {};

template <typename Host>
inline constexpr bool IsNativeHostV = IsNativeHost<Host>::value;

} // namespace effindom::v2::native
