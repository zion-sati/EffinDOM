#include "LinuxSystemThemeBridge.h"

#include "SDL3/SDL.h"

#include <dbus/dbus.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

namespace effindom::v2::native {

namespace detail {

std::optional<std::uint32_t> PackLinuxPortalAccentColor(
    double red,
    double green,
    double blue) {
    if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue) ||
        red < 0.0 || red > 1.0 || green < 0.0 || green > 1.0 ||
        blue < 0.0 || blue > 1.0) {
        return std::nullopt;
    }
    const auto component = [](double value) {
        return static_cast<std::uint32_t>(std::lround(value * 255.0));
    };
    return (component(red) << 24U) |
        (component(green) << 16U) |
        (component(blue) << 8U) |
        0xFFU;
}

LinuxAccentColorState::LinuxAccentColorState(
    std::optional<std::uint32_t> initial,
    ChangedHandler changed_handler)
    : changed_handler_(std::move(changed_handler)) {
    if (initial.has_value()) current_ = *initial;
}

std::uint32_t LinuxAccentColorState::Current() const { return current_; }

bool LinuxAccentColorState::Apply(std::optional<std::uint32_t> color) {
    if (!color.has_value() || *color == current_) return false;
    current_ = *color;
    changed_handler_(current_);
    return true;
}

} // namespace detail

namespace {

constexpr const char* kPortalBus = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kSettingsInterface = "org.freedesktop.portal.Settings";
constexpr const char* kAppearanceNamespace = "org.freedesktop.appearance";
constexpr const char* kAccentColorKey = "accent-color";
constexpr const char* kAccentSignalMatch =
    "type='signal',sender='org.freedesktop.portal.Desktop',"
    "path='/org/freedesktop/portal/desktop',"
    "interface='org.freedesktop.portal.Settings',member='SettingChanged',"
    "arg0='org.freedesktop.appearance',arg1='accent-color'";

void InitializeDbusThreads() {
    static std::once_flag initialized;
    std::call_once(initialized, [] { dbus_threads_init_default(); });
}

DBusConnection* OpenPortalConnection() {
    InitializeDbusThreads();
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    if (connection != nullptr) dbus_connection_set_exit_on_disconnect(connection, false);
    return connection;
}

bool PortalIsRunning(DBusConnection* connection) {
    if (connection == nullptr) return false;
    DBusError error;
    dbus_error_init(&error);
    const dbus_bool_t has_owner = dbus_bus_name_has_owner(connection, kPortalBus, &error);
    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
        return false;
    }
    return has_owner != 0;
}

DBusMessage* CallPortal(DBusConnection* connection, DBusMessage* request) {
    if (connection == nullptr || request == nullptr) {
        if (request != nullptr) dbus_message_unref(request);
        return nullptr;
    }
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        connection, request, 1500, &error);
    dbus_message_unref(request);
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    return reply;
}

std::optional<std::uint32_t> ReadSettingsVersion(DBusConnection* connection) {
    DBusMessage* request = dbus_message_new_method_call(
        kPortalBus,
        kPortalPath,
        DBUS_INTERFACE_PROPERTIES,
        "Get");
    const char* interface_name = kSettingsInterface;
    const char* property_name = "version";
    if (request == nullptr || !dbus_message_append_args(
            request,
            DBUS_TYPE_STRING, &interface_name,
            DBUS_TYPE_STRING, &property_name,
            DBUS_TYPE_INVALID)) {
        if (request != nullptr) dbus_message_unref(request);
        return std::nullopt;
    }
    DBusMessage* reply = CallPortal(connection, request);
    if (reply == nullptr) return std::nullopt;
    DBusMessageIter iterator;
    std::optional<std::uint32_t> version;
    if (dbus_message_iter_init(reply, &iterator) &&
        dbus_message_iter_get_arg_type(&iterator) == DBUS_TYPE_VARIANT) {
        DBusMessageIter value;
        dbus_message_iter_recurse(&iterator, &value);
        if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_UINT32) {
            dbus_uint32_t raw = 0U;
            dbus_message_iter_get_basic(&value, &raw);
            version = static_cast<std::uint32_t>(raw);
        }
    }
    dbus_message_unref(reply);
    return version;
}

