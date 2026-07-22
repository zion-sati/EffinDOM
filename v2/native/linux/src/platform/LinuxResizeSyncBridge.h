#pragma once

#include <memory>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2::native {

// SDL intentionally leaves the X11 resize-sync protocol disabled for Vulkan
// windows. This adapter restores that native paint boundary without exposing
// X11 details to the shared host. Other SDL video drivers use the null path.
class LinuxResizeSyncBridge final {
public:
    static std::unique_ptr<LinuxResizeSyncBridge> Create(SDL_Window* window);

    ~LinuxResizeSyncBridge();
    LinuxResizeSyncBridge(const LinuxResizeSyncBridge&) = delete;
    LinuxResizeSyncBridge& operator=(const LinuxResizeSyncBridge&) = delete;

    void PumpNativeEvents();
    void WaitForNativeEvents(int timeout_ms);
    void HandleSdlEvent(const SDL_Event& event);
    void DidPresentFrame();
    bool IsEnabled() const;

private:
    struct Impl;
    explicit LinuxResizeSyncBridge(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace effindom::v2::native
