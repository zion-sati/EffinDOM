#include "SdlDropTarget.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SDL drop begin enters and immediately updates the external drop target",
    "[v2][native][common][drop]") {
    const auto events = effindom::v2::native::SdlDropBeginEventSequence();

    REQUIRE(events[0] == effindom::v2::native::kSdlExternalDragEnterEvent);
    REQUIRE(events[1] == effindom::v2::native::kSdlExternalDragOverEvent);
}
