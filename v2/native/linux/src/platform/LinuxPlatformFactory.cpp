#include "NativePlatformFactory.h"

#include "LinuxNativePlatform.h"

namespace effindom::v2::native {
namespace {

class LinuxPlatformFactory final : public NativePlatformFactory {
public:
    std::unique_ptr<NativePlatformHost> CreateHost(bool visible) override {
        return std::make_unique<LinuxNativePlatform>(visible);
    }
};

} // namespace

std::unique_ptr<NativePlatformFactory> CreateNativePlatformFactory() {
    return std::make_unique<LinuxPlatformFactory>();
}

} // namespace effindom::v2::native
