#include "MacosAccessibilityAdapter.h"

#include "SDL3/SDL.h"

#import <AppKit/AppKit.h>

#include <unordered_map>
#include <utility>

#ifndef NSAccessibilityHeadingRole
#define NSAccessibilityHeadingRole @"AXHeading"
#endif

using effindom::v2::native::NativeAccessibilityAction;
using effindom::v2::native::NativeAccessibilityActionHandler;
using effindom::v2::native::NativeAccessibilityRole;

@interface EffinDomAccessibilityElement : NSAccessibilityElement {
@public
    std::uint64_t effindomHandle;
    NativeAccessibilityRole effindomRole;
    NativeAccessibilityActionHandler* effindomActionHandler;
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
@end

namespace effindom::v2::native {
namespace {

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
    }

    ~MacosAccessibilityAdapter() override { Clear(); }

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
                [element setAccessibilityParent:content_view_];
                [element setAccessibilityRole:RoleName(node.role)];
                NSString* label = [[NSString alloc] initWithBytes:node.label.data()
                    length:node.label.size() encoding:NSUTF8StringEncoding];
                [element setAccessibilityLabel:label == nil ? @"" : label];
                [label release];
                [element setAccessibilityEnabled:!node.disabled];
                if (node.checked != NativeAccessibilityCheckedState::None) {
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
            [element release];
        }
        elements_.clear();
    }

private:
    SDL_Window* window_ = nullptr;
    NSView* content_view_ = nil;
    NativeAccessibilityActionHandler action_handler_;
    std::unordered_map<std::uint64_t, EffinDomAccessibilityElement*> elements_;
};

} // namespace

std::unique_ptr<NativeAccessibilityAdapter> CreateMacosAccessibilityAdapter(
    SDL_Window* window, NativeAccessibilityActionHandler action_handler) {
    if (window == nullptr) return nullptr;
    return std::make_unique<MacosAccessibilityAdapter>(window, std::move(action_handler));
}

} // namespace effindom::v2::native
