#pragma once

#include "NativeAccessibility.h"

#include <dbus/dbus.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace effindom::v2::native::detail {

inline constexpr const char* kLinuxAtSpiTextInterface = "org.a11y.atspi.Text";
inline constexpr const char* kLinuxAtSpiEditableTextInterface =
    "org.a11y.atspi.EditableText";

using LinuxAtSpiTextExecutor =
    std::function<bool(const std::function<void()>&)>;

struct LinuxAtSpiTextObject {
    std::uint64_t handle = 0U;
    int window_x = 0;
    int window_y = 0;
    std::shared_ptr<NativeAccessibilityTextProvider> provider;
    LinuxAtSpiTextExecutor execute;
};

struct LinuxAtSpiTextEventDescriptor {
    const char* member = nullptr;
    const char* detail = nullptr;
    bool emit_caret = false;
};

bool LinuxAtSpiRoleSupportsText(NativeAccessibilityRole role);
bool LinuxAtSpiNodeSupportsEditableText(const NativeAccessibilityNode& node);
bool IsLinuxAtSpiTextPropertyRequest(DBusMessage* request);
DBusMessage* HandleLinuxAtSpiTextMessage(
    DBusMessage* request,
    const LinuxAtSpiTextObject& object);
LinuxAtSpiTextEventDescriptor LinuxAtSpiTextEvent(
    NativeAccessibilityTextEvent event);

} // namespace effindom::v2::native::detail
