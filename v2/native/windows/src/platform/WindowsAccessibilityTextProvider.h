#pragma once

#include "NativeAccessibility.h"

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>

#include <cstdint>
#include <memory>

namespace effindom::v2::native {

EVENTID WindowsAccessibilityTextEventId(NativeAccessibilityTextEvent event);

HRESULT CreateWindowsAccessibilityTextPattern(
    HWND window,
    std::uint64_t handle,
    std::shared_ptr<NativeAccessibilityTextProvider> provider,
    IRawElementProviderSimple* owner,
    PATTERNID pattern,
    IUnknown** result);

} // namespace effindom::v2::native
