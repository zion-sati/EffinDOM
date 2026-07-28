#include "NativeHost.h"
#include "effindom_ui.h"
#include "platform/MacosAccessibilityAdapter.h"

#include <catch2/catch_test_macros.hpp>

#import <AppKit/AppKit.h>

#include <cstdint>
#include <string>

extern "C" std::uint64_t __fui_native_context_editor_handle();
extern "C" std::uint64_t __fui_native_scroll_view_handle();

namespace {

std::string TextDocument(std::uint64_t handle) {
    std::uint64_t revision = 0U;
    std::uint32_t characters = 0U;
    std::uint32_t selection_start = 0U;
    std::uint32_t selection_end = 0U;
    std::uint32_t flags = 0U;
    if (!ui_get_accessibility_text_info(handle, &revision, &characters,
            &selection_start, &selection_end, &flags)) return {};
    std::uint32_t length = 0U;
    if (ui_get_accessibility_text_range_utf8_length(
            handle, revision, 0U, characters, &length) != UI_TEXT_ACCESSIBILITY_QUERY_OK) return {};
    std::string text(length, '\0');
    if (ui_copy_accessibility_text_range_utf8(handle, revision, 0U, characters,
            reinterpret_cast<std::uint8_t*>(text.data()), length) !=
        UI_TEXT_ACCESSIBILITY_QUERY_OK) return {};
    return text;
}

} // namespace

TEST_CASE("macOS AX text element lazily queries edits and protects obscured documents",
    "[v2][native][macos][accessibility][text]") {
    [NSApplication sharedApplication];
    effindom::v2::native::NativeHost host(true);
    host.MountApplication();
    host.DrainFrames();
    ui_set_scroll_offset(__fui_native_scroll_view_handle(), 0.0f, 10000.0f);
    host.RequestFrame();
    host.DrainFrames();

    REQUIRE(__fui_native_context_editor_handle() != 0U);
    std::uint64_t handle = 0U;
    std::string original;
    for (const auto& node : host.AccessibilitySnapshotForTesting().nodes) {
        if (node.role != effindom::v2::native::NativeAccessibilityRole::TextBox) continue;
        const std::string candidate = TextDocument(node.handle);
        if (!candidate.empty()) {
            handle = node.handle;
            original = candidate;
            break;
        }
    }
    REQUIRE(handle != 0U);
    REQUIRE_FALSE(original.empty());
    NSString* original_text = [[[NSString alloc] initWithBytes:original.data()
        length:original.size() encoding:NSUTF8StringEncoding] autorelease];
    REQUIRE(original_text != nil);
    id element = (__bridge id)effindom::v2::native::detail::
        MacosAccessibilityElementForTesting(handle);
    if (element == nil) {
        for (const auto& node : host.AccessibilitySnapshotForTesting().nodes) {
            if (node.role != effindom::v2::native::NativeAccessibilityRole::TextBox &&
                node.role != effindom::v2::native::NativeAccessibilityRole::StaticText) continue;
            id candidate = (__bridge id)effindom::v2::native::detail::
                MacosAccessibilityElementForTesting(node.handle);
            if ([[candidate accessibilityValue] isEqualToString:original_text]) {
                element = candidate;
                break;
            }
        }
    }
    REQUIRE(element != nil);
    CHECK([[element accessibilityValue] isEqualToString:original_text]);
    CHECK([element accessibilityNumberOfCharacters] ==
        static_cast<NSInteger>([original_text length]));
    CHECK([[element accessibilityStringForRange:NSMakeRange(0U, [original_text length])]
        isEqualToString:original_text]);
    CHECK_FALSE(NSEqualRects([element accessibilityFrameForRange:NSMakeRange(0U, 1U)], NSZeroRect));

    [element setAccessibilitySelectedTextRange:NSMakeRange(0U, 1U)];
    CHECK(NSEqualRanges([element accessibilitySelectedTextRange], NSMakeRange(0U, 1U)));
    [element setAccessibilitySelectedText:@"\u4F60\U0001F600"];
    const std::string edited = TextDocument(handle);
    CHECK(edited.compare(0U, 7U, "\xE4\xBD\xA0\xF0\x9F\x98\x80") == 0);

    [element setAccessibilityValue:original_text];
    CHECK(TextDocument(handle) == original);

    ui_set_text_obscured(handle, true);
    CHECK([element accessibilityValue] == nil);
    CHECK([element accessibilityStringForRange:NSMakeRange(0U, 1U)] == nil);
    CHECK([element accessibilityNumberOfCharacters] == 0);
    ui_set_text_obscured(handle, false);
}
