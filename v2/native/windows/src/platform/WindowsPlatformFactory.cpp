#include "NativePlatformFactory.h"

#include "WindowsNativePlatform.h"

namespace effindom::v2::native {
namespace {

class WindowsPlatformFactory final : public NativePlatformFactory {
public:
    std::unique_ptr<NativePlatformHost> CreateHost(bool visible) override {
        return std::make_unique<WindowsNativePlatform>(visible);
    }
};

} // namespace

std::unique_ptr<NativePlatformFactory> CreateNativePlatformFactory() {
    return std::make_unique<WindowsPlatformFactory>();
}

} // namespace effindom::v2::native
