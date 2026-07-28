#include "LinuxAccessibilityText.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace effindom::v2::native::detail {
namespace {

constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";

DBusMessage* ErrorReply(
    DBusMessage* request, const char* name, const char* message) {
    return dbus_message_new_error(request, name, message);
}

DBusMessage* InvalidArguments(DBusMessage* request, const char* message) {
    return ErrorReply(request, DBUS_ERROR_INVALID_ARGS, message);
}

DBusMessage* UnavailableText(DBusMessage* request) {
    return ErrorReply(request, DBUS_ERROR_ACCESS_DENIED,
        "Text is unavailable for this accessible object");
}

DBusMessage* StatusError(
    DBusMessage* request, NativeAccessibilityTextStatus status) {
    switch (status) {
        case NativeAccessibilityTextStatus::Ok:
            return nullptr;
        case NativeAccessibilityTextStatus::Obscured:
            return UnavailableText(request);
        case NativeAccessibilityTextStatus::StaleRevision:
            return ErrorReply(request, DBUS_ERROR_FAILED,
                "The text range revision is stale");
        case NativeAccessibilityTextStatus::InvalidRange:
            return InvalidArguments(request, "Text range is invalid");
        case NativeAccessibilityTextStatus::ReadOnly:
            return ErrorReply(request, DBUS_ERROR_ACCESS_DENIED,
                "Text is read-only");
        case NativeAccessibilityTextStatus::NotText:
        case NativeAccessibilityTextStatus::BufferTooSmall:
        case NativeAccessibilityTextStatus::InvalidText:
            return ErrorReply(request, DBUS_ERROR_FAILED,
                "The text operation failed");
    }
    return ErrorReply(request, DBUS_ERROR_FAILED, "The text operation failed");
}

bool Execute(const LinuxAtSpiTextObject& object,
    const std::function<void()>& operation) {
    if (object.execute) return object.execute(operation);
    operation();
    return true;
}

bool GetInfo(const LinuxAtSpiTextObject& object,
    NativeAccessibilityTextInfo& info) {
    bool available = false;
    return object.provider != nullptr && Execute(object, [&] {
        available = object.provider->GetInfo(object.handle, info);
    }) && available;
}

std::uint32_t NormalizeEnd(
    dbus_int32_t end, const NativeAccessibilityTextInfo& info) {
    return end < 0 ? info.character_count : static_cast<std::uint32_t>(end);
}

bool ValidRange(dbus_int32_t start, dbus_int32_t end,
    const NativeAccessibilityTextInfo& info) {
    return start >= 0 && (end < 0 || end >= start) &&
        static_cast<std::uint32_t>(start) <= info.character_count &&
        NormalizeEnd(end, info) <= info.character_count;
}

bool ValidEndpoints(dbus_int32_t start, dbus_int32_t end,
    const NativeAccessibilityTextInfo& info) {
    return start >= 0 && end >= 0 &&
        static_cast<std::uint32_t>(start) <= info.character_count &&
        static_cast<std::uint32_t>(end) <= info.character_count;
}

std::uint32_t FirstScalar(std::string_view text) {
    if (text.empty()) return 0U;
    const auto first = static_cast<std::uint8_t>(text[0]);
    if ((first & 0x80U) == 0U) return first;
    if ((first & 0xE0U) == 0xC0U && text.size() >= 2U) {
        return ((first & 0x1FU) << 6U) |
            (static_cast<std::uint8_t>(text[1]) & 0x3FU);
    }
    if ((first & 0xF0U) == 0xE0U && text.size() >= 3U) {
        return ((first & 0x0FU) << 12U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[2]) & 0x3FU);
    }
    if ((first & 0xF8U) == 0xF0U && text.size() >= 4U) {
        return ((first & 0x07U) << 18U) |
            ((static_cast<std::uint8_t>(text[1]) & 0x3FU) << 12U) |
            ((static_cast<std::uint8_t>(text[2]) & 0x3FU) << 6U) |
            (static_cast<std::uint8_t>(text[3]) & 0x3FU);
    }
    return 0xFFFDU;
}

