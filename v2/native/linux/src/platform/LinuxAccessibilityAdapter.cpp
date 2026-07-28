#include "LinuxAccessibilityAdapter.h"
#include "LinuxAccessibilityText.h"

#include "SDL3/SDL.h"

#include <dbus/dbus.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>

namespace effindom::v2::native {
namespace {

constexpr const char* kRootPath = "/org/a11y/atspi/accessible/root";
constexpr const char* kNullPath = "/org/a11y/atspi/null";
constexpr const char* kAccessiblePrefix = "/org/a11y/atspi/accessible";
constexpr const char* kCachePath = "/org/a11y/atspi/cache";
constexpr const char* kAccessibleInterface = "org.a11y.atspi.Accessible";
constexpr const char* kActionInterface = "org.a11y.atspi.Action";
constexpr const char* kApplicationInterface = "org.a11y.atspi.Application";
constexpr const char* kComponentInterface = "org.a11y.atspi.Component";
constexpr const char* kCacheInterface = "org.a11y.atspi.Cache";
constexpr const char* kValueInterface = "org.a11y.atspi.Value";
constexpr const char* kObjectEventInterface = "org.a11y.atspi.Event.Object";
constexpr const char* kRegistryBus = "org.a11y.atspi.Registry";
constexpr const char* kRegistryPath = "/org/a11y/atspi/accessible/root";
constexpr const char* kSocketInterface = "org.a11y.atspi.Socket";

constexpr std::uint32_t kRoleApplication = 75U;
constexpr std::uint32_t kRolePanel = 39U;
constexpr std::uint32_t kStateChecked = 4U;
constexpr std::uint32_t kStateEditable = 7U;
constexpr std::uint32_t kStateEnabled = 8U;
constexpr std::uint32_t kStateExpandable = 9U;
constexpr std::uint32_t kStateExpanded = 10U;
constexpr std::uint32_t kStateFocusable = 11U;
constexpr std::uint32_t kStateFocused = 12U;
constexpr std::uint32_t kStateHorizontal = 14U;
constexpr std::uint32_t kStateMultiline = 17U;
constexpr std::uint32_t kStateSelectable = 22U;
constexpr std::uint32_t kStateSelected = 23U;
constexpr std::uint32_t kStateSensitive = 24U;
constexpr std::uint32_t kStateShowing = 25U;
constexpr std::uint32_t kStateSingleLine = 26U;
constexpr std::uint32_t kStateVertical = 29U;
constexpr std::uint32_t kStateVisible = 30U;
constexpr std::uint32_t kStateIndeterminate = 32U;
constexpr std::uint32_t kStateReadOnly = 43U;

bool IsInvokable(NativeAccessibilityRole role) {
    return role == NativeAccessibilityRole::Button ||
        role == NativeAccessibilityRole::Link ||
        role == NativeAccessibilityRole::CheckBox ||
        role == NativeAccessibilityRole::Radio ||
        role == NativeAccessibilityRole::Switch ||
        role == NativeAccessibilityRole::ComboBox;
}

bool IsFocusable(NativeAccessibilityRole role) {
    return IsInvokable(role) || role == NativeAccessibilityRole::TextBox ||
        role == NativeAccessibilityRole::Slider;
}

void AddState(std::vector<std::uint32_t>& words, std::uint32_t state) {
    const std::size_t word = state / 32U;
    if (words.size() <= word) words.resize(word + 1U, 0U);
    words[word] |= 1U << (state % 32U);
}

const char* RoleName(std::uint32_t role) {
    switch (role) {
        case 7U: return "check box";
        case 11U: return "combo box";
        case 16U: return "dialog";
        case 27U: return "image";
        case 31U: return "list";
        case 32U: return "list item";
        case 39U: return "panel";
        case 43U: return "push button";
        case 44U: return "radio button";
        case 51U: return "slider";
        case 62U: return "toggle button";
        case 75U: return "application";
        case 79U: return "entry";
        case 83U: return "heading";
        case 87U: return "form";
        case 88U: return "link";
        case 99U: return "grouping";
        case 116U: return "static";
        default: return "unknown";
    }
}

void InitializeDbusThreads() {
    static std::once_flag initialized;
    std::call_once(initialized, [] { dbus_threads_init_default(); });
}

std::string AccessibilityBusAddress() {
    const char* environment = std::getenv("AT_SPI_BUS_ADDRESS");
    if (environment != nullptr && environment[0] != '\0') return environment;
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* session = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (session == nullptr) {
        if (dbus_error_is_set(&error)) dbus_error_free(&error);
        return {};
    }
    dbus_connection_set_exit_on_disconnect(session, false);
    DBusMessage* request = dbus_message_new_method_call(
        "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    DBusMessage* reply = request == nullptr ? nullptr
        : dbus_connection_send_with_reply_and_block(session, request, 1500, &error);
    if (request != nullptr) dbus_message_unref(request);
    std::string address;
    const char* raw = nullptr;
    if (reply != nullptr && dbus_message_get_args(
            reply, &error, DBUS_TYPE_STRING, &raw, DBUS_TYPE_INVALID) && raw != nullptr) {
        address = raw;
    }
    if (reply != nullptr) dbus_message_unref(reply);
    dbus_connection_close(session);
    dbus_connection_unref(session);
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    return address;
}

void AppendObjectReference(
    DBusMessageIter* iterator, const std::string& bus, const std::string& path) {
    DBusMessageIter structure;
    dbus_message_iter_open_container(iterator, DBUS_TYPE_STRUCT, nullptr, &structure);
    const char* bus_value = bus.c_str();
    const char* path_value = path.c_str();
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_STRING, &bus_value);
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_OBJECT_PATH, &path_value);
    dbus_message_iter_close_container(iterator, &structure);
}

void AppendEmptyDictionary(DBusMessageIter* iterator) {
    DBusMessageIter dictionary;
    dbus_message_iter_open_container(iterator, DBUS_TYPE_ARRAY, "{sv}", &dictionary);
    dbus_message_iter_close_container(iterator, &dictionary);
}

DBusMessage* ErrorReply(DBusMessage* request, const char* name, const char* message) {
    return dbus_message_new_error(request, name, message);
}

struct ObjectSnapshot {
    bool root = false;
    std::optional<NativeAccessibilityNode> node;
    std::int32_t index = -1;
    NativeAccessibilitySnapshot snapshot;
    std::string bus_name;
    std::string parent_bus;
    std::string parent_path = kNullPath;
    std::int32_t application_id = -1;
    int window_x = 0;
    int window_y = 0;
    int window_width = 0;
    int window_height = 0;
};

class LinuxAccessibilityAdapter final : public NativeAccessibilityAdapter {
public:
    LinuxAccessibilityAdapter(SDL_Window* window, NativeAccessibilityActionHandler action_handler)
        : window_(window), action_handler_(std::move(action_handler)) {
        InitializeDbusThreads();
        const std::string address = AccessibilityBusAddress();
        if (address.empty()) return;
        DBusError error;
        dbus_error_init(&error);
        connection_ = dbus_connection_open_private(address.c_str(), &error);
        if (connection_ == nullptr || !dbus_bus_register(connection_, &error)) {
            if (connection_ != nullptr) {
                dbus_connection_close(connection_);
                dbus_connection_unref(connection_);
                connection_ = nullptr;
            }
            if (dbus_error_is_set(&error)) dbus_error_free(&error);
            return;
        }
        dbus_connection_set_exit_on_disconnect(connection_, false);
        const char* unique = dbus_bus_get_unique_name(connection_);
        if (unique != nullptr) bus_name_ = unique;
        static DBusObjectPathVTable vtable{
            nullptr, &LinuxAccessibilityAdapter::HandleMessageThunk,
            nullptr, nullptr, nullptr, nullptr};
        if (bus_name_.empty() || !dbus_connection_register_fallback(
                connection_, kAccessiblePrefix, &vtable, this)) {
            dbus_connection_close(connection_);
            dbus_connection_unref(connection_);
            connection_ = nullptr;
            return;
        }
        if (!dbus_connection_register_object_path(connection_, kCachePath, &vtable, this)) {
            dbus_connection_unregister_object_path(connection_, kAccessiblePrefix);
            dbus_connection_close(connection_);
            dbus_connection_unref(connection_);
            connection_ = nullptr;
            return;
        }
        if (!dbus_connection_get_unix_fd(connection_, &bus_fd_)) return;
        stop_fd_ = eventfd(0U, EFD_CLOEXEC | EFD_NONBLOCK);
        if (stop_fd_ < 0) return;
        listener_ = std::thread([this] { Listen(); });
    }

