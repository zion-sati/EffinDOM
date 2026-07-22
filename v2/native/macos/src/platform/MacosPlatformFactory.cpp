#include "NativePlatformFactory.h"

#include "MacosNativePlatform.h"

namespace effindom::v2::native {
namespace {

class MacosPlatformFactory final : public NativePlatformFactory {
public:
    std::unique_ptr<NativePlatformHost> CreateHost(bool visible) override {
        return std::make_unique<MacosNativePlatform>(visible);
    }
};

} // namespace

std::unique_ptr<NativePlatformFactory> CreateNativePlatformFactory() {
    return std::make_unique<MacosPlatformFactory>();
}

} // namespace effindom::v2::native