std::string PrefixScalars(std::string_view text, std::uint32_t count) {
    std::size_t byte = 0U;
    std::uint32_t scalars = 0U;
    while (byte < text.size() && scalars < count) {
        const auto first = static_cast<std::uint8_t>(text[byte]);
        std::size_t width = 1U;
        if ((first & 0xE0U) == 0xC0U) width = 2U;
        else if ((first & 0xF0U) == 0xE0U) width = 3U;
        else if ((first & 0xF8U) == 0xF0U) width = 4U;
        byte = std::min(text.size(), byte + width);
        ++scalars;
    }
    return std::string(text.substr(0U, byte));
}

NativeAccessibilityTextRect UnionRects(
    const std::vector<NativeAccessibilityTextRect>& rects) {
    if (rects.empty()) return {};
    float left = rects.front().x;
    float top = rects.front().y;
    float right = left + rects.front().width;
    float bottom = top + rects.front().height;
    for (const auto& rect : rects) {
        left = std::min(left, rect.x);
        top = std::min(top, rect.y);
        right = std::max(right, rect.x + rect.width);
        bottom = std::max(bottom, rect.y + rect.height);
    }
    return {left, top, right - left, bottom - top};
}

void AppendRect(DBusMessage* reply, NativeAccessibilityTextRect rect,
    dbus_uint32_t coordinates, const LinuxAtSpiTextObject& object,
    bool structure) {
    dbus_int32_t x = static_cast<dbus_int32_t>(std::lround(rect.x)) +
        (coordinates == 0U ? object.window_x : 0);
    dbus_int32_t y = static_cast<dbus_int32_t>(std::lround(rect.y)) +
        (coordinates == 0U ? object.window_y : 0);
    dbus_int32_t width = static_cast<dbus_int32_t>(std::lround(rect.width));
    dbus_int32_t height = static_cast<dbus_int32_t>(std::lround(rect.height));
    if (!structure) {
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &x,
            DBUS_TYPE_INT32, &y, DBUS_TYPE_INT32, &width,
            DBUS_TYPE_INT32, &height, DBUS_TYPE_INVALID);
        return;
    }
    DBusMessageIter output;
    DBusMessageIter value;
    dbus_message_iter_init_append(reply, &output);
    dbus_message_iter_open_container(&output, DBUS_TYPE_STRUCT, nullptr, &value);
    dbus_message_iter_append_basic(&value, DBUS_TYPE_INT32, &x);
    dbus_message_iter_append_basic(&value, DBUS_TYPE_INT32, &y);
    dbus_message_iter_append_basic(&value, DBUS_TYPE_INT32, &width);
    dbus_message_iter_append_basic(&value, DBUS_TYPE_INT32, &height);
    dbus_message_iter_close_container(&output, &value);
}

DBusMessage* HandleTextProperties(DBusMessage* request,
    const LinuxAtSpiTextObject& object) {
    const char* interface_name = nullptr;
    const char* property_name = nullptr;
    const bool get_all = dbus_message_is_method_call(
        request, kPropertiesInterface, "GetAll");
    if (get_all) {
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING,
                &interface_name, DBUS_TYPE_INVALID)) {
            return InvalidArguments(request, "Invalid text property interface");
        }
    } else if (!dbus_message_get_args(request, nullptr,
            DBUS_TYPE_STRING, &interface_name, DBUS_TYPE_STRING,
            &property_name, DBUS_TYPE_INVALID)) {
        return InvalidArguments(request, "Invalid text property request");
    }
    if (interface_name == nullptr ||
        std::strcmp(interface_name, kLinuxAtSpiTextInterface) != 0) return nullptr;
    NativeAccessibilityTextInfo info;
    if (!GetInfo(object, info)) return UnavailableText(request);
    const auto append_property = [&info](DBusMessageIter* output, const char* property) {
        dbus_int32_t value = std::strcmp(property, "CharacterCount") == 0
            ? static_cast<dbus_int32_t>(info.character_count)
            : static_cast<dbus_int32_t>(info.selection_end);
        dbus_message_iter_append_basic(output, DBUS_TYPE_INT32, &value);
    };
    DBusMessage* reply = dbus_message_new_method_return(request);
    DBusMessageIter output;
    dbus_message_iter_init_append(reply, &output);
    if (get_all) {
        DBusMessageIter dictionary;
        dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "{sv}", &dictionary);
        for (const char* property : {"CharacterCount", "CaretOffset"}) {
            DBusMessageIter entry;
            DBusMessageIter variant;
            dbus_message_iter_open_container(
                &dictionary, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &property);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &variant);
            append_property(&variant, property);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&dictionary, &entry);
        }
        dbus_message_iter_close_container(&output, &dictionary);
        return reply;
    }
    if (property_name == nullptr ||
        (std::strcmp(property_name, "CharacterCount") != 0 &&
         std::strcmp(property_name, "CaretOffset") != 0)) {
        dbus_message_unref(reply);
        return ErrorReply(request, DBUS_ERROR_UNKNOWN_PROPERTY,
            "Unknown text property");
    }
    DBusMessageIter variant;
    dbus_message_iter_open_container(&output, DBUS_TYPE_VARIANT, "i", &variant);
    append_property(&variant, property_name);
    dbus_message_iter_close_container(&output, &variant);
    return reply;
}

