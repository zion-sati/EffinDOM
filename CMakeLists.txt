cmake_minimum_required(VERSION 3.20)
project(EffinDom LANGUAGES CXX)

include(cmake/EffinDomTargetArchitecture.cmake)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(EFFINDOM_SIMD "Enable WebAssembly SIMD code generation for EffinDom bundles" OFF)
option(EFFINDOM_BUILD_LEGACY_WASM "Build legacy v1 WebAssembly bundles" OFF)
option(EFFINDOM_BUILD_NATIVE_HEADLESS "Build the native headless conformance host" ON)
option(EFFINDOM_BUILD_NATIVE_MACOS "Build the native macOS host" ${APPLE})
option(EFFINDOM_BUILD_NATIVE_WINDOWS "Build the native Windows host" ${WIN32})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_EFFINDOM_DEFAULT_BUILD_NATIVE_LINUX ON)
else()
    set(_EFFINDOM_DEFAULT_BUILD_NATIVE_LINUX OFF)
endif()
option(EFFINDOM_BUILD_NATIVE_LINUX "Build the native Linux host" ${_EFFINDOM_DEFAULT_BUILD_NATIVE_LINUX})
option(EFFINDOM_BUILD_NATIVE_FUI_RS_DEMO "Build the native FUI-RS demo integration executable" ON)
set(EFFINDOM_NATIVE_DEPS_MODE "source" CACHE STRING "Native dependency source (source or prebuilt)")
set_property(CACHE EFFINDOM_NATIVE_DEPS_MODE PROPERTY STRINGS source prebuilt)
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/EffinDomOssDistribution.cmake")
    # The distribution overlay selects the published native dependency SDK.
    include("${CMAKE_SOURCE_DIR}/cmake/EffinDomOssDistribution.cmake")
endif()
if(APPLE)
    set(_EFFINDOM_DEFAULT_NATIVE_GRAPHICS_BACKEND "metal")
elseif(WIN32)
    set(_EFFINDOM_DEFAULT_NATIVE_GRAPHICS_BACKEND "d3d")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_EFFINDOM_DEFAULT_NATIVE_GRAPHICS_BACKEND "vulkan")
else()
    set(_EFFINDOM_DEFAULT_NATIVE_GRAPHICS_BACKEND "raster")
endif()
set(EFFINDOM_NATIVE_GRAPHICS_BACKEND "${_EFFINDOM_DEFAULT_NATIVE_GRAPHICS_BACKEND}" CACHE STRING "Native graphics backend (raster, metal, d3d, or vulkan)")
set_property(CACHE EFFINDOM_NATIVE_GRAPHICS_BACKEND PROPERTY STRINGS raster metal d3d vulkan)

if(WIN32)
    # FetchContent's generated stamp names are long enough to exceed the
    # legacy Windows MAX_PATH limit under the descriptive native build trees.
    set(FETCHCONTENT_BASE_DIR
        "${CMAKE_SOURCE_DIR}/build/fc/${EFFINDOM_TARGET_ARCH}/${CMAKE_BUILD_TYPE}/${EFFINDOM_NATIVE_GRAPHICS_BACKEND}")
endif()

add_library(effindom_compile_flags INTERFACE)
if(MSVC)
    add_compile_options(/utf-8)
    target_compile_options(effindom_compile_flags INTERFACE /W4 /WX)
else()
    target_compile_options(effindom_compile_flags INTERFACE -Wall -Wextra -Werror)
endif()

set(EFFINDOM_WASM_DEPS_ROOT "" CACHE PATH "Verified prebuilt WASM dependency SDK root")

add_library(effindom_wasm_feature_flags INTERFACE)
if(EMSCRIPTEN AND EFFINDOM_SIMD)
    target_compile_options(effindom_wasm_feature_flags INTERFACE -O3 -msimd128)
    target_link_options(effindom_wasm_feature_flags INTERFACE -O3 -msimd128)
endif()

if(NOT (EMSCRIPTEN AND EFFINDOM_WASM_DEPS_ROOT))
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)
    if(MSVC AND EFFINDOM_TARGET_ARCH STREQUAL "arm64")
        # Catch2 3.5.4 incorrectly enables the x64-only _umul128 intrinsic for
        # ARM64. Use Catch2's supported portable multiplication escape hatch.
        target_compile_definitions(Catch2 PRIVATE CATCH_CONFIG_NO_MSVC_UMUL128)
    endif()
endif()

if(EMSCRIPTEN AND EFFINDOM_WASM_DEPS_ROOT)
    include(cmake/PrebuiltWasmDeps.cmake)
    effindom_use_prebuilt_wasm_deps()
elseif(NOT EMSCRIPTEN AND EFFINDOM_NATIVE_DEPS_MODE STREQUAL "prebuilt")
    include(cmake/PrebuiltNativeDeps.cmake)
    effindom_use_prebuilt_native_deps()
else()
    set(_EFFINDOM_PRE_YOGA_CXX_STANDARD ${CMAKE_CXX_STANDARD})
    FetchContent_Declare(
        Yoga
        GIT_REPOSITORY https://github.com/facebook/yoga.git
        GIT_TAG        v3.2.1
        SOURCE_SUBDIR  yoga
    )
    FetchContent_MakeAvailable(Yoga)
    set(CMAKE_CXX_STANDARD ${_EFFINDOM_PRE_YOGA_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(TARGET yogacore)
        target_link_libraries(yogacore PUBLIC effindom_wasm_feature_flags)
    endif()

    include(cmake/SkiaPinnedTextDeps.cmake)
    include(cmake/PinnedWoff2Deps.cmake)
endif()

add_subdirectory(v2)