    ~LinuxAccessibilityAdapter() override {
        if (stop_fd_ >= 0) {
            const std::uint64_t stop = 1U;
            const ssize_t written = write(stop_fd_, &stop, sizeof(stop));
            if (written < 0) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "EffinDOM could not wake the Linux accessibility listener during teardown");
            }
        }
        if (listener_.joinable()) listener_.join();
        if (connection_ != nullptr) {
            dbus_connection_unregister_object_path(connection_, kCachePath);
            dbus_connection_unregister_object_path(connection_, kAccessiblePrefix);
            dbus_connection_close(connection_);
            dbus_connection_unref(connection_);
        }
        if (stop_fd_ >= 0) close(stop_fd_);
    }

    bool IsConnected() const { return connection_ != nullptr; }

    void Update(const NativeAccessibilitySnapshot& snapshot) override {
        NativeAccessibilitySnapshot previous;
        {
            std::lock_guard lock(mutex_);
            previous = snapshot_;
            snapshot_ = snapshot;
            if (window_ != nullptr) {
                SDL_GetWindowPosition(window_, &window_x_, &window_y_);
                SDL_GetWindowSize(window_, &window_width_, &window_height_);
            }
        }
        bool expected = false;
        if (!snapshot.nodes.empty() &&
            registered_.compare_exchange_strong(expected, true)) {
            RegisterApplication();
        }
        PublishChanges(previous, snapshot);
    }

    void Announce(const NativeAccessibilityNode& node) override {
        SendEvent(detail::LinuxAtSpiObjectPath(node.handle), "Announcement",
            node.label, 1, [&node](DBusMessageIter* variant) {
                const char* text = node.label.c_str();
                dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &text);
            }, "s");
    }

    void SetTextProvider(
        std::shared_ptr<NativeAccessibilityTextProvider> provider) override {
        std::lock_guard lock(mutex_);
        text_provider_ = std::move(provider);
    }

    void TextChanged(
        NativeAccessibilityTextEvent event, std::uint64_t handle) override {
        const auto descriptor = detail::LinuxAtSpiTextEvent(event);
        SendEvent(detail::LinuxAtSpiObjectPath(handle), descriptor.member,
            descriptor.detail, 0, [](DBusMessageIter* variant) {
                const char* empty = "";
                dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &empty);
            }, "s");
        if (!descriptor.emit_caret) return;
        std::shared_ptr<NativeAccessibilityTextProvider> provider;
        {
            std::lock_guard lock(mutex_);
            provider = text_provider_;
        }
        NativeAccessibilityTextInfo info;
        if (provider == nullptr || !provider->GetInfo(handle, info)) return;
        SendEvent(detail::LinuxAtSpiObjectPath(handle), "TextCaretMoved", "",
            static_cast<std::int32_t>(info.selection_end),
            [](DBusMessageIter* variant) {
                const char* empty = "";
                dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &empty);
            }, "s");
    }

    void Clear() override {
        NativeAccessibilitySnapshot previous;
        {
            std::lock_guard lock(mutex_);
            previous = std::move(snapshot_);
            snapshot_ = {};
        }
        for (std::size_t index = 0U; index < previous.nodes.size(); ++index) {
            SendChildEvent("remove", static_cast<std::int32_t>(index), previous.nodes[index]);
            SendCacheRemoved(previous.nodes[index]);
        }
    }