DBusMessage* HandleText(DBusMessage* request,
    const LinuxAtSpiTextObject& object) {
    NativeAccessibilityTextInfo info;
    if (!GetInfo(object, info)) return UnavailableText(request);
    const char* member = dbus_message_get_member(request);
    DBusMessage* reply = dbus_message_new_method_return(request);
    if (std::strcmp(member, "GetText") == 0) {
        dbus_int32_t start = -1;
        dbus_int32_t end = -1;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &start,
                DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID) ||
            !ValidRange(start, end, info)) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Invalid text range");
        }
        std::string text;
        NativeAccessibilityTextStatus status{};
        if (!Execute(object, [&] {
                status = object.provider->ReadRange(object.handle, info.revision,
                    static_cast<std::uint32_t>(start), NormalizeEnd(end, info), text);
            })) {
            dbus_message_unref(reply);
            return ErrorReply(request, DBUS_ERROR_FAILED, "Text query dispatch failed");
        }
        if (status != NativeAccessibilityTextStatus::Ok) {
            dbus_message_unref(reply);
            return StatusError(request, status);
        }
        const char* value = text.c_str();
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "SetCaretOffset") == 0) {
        dbus_int32_t offset = -1;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &offset,
                DBUS_TYPE_INVALID) || offset < 0 ||
            static_cast<std::uint32_t>(offset) > info.character_count) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Caret offset is out of range");
        }
        NativeAccessibilityTextStatus status{};
        if (!Execute(object, [&] {
                status = object.provider->SetSelection(object.handle, info.revision,
                    static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(offset));
            })) status = NativeAccessibilityTextStatus::NotText;
        const dbus_bool_t success = status == NativeAccessibilityTextStatus::Ok;
        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "GetCharacterAtOffset") == 0) {
        dbus_int32_t offset = -1;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &offset,
                DBUS_TYPE_INVALID) || offset < 0 ||
            static_cast<std::uint32_t>(offset) >= info.character_count) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Character offset is out of range");
        }
        std::string text;
        NativeAccessibilityTextStatus status{};
        if (!Execute(object, [&] {
                status = object.provider->ReadRange(object.handle, info.revision,
                    static_cast<std::uint32_t>(offset),
                    static_cast<std::uint32_t>(offset) + 1U, text);
            })) status = NativeAccessibilityTextStatus::NotText;
        if (status != NativeAccessibilityTextStatus::Ok) {
            dbus_message_unref(reply);
            return StatusError(request, status);
        }
        const dbus_int32_t scalar = static_cast<dbus_int32_t>(FirstScalar(text));
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &scalar, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "GetCharacterExtents") == 0 ||
        std::strcmp(member, "GetRangeExtents") == 0) {
        dbus_int32_t start = -1;
        dbus_int32_t end = -1;
        dbus_uint32_t coordinates = 0U;
        const bool character = std::strcmp(member, "GetCharacterExtents") == 0;
        const bool valid_args = character
            ? dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &start,
                DBUS_TYPE_UINT32, &coordinates, DBUS_TYPE_INVALID)
            : dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &start,
                DBUS_TYPE_INT32, &end, DBUS_TYPE_UINT32, &coordinates,
                DBUS_TYPE_INVALID);
        if (character) end = start + 1;
        if (!valid_args || !ValidRange(start, end, info)) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Geometry range is invalid");
        }
        std::vector<NativeAccessibilityTextRect> rects;
        NativeAccessibilityTextStatus status{};
        if (!Execute(object, [&] {
                status = object.provider->RangeRects(object.handle, info.revision,
                    static_cast<std::uint32_t>(start), NormalizeEnd(end, info), rects);
            })) status = NativeAccessibilityTextStatus::NotText;
        if (status != NativeAccessibilityTextStatus::Ok) {
            dbus_message_unref(reply);
            return StatusError(request, status);
        }
        AppendRect(reply, UnionRects(rects), coordinates, object, !character);
    } else if (std::strcmp(member, "GetOffsetAtPoint") == 0) {
        dbus_int32_t x = 0;
        dbus_int32_t y = 0;
        dbus_uint32_t coordinates = 0U;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &x,
                DBUS_TYPE_INT32, &y, DBUS_TYPE_UINT32, &coordinates,
                DBUS_TYPE_INVALID)) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Invalid point");
        }
        (void)x;
        (void)y;
        (void)coordinates;
        const dbus_int32_t offset = -1;
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &offset, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "GetNSelections") == 0) {
        const dbus_int32_t count = info.selection_start == info.selection_end ? 0 : 1;
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &count, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "GetSelection") == 0) {
        dbus_int32_t index = -1;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &index,
                DBUS_TYPE_INVALID) || index != 0 ||
            info.selection_start == info.selection_end) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Selection index is out of range");
        }
        const dbus_int32_t start = static_cast<dbus_int32_t>(info.selection_start);
        const dbus_int32_t end = static_cast<dbus_int32_t>(info.selection_end);
        dbus_message_append_args(reply, DBUS_TYPE_INT32, &start,
            DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "AddSelection") == 0 ||
        std::strcmp(member, "SetSelection") == 0) {
        dbus_int32_t index = 0;
        dbus_int32_t start = -1;
        dbus_int32_t end = -1;
        const bool set = std::strcmp(member, "SetSelection") == 0;
        const bool valid_args = set
            ? dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &index,
                DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID)
            : dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &start,
                DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
        if (!valid_args || (set && index != 0) || !ValidEndpoints(start, end, info)) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Selection range is invalid");
        }
        NativeAccessibilityTextStatus status{};
        if (!Execute(object, [&] {
                status = object.provider->SetSelection(object.handle, info.revision,
                    static_cast<std::uint32_t>(start), NormalizeEnd(end, info));
            })) status = NativeAccessibilityTextStatus::NotText;
        const dbus_bool_t success = status == NativeAccessibilityTextStatus::Ok;
        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "RemoveSelection") == 0) {
        dbus_int32_t index = -1;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &index,
                DBUS_TYPE_INVALID) || index != 0 ||
            info.selection_start == info.selection_end) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Selection index is out of range");
        }
        NativeAccessibilityTextStatus status{};
        if (!Execute(object, [&] {
                status = object.provider->SetSelection(object.handle, info.revision,
                    info.selection_end, info.selection_end);
            })) status = NativeAccessibilityTextStatus::NotText;
        const dbus_bool_t success = status == NativeAccessibilityTextStatus::Ok;
        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
    } else if (std::strcmp(member, "ScrollSubstringTo") == 0) {
        dbus_int32_t start = -1;
        dbus_int32_t end = -1;
        dbus_uint32_t scroll_type = 0U;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &start,
                DBUS_TYPE_INT32, &end, DBUS_TYPE_UINT32, &scroll_type,
                DBUS_TYPE_INVALID) || !ValidRange(start, end, info)) {
            dbus_message_unref(reply);
            return InvalidArguments(request, "Reveal range is invalid");
        }
        (void)scroll_type;
        NativeAccessibilityTextStatus status{};
        if (!Execute(object, [&] {
                status = object.provider->RevealRange(object.handle, info.revision,
                    static_cast<std::uint32_t>(start), NormalizeEnd(end, info));
            })) status = NativeAccessibilityTextStatus::NotText;
        const dbus_bool_t success = status == NativeAccessibilityTextStatus::Ok;
        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
    } else {
        dbus_message_unref(reply);
        return nullptr;
    }
    return reply;
}

