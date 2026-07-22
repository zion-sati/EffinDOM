#pragma once

namespace effindom::v2::native {

class NativePlatformHost;

// The platform factory binds exactly one live host while the native FUI
// runtime is mounted. The C ABI remains platform-neutral and delegates only
// genuine OS operations through NativePlatformHost.
void SetActiveNativePlatformHost(NativePlatformHost* host);
NativePlatformHost* ActiveNativePlatformHost();

} // namespace effindom::v2::native
