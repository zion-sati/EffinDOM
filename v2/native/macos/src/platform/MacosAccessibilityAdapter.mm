#include "MacosAccessibilityAdapter.h"

#include "SDL3/SDL.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NSAccessibilityHeadingRole
#define NSAccessibilityHeadingRole @"AXHeading"
#endif

using effindom::v2::native::NativeAccessibilityAction;
using effindom::v2::native::NativeAccessibilityActionHandler;
using effindom::v2::native::NativeAccessibilityRole;
using effindom::v2::native::NativeAccessibilityTextInfo;
using effindom::v2::native::NativeAccessibilityTextProvider;
using effindom::v2::native::NativeAccessibilityTextStatus;

namespace {

bool DecodeUtf8(std::string_view text, std::size_t& offset, std::uint32_t& scalar) {
    if (offset >= text.size()) return false;
    const auto first = static_cast<std::uint8_t>(text[offset]);
    std::size_t width = 1U;
    scalar = first;
    if ((first & 0xE0U) == 0xC0U) { width = 2U; scalar = first & 0x1FU; }
    else if ((first & 0xF0U) == 0xE0U) { width = 3U; scalar = first & 0x0FU; }
    else if ((first & 0xF8U) == 0xF0U) { width = 4U; scalar = first & 0x07U; }
    if (offset + width > text.size()) return false;
    for (std::size_t index = 1U; index < width; ++index) {
        const auto next = static_cast<std::uint8_t>(text[offset + index]);
        if ((next & 0xC0U) != 0x80U) return false;
        scalar = (scalar << 6U) | (next & 0x3FU);
    }
    offset += width;
    return true;
}

bool LoadText(NativeAccessibilityTextProvider* provider, std::uint64_t handle,
    NativeAccessibilityTextInfo& info, std::string& text) {
    return provider != nullptr && provider->GetInfo(handle, info) &&
        provider->ReadRange(handle, info.revision, 0U, info.character_count, text) ==
            NativeAccessibilityTextStatus::Ok;
}

NSString* ToNSString(const std::string& text) {
    return [[[NSString alloc] initWithBytes:text.data() length:text.size()
        encoding:NSUTF8StringEncoding] autorelease];
}

} // namespace

@interface EffinDomAccessibilityElement : NSAccessibilityElement {
@public
    std::uint64_t effindomHandle;
    NativeAccessibilityRole effindomRole;
    NativeAccessibilityActionHandler* effindomActionHandler;
    NativeAccessibilityTextProvider* effindomTextProvider;
    NSView* effindomContentView;
}
@end