private:
    static DBusHandlerResult HandleMessageThunk(
        DBusConnection*, DBusMessage* message, void* data) {
        return static_cast<LinuxAccessibilityAdapter*>(data)->HandleMessage(message);
    }

    void RegisterApplication() {
        DBusMessage* request = dbus_message_new_method_call(
            kRegistryBus, kRegistryPath, kSocketInterface, "Embed");
        if (request == nullptr) return;
        DBusMessageIter iterator;
        dbus_message_iter_init_append(request, &iterator);
        AppendObjectReference(&iterator, bus_name_, kRootPath);
        DBusError error;
        dbus_error_init(&error);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(
            connection_, request, 1500, &error);
        dbus_message_unref(request);
        if (reply != nullptr) {
            DBusMessageIter value;
            if (dbus_message_iter_init(reply, &value) &&
                dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRUCT) {
                DBusMessageIter fields;
                dbus_message_iter_recurse(&value, &fields);
                const char* bus = nullptr;
                const char* path = nullptr;
                dbus_message_iter_get_basic(&fields, &bus);
                if (dbus_message_iter_next(&fields)) dbus_message_iter_get_basic(&fields, &path);
                if (bus != nullptr && path != nullptr) {
                    std::lock_guard lock(mutex_);
                    parent_bus_ = bus;
                    parent_path_ = path;
                }
            }
            dbus_message_unref(reply);
        }
        if (dbus_error_is_set(&error)) dbus_error_free(&error);
    }

    void Listen() {
        pollfd descriptors[2]{{bus_fd_, POLLIN, 0}, {stop_fd_, POLLIN, 0}};
        for (;;) {
            const int result = poll(descriptors, 2, -1);
            if (result < 0) continue;
            if ((descriptors[1].revents & POLLIN) != 0) return;
            if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return;
            if ((descriptors[0].revents & POLLIN) == 0) continue;
            if (!dbus_connection_read_write(connection_, 0)) return;
            while (dbus_connection_get_dispatch_status(connection_) == DBUS_DISPATCH_DATA_REMAINS) {
                if (dbus_connection_dispatch(connection_) == DBUS_DISPATCH_COMPLETE) break;
            }
        }
    }

    ObjectSnapshot Resolve(const char* path) const {
        ObjectSnapshot result;
        std::lock_guard lock(mutex_);
        result.root = path != nullptr && std::strcmp(path, kRootPath) == 0;
        result.snapshot = snapshot_;
        result.bus_name = bus_name_;
        result.parent_bus = parent_bus_;
        result.parent_path = parent_path_;
        result.application_id = application_id_;
        result.window_x = window_x_;
        result.window_y = window_y_;
        result.window_width = window_width_;
        result.window_height = window_height_;
        if (!result.root && path != nullptr) {
            for (std::size_t index = 0U; index < snapshot_.nodes.size(); ++index) {
                if (detail::LinuxAtSpiObjectPath(snapshot_.nodes[index].handle) == path) {
                    result.node = snapshot_.nodes[index];
                    result.index = static_cast<std::int32_t>(index);
                    break;
                }
            }
        }
        return result;
    }

    DBusHandlerResult HandleMessage(DBusMessage* message) {
        if (std::strcmp(dbus_message_get_path(message), kCachePath) == 0) {
            DBusMessage* reply = HandleCache(message);
            if (reply == nullptr) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            dbus_connection_send(connection_, reply, nullptr);
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        const ObjectSnapshot object = Resolve(dbus_message_get_path(message));
        if (!object.root && !object.node.has_value()) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        DBusMessage* reply = nullptr;
        if (detail::IsLinuxAtSpiTextPropertyRequest(message) ||
            dbus_message_has_interface(message, detail::kLinuxAtSpiTextInterface) ||
            dbus_message_has_interface(message, detail::kLinuxAtSpiEditableTextInterface)) {
            reply = detail::HandleLinuxAtSpiTextMessage(message, TextObject(object));
        } else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Get")) {
            reply = HandlePropertyGet(message, object);
        } else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "GetAll")) {
            reply = HandlePropertyGetAll(message, object);
        } else if (dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Set")) {
            reply = HandlePropertySet(message);
        } else if (dbus_message_is_method_call(message, DBUS_INTERFACE_INTROSPECTABLE, "Introspect")) {
            reply = dbus_message_new_method_return(message);
            const char* xml = IntrospectionXml();
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        } else if (dbus_message_has_interface(message, kAccessibleInterface)) {
            reply = HandleAccessible(message, object);
        } else if (dbus_message_has_interface(message, kComponentInterface)) {
            reply = HandleComponent(message, object);
        } else if (dbus_message_has_interface(message, kActionInterface)) {
            reply = HandleAction(message, object);
        } else if (dbus_message_has_interface(message, kApplicationInterface)) {
            reply = HandleApplication(message);
        }
        if (reply == nullptr) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        dbus_connection_send(connection_, reply, nullptr);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    detail::LinuxAtSpiTextObject TextObject(const ObjectSnapshot& object) const {
        std::shared_ptr<NativeAccessibilityTextProvider> provider;
        {
            std::lock_guard lock(mutex_);
            provider = text_provider_;
        }
        return {
            object.node.has_value() ? object.node->handle : 0U,
            object.window_x,
            object.window_y,
            std::move(provider),
            [](const std::function<void()>& operation) {
                struct Request {
                    const std::function<void()>* operation;
                } request{&operation};
                return SDL_RunOnMainThread([](void* data) {
                    const auto* request = static_cast<Request*>(data);
                    (*request->operation)();
                }, &request, true);
            },
        };
    }

    void AppendCacheItem(DBusMessageIter* output, const ObjectSnapshot& object) const {
        DBusMessageIter item;
        dbus_message_iter_open_container(output, DBUS_TYPE_STRUCT, nullptr, &item);
        const std::string path = object.root
            ? std::string(kRootPath) : detail::LinuxAtSpiObjectPath(object.node->handle);
        AppendObjectReference(&item, object.bus_name, path);
        AppendObjectReference(&item, object.bus_name, kRootPath);
        if (object.root) AppendObjectReference(&item, "", kNullPath);
        else AppendObjectReference(&item, object.bus_name, kRootPath);
        const dbus_int32_t index = object.root ? -1 : object.index;
        const dbus_int32_t child_count = object.root
            ? static_cast<dbus_int32_t>(object.snapshot.nodes.size()) : 0;
        dbus_message_iter_append_basic(&item, DBUS_TYPE_INT32, &index);
        dbus_message_iter_append_basic(&item, DBUS_TYPE_INT32, &child_count);
        std::vector<std::string> interfaces = object.root
            ? std::vector<std::string>{
                kAccessibleInterface, kComponentInterface, kApplicationInterface}
            : detail::LinuxAtSpiInterfaces(*object.node);
        DBusMessageIter interface_array;
        dbus_message_iter_open_container(&item, DBUS_TYPE_ARRAY, "s", &interface_array);
        for (const std::string& interface_name : interfaces) {
            const char* value = interface_name.c_str();
            dbus_message_iter_append_basic(&interface_array, DBUS_TYPE_STRING, &value);
        }
        dbus_message_iter_close_container(&item, &interface_array);
        const char* name = object.root ? "EffinDOM" : object.node->label.c_str();
        dbus_message_iter_append_basic(&item, DBUS_TYPE_STRING, &name);
        const dbus_uint32_t role = object.root
            ? kRoleApplication : detail::LinuxAtSpiRole(object.node->role);
        dbus_message_iter_append_basic(&item, DBUS_TYPE_UINT32, &role);
        const char* description = "";
        dbus_message_iter_append_basic(&item, DBUS_TYPE_STRING, &description);
        const auto states = object.root
            ? std::vector<std::uint32_t>{
                (1U << kStateEnabled) | (1U << kStateSensitive) |
                (1U << kStateShowing) | (1U << kStateVisible), 0U}
            : detail::LinuxAtSpiStates(
                *object.node, object.node->handle == object.snapshot.focused_handle);
        DBusMessageIter state_array;
        dbus_message_iter_open_container(&item, DBUS_TYPE_ARRAY, "u", &state_array);
        for (std::uint32_t state : states) {
            const dbus_uint32_t value = state;
            dbus_message_iter_append_basic(&state_array, DBUS_TYPE_UINT32, &value);
        }
        dbus_message_iter_close_container(&item, &state_array);
        dbus_message_iter_close_container(output, &item);
    }

    DBusMessage* HandleCache(DBusMessage* request) const {
        if (!dbus_message_is_method_call(request, kCacheInterface, "GetItems")) return nullptr;
        const ObjectSnapshot root = Resolve(kRootPath);
        DBusMessage* reply = dbus_message_new_method_return(request);
        DBusMessageIter output;
        DBusMessageIter items;
        dbus_message_iter_init_append(reply, &output);
        dbus_message_iter_open_container(
            &output, DBUS_TYPE_ARRAY, "((so)(so)(so)iiassusau)", &items);
        AppendCacheItem(&items, root);
        for (std::size_t index = 0U; index < root.snapshot.nodes.size(); ++index) {
            ObjectSnapshot child = root;
            child.root = false;
            child.node = root.snapshot.nodes[index];
            child.index = static_cast<std::int32_t>(index);
            AppendCacheItem(&items, child);
        }
        dbus_message_iter_close_container(&output, &items);
        return reply;
    }

    DBusMessage* HandleAccessible(DBusMessage* request, const ObjectSnapshot& object) {
        const char* member = dbus_message_get_member(request);
        DBusMessage* reply = dbus_message_new_method_return(request);
        DBusMessageIter output;
        dbus_message_iter_init_append(reply, &output);
        if (std::strcmp(member, "GetChildren") == 0) {
            DBusMessageIter array;
            dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "(so)", &array);
            if (object.root) {
                for (const auto& node : object.snapshot.nodes) {
                    AppendObjectReference(&array, object.bus_name,
                        detail::LinuxAtSpiObjectPath(node.handle));
                }
            }
            dbus_message_iter_close_container(&output, &array);
        } else if (std::strcmp(member, "GetChildAtIndex") == 0) {
            dbus_int32_t index = -1;
            if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID) ||
                !object.root || index < 0 || static_cast<std::size_t>(index) >= object.snapshot.nodes.size()) {
                dbus_message_unref(reply);
                return ErrorReply(request, DBUS_ERROR_INVALID_ARGS, "Child index is out of range");
            }
            AppendObjectReference(&output, object.bus_name,
                detail::LinuxAtSpiObjectPath(object.snapshot.nodes[static_cast<std::size_t>(index)].handle));
        } else if (std::strcmp(member, "GetIndexInParent") == 0) {
            dbus_int32_t index = object.root ? -1 : object.index;
            dbus_message_iter_append_basic(&output, DBUS_TYPE_INT32, &index);
        } else if (std::strcmp(member, "GetRole") == 0) {
            dbus_uint32_t role = object.root ? kRoleApplication : detail::LinuxAtSpiRole(object.node->role);
            dbus_message_iter_append_basic(&output, DBUS_TYPE_UINT32, &role);
        } else if (std::strcmp(member, "GetRoleName") == 0 ||
            std::strcmp(member, "GetLocalizedRoleName") == 0) {
            const std::uint32_t role = object.root ? kRoleApplication : detail::LinuxAtSpiRole(object.node->role);
            const char* name = RoleName(role);
            dbus_message_iter_append_basic(&output, DBUS_TYPE_STRING, &name);
        } else if (std::strcmp(member, "GetState") == 0) {
            const auto states = object.root
                ? std::vector<std::uint32_t>{
                    (1U << kStateEnabled) | (1U << kStateSensitive) |
                    (1U << kStateShowing) | (1U << kStateVisible), 0U}
                : detail::LinuxAtSpiStates(
                    *object.node, object.node->handle == object.snapshot.focused_handle);
            DBusMessageIter array;
            dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "u", &array);
            for (std::uint32_t value : states) {
                dbus_uint32_t word = value;
                dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &word);
            }
            dbus_message_iter_close_container(&output, &array);
        } else if (std::strcmp(member, "GetRelationSet") == 0) {
            DBusMessageIter array;
            dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "(ua(so))", &array);
            dbus_message_iter_close_container(&output, &array);
        } else if (std::strcmp(member, "GetAttributes") == 0) {
            DBusMessageIter dictionary;
            dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "{ss}", &dictionary);
            dbus_message_iter_close_container(&output, &dictionary);
        } else if (std::strcmp(member, "GetApplication") == 0) {
            AppendObjectReference(&output, object.bus_name, kRootPath);
        } else if (std::strcmp(member, "GetInterfaces") == 0) {
            std::vector<std::string> interfaces{kAccessibleInterface, kComponentInterface};
            if (object.root) interfaces.push_back(kApplicationInterface);
            else interfaces = detail::LinuxAtSpiInterfaces(*object.node);
            DBusMessageIter array;
            dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "s", &array);
            for (const std::string& interface_name : interfaces) {
                const char* value = interface_name.c_str();
                dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &value);
            }
            dbus_message_iter_close_container(&output, &array);
        } else {
            dbus_message_unref(reply);
            return nullptr;
        }
        return reply;
    }

    NativeAccessibilityBounds Bounds(const ObjectSnapshot& object) const {
        if (object.root) return {0.0f, 0.0f,
            static_cast<float>(object.window_width), static_cast<float>(object.window_height)};
        return object.node->bounds;
    }

    DBusMessage* HandleComponent(DBusMessage* request, const ObjectSnapshot& object) {
        const char* member = dbus_message_get_member(request);
        DBusMessage* reply = dbus_message_new_method_return(request);
        const NativeAccessibilityBounds bounds = Bounds(object);
        if (std::strcmp(member, "GrabFocus") == 0) {
            const dbus_bool_t success = object.node.has_value() && !object.node->disabled &&
                DispatchAction(NativeAccessibilityAction::Focus, object.node->handle);
            dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
        } else if (std::strcmp(member, "GetExtents") == 0 ||
            std::strcmp(member, "GetPosition") == 0 || std::strcmp(member, "Contains") == 0) {
            dbus_int32_t x = 0;
            dbus_int32_t y = 0;
            dbus_uint32_t coordinates = 0U;
            if (std::strcmp(member, "Contains") == 0) {
                if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &x,
                        DBUS_TYPE_INT32, &y, DBUS_TYPE_UINT32, &coordinates, DBUS_TYPE_INVALID)) {
                    dbus_message_unref(reply);
                    return ErrorReply(request, DBUS_ERROR_INVALID_ARGS, "Invalid coordinates");
                }
                const int offset_x = coordinates == 0U ? object.window_x : 0;
                const int offset_y = coordinates == 0U ? object.window_y : 0;
                const dbus_bool_t contains = x >= std::lround(bounds.x) + offset_x &&
                    y >= std::lround(bounds.y) + offset_y &&
                    x < std::lround(bounds.x + bounds.width) + offset_x &&
                    y < std::lround(bounds.y + bounds.height) + offset_y;
                dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &contains, DBUS_TYPE_INVALID);
                return reply;
            }
            (void)dbus_message_get_args(
                request, nullptr, DBUS_TYPE_UINT32, &coordinates, DBUS_TYPE_INVALID);
            x = static_cast<dbus_int32_t>(std::lround(bounds.x)) +
                (coordinates == 0U ? object.window_x : 0);
            y = static_cast<dbus_int32_t>(std::lround(bounds.y)) +
                (coordinates == 0U ? object.window_y : 0);
            if (std::strcmp(member, "GetExtents") == 0) {
                const dbus_int32_t width = static_cast<dbus_int32_t>(std::lround(bounds.width));
                const dbus_int32_t height = static_cast<dbus_int32_t>(std::lround(bounds.height));
                DBusMessageIter output;
                DBusMessageIter structure;
                dbus_message_iter_init_append(reply, &output);
                dbus_message_iter_open_container(&output, DBUS_TYPE_STRUCT, nullptr, &structure);
                dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &x);
                dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &y);
                dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &width);
                dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &height);
                dbus_message_iter_close_container(&output, &structure);
            } else {
                dbus_message_append_args(reply, DBUS_TYPE_INT32, &x,
                    DBUS_TYPE_INT32, &y, DBUS_TYPE_INVALID);
            }
        } else if (std::strcmp(member, "GetSize") == 0) {
            const dbus_int32_t width = static_cast<dbus_int32_t>(std::lround(bounds.width));
            const dbus_int32_t height = static_cast<dbus_int32_t>(std::lround(bounds.height));
            dbus_message_append_args(reply, DBUS_TYPE_INT32, &width,
                DBUS_TYPE_INT32, &height, DBUS_TYPE_INVALID);
        } else if (std::strcmp(member, "GetLayer") == 0) {
            const dbus_uint32_t layer = object.root ? 7U : 3U;
            dbus_message_append_args(reply, DBUS_TYPE_UINT32, &layer, DBUS_TYPE_INVALID);
        } else if (std::strcmp(member, "GetMDIZOrder") == 0) {
            const dbus_int16_t order = -1;
            dbus_message_append_args(reply, DBUS_TYPE_INT16, &order, DBUS_TYPE_INVALID);
        } else if (std::strcmp(member, "GetAlpha") == 0) {
            const double alpha = 1.0;
            dbus_message_append_args(reply, DBUS_TYPE_DOUBLE, &alpha, DBUS_TYPE_INVALID);
        } else {
            dbus_message_unref(reply);
            return nullptr;
        }
        return reply;
    }

    DBusMessage* HandleAction(DBusMessage* request, const ObjectSnapshot& object) {
        if (!object.node.has_value()) return nullptr;
        const char* member = dbus_message_get_member(request);
        const std::uint32_t count = detail::LinuxAtSpiActionCount(*object.node);
        DBusMessage* reply = dbus_message_new_method_return(request);
        if (std::strcmp(member, "GetActions") == 0) {
            DBusMessageIter output;
            DBusMessageIter array;
            dbus_message_iter_init_append(reply, &output);
            dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "(sss)", &array);
            for (std::uint32_t index = 0U; index < count; ++index) {
                DBusMessageIter action;
                dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, nullptr, &action);
                const char* name = object.node->role == NativeAccessibilityRole::Slider
                    ? (index == 0U ? "increment" : "decrement") : "click";
                const char* empty = "";
                dbus_message_iter_append_basic(&action, DBUS_TYPE_STRING, &name);
                dbus_message_iter_append_basic(&action, DBUS_TYPE_STRING, &empty);
                dbus_message_iter_append_basic(&action, DBUS_TYPE_STRING, &empty);
                dbus_message_iter_close_container(&array, &action);
            }
            dbus_message_iter_close_container(&output, &array);
        } else if (std::strcmp(member, "GetName") == 0 ||
            std::strcmp(member, "GetLocalizedName") == 0 ||
            std::strcmp(member, "GetDescription") == 0 ||
            std::strcmp(member, "GetKeyBinding") == 0) {
            dbus_int32_t index = -1;
            if (!dbus_message_get_args(
                    request, nullptr, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID) ||
                index < 0 || static_cast<std::uint32_t>(index) >= count) {
                dbus_message_unref(reply);
                return ErrorReply(request, DBUS_ERROR_INVALID_ARGS,
                    "Action index is out of range");
            }
            const char* value = "";
            if (std::strcmp(member, "GetName") == 0 ||
                std::strcmp(member, "GetLocalizedName") == 0) {
                value = object.node->role == NativeAccessibilityRole::Slider
                    ? (index == 0 ? "increment" : "decrement") : "click";
            }
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
        } else if (std::strcmp(member, "DoAction") == 0) {
            dbus_int32_t index = -1;
            const bool valid = dbus_message_get_args(
                request, nullptr, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID) &&
                index >= 0 && static_cast<std::uint32_t>(index) < count && !object.node->disabled;
            NativeAccessibilityAction action = NativeAccessibilityAction::Press;
            if (object.node->role == NativeAccessibilityRole::Slider) {
                action = index == 0 ? NativeAccessibilityAction::Increment
                                    : NativeAccessibilityAction::Decrement;
            }
            const dbus_bool_t success = valid && DispatchAction(action, object.node->handle);
            dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
        } else {
            dbus_message_unref(reply);
            return nullptr;
        }
        return reply;
    }

    DBusMessage* HandleApplication(DBusMessage* request) {
        if (std::strcmp(dbus_message_get_member(request), "GetLocale") != 0 &&
            std::strcmp(dbus_message_get_member(request), "GetApplicationBusAddress") != 0) {
            return nullptr;
        }
        DBusMessage* reply = dbus_message_new_method_return(request);
        const char* value = std::strcmp(dbus_message_get_member(request), "GetLocale") == 0
            ? Locale() : bus_name_.c_str();
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
        return reply;
    }

    DBusMessage* HandlePropertyGet(DBusMessage* request, const ObjectSnapshot& object) {
        const char* interface_name = nullptr;
        const char* property_name = nullptr;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING, &interface_name,
                DBUS_TYPE_STRING, &property_name, DBUS_TYPE_INVALID)) {
            return ErrorReply(request, DBUS_ERROR_INVALID_ARGS, "Invalid property request");
        }
        DBusMessage* reply = dbus_message_new_method_return(request);
        DBusMessageIter output;
        DBusMessageIter variant;
        dbus_message_iter_init_append(reply, &output);
        const char* signature = PropertySignature(interface_name, property_name);
        if (signature == nullptr) {
            dbus_message_unref(reply);
            return ErrorReply(request, DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property");
        }
        dbus_message_iter_open_container(&output, DBUS_TYPE_VARIANT, signature, &variant);
        AppendProperty(&variant, interface_name, property_name, object);
        dbus_message_iter_close_container(&output, &variant);
        return reply;
    }

    DBusMessage* HandlePropertyGetAll(DBusMessage* request, const ObjectSnapshot& object) {
        const char* interface_name = nullptr;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING, &interface_name,
                DBUS_TYPE_INVALID)) {
            return ErrorReply(request, DBUS_ERROR_INVALID_ARGS, "Invalid interface");
        }
        static constexpr const char* accessible[] = {
            "version", "Name", "Description", "Parent", "ChildCount",
            "Locale", "AccessibleId", "HelpText"};
        static constexpr const char* component[] = {"version"};
        static constexpr const char* action[] = {"version", "NActions"};
        static constexpr const char* application[] = {
            "ToolkitName", "Version", "ToolkitVersion", "AtspiVersion", "InterfaceVersion", "Id"};
        static constexpr const char* value[] = {
            "version", "MinimumValue", "MaximumValue", "MinimumIncrement", "CurrentValue", "Text"};
        const char* const* properties = nullptr;
        std::size_t count = 0U;
        if (std::strcmp(interface_name, kAccessibleInterface) == 0) {
            properties = accessible; count = std::size(accessible);
        } else if (std::strcmp(interface_name, kComponentInterface) == 0) {
            properties = component; count = std::size(component);
        } else if (std::strcmp(interface_name, kActionInterface) == 0) {
            properties = action; count = std::size(action);
        } else if (std::strcmp(interface_name, kApplicationInterface) == 0) {
            properties = application; count = std::size(application);
        } else if (std::strcmp(interface_name, kValueInterface) == 0) {
            properties = value; count = std::size(value);
        }
        DBusMessage* reply = dbus_message_new_method_return(request);
        DBusMessageIter output;
        DBusMessageIter dictionary;
        dbus_message_iter_init_append(reply, &output);
        dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "{sv}", &dictionary);
        for (std::size_t index = 0U; index < count; ++index) {
            const char* signature = PropertySignature(interface_name, properties[index]);
            DBusMessageIter entry;
            DBusMessageIter variant;
            dbus_message_iter_open_container(&dictionary, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &properties[index]);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant);
            AppendProperty(&variant, interface_name, properties[index], object);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&dictionary, &entry);
        }
        dbus_message_iter_close_container(&output, &dictionary);
        return reply;
    }

    DBusMessage* HandlePropertySet(DBusMessage* request) {
        const char* interface_name = nullptr;
        const char* property_name = nullptr;
        DBusMessageIter iterator;
        if (!dbus_message_iter_init(request, &iterator)) {
            return ErrorReply(request, DBUS_ERROR_INVALID_ARGS, "Invalid property value");
        }
        dbus_message_iter_get_basic(&iterator, &interface_name);
        if (!dbus_message_iter_next(&iterator)) return ErrorReply(
            request, DBUS_ERROR_INVALID_ARGS, "Invalid property name");
        dbus_message_iter_get_basic(&iterator, &property_name);
        if (interface_name == nullptr || property_name == nullptr ||
            std::strcmp(interface_name, kApplicationInterface) != 0 ||
            std::strcmp(property_name, "Id") != 0 || !dbus_message_iter_next(&iterator) ||
            dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_VARIANT) {
            return ErrorReply(request, DBUS_ERROR_PROPERTY_READ_ONLY, "Property is read-only");
        }
        DBusMessageIter value;
        dbus_message_iter_recurse(&iterator, &value);
        if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_INT32) {
            return ErrorReply(request, DBUS_ERROR_INVALID_ARGS, "Application Id must be int32");
        }
        dbus_int32_t id = -1;
        dbus_message_iter_get_basic(&value, &id);
        {
            std::lock_guard lock(mutex_);
            application_id_ = id;
        }
        return dbus_message_new_method_return(request);
    }

    static const char* PropertySignature(const char* interface_name, const char* property) {
        if (std::strcmp(property, "version") == 0 ||
            std::strcmp(property, "InterfaceVersion") == 0) return "u";
        if (std::strcmp(property, "ChildCount") == 0 ||
            std::strcmp(property, "NActions") == 0 || std::strcmp(property, "Id") == 0) return "i";
        if (std::strcmp(property, "Parent") == 0) return "(so)";
        if (std::strcmp(interface_name, kValueInterface) == 0 &&
            std::strcmp(property, "Text") != 0) return "d";
        return "s";
    }

    void AppendProperty(DBusMessageIter* output, const char* interface_name,
        const char* property, const ObjectSnapshot& object) const {
        if (std::strcmp(property, "version") == 0 ||
            std::strcmp(property, "InterfaceVersion") == 0) {
            const dbus_uint32_t value = 1U;
            dbus_message_iter_append_basic(output, DBUS_TYPE_UINT32, &value);
        } else if (std::strcmp(property, "Parent") == 0) {
            if (object.root) AppendObjectReference(output,
                object.parent_bus, object.parent_path);
            else AppendObjectReference(output, object.bus_name, kRootPath);
        } else if (std::strcmp(property, "ChildCount") == 0) {
            const dbus_int32_t value = object.root
                ? static_cast<dbus_int32_t>(object.snapshot.nodes.size()) : 0;
            dbus_message_iter_append_basic(output, DBUS_TYPE_INT32, &value);
        } else if (std::strcmp(property, "NActions") == 0) {
            const dbus_int32_t value = object.node.has_value()
                ? static_cast<dbus_int32_t>(detail::LinuxAtSpiActionCount(*object.node)) : 0;
            dbus_message_iter_append_basic(output, DBUS_TYPE_INT32, &value);
        } else if (std::strcmp(property, "Id") == 0) {
            const dbus_int32_t value = object.application_id;
            dbus_message_iter_append_basic(output, DBUS_TYPE_INT32, &value);
        } else if (std::strcmp(interface_name, kValueInterface) == 0 &&
            std::strcmp(property, "Text") != 0) {
            double value = 0.0;
            if (object.node.has_value()) {
                if (std::strcmp(property, "MinimumValue") == 0) value = object.node->minimum;
                else if (std::strcmp(property, "MaximumValue") == 0) value = object.node->maximum;
                else if (std::strcmp(property, "CurrentValue") == 0) value = object.node->value;
            }
            dbus_message_iter_append_basic(output, DBUS_TYPE_DOUBLE, &value);
        } else {
            std::string storage;
            if (std::strcmp(property, "Name") == 0) storage = object.root
                ? "EffinDOM application" : object.node->label;
            else if (std::strcmp(property, "Locale") == 0) storage = Locale();
            else if (std::strcmp(property, "AccessibleId") == 0) storage = object.root
                ? "effindom-root" : std::to_string(object.node->handle);
            else if (std::strcmp(property, "ToolkitName") == 0) storage = "EffinDOM";
            else if (std::strcmp(property, "Version") == 0 ||
                std::strcmp(property, "ToolkitVersion") == 0) storage = "2";
            else if (std::strcmp(property, "AtspiVersion") == 0) storage = "2.1";
            const char* value = storage.c_str();
            dbus_message_iter_append_basic(output, DBUS_TYPE_STRING, &value);
        }
    }

    bool DispatchAction(NativeAccessibilityAction action, std::uint64_t handle) const {
        struct Request {
            NativeAccessibilityActionHandler handler;
            NativeAccessibilityAction action;
            std::uint64_t handle;
        };
        auto* request = new Request{action_handler_, action, handle};
        if (!SDL_RunOnMainThread([](void* data) {
                std::unique_ptr<Request> owned(static_cast<Request*>(data));
                owned->handler(owned->action, owned->handle);
            }, request, true)) {
            delete request;
            return false;
        }
        return true;
    }

    template <typename AppendValue>
    void SendEvent(const std::string& path, const char* member, const std::string& detail,
        std::int32_t first, AppendValue append_value, const char* signature) const {
        if (connection_ == nullptr) return;
        DBusMessage* signal = dbus_message_new_signal(path.c_str(), kObjectEventInterface, member);
        if (signal == nullptr) return;
        DBusMessageIter output;
        DBusMessageIter variant;
        dbus_message_iter_init_append(signal, &output);
        const char* detail_value = detail.c_str();
        const dbus_int32_t second = 0;
        dbus_message_iter_append_basic(&output, DBUS_TYPE_STRING, &detail_value);
        dbus_message_iter_append_basic(&output, DBUS_TYPE_INT32, &first);
        dbus_message_iter_append_basic(&output, DBUS_TYPE_INT32, &second);
        dbus_message_iter_open_container(&output, DBUS_TYPE_VARIANT, signature, &variant);
        append_value(&variant);
        dbus_message_iter_close_container(&output, &variant);
        AppendEmptyDictionary(&output);
        dbus_connection_send(connection_, signal, nullptr);
        dbus_message_unref(signal);
        dbus_connection_flush(connection_);
    }

    void SendChildEvent(const char* operation, std::int32_t index,
        const NativeAccessibilityNode& node) const {
        SendEvent(kRootPath, "ChildrenChanged", operation, index,
            [this, &node](DBusMessageIter* variant) {
                AppendObjectReference(variant, bus_name_, detail::LinuxAtSpiObjectPath(node.handle));
            }, "(so)");
    }

    void SendCacheAdded(const NativeAccessibilitySnapshot& snapshot,
        std::int32_t index, const NativeAccessibilityNode& node) const {
        DBusMessage* signal = dbus_message_new_signal(
            kCachePath, kCacheInterface, "AddAccessible");
        if (signal == nullptr) return;
        ObjectSnapshot object;
        object.node = node;
        object.index = index;
        object.snapshot = snapshot;
        object.bus_name = bus_name_;
        DBusMessageIter output;
        dbus_message_iter_init_append(signal, &output);
        AppendCacheItem(&output, object);
        dbus_connection_send(connection_, signal, nullptr);
        dbus_message_unref(signal);
    }

    void SendCacheRemoved(const NativeAccessibilityNode& node) const {
        DBusMessage* signal = dbus_message_new_signal(
            kCachePath, kCacheInterface, "RemoveAccessible");
        if (signal == nullptr) return;
        DBusMessageIter output;
        dbus_message_iter_init_append(signal, &output);
        AppendObjectReference(
            &output, bus_name_, detail::LinuxAtSpiObjectPath(node.handle));
        dbus_connection_send(connection_, signal, nullptr);
        dbus_message_unref(signal);
    }

    void SendStateEvent(const NativeAccessibilityNode& node,
        const char* state, bool enabled) const {
        SendEvent(detail::LinuxAtSpiObjectPath(node.handle), "StateChanged", state,
            enabled ? 1 : 0, [](DBusMessageIter* variant) {
                const char* empty = "";
                dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &empty);
            }, "s");
    }

    void PublishChanges(const NativeAccessibilitySnapshot& previous,
        const NativeAccessibilitySnapshot& current) const {
        std::unordered_map<std::uint64_t, const NativeAccessibilityNode*> old_nodes;
        for (const auto& node : previous.nodes) old_nodes.emplace(node.handle, &node);
        std::unordered_map<std::uint64_t, const NativeAccessibilityNode*> new_nodes;
        for (const auto& node : current.nodes) new_nodes.emplace(node.handle, &node);
        for (std::size_t index = 0U; index < previous.nodes.size(); ++index) {
            if (new_nodes.find(previous.nodes[index].handle) == new_nodes.end()) {
                SendChildEvent("remove", static_cast<std::int32_t>(index), previous.nodes[index]);
                SendCacheRemoved(previous.nodes[index]);
            }
        }
        for (std::size_t index = 0U; index < current.nodes.size(); ++index) {
            const auto& node = current.nodes[index];
            const auto old = old_nodes.find(node.handle);
            if (old == old_nodes.end()) {
                SendChildEvent("add", static_cast<std::int32_t>(index), node);
                SendCacheAdded(current, static_cast<std::int32_t>(index), node);
                continue;
            }
            const NativeAccessibilityNode& before = *old->second;
            if (before.label != node.label) {
                SendEvent(detail::LinuxAtSpiObjectPath(node.handle), "PropertyChange",
                    "accessible-name", 0, [&node](DBusMessageIter* variant) {
                        const char* value = node.label.c_str();
                        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &value);
                    }, "s");
            }
            if (before.bounds.x != node.bounds.x || before.bounds.y != node.bounds.y ||
                before.bounds.width != node.bounds.width || before.bounds.height != node.bounds.height) {
                SendEvent(detail::LinuxAtSpiObjectPath(node.handle), "BoundsChanged", "", 0,
                    [&node](DBusMessageIter* variant) {
                        DBusMessageIter bounds;
                        dbus_message_iter_open_container(variant, DBUS_TYPE_STRUCT, nullptr, &bounds);
                        const dbus_int32_t x = std::lround(node.bounds.x);
                        const dbus_int32_t y = std::lround(node.bounds.y);
                        const dbus_int32_t width = std::lround(node.bounds.width);
                        const dbus_int32_t height = std::lround(node.bounds.height);
                        dbus_message_iter_append_basic(&bounds, DBUS_TYPE_INT32, &x);
                        dbus_message_iter_append_basic(&bounds, DBUS_TYPE_INT32, &y);
                        dbus_message_iter_append_basic(&bounds, DBUS_TYPE_INT32, &width);
                        dbus_message_iter_append_basic(&bounds, DBUS_TYPE_INT32, &height);
                        dbus_message_iter_close_container(variant, &bounds);
                    }, "(iiii)");
            }
            if (before.selected != node.selected) SendStateEvent(node, "selected", node.selected);
            if (before.expanded != node.expanded) SendStateEvent(node, "expanded", node.expanded);
            if (before.disabled != node.disabled) {
                SendStateEvent(node, "enabled", !node.disabled);
                SendStateEvent(node, "sensitive", !node.disabled);
            }
            if (before.checked != node.checked) {
                SendStateEvent(node, "checked", node.checked == NativeAccessibilityCheckedState::True);
                SendStateEvent(node, "indeterminate", node.checked == NativeAccessibilityCheckedState::Mixed);
            }
            if (before.value != node.value) {
                SendEvent(detail::LinuxAtSpiObjectPath(node.handle), "PropertyChange",
                    "accessible-value", 0, [&node](DBusMessageIter* variant) {
                        const double value = node.value;
                        dbus_message_iter_append_basic(variant, DBUS_TYPE_DOUBLE, &value);
                    }, "d");
            }
        }
        if (previous.focused_handle != current.focused_handle) {
            const auto old = old_nodes.find(previous.focused_handle);
            if (old != old_nodes.end()) SendStateEvent(*old->second, "focused", false);
            const auto focused = new_nodes.find(current.focused_handle);
            if (focused != new_nodes.end()) SendStateEvent(*focused->second, "focused", true);
        }
    }

    static const char* Locale() {
        const char* locale = std::setlocale(LC_MESSAGES, nullptr);
        return locale == nullptr ? "C" : locale;
    }

    static const char* IntrospectionXml() {
        return "<node>"
            "<interface name='org.a11y.atspi.Accessible'/>"
            "<interface name='org.a11y.atspi.Component'/>"
            "<interface name='org.a11y.atspi.Action'/>"
            "<interface name='org.a11y.atspi.Application'/>"
            "<interface name='org.a11y.atspi.Value'/>"
            "<interface name='org.a11y.atspi.Text'/>"
            "<interface name='org.a11y.atspi.EditableText'/>"
            "<interface name='org.a11y.atspi.Cache'/>"
            "<interface name='org.freedesktop.DBus.Properties'/>"
            "</node>";
    }

    SDL_Window* window_ = nullptr;
    NativeAccessibilityActionHandler action_handler_;
    DBusConnection* connection_ = nullptr;
    std::string bus_name_;
    mutable std::mutex mutex_;
    NativeAccessibilitySnapshot snapshot_;
    std::shared_ptr<NativeAccessibilityTextProvider> text_provider_;
    std::string parent_bus_;
    std::string parent_path_ = kNullPath;
    std::int32_t application_id_ = -1;
    std::atomic<bool> registered_{false};
    int window_x_ = 0;
    int window_y_ = 0;
    int window_width_ = 0;
    int window_height_ = 0;
    int bus_fd_ = -1;
    int stop_fd_ = -1;
    std::thread listener_;
};

} // namespace