std::optional<std::uint32_t> ParseAccentVariant(DBusMessageIter variant) {
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_VARIANT) return std::nullopt;
    DBusMessageIter tuple;
    dbus_message_iter_recurse(&variant, &tuple);
    if (dbus_message_iter_get_arg_type(&tuple) != DBUS_TYPE_STRUCT) return std::nullopt;
    DBusMessageIter components;
    dbus_message_iter_recurse(&tuple, &components);
    double rgb[3]{};
    for (std::size_t index = 0U; index < 3U; ++index) {
        if (dbus_message_iter_get_arg_type(&components) != DBUS_TYPE_DOUBLE) {
            return std::nullopt;
        }
        dbus_message_iter_get_basic(&components, &rgb[index]);
        if (index < 2U && !dbus_message_iter_next(&components)) return std::nullopt;
    }
    if (dbus_message_iter_next(&components)) return std::nullopt;
    return detail::PackLinuxPortalAccentColor(rgb[0], rgb[1], rgb[2]);
}

std::optional<std::uint32_t> ReadPortalAccentColor(DBusConnection* connection) {
    const std::optional<std::uint32_t> version = ReadSettingsVersion(connection);
    if (!version.has_value() || *version < 2U) return std::nullopt;
    DBusMessage* request = dbus_message_new_method_call(
        kPortalBus,
        kPortalPath,
        kSettingsInterface,
        "ReadOne");
    const char* namespace_name = kAppearanceNamespace;
    const char* key_name = kAccentColorKey;
    if (request == nullptr || !dbus_message_append_args(
            request,
            DBUS_TYPE_STRING, &namespace_name,
            DBUS_TYPE_STRING, &key_name,
            DBUS_TYPE_INVALID)) {
        if (request != nullptr) dbus_message_unref(request);
        return std::nullopt;
    }
    DBusMessage* reply = CallPortal(connection, request);
    if (reply == nullptr) return std::nullopt;
    DBusMessageIter iterator;
    const std::optional<std::uint32_t> color = dbus_message_iter_init(reply, &iterator)
        ? ParseAccentVariant(iterator)
        : std::nullopt;
    dbus_message_unref(reply);
    return color;
}

std::optional<std::uint32_t> ParseAccentSignal(DBusMessage* message) {
    if (!dbus_message_is_signal(message, kSettingsInterface, "SettingChanged")) {
        return std::nullopt;
    }
    DBusMessageIter iterator;
    if (!dbus_message_iter_init(message, &iterator) ||
        dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_STRING) {
        return std::nullopt;
    }
    const char* namespace_name = nullptr;
    dbus_message_iter_get_basic(&iterator, &namespace_name);
    if (namespace_name == nullptr || std::strcmp(namespace_name, kAppearanceNamespace) != 0 ||
        !dbus_message_iter_next(&iterator) ||
        dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_STRING) {
        return std::nullopt;
    }
    const char* key_name = nullptr;
    dbus_message_iter_get_basic(&iterator, &key_name);
    if (key_name == nullptr || std::strcmp(key_name, kAccentColorKey) != 0 ||
        !dbus_message_iter_next(&iterator)) {
        return std::nullopt;
    }
    return ParseAccentVariant(iterator);
}

std::int32_t EncodeEventColor(std::uint32_t color) {
    std::int32_t encoded = 0;
    static_assert(sizeof(encoded) == sizeof(color));
    std::memcpy(&encoded, &color, sizeof(color));
    return encoded;
}

std::uint32_t DecodeEventColor(std::int32_t encoded) {
    std::uint32_t color = 0U;
    std::memcpy(&color, &encoded, sizeof(color));
    return color;
}

} // namespace

