include(FetchContent)

if(NOT TARGET SDL3::SDL3)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

add_library(effindom_v2_native_common STATIC
    src/NativeAccessibility.cpp
    src/NativeContextMenuCoordinator.cpp
    src/NativeAssetService.cpp
    src/NativeGraphicsCoordinator.cpp
    src/NativeHostCore.cpp
    src/NativeHost.cpp
    src/NativeFuiRuntimeBridge.cpp
    src/NativeFuiHostStubs.cpp
    src/NativeInputRouter.cpp
    src/NativeRasterSurface.cpp
    src/SdlDropTarget.cpp
    src/SdlEventAdapter.cpp
    src/SdlFileDialogs.cpp
    src/SdlUiDispatcher.cpp
)
target_include_directories(effindom_v2_native_common
    PUBLIC
        include
    PRIVATE
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
)
target_link_libraries(effindom_v2_native_common
    PUBLIC effindom_v2_core effindom_v2_ui SDL3::SDL3
    PRIVATE effindom_compile_flags
)
if(MSVC)
    target_compile_options(effindom_v2_native_common PRIVATE /wd4251 /wd4275)
endif()
