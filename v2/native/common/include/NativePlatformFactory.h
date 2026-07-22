#pragma once

#include <memory>

namespace effindom::v2::native {

class NativePlatformHost;

class NativePlatformFactory {
public:
    virtual ~NativePlatformFactory() = default;
    virtual std::unique_ptr<NativePlatformHost> CreateHost(bool visible) = 0;
};

std::unique_ptr<NativePlatformFactory> CreateNativePlatformFactory();

} // namespace effindom::v2::native
