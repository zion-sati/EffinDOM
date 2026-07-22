#pragma once

#include <cstdint>
#include <string>
#include <vector>

union SDL_Event;
struct SDL_Window;

namespace effindom::v2 {
class Engine;
namespace native {

class SdlDropTarget final {
public:
    SdlDropTarget(SDL_Window* window, Engine& engine);

    bool HandleEvent(const SDL_Event& event);
    void Clear();

private:
    struct Item {
        std::uint32_t kind = 0U;
        double size_bytes = 0.0;
        std::string id;
        std::string name;
        std::string mime_type;
    };

    void Begin(float x, float y);
    void AddFile(const char* path);
    void AddText(const char* text);
    std::uint32_t Dispatch(std::uint32_t event_type) const;
    std::vector<std::uint8_t> EncodePayload() const;

    SDL_Window* window_;
    Engine& engine_;
    bool active_ = false;
    bool entered_ = false;
    float x_ = 0.0f;
    float y_ = 0.0f;
    std::vector<Item> items_;
};

} // namespace native
} // namespace effindom::v2
