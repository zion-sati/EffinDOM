#include "MacosSystemThemeBridge.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace effindom::v2::native {

namespace detail {

std::optional<std::uint32_t> PackMacosAccentColor(
    double red,
    double green,
    double blue) {
    if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
        return std::nullopt;
    }
    const auto component = [](double value) {
        return static_cast<std::uint32_t>(
            std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    return (component(red) << 24U) |
        (component(green) << 16U) |
        (component(blue) << 8U) |
        0xFFU;
}

std::optional<std::uint32_t> ReadMacosAccentColor() {
    NSColor* accent = [NSColor controlAccentColor];
    NSColor* srgb = [accent colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (srgb == nil) return std::nullopt;
    return PackMacosAccentColor(
        static_cast<double>(srgb.redComponent),
        static_cast<double>(srgb.greenComponent),
        static_cast<double>(srgb.blueComponent));
}

MacosAccentColorState::MacosAccentColorState(
    Reader reader,
    ChangedHandler changed_handler)
    : reader_(std::move(reader)), changed_handler_(std::move(changed_handler)) {
    if (const std::optional<std::uint32_t> color = reader_()) current_ = *color;
}

std::uint32_t MacosAccentColorState::Current() const { return current_; }

bool MacosAccentColorState::Refresh() {
    const std::optional<std::uint32_t> color = reader_();
    if (!color.has_value() || *color == current_) return false;
    current_ = *color;
    changed_handler_(current_);
    return true;
}

} // namespace detail

} // namespace effindom::v2::native

using SystemColorsChangedCallback = void (*)(void*);

@interface EffinDOMSystemColorsObserver : NSObject {
@public
    void* context;
    SystemColorsChangedCallback callback;
}
- (void)systemColorsChanged:(NSNotification*)notification;
@end

@implementation EffinDOMSystemColorsObserver
- (void)systemColorsChanged:(NSNotification*)notification {
    (void)notification;
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:_cmd withObject:notification waitUntilDone:NO];
        return;
    }
    if (callback != nullptr) callback(context);
}
@end

namespace effindom::v2::native {

struct MacosSystemThemeBridge::Impl {
    explicit Impl(AccentChangedHandler changed_handler)
        : state(&detail::ReadMacosAccentColor, std::move(changed_handler)) {
        observer = [[EffinDOMSystemColorsObserver alloc] init];
        observer->context = this;
        observer->callback = &Impl::SystemColorsChanged;
        [[NSNotificationCenter defaultCenter]
            addObserver:observer
               selector:@selector(systemColorsChanged:)
                   name:NSSystemColorsDidChangeNotification
                 object:nil];
    }

    ~Impl() {
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
        observer->callback = nullptr;
        observer->context = nullptr;
#if !__has_feature(objc_arc)
        [observer release];
#endif
        observer = nil;
    }

    static void SystemColorsChanged(void* context) {
        static_cast<Impl*>(context)->state.Refresh();
    }

    detail::MacosAccentColorState state;
    EffinDOMSystemColorsObserver* observer = nil;
};

MacosSystemThemeBridge::MacosSystemThemeBridge(AccentChangedHandler changed_handler)
    : impl_(std::make_unique<Impl>(std::move(changed_handler))) {}

MacosSystemThemeBridge::~MacosSystemThemeBridge() = default;

std::uint32_t MacosSystemThemeBridge::AccentColor() const {
    return impl_->state.Current();
}

} // namespace effindom::v2::native
