# Normalize the target architecture once for CMake, Rust, Skia, cache paths,
# and packaging. Call this after project() so compiler target information is
# available.

set(EFFINDOM_TARGET_ARCH "" CACHE STRING "EffinDOM target architecture: x86, x64, or arm64")
set_property(CACHE EFFINDOM_TARGET_ARCH PROPERTY STRINGS x86 x64 arm64 wasm32 wasm64)

if(NOT EFFINDOM_TARGET_ARCH)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _effindom_generator_platform)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _effindom_system_processor)
    string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" _effindom_osx_architectures)

    if(EMSCRIPTEN AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(EFFINDOM_TARGET_ARCH wasm64 CACHE STRING "EffinDOM target architecture" FORCE)
    elseif(EMSCRIPTEN)
        set(EFFINDOM_TARGET_ARCH wasm32 CACHE STRING "EffinDOM target architecture" FORCE)
    elseif(APPLE AND _effindom_osx_architectures MATCHES "^(arm64|aarch64)$")
        set(EFFINDOM_TARGET_ARCH arm64 CACHE STRING "EffinDOM target architecture" FORCE)
    elseif(APPLE AND _effindom_osx_architectures MATCHES "^(x86_64|x64|amd64)$")
        set(EFFINDOM_TARGET_ARCH x64 CACHE STRING "EffinDOM target architecture" FORCE)
    elseif(APPLE AND _effindom_osx_architectures MATCHES ";")
        message(FATAL_ERROR
            "Universal macOS native builds are not supported in one build tree. "
            "Configure arm64 and x86_64 separately so Rust and Skia artifacts remain architecture-qualified.")
    elseif(_effindom_generator_platform MATCHES "^(win32|x86)$" OR
       _effindom_system_processor MATCHES "^(i[3-6]86|x86)$")
        set(EFFINDOM_TARGET_ARCH x86 CACHE STRING "EffinDOM target architecture" FORCE)
    elseif(_effindom_generator_platform MATCHES "^(arm64|aarch64)$" OR
           _effindom_system_processor MATCHES "^(arm64|aarch64)$")
        set(EFFINDOM_TARGET_ARCH arm64 CACHE STRING "EffinDOM target architecture" FORCE)
    elseif(_effindom_generator_platform MATCHES "^(x64|amd64|x86_64)$" OR
           _effindom_system_processor MATCHES "^(x64|amd64|x86_64)$")
        set(EFFINDOM_TARGET_ARCH x64 CACHE STRING "EffinDOM target architecture" FORCE)
    else()
        message(FATAL_ERROR
            "Could not normalize target architecture from generator platform "
            "'${CMAKE_GENERATOR_PLATFORM}' and system processor '${CMAKE_SYSTEM_PROCESSOR}'. "
            "Set -DEFFINDOM_TARGET_ARCH=x86, x64, arm64, wasm32, or wasm64.")
    endif()
endif()

string(TOLOWER "${EFFINDOM_TARGET_ARCH}" EFFINDOM_TARGET_ARCH)
if(NOT EFFINDOM_TARGET_ARCH MATCHES "^(x86|x64|arm64|wasm32|wasm64)$")
    message(FATAL_ERROR
        "Unsupported EFFINDOM_TARGET_ARCH='${EFFINDOM_TARGET_ARCH}'. "
        "Expected x86, x64, arm64, wasm32, or wasm64.")
endif()

if(EFFINDOM_TARGET_ARCH STREQUAL "wasm32")
    set(EFFINDOM_RUST_TARGET "wasm32-unknown-unknown" CACHE INTERNAL "Rust target triple")
    set(EFFINDOM_SKIA_TARGET_CPU "wasm" CACHE INTERNAL "Skia GN target_cpu")
    set(_effindom_expected_pointer_size 4)
elseif(EFFINDOM_TARGET_ARCH STREQUAL "wasm64")
    set(EFFINDOM_RUST_TARGET "wasm64-unknown-unknown" CACHE INTERNAL "Rust target triple")
    set(EFFINDOM_SKIA_TARGET_CPU "wasm" CACHE INTERNAL "Skia GN target_cpu")
    set(_effindom_expected_pointer_size 8)
elseif(EFFINDOM_TARGET_ARCH STREQUAL "x86")
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(EFFINDOM_RUST_TARGET "i686-pc-windows-msvc" CACHE INTERNAL "Rust target triple")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(FATAL_ERROR "macOS x86 (32-bit) is not a supported native target")
    else()
        set(EFFINDOM_RUST_TARGET "i686-unknown-linux-gnu" CACHE INTERNAL "Rust target triple")
    endif()
    set(EFFINDOM_SKIA_TARGET_CPU "x86" CACHE INTERNAL "Skia GN target_cpu")
    set(_effindom_expected_pointer_size 4)
elseif(EFFINDOM_TARGET_ARCH STREQUAL "arm64")
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(EFFINDOM_RUST_TARGET "aarch64-pc-windows-msvc" CACHE INTERNAL "Rust target triple")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(EFFINDOM_RUST_TARGET "aarch64-apple-darwin" CACHE INTERNAL "Rust target triple")
    else()
        set(EFFINDOM_RUST_TARGET "aarch64-unknown-linux-gnu" CACHE INTERNAL "Rust target triple")
    endif()
    set(EFFINDOM_SKIA_TARGET_CPU "arm64" CACHE INTERNAL "Skia GN target_cpu")
    set(_effindom_expected_pointer_size 8)
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(EFFINDOM_RUST_TARGET "x86_64-pc-windows-msvc" CACHE INTERNAL "Rust target triple")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(EFFINDOM_RUST_TARGET "x86_64-apple-darwin" CACHE INTERNAL "Rust target triple")
    else()
        set(EFFINDOM_RUST_TARGET "x86_64-unknown-linux-gnu" CACHE INTERNAL "Rust target triple")
    endif()
    set(EFFINDOM_SKIA_TARGET_CPU "x64" CACHE INTERNAL "Skia GN target_cpu")
    set(_effindom_expected_pointer_size 8)
endif()

if(DEFINED CMAKE_SIZEOF_VOID_P AND
   NOT CMAKE_SIZEOF_VOID_P EQUAL ${_effindom_expected_pointer_size})
    message(FATAL_ERROR
        "EFFINDOM_TARGET_ARCH=${EFFINDOM_TARGET_ARCH} expects "
        "${_effindom_expected_pointer_size}-byte pointers, but the selected "
        "compiler targets ${CMAKE_SIZEOF_VOID_P}-byte pointers.")
endif()

set(EFFINDOM_TARGET_TRIPLET
    "${CMAKE_SYSTEM_NAME}-${EFFINDOM_TARGET_ARCH}-${CMAKE_CXX_COMPILER_ID}"
    CACHE INTERNAL "Normalized EffinDOM platform/architecture/compiler triplet")

# Whether executables built for the selected target can run on the configure
# host. Windows x86/x64 targets are supported by Windows compatibility on the
# supported hosts, but an ARM64 executable cannot run on an Intel/AMD host.
set(EFFINDOM_TARGET_RUNNABLE ON CACHE INTERNAL
    "Whether target executables can run on the configure host" FORCE)
if(WIN32 AND EFFINDOM_TARGET_ARCH STREQUAL "arm64" AND
   NOT "$ENV{PROCESSOR_ARCHITECTURE};$ENV{PROCESSOR_ARCHITEW6432}" MATCHES "(^|;)[Aa][Rr][Mm]64($|;)")
    set(EFFINDOM_TARGET_RUNNABLE OFF CACHE INTERNAL
        "Whether target executables can run on the configure host" FORCE)
endif()

message(STATUS
    "EffinDOM target: arch=${EFFINDOM_TARGET_ARCH}, "
    "rust=${EFFINDOM_RUST_TARGET}, skia=${EFFINDOM_SKIA_TARGET_CPU}, "
    "runnable=${EFFINDOM_TARGET_RUNNABLE}")
