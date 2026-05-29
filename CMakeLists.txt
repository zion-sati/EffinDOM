cmake_minimum_required(VERSION 3.20)
project(EffinDom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EFFINDOM_SIMD "Enable WebAssembly SIMD code generation for EffinDom bundles" OFF)
option(EFFINDOM_BUILD_LEGACY_WASM "Build legacy v1 WebAssembly bundles" OFF)

add_library(effindom_compile_flags INTERFACE)
if(MSVC)
    target_compile_options(effindom_compile_flags INTERFACE /W4 /WX)
else()
    target_compile_options(effindom_compile_flags INTERFACE -Wall -Wextra -Werror)
endif()

add_library(effindom_wasm_feature_flags INTERFACE)
if(EMSCRIPTEN AND EFFINDOM_SIMD)
    target_compile_options(effindom_wasm_feature_flags INTERFACE -O3 -msimd128)
    target_link_options(effindom_wasm_feature_flags INTERFACE -O3 -msimd128)
endif()

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

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

add_subdirectory(v2)