namespace detail {

std::uint32_t LinuxAtSpiRole(NativeAccessibilityRole role) {
    switch (role) {
        case NativeAccessibilityRole::Button: return 43U;
        case NativeAccessibilityRole::TextBox: return 79U;
        case NativeAccessibilityRole::Link: return 88U;
        case NativeAccessibilityRole::Heading: return 83U;
        case NativeAccessibilityRole::Form: return 87U;
        case NativeAccessibilityRole::List: return 31U;
        case NativeAccessibilityRole::ListItem: return 32U;
        case NativeAccessibilityRole::Image: return 27U;
        case NativeAccessibilityRole::Dialog: return 16U;
        case NativeAccessibilityRole::StaticText: return 116U;
        case NativeAccessibilityRole::CheckBox: return 7U;
        case NativeAccessibilityRole::Radio: return 44U;
        case NativeAccessibilityRole::RadioGroup: return 99U;
        case NativeAccessibilityRole::Switch: return 62U;
        case NativeAccessibilityRole::Slider: return 51U;
        case NativeAccessibilityRole::ComboBox: return 11U;
    }
    return kRolePanel;
}

std::vector<std::uint32_t> LinuxAtSpiStates(
    const NativeAccessibilityNode& node, bool focused) {
    std::vector<std::uint32_t> states(2U, 0U);
    AddState(states, kStateVisible);
    AddState(states, kStateShowing);
    if (!node.disabled) {
        AddState(states, kStateEnabled);
        AddState(states, kStateSensitive);
    }
    if (IsFocusable(node.role)) AddState(states, kStateFocusable);
    if (focused) AddState(states, kStateFocused);
    if (node.checked == NativeAccessibilityCheckedState::True) AddState(states, kStateChecked);
    if (node.checked == NativeAccessibilityCheckedState::Mixed) AddState(states, kStateIndeterminate);
    if (node.has_expanded) AddState(states, kStateExpandable);
    if (node.expanded) AddState(states, kStateExpanded);
    if (node.has_selected) AddState(states, kStateSelectable);
    if (node.selected) AddState(states, kStateSelected);
    if (node.role == NativeAccessibilityRole::TextBox) {
        AddState(states, node.multiline ? kStateMultiline : kStateSingleLine);
        if (node.read_only) AddState(states, kStateReadOnly);
        else AddState(states, kStateEditable);
    }
    if (node.orientation == NativeAccessibilityOrientation::Horizontal) AddState(states, kStateHorizontal);
    if (node.orientation == NativeAccessibilityOrientation::Vertical) AddState(states, kStateVertical);
    return states;
}

std::vector<std::string> LinuxAtSpiInterfaces(const NativeAccessibilityNode& node) {
    std::vector<std::string> interfaces{kAccessibleInterface, kComponentInterface};
    if (LinuxAtSpiActionCount(node) != 0U) interfaces.emplace_back(kActionInterface);
    if (node.has_value_range) interfaces.emplace_back(kValueInterface);
    if (LinuxAtSpiRoleSupportsText(node.role)) interfaces.emplace_back(kLinuxAtSpiTextInterface);
    if (LinuxAtSpiNodeSupportsEditableText(node)) {
        interfaces.emplace_back(kLinuxAtSpiEditableTextInterface);
    }
    return interfaces;
}

std::string LinuxAtSpiObjectPath(std::uint64_t handle) {
    std::ostringstream output;
    output << kAccessiblePrefix << "/effindom/h" << std::hex << handle;
    return output.str();
}

std::uint32_t LinuxAtSpiActionCount(const NativeAccessibilityNode& node) {
    if (node.role == NativeAccessibilityRole::Slider) return 2U;
    return IsInvokable(node.role) ? 1U : 0U;
}

} // namespace detail

std::unique_ptr<NativeAccessibilityAdapter> CreateLinuxAccessibilityAdapter(
    SDL_Window* window, NativeAccessibilityActionHandler action_handler) {
    if (window == nullptr) return nullptr;
    auto adapter = std::make_unique<LinuxAccessibilityAdapter>(
        window, std::move(action_handler));
    if (!adapter->IsConnected()) return nullptr;
    return adapter;
}

} // namespace effindom::v2::native
