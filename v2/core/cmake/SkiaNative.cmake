if(TARGET effindom_v2_skia_native)
    return()
endif()

if(WIN32)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _EFFINDOM_V2_SKIA_CONFIGURATION)
    if(NOT _EFFINDOM_V2_SKIA_CONFIGURATION)
        set(_EFFINDOM_V2_SKIA_CONFIGURATION "release")
    endif()
    set(_EFFINDOM_V2_SKIA_NATIVE_DEFAULT_DIR
        "${CMAKE_SOURCE_DIR}/skia/native/windows-${EFFINDOM_TARGET_ARCH}-msvc-${_EFFINDOM_V2_SKIA_CONFIGURATION}-${EFFINDOM_NATIVE_GRAPHICS_BACKEND}")
    set(_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_PREFIX "")
    set(_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_SUFFIX ".lib")
else()
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _EFFINDOM_V2_SKIA_CONFIGURATION)
    if(NOT _EFFINDOM_V2_SKIA_CONFIGURATION)
        set(_EFFINDOM_V2_SKIA_CONFIGURATION "release")
    endif()
    string(TOLOWER "${CMAKE_CXX_COMPILER_ID}" _EFFINDOM_V2_SKIA_COMPILER)
    if(APPLE)
        set(_EFFINDOM_V2_SKIA_PLATFORM "macos")
        set(_EFFINDOM_V2_SKIA_BACKEND "metal")
    else()
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" _EFFINDOM_V2_SKIA_PLATFORM)
        set(_EFFINDOM_V2_SKIA_BACKEND "${EFFINDOM_NATIVE_GRAPHICS_BACKEND}")
    endif()
    set(_EFFINDOM_V2_SKIA_NATIVE_DEFAULT_DIR
        "${CMAKE_SOURCE_DIR}/skia/native/${_EFFINDOM_V2_SKIA_PLATFORM}-${EFFINDOM_TARGET_ARCH}-${_EFFINDOM_V2_SKIA_COMPILER}-${_EFFINDOM_V2_SKIA_CONFIGURATION}-${_EFFINDOM_V2_SKIA_BACKEND}")
    set(_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_PREFIX "lib")
    set(_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_SUFFIX ".a")
endif()

set(EFFINDOM_V2_SKIA_NATIVE_DIR
    "${_EFFINDOM_V2_SKIA_NATIVE_DEFAULT_DIR}"
    CACHE PATH "Staged native Skia directory for EffinDom v2")

# CMake preserves PATH cache entries when an existing build tree is switched
# between Debug and Release. Keep our generated default in sync while leaving
# genuinely custom staging directories alone.
set(_EFFINDOM_V2_SKIA_RECOGNIZED_DEFAULT_DIRS "${CMAKE_SOURCE_DIR}/skia/native")
foreach(_configuration IN ITEMS debug release relwithdebinfo minsizerel)
    if(WIN32)
        foreach(_backend IN ITEMS raster d3d)
            list(APPEND _EFFINDOM_V2_SKIA_RECOGNIZED_DEFAULT_DIRS
                "${CMAKE_SOURCE_DIR}/skia/native/windows-${EFFINDOM_TARGET_ARCH}-msvc-${_configuration}-${_backend}")
        endforeach()
    else()
        foreach(_backend IN ITEMS raster vulkan)
            list(APPEND _EFFINDOM_V2_SKIA_RECOGNIZED_DEFAULT_DIRS
                "${CMAKE_SOURCE_DIR}/skia/native/${_EFFINDOM_V2_SKIA_PLATFORM}-${EFFINDOM_TARGET_ARCH}-${_EFFINDOM_V2_SKIA_COMPILER}-${_configuration}-${_backend}")
        endforeach()
    endif()
endforeach()
list(FIND _EFFINDOM_V2_SKIA_RECOGNIZED_DEFAULT_DIRS
    "${EFFINDOM_V2_SKIA_NATIVE_DIR}"
    _EFFINDOM_V2_SKIA_RECOGNIZED_DEFAULT_INDEX)
if(NOT _EFFINDOM_V2_SKIA_RECOGNIZED_DEFAULT_INDEX EQUAL -1 AND
   NOT EFFINDOM_V2_SKIA_NATIVE_DIR STREQUAL _EFFINDOM_V2_SKIA_NATIVE_DEFAULT_DIR)
    set(EFFINDOM_V2_SKIA_NATIVE_DIR
        "${_EFFINDOM_V2_SKIA_NATIVE_DEFAULT_DIR}"
        CACHE PATH "Staged native Skia directory for EffinDom v2" FORCE)
endif()