@implementation EffinDomAccessibilityElement
- (BOOL)accessibilityPerformPress {
    if (effindomActionHandler == nullptr) return NO;
    (*effindomActionHandler)(NativeAccessibilityAction::Press, effindomHandle);
    return YES;
}
- (BOOL)accessibilityPerformIncrement {
    if (effindomActionHandler == nullptr) return NO;
    (*effindomActionHandler)(NativeAccessibilityAction::Increment, effindomHandle);
    return YES;
}
- (BOOL)accessibilityPerformDecrement {
    if (effindomActionHandler == nullptr) return NO;
    (*effindomActionHandler)(NativeAccessibilityAction::Decrement, effindomHandle);
    return YES;
}
- (void)setAccessibilityFocused:(BOOL)focused {
    if (focused && effindomActionHandler != nullptr) {
        (*effindomActionHandler)(NativeAccessibilityAction::Focus, effindomHandle);
    }
}
- (NSInteger)accessibilityNumberOfCharacters {
    NativeAccessibilityTextInfo info;
    std::string text;
    return LoadText(effindomTextProvider, effindomHandle, info, text)
        ? static_cast<NSInteger>(effindom::v2::native::detail::MacosAccessibilityUtf16Length(text)) : 0;
}
- (id)accessibilityValue {
    if (effindomRole != NativeAccessibilityRole::TextBox &&
        effindomRole != NativeAccessibilityRole::StaticText) return [super accessibilityValue];
    NativeAccessibilityTextInfo info;
    std::string text;
    return LoadText(effindomTextProvider, effindomHandle, info, text) ? ToNSString(text) : nil;
}
- (void)setAccessibilityValue:(id)value {
    if (effindomRole != NativeAccessibilityRole::TextBox) {
        [super setAccessibilityValue:value];
        return;
    }
    NativeAccessibilityTextInfo info;
    if (effindomTextProvider == nullptr || !effindomTextProvider->GetInfo(effindomHandle, info) ||
        info.read_only || ![value isKindOfClass:[NSString class]]) return;
    NSString* string = (NSString*)value;
    const char* utf8 = [string UTF8String];
    std::uint64_t revision = 0U;
    effindomTextProvider->ReplaceRange(effindomHandle, info.revision, 0U,
        info.character_count, utf8 == nullptr ? std::string{} : std::string(utf8), revision);
}
- (NSRange)accessibilityVisibleCharacterRange {
    const NSInteger length = [self accessibilityNumberOfCharacters];
    return NSMakeRange(0U, static_cast<NSUInteger>(std::max<NSInteger>(0, length)));
}
- (NSRange)accessibilitySelectedTextRange {
    NativeAccessibilityTextInfo info;
    std::string text;
    std::uint32_t location = 0U;
    std::uint32_t length = 0U;
    if (!LoadText(effindomTextProvider, effindomHandle, info, text) ||
        !effindom::v2::native::detail::MacosAccessibilityCharacterRangeToUtf16Range(
            text, info.selection_start, info.selection_end, location, length)) return NSMakeRange(0U, 0U);
    return NSMakeRange(location, length);
}
- (void)setAccessibilitySelectedTextRange:(NSRange)range {
    if (range.location > UINT32_MAX || range.length > UINT32_MAX) return;
    NativeAccessibilityTextInfo info;
    std::string text;
    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
    if (LoadText(effindomTextProvider, effindomHandle, info, text) &&
        effindom::v2::native::detail::MacosAccessibilityUtf16RangeToCharacterRange(
            text, static_cast<std::uint32_t>(range.location),
            static_cast<std::uint32_t>(range.length), start, end)) {
        effindomTextProvider->SetSelection(effindomHandle, info.revision, start, end);
    }
}
- (NSString*)accessibilitySelectedText {
    return [self accessibilityStringForRange:[self accessibilitySelectedTextRange]];
}
- (void)setAccessibilitySelectedText:(NSString*)replacement {
    NativeAccessibilityTextInfo info;
    if (effindomTextProvider == nullptr || !effindomTextProvider->GetInfo(effindomHandle, info) ||
        info.read_only || replacement == nil) return;
    const char* utf8 = [replacement UTF8String];
    std::uint64_t revision = 0U;
    effindomTextProvider->ReplaceRange(effindomHandle, info.revision,
        std::min(info.selection_start, info.selection_end),
        std::max(info.selection_start, info.selection_end),
        utf8 == nullptr ? std::string{} : std::string(utf8), revision);
}
- (NSArray<NSValue*>*)accessibilitySelectedTextRanges {
    return @[[NSValue valueWithRange:[self accessibilitySelectedTextRange]]];
}
- (NSString*)accessibilityStringForRange:(NSRange)range {
    if (range.location > UINT32_MAX || range.length > UINT32_MAX) return nil;
    NativeAccessibilityTextInfo info;
    std::string text;
    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
    if (!LoadText(effindomTextProvider, effindomHandle, info, text) ||
        !effindom::v2::native::detail::MacosAccessibilityUtf16RangeToCharacterRange(
            text, static_cast<std::uint32_t>(range.location),
            static_cast<std::uint32_t>(range.length), start, end)) return nil;
    std::string result;
    return effindomTextProvider->ReadRange(effindomHandle, info.revision, start, end, result) ==
        NativeAccessibilityTextStatus::Ok ? ToNSString(result) : nil;
}
- (NSAttributedString*)accessibilityAttributedStringForRange:(NSRange)range {
    NSString* text = [self accessibilityStringForRange:range];
    return text == nil ? nil : [[[NSAttributedString alloc] initWithString:text] autorelease];
}
- (NSRect)accessibilityFrameForRange:(NSRange)range {
    if (range.location > UINT32_MAX || range.length > UINT32_MAX) return NSZeroRect;
    NativeAccessibilityTextInfo info;
    std::string text;
    std::uint32_t start = 0U;
    std::uint32_t end = 0U;
    if (!LoadText(effindomTextProvider, effindomHandle, info, text) ||
        !effindom::v2::native::detail::MacosAccessibilityUtf16RangeToCharacterRange(
            text, static_cast<std::uint32_t>(range.location),
            static_cast<std::uint32_t>(range.length), start, end)) return NSZeroRect;
    std::vector<effindom::v2::native::NativeAccessibilityTextRect> rects;
    if (effindomTextProvider->RangeRects(effindomHandle, info.revision, start, end, rects) !=
        NativeAccessibilityTextStatus::Ok || rects.empty() || effindomContentView == nil) return NSZeroRect;
    NSRect combined = NSZeroRect;
    const NSRect content_bounds = [effindomContentView bounds];
    for (const auto& rect : rects) {
        const NSRect local = NSMakeRect(rect.x,
            NSHeight(content_bounds) - rect.y - rect.height, rect.width, rect.height);
        combined = NSEqualRects(combined, NSZeroRect) ? local : NSUnionRect(combined, local);
    }
    return [[effindomContentView window] convertRectToScreen:combined];
}
@end

