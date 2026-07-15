set(EFFINDOM_V2_SKIA_NATIVE_DIR
    "${CMAKE_SOURCE_DIR}/skia/native"
    CACHE PATH "Staged native Skia directory for EffinDom v2")

set(_EFFINDOM_V2_SKIA_NATIVE_LIB "${EFFINDOM_V2_SKIA_NATIVE_DIR}/libskia.a")
set(_EFFINDOM_V2_SKIA_NATIVE_SVG_LIB "${EFFINDOM_V2_SKIA_NATIVE_DIR}/libsvg.a")
set(_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER_LIB "${EFFINDOM_V2_SKIA_NATIVE_DIR}/libskshaper.a")
set(_EFFINDOM_V2_SKIA_NATIVE_HEADER "${EFFINDOM_V2_SKIA_NATIVE_DIR}/include/core/SkCanvas.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SKCMS "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/skcms/skcms.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SKRESOURCES "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/skresources/include/SkResources.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SKSHAPER "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/skshaper/include/SkShaper_factory.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SVG "${EFFINDOM_V2_SKIA_NATIVE_DIR}/modules/svg/include/SkSVGDOM.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SRC_CORE "${EFFINDOM_V2_SKIA_NATIVE_DIR}/src/core/SkTHash.h")
set(_EFFINDOM_V2_SKIA_NATIVE_SRC_BASE "${EFFINDOM_V2_SKIA_NATIVE_DIR}/src/base/SkMathPriv.h")
set(_EFFINDOM_V2_SKIA_NATIVE_BACKEND_STAMP "${EFFINDOM_V2_SKIA_NATIVE_DIR}/.effindom-skia-backend")

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
    COMMAND "${CMAKE_COMMAND}" -E env
        "SKIA_NATIVE_DIR=${EFFINDOM_V2_SKIA_NATIVE_DIR}"
        "${CMAKE_SOURCE_DIR}/scripts/build_skia_native.sh"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/scripts/build_skia_native.sh"
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