set(_EFFINDOM_V2_SKIA_NATIVE_LIB "${EFFINDOM_V2_SKIA_NATIVE_DIR}/${_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_PREFIX}skia${_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_SUFFIX}")
set(_EFFINDOM_V2_SKIA_NATIVE_SVG_LIB "${EFFINDOM_V2_SKIA_NATIVE_DIR}/${_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_PREFIX}svg${_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_SUFFIX}")
set(_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER_LIB "${EFFINDOM_V2_SKIA_NATIVE_DIR}/${_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_PREFIX}skshaper${_EFFINDOM_V2_SKIA_NATIVE_LIBRARY_SUFFIX}")
set(_EFFINDOM_V2_SKIA_NATIVE_HEADER "${EFFINDOM_V2_SKIA_NATIVE_DIR}/include/core/SkCanvas.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SKCMS "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/skcms/skcms.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SKRESOURCES "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/skresources/include/SkResources.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/skshaper/include/SkShaper_factory.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SVG "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/svg/include/SkSVGDOM.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SRC_CORE "${EFFINDOM_V2_SKIA_NATIVE_DIR}/src/core/SkTHash.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SRC_BASE "${EFFINDOM_V2_SKIA_NATIVE_DIR}/src/base/SkMathPriv.h")
if(WIN32)
    set(_EFFINDOM_V2_SKIA_NATIVE_BACKEND_STAMP "${EFFINDOM_V2_SKIA_NATIVE_DIR}/.effindom-skia-build.json")
else()
    set(_EFFINDOM_V2_SKIA_NATIVE_BACKEND_STAMP "${EFFINDOM_V2_SKIA_NATIVE_DIR}/.effindom-skia-backend")
endif()

if(WIN32)
    set(_EFFINDOM_V2_SKIA_BUILD_COMMAND
        powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/scripts/build_skia_native.ps1"
        -Architecture "${EFFINDOM_TARGET_ARCH}"
        -Configuration "${CMAKE_BUILD_TYPE}"
        -Backend "${EFFINDOM_NATIVE_GRAPHICS_BACKEND}"
        -StagingDir "${EFFINDOM_V2_SKIA_NATIVE_DIR}")
    set(_EFFINDOM_V2_SKIA_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/build_skia_native.ps1")
else()
    set(_EFFINDOM_V2_SKIA_BUILD_COMMAND
        "${CMAKE_COMMAND}" -E env
        "SKIA_NATIVE_DIR=${EFFINDOM_V2_SKIA_NATIVE_DIR}"
        "SKIA_TARGET_ARCH=${EFFINDOM_TARGET_ARCH}"
        "SKIA_BUILD_CONFIGURATION=${CMAKE_BUILD_TYPE}"
        "SKIA_NATIVE_BACKEND=${_EFFINDOM_V2_SKIA_BACKEND}"
        "SKIA_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "${CMAKE_SOURCE_DIR}/scripts/build_skia_native.sh")
    set(_EFFINDOM_V2_SKIA_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/build_skia_native.sh")
endif()

add_custom_command(
    OUTPUT
        "${_EFFINDOM_V2_SKIA_NATIVE_LIB}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SVG_LIB}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER_LIB}"
        "${_EFFINDOM_V2_SKIA_NATIVE_HEADER}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKCMS}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKRESOURCES}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SVG}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SRC_CORE}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SRC_BASE}"
        "${_EFFINDOM_V2_SKIA_NATIVE_BACKEND_STAMP}"
    COMMAND ${_EFFINDOM_V2_SKIA_BUILD_COMMAND}
    DEPENDS
        "${_EFFINDOM_V2_SKIA_BUILD_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)

add_custom_target(effindom_v2_skia_native_build
    DEPENDS
        "${_EFFINDOM_V2_SKIA_NATIVE_LIB}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SVG_LIB}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER_LIB}"
        "${_EFFINDOM_V2_SKIA_NATIVE_HEADER}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKCMS}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKRESOURCES}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SVG}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SRC_CORE}"
        "${_EFFINDOM_V2_SKIA_NATIVE_SRC_BASE}"
        "${_EFFINDOM_V2_SKIA_NATIVE_BACKEND_STAMP}"
)

add_library(effindom_v2_skia_native INTERFACE)
add_dependencies(effindom_v2_skia_native effindom_v2_skia_native_build)
target_include_directories(effindom_v2_skia_native SYSTEM INTERFACE
    "${EFFINDOM_V2_SKIA_NATIVE_DIR}"
    "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/skcms"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Skia applies trivial_abi to public value types in non-official Debug Clang
    # builds, but omits it from official Release builds. Consumers must match or
    # returned sk_sp values use a different calling convention and are corrupted.
    target_compile_definitions(effindom_v2_skia_native INTERFACE
        "$<$<CONFIG:Debug>:SK_TRIVIAL_ABI=[[clang::trivial_abi]]>"
    )
endif()
if(APPLE)
    target_compile_definitions(effindom_v2_skia_native INTERFACE
        SK_GANESH
        SK_METAL
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EFFINDOM_NATIVE_GRAPHICS_BACKEND STREQUAL "vulkan")
    target_compile_definitions(effindom_v2_skia_native INTERFACE
        SK_GANESH
        SK_VULKAN
    )
    find_package(Vulkan REQUIRED)
    target_link_libraries(effindom_v2_skia_native INTERFACE Vulkan::Vulkan)
endif()
target_link_libraries(effindom_v2_skia_native INTERFACE
    "${_EFFINDOM_V2_SKIA_NATIVE_SVG_LIB}"
    "${_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER_LIB}"
    "${_EFFINDOM_V2_SKIA_NATIVE_LIB}"
)

find_package(Threads QUIET)
if(Threads_FOUND)
    target_link_libraries(effindom_v2_skia_native INTERFACE Threads::Threads)
endif()

find_library(_EFFINDOM_V2_LIBM NAMES m)
if(_EFFINDOM_V2_LIBM)
    target_link_libraries(effindom_v2_skia_native INTERFACE "${_EFFINDOM_V2_LIBM}")
endif()

find_library(_EFFINDOM_V2_LIBEXPAT NAMES expat)
if(_EFFINDOM_V2_LIBEXPAT)
    target_link_libraries(effindom_v2_skia_native INTERFACE "${_EFFINDOM_V2_LIBEXPAT}")
endif()

if(APPLE)
    foreach(_framework IN ITEMS
        ApplicationServices
        CoreFoundation
        CoreGraphics
        CoreText
        CoreServices
        ImageIO
        Foundation
    )
        find_library(_EFFINDOM_V2_FRAMEWORK_${_framework} NAMES ${_framework})
        if(_EFFINDOM_V2_FRAMEWORK_${_framework})
            target_link_libraries(effindom_v2_skia_native INTERFACE "${_EFFINDOM_V2_FRAMEWORK_${_framework}}")
        endif()
    endforeach()
endif()