namespace effindom::v2::native {
namespace detail {

class MacosAccessibilityAdapterProbe;

std::uint32_t MacosAccessibilityUtf16Length(std::string_view utf8) {
    std::size_t offset = 0U;
    std::uint32_t length = 0U;
    std::uint32_t scalar = 0U;
    while (DecodeUtf8(utf8, offset, scalar)) length += scalar > 0xFFFFU ? 2U : 1U;
    return length;
}

bool MacosAccessibilityUtf16RangeToCharacterRange(std::string_view utf8,
    std::uint32_t utf16_location, std::uint32_t utf16_length,
    std::uint32_t& character_start, std::uint32_t& character_end) {
    const std::uint64_t requested_end = static_cast<std::uint64_t>(utf16_location) + utf16_length;
    std::size_t offset = 0U;
    std::uint32_t utf16 = 0U;
    std::uint32_t character = 0U;
    bool found_start = utf16_location == 0U;
    character_start = 0U;
    if (found_start && requested_end == 0U) {
        character_end = 0U;
        return true;
    }
    while (offset < utf8.size()) {
        if (!found_start && utf16 == utf16_location) {
            character_start = character;
            found_start = true;
        }
        if (found_start && utf16 == requested_end) {
            character_end = character;
            return true;
        }
        std::uint32_t scalar = 0U;
        if (!DecodeUtf8(utf8, offset, scalar)) return false;
        const std::uint32_t width = scalar > 0xFFFFU ? 2U : 1U;
        if ((!found_start && utf16 > utf16_location) ||
            (!found_start && utf16 + width > utf16_location) ||
            (requested_end > utf16 && requested_end < static_cast<std::uint64_t>(utf16 + width))) return false;
        utf16 += width;
        ++character;
        if (found_start && utf16 == requested_end) {
            character_end = character;
            return true;
        }
    }
    if (!found_start && utf16 == utf16_location) {
        character_start = character;
        found_start = true;
    }
    if (found_start && utf16 == requested_end) {
        character_end = character;
        return true;
    }
    return false;
}

bool MacosAccessibilityCharacterRangeToUtf16Range(std::string_view utf8,
    std::uint32_t character_start, std::uint32_t character_end,
    std::uint32_t& utf16_location, std::uint32_t& utf16_length) {
    if (character_start > character_end) std::swap(character_start, character_end);
    std::size_t offset = 0U;
    std::uint32_t character = 0U;
    std::uint32_t utf16 = 0U;
    bool found_start = character_start == 0U;
    utf16_location = 0U;
    while (offset < utf8.size()) {
        if (!found_start && character == character_start) {
            utf16_location = utf16;
            found_start = true;
        }
        if (found_start && character == character_end) {
            utf16_length = utf16 - utf16_location;
            return true;
        }
        std::uint32_t scalar = 0U;
        if (!DecodeUtf8(utf8, offset, scalar)) return false;
        utf16 += scalar > 0xFFFFU ? 2U : 1U;
        ++character;
    }
    if (!found_start && character == character_start) {
        utf16_location = utf16;
        found_start = true;
    }
    if (found_start && character == character_end) {
        utf16_length = utf16 - utf16_location;
        return true;
    }
    return false;
}

} // namespace detail
namespace {

class MacosAccessibilityAdapter;
MacosAccessibilityAdapter* active_adapter = nullptr;

NSString* RoleName(NativeAccessibilityRole role) {
    switch (role) {
        case NativeAccessibilityRole::Button: return NSAccessibilityButtonRole;
        case NativeAccessibilityRole::TextBox: return NSAccessibilityTextFieldRole;
        case NativeAccessibilityRole::Link: return NSAccessibilityLinkRole;
        case NativeAccessibilityRole::Heading: return NSAccessibilityHeadingRole;
        case NativeAccessibilityRole::Form: return NSAccessibilityGroupRole;
        case NativeAccessibilityRole::List: return NSAccessibilityListRole;
        case NativeAccessibilityRole::ListItem: return NSAccessibilityGroupRole;
        case NativeAccessibilityRole::Image: return NSAccessibilityImageRole;
        case NativeAccessibilityRole::Dialog: return NSAccessibilityGroupRole;
        case NativeAccessibilityRole::StaticText: return NSAccessibilityStaticTextRole;
        case NativeAccessibilityRole::CheckBox: return NSAccessibilityCheckBoxRole;
        case NativeAccessibilityRole::Radio: return NSAccessibilityRadioButtonRole;
        case NativeAccessibilityRole::RadioGroup: return NSAccessibilityRadioGroupRole;
        case NativeAccessibilityRole::Switch: return NSAccessibilityCheckBoxRole;
        case NativeAccessibilityRole::Slider: return NSAccessibilitySliderRole;
        case NativeAccessibilityRole::ComboBox: return NSAccessibilityComboBoxRole;
        case NativeAccessibilityRole::TabList: return NSAccessibilityTabGroupRole;
        case NativeAccessibilityRole::Tab: return NSAccessibilityRadioButtonRole;
        case NativeAccessibilityRole::TabPanel: return NSAccessibilityGroupRole;
    }
}

class MacosAccessibilityAdapter final : public NativeAccessibilityAdapter {
public:
    MacosAccessibilityAdapter(SDL_Window* window, NativeAccessibilityActionHandler action_handler)
        : window_(window), action_handler_(std::move(action_handler)) {
        NSWindow* native_window = (__bridge NSWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        content_view_ = [native_window contentView];
        [content_view_ setAccessibilityElement:YES];
        [content_view_ setAccessibilityRole:NSAccessibilityGroupRole];
        [content_view_ setAccessibilityLabel:@"EffinDOM application"];
        active_adapter = this;
    }

    ~MacosAccessibilityAdapter() override {
        if (active_adapter == this) active_adapter = nullptr;
        Clear();
    }

    void SetTextProvider(std::shared_ptr<NativeAccessibilityTextProvider> provider) override {
        text_provider_ = std::move(provider);
    }

    void TextChanged(NativeAccessibilityTextEvent event, std::uint64_t handle) override {
        const auto found = elements_.find(handle);
        if (found == elements_.end()) return;
        NSAccessibilityPostNotification(found->second,
            event == NativeAccessibilityTextEvent::DocumentChanged
                ? NSAccessibilityValueChangedNotification
                : NSAccessibilitySelectedTextChangedNotification);
    }

    void Update(const NativeAccessibilitySnapshot& snapshot) override {
        @autoreleasepool {
            NSMutableArray* children = [NSMutableArray arrayWithCapacity:snapshot.nodes.size()];
            std::unordered_map<std::uint64_t, EffinDomAccessibilityElement*> retained;
            retained.reserve(snapshot.nodes.size());
            for (const auto& node : snapshot.nodes) {
                EffinDomAccessibilityElement* element = nullptr;
                const auto existing = elements_.find(node.handle);
                if (existing != elements_.end()) {
                    element = existing->second;
                    [element retain];
                } else {
                    element = [[EffinDomAccessibilityElement alloc] init];
                }
                element->effindomHandle = node.handle;
                element->effindomRole = node.role;
                element->effindomActionHandler = &action_handler_;
                element->effindomTextProvider = text_provider_.get();
                element->effindomContentView = content_view_;
                [element setAccessibilityParent:content_view_];
                [element setAccessibilityRole:RoleName(node.role)];
                [element setAccessibilityIdentifier:
                    [NSString stringWithFormat:@"effindom-%llu", node.handle]];
                NSString* label = [[NSString alloc] initWithBytes:node.label.data()
                    length:node.label.size() encoding:NSUTF8StringEncoding];
                [element setAccessibilityLabel:label == nil ? @"" : label];
                [label release];
                [element setAccessibilityEnabled:!node.disabled];
                if (node.role == NativeAccessibilityRole::TextBox ||
                    node.role == NativeAccessibilityRole::StaticText) {
                    // Text is queried lazily through the AX text methods above.
                } else if (node.checked != NativeAccessibilityCheckedState::None) {
                    [element setAccessibilityValue:@(node.checked == NativeAccessibilityCheckedState::True ? 1 :
                        node.checked == NativeAccessibilityCheckedState::Mixed ? 2 : 0)];
                } else if (node.has_value_range) {
                    [element setAccessibilityValue:@(node.value)];
                    [element setAccessibilityMinValue:@(node.minimum)];
                    [element setAccessibilityMaxValue:@(node.maximum)];
                } else if (node.has_selected) {
                    [element setAccessibilitySelected:node.selected];
                }
                const NSRect content_bounds = [content_view_ bounds];
                const NSRect local = NSMakeRect(node.bounds.x,
                    NSHeight(content_bounds) - node.bounds.y - node.bounds.height,
                    node.bounds.width, node.bounds.height);
                [element setAccessibilityFrame:[[content_view_ window] convertRectToScreen:local]];
                [children addObject:element];
                retained.emplace(node.handle, element);
                if (snapshot.focused_handle == node.handle) {
                    NSAccessibilityPostNotification(element, NSAccessibilityFocusedUIElementChangedNotification);
                }
            }
            for (const auto& [handle, element] : elements_) {
                (void)handle;
                [element release];
            }
            elements_ = std::move(retained);
            [content_view_ setAccessibilityChildren:children];
            NSAccessibilityPostNotification(content_view_, NSAccessibilityLayoutChangedNotification);
        }
    }

    void Announce(const NativeAccessibilityNode& node) override {
        @autoreleasepool {
            NSString* text = [[NSString alloc] initWithBytes:node.label.data()
                length:node.label.size() encoding:NSUTF8StringEncoding];
            if (text != nil) {
                NSAccessibilityPostNotificationWithUserInfo(content_view_,
                    NSAccessibilityAnnouncementRequestedNotification,
                    @{NSAccessibilityAnnouncementKey: text,
                      NSAccessibilityPriorityKey: @(NSAccessibilityPriorityMedium)});
                [text release];
            }
        }
    }

    void Clear() override {
        if (content_view_ != nil) [content_view_ setAccessibilityChildren:@[]];
        for (const auto& [handle, element] : elements_) {
            (void)handle;
            element->effindomActionHandler = nullptr;
            element->effindomTextProvider = nullptr;
            element->effindomContentView = nil;
            [element release];
        }
        elements_.clear();
    }

    EffinDomAccessibilityElement* ElementForTesting(std::uint64_t handle) const {
        const auto found = elements_.find(handle);
        return found == elements_.end() ? nil : found->second;
    }

private:
    SDL_Window* window_ = nullptr;
    NSView* content_view_ = nil;
    NativeAccessibilityActionHandler action_handler_;
    std::shared_ptr<NativeAccessibilityTextProvider> text_provider_;
    std::unordered_map<std::uint64_t, EffinDomAccessibilityElement*> elements_;
};

} // namespace

namespace detail {

void* MacosAccessibilityElementForTesting(std::uint64_t handle) {
    return active_adapter == nullptr
        ? nullptr
        : (__bridge void*)active_adapter->ElementForTesting(handle);
}

} // namespace detail

std::unique_ptr<NativeAccessibilityAdapter> CreateMacosAccessibilityAdapter(
    SDL_Window* window, NativeAccessibilityActionHandler action_handler) {
    if (window == nullptr) return nullptr;
    return std::make_unique<MacosAccessibilityAdapter>(window, std::move(action_handler));
}

} // namespace effindom::v2::native