struct LinuxSystemThemeBridge::Impl {
    Impl(SDL_Window* window, AccentChangedHandler changed_handler)
        : connection(OpenPortalConnection()),
          portal_running(PortalIsRunning(connection)),
          state(
              portal_running
                  ? ReadPortalAccentColor(connection)
                  : std::nullopt,
              std::move(changed_handler)) {
        wake_event_type = SDL_RegisterEvents(1);
        window_id = window == nullptr ? 0U : SDL_GetWindowID(window);
        if (!portal_running || wake_event_type == 0U) return;

        DBusError error;
        dbus_error_init(&error);
        dbus_bus_add_match(connection, kAccentSignalMatch, &error);
        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
            return;
        }
        match_installed = true;
        dbus_connection_flush(connection);
        if (!dbus_connection_get_unix_fd(connection, &bus_fd)) return;
        stop_fd = eventfd(0U, EFD_CLOEXEC | EFD_NONBLOCK);
        if (stop_fd < 0) return;
        listener = std::thread([this] { Listen(); });
    }

    ~Impl() {
        if (stop_fd >= 0) {
            const std::uint64_t stop = 1U;
            const ssize_t written = write(stop_fd, &stop, sizeof(stop));
            if (written < 0) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "EffinDOM could not wake the Linux system-theme listener during teardown");
            }
        }
        if (listener.joinable()) listener.join();
        if (connection != nullptr && match_installed) {
            DBusError error;
            dbus_error_init(&error);
            dbus_bus_remove_match(connection, kAccentSignalMatch, &error);
            if (dbus_error_is_set(&error)) dbus_error_free(&error);
        }
        if (connection != nullptr) {
            dbus_connection_close(connection);
            dbus_connection_unref(connection);
        }
        if (stop_fd >= 0) close(stop_fd);
    }

    void Listen() {
        pollfd descriptors[2] = {
            {bus_fd, POLLIN, 0},
            {stop_fd, POLLIN, 0},
        };
        for (;;) {
            const int result = poll(descriptors, 2, -1);
            if (result < 0) continue;
            if ((descriptors[1].revents & POLLIN) != 0) return;
            if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return;
            if ((descriptors[0].revents & POLLIN) == 0) continue;
            if (!dbus_connection_read_write(connection, 0)) return;
            while (DBusMessage* message = dbus_connection_pop_message(connection)) {
                const std::optional<std::uint32_t> color = ParseAccentSignal(message);
                dbus_message_unref(message);
                if (color.has_value()) PostAccent(*color);
            }
        }
    }

    void PostAccent(std::uint32_t color) const {
        SDL_Event event{};
        event.type = wake_event_type;
        event.user.windowID = window_id;
        event.user.code = EncodeEventColor(color);
        if (!SDL_PushEvent(&event)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "EffinDOM could not wake the SDL event loop for a system accent change: %s",
                SDL_GetError());
        }
    }

    DBusConnection* connection = nullptr;
    bool portal_running = false;
    bool match_installed = false;
    detail::LinuxAccentColorState state;
    std::thread listener;
    int bus_fd = -1;
    int stop_fd = -1;
    std::uint32_t wake_event_type = 0U;
    SDL_WindowID window_id = 0U;
};

LinuxSystemThemeBridge::LinuxSystemThemeBridge(
    SDL_Window* window,
    AccentChangedHandler changed_handler)
    : impl_(std::make_unique<Impl>(window, std::move(changed_handler))) {}

LinuxSystemThemeBridge::~LinuxSystemThemeBridge() = default;

std::uint32_t LinuxSystemThemeBridge::AccentColor() const {
    return impl_->state.Current();
}

bool LinuxSystemThemeBridge::HandleEvent(const SDL_Event& event) {
    if (impl_->wake_event_type == 0U || event.type != impl_->wake_event_type) return false;
    impl_->state.Apply(DecodeEventColor(event.user.code));
    return true;
}

} // namespace effindom::v2::native