DBusMessage* HandleEditableText(DBusMessage* request,
    const LinuxAtSpiTextObject& object) {
    NativeAccessibilityTextInfo info;
    if (!GetInfo(object, info)) return UnavailableText(request);
    if (info.read_only) return ErrorReply(
        request, DBUS_ERROR_ACCESS_DENIED, "Text is read-only");
    const char* member = dbus_message_get_member(request);
    dbus_int32_t start = 0;
    dbus_int32_t end = 0;
    std::string replacement;
    if (std::strcmp(member, "SetTextContents") == 0) {
        const char* text = nullptr;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING, &text,
                DBUS_TYPE_INVALID) || text == nullptr) {
            return InvalidArguments(request, "Invalid replacement text");
        }
        end = static_cast<dbus_int32_t>(info.character_count);
        replacement = text;
    } else if (std::strcmp(member, "InsertText") == 0) {
        const char* text = nullptr;
        dbus_int32_t length = -1;
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &start,
                DBUS_TYPE_STRING, &text, DBUS_TYPE_INT32, &length,
                DBUS_TYPE_INVALID) || start < 0 || length < 0 || text == nullptr ||
            static_cast<std::uint32_t>(start) > info.character_count) {
            return InvalidArguments(request, "Invalid inserted text");
        }
        end = start;
        replacement = PrefixScalars(text, static_cast<std::uint32_t>(length));
    } else if (std::strcmp(member, "DeleteText") == 0) {
        if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &start,
                DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID) ||
            !ValidRange(start, end, info)) {
            return InvalidArguments(request, "Invalid deletion range");
        }
    } else if (std::strcmp(member, "SetAttributes") == 0 ||
        std::strcmp(member, "CopyText") == 0 ||
        std::strcmp(member, "CutText") == 0 ||
        std::strcmp(member, "PasteText") == 0) {
        DBusMessage* reply = dbus_message_new_method_return(request);
        const dbus_bool_t success = false;
        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
        return reply;
    } else {
        return nullptr;
    }
    NativeAccessibilityTextStatus status{};
    std::uint64_t output_revision = info.revision;
    if (!Execute(object, [&] {
            status = object.provider->ReplaceRange(object.handle, info.revision,
                static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end),
                replacement, output_revision);
        })) status = NativeAccessibilityTextStatus::NotText;
    DBusMessage* reply = dbus_message_new_method_return(request);
    const dbus_bool_t success = status == NativeAccessibilityTextStatus::Ok;
    dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
    return reply;
}

} // namespace

