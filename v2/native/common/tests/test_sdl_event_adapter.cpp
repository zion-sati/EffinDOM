#include "SdlEventAdapter.h"
#include "effindom_ui.h"

#include "SDL3/SDL.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <tuple>

using effindom::v2::native::SdlEventAdapter;
using Catch::Approx;

TEST_CASE("SDL pointer buttons map to EffinDOM values", "[v2][native][input]") {
    CHECK(SdlEventAdapter::PointerButton(SDL_BUTTON_LEFT) == 0);
    CHECK(SdlEventAdapter::PointerButton(SDL_BUTTON_MIDDLE) == 1);
    CHECK(SdlEventAdapter::PointerButton(SDL_BUTTON_RIGHT) == 2);
    CHECK(SdlEventAdapter::PointerButton(SDL_BUTTON_X1) == 3);
    CHECK(SdlEventAdapter::PointerButton(SDL_BUTTON_X2) == 4);
    CHECK(SdlEventAdapter::PointerButton(0xffU) == -1);
    CHECK(SdlEventAdapter::PointerButtons(
        SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_X2MASK) == 19U);
}

TEST_CASE("SDL modifiers map without platform leakage", "[v2][native][input]") {
    CHECK(SdlEventAdapter::Modifiers(
        SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI) ==
        (UI_KEY_MOD_SHIFT | UI_KEY_MOD_CTRL | UI_KEY_MOD_ALT | UI_KEY_MOD_META));
}

TEST_CASE("SDL key names use browser KeyboardEvent conventions", "[v2][native][input]") {
    constexpr std::uint32_t unknown = SDL_SCANCODE_UNKNOWN;
    CHECK(SdlEventAdapter::KeyName(SDLK_UP, unknown, 0U, false) == "ArrowUp");
    CHECK(SdlEventAdapter::KeyName(SDLK_DOWN, unknown, 0U, false) == "ArrowDown");
    CHECK(SdlEventAdapter::KeyName(SDLK_LEFT, unknown, 0U, false) == "ArrowLeft");
    CHECK(SdlEventAdapter::KeyName(SDLK_RIGHT, unknown, 0U, false) == "ArrowRight");
    CHECK(SdlEventAdapter::KeyName(SDLK_RETURN, unknown, 0U, false) == "Enter");
    CHECK(SdlEventAdapter::KeyName(SDLK_SPACE, unknown, 0U, false) == " ");
    CHECK(SdlEventAdapter::KeyName(SDLK_INSERT, unknown, 0U, false) == "Insert");
    CHECK(SdlEventAdapter::KeyName(SDLK_PAGEUP, unknown, 0U, false) == "PageUp");
    CHECK(SdlEventAdapter::KeyName(SDLK_LGUI, unknown, 0U, false) == "Meta");
    CHECK(SdlEventAdapter::KeyName(SDLK_APPLICATION, unknown, 0U, false) == "ContextMenu");
}

TEST_CASE("SDL printable key names honor Caps Lock and keypad state", "[v2][native][input][text]") {
    constexpr std::uint32_t unknown = SDL_SCANCODE_UNKNOWN;
    CHECK(SdlEventAdapter::KeyName(SDLK_A, unknown, 0U, false) == "a");
    CHECK(SdlEventAdapter::KeyName(SDLK_A, unknown, SDL_KMOD_SHIFT, false) == "A");
    CHECK(SdlEventAdapter::KeyName(SDLK_A, unknown, SDL_KMOD_CAPS, false) == "A");
    CHECK(SdlEventAdapter::KeyName(SDLK_A, unknown,
        SDL_KMOD_SHIFT | SDL_KMOD_CAPS, false) == "a");
    CHECK(SdlEventAdapter::KeyName(SDLK_1, SDL_SCANCODE_1,
        SDL_KMOD_SHIFT, false) == "!");
    CHECK(SdlEventAdapter::KeyName(SDLK_LEFTBRACKET, SDL_SCANCODE_LEFTBRACKET,
        SDL_KMOD_SHIFT, false) == "{");
    CHECK(SdlEventAdapter::KeyName(SDLK_END, SDL_SCANCODE_KP_1, 0U, true) == "1");
    CHECK(SdlEventAdapter::KeyName(SDLK_KP_1, SDL_SCANCODE_KP_1, SDL_KMOD_NUM, false) == "1");
    CHECK(SdlEventAdapter::KeyName(SDLK_KP_1, SDL_SCANCODE_KP_1, 0U, false) == "End");
    CHECK(SdlEventAdapter::KeyName(SDLK_CLEAR, SDL_SCANCODE_KP_5, 0U, true) == "5");
    CHECK(SdlEventAdapter::KeyName(SDLK_KP_5, SDL_SCANCODE_KP_5, 0U, false) == "Clear");
    CHECK(SdlEventAdapter::KeyName(SDLK_KP_PLUS, SDL_SCANCODE_KP_PLUS, 0U, true) == "+");
    CHECK(SdlEventAdapter::KeyName(SDLK_DELETE, SDL_SCANCODE_KP_PERIOD, 0U, true) == ".");
    CHECK(SdlEventAdapter::KeyName(SDLK_KP_PERIOD, SDL_SCANCODE_KP_PERIOD, 0U, false) == "Delete");
}

TEST_CASE("SDL coordinate policy covers backing and display content scales", "[v2][native][input]") {
    CHECK(SdlEventAdapter::LogicalCoordinate(240.0f, 2.0f, false) == 240.0f);
    CHECK(SdlEventAdapter::LogicalCoordinate(240.0f, 2.0f, true) == 120.0f);
    CHECK(SdlEventAdapter::LogicalCoordinate(240.0f, 0.0f, true) == 240.0f);
    CHECK(SdlEventAdapter::DisplayContentScale(2.0f, 1.0f) == 2.0f);
    CHECK(SdlEventAdapter::DisplayContentScale(2.0f, 2.0f) == 1.0f);
    CHECK(SdlEventAdapter::DisplayContentScale(1.5f, 1.0f) == 1.5f);
    CHECK(SdlEventAdapter::DisplayContentScale(0.0f, 1.0f) == 1.0f);
}

TEST_CASE("SDL wheel policy translates to browser-style content deltas",
    "[v2][native][input]") {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.x = 0.25f;
    event.wheel.y = -0.625f;
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    auto [delta_x, delta_y] = SdlEventAdapter::WheelDeltas(event);
    CHECK(delta_x == Approx(24.0f));
    CHECK(delta_y == Approx(60.0f));

    event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    std::tie(delta_x, delta_y) = SdlEventAdapter::WheelDeltas(event);
    CHECK(delta_x == Approx(24.0f));
    CHECK(delta_y == Approx(60.0f));
}

TEST_CASE("SDL resize and expose events end input batches", "[v2][native][window]") {
    CHECK(SdlEventAdapter::EndsInputBatch(SDL_EVENT_WINDOW_RESIZED));
    CHECK(SdlEventAdapter::EndsInputBatch(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED));
    CHECK(SdlEventAdapter::EndsInputBatch(SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED));
    CHECK(SdlEventAdapter::EndsInputBatch(SDL_EVENT_WINDOW_EXPOSED));
    CHECK_FALSE(SdlEventAdapter::EndsInputBatch(SDL_EVENT_MOUSE_MOTION));
    CHECK_FALSE(SdlEventAdapter::EndsInputBatch(SDL_EVENT_MOUSE_WHEEL));
}