bool LinuxAtSpiRoleSupportsText(NativeAccessibilityRole role) {
    return role == NativeAccessibilityRole::TextBox ||
        role == NativeAccessibilityRole::StaticText ||
        role == NativeAccessibilityRole::Heading ||
        role == NativeAccessibilityRole::Link;
}

bool LinuxAtSpiNodeSupportsEditableText(const NativeAccessibilityNode& node) {
    return node.role == NativeAccessibilityRole::TextBox && !node.read_only;
}

bool IsLinuxAtSpiTextPropertyRequest(DBusMessage* request) {
    if (!dbus_message_is_method_call(request, kPropertiesInterface, "Get") &&
        !dbus_message_is_method_call(request, kPropertiesInterface, "GetAll")) return false;
    DBusMessageIter input;
    if (!dbus_message_iter_init(request, &input) ||
        dbus_message_iter_get_arg_type(&input) != DBUS_TYPE_STRING) return false;
    const char* interface_name = nullptr;
    dbus_message_iter_get_basic(&input, &interface_name);
    return interface_name != nullptr &&
        std::strcmp(interface_name, kLinuxAtSpiTextInterface) == 0;
}

DBusMessage* HandleLinuxAtSpiTextMessage(
    DBusMessage* request, const LinuxAtSpiTextObject& object) {
    if (IsLinuxAtSpiTextPropertyRequest(request)) {
        return HandleTextProperties(request, object);
    }
    if (dbus_message_has_interface(request, kLinuxAtSpiTextInterface)) {
        return HandleText(request, object);
    }
    if (dbus_message_has_interface(request, kLinuxAtSpiEditableTextInterface)) {
        return HandleEditableText(request, object);
    }
    return nullptr;
}

LinuxAtSpiTextEventDescriptor LinuxAtSpiTextEvent(
    NativeAccessibilityTextEvent event) {
    if (event == NativeAccessibilityTextEvent::DocumentChanged) {
        return {"TextChanged", "", false};
    }
    return {"TextSelectionChanged", "", true};
}

} // namespace effindom::v2::native::detail
