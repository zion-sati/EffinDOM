include(FetchContent)

find_program(EFFINDOM_MESON_EXECUTABLE NAMES meson REQUIRED)
find_program(EFFINDOM_HARFBUZZ_NINJA_EXECUTABLE NAMES ninja ninja-build REQUIRED)
find_package(Threads QUIET)

function(_effindom_resolve_meson_buildtype out_var)
    set(_buildtype "release")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_buildtype "debug")
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(_buildtype "debugoptimized")
    elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        set(_buildtype "minsize")
    endif()
    set(${out_var} "${_buildtype}" PARENT_SCOPE)
endfunction()

# Keep UI text shaping aligned with the same HarfBuzz / ICU revisions pinned by
# the Skia chrome/m136 toolchain used elsewhere in this repository.
set(EFFINDOM_SKIA_PINNED_HARFBUZZ_REV "ca3cd48fa3e06fa81d7c8a3f716cca44ed2de26a")
set(EFFINDOM_SKIA_PINNED_ICU_REV "364118a1d9da24bb5b770ac3d762ac144d6da5a4")

FetchContent_Declare(
    effindom_skia_pinned_harfbuzz
    GIT_REPOSITORY https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git
    GIT_TAG        ${EFFINDOM_SKIA_PINNED_HARFBUZZ_REV}
    # HarfBuzz is built through the pinned Meson configuration below.
    SOURCE_SUBDIR  __effindom_source_only
)
FetchContent_MakeAvailable(effindom_skia_pinned_harfbuzz)

file(TO_CMAKE_PATH "${EFFINDOM_MESON_EXECUTABLE}" _EFFINDOM_MESON_EXECUTABLE_PATH)
file(TO_CMAKE_PATH "${EFFINDOM_HARFBUZZ_NINJA_EXECUTABLE}" _EFFINDOM_HARFBUZZ_NINJA_EXECUTABLE_PATH)
file(TO_CMAKE_PATH "${effindom_skia_pinned_harfbuzz_SOURCE_DIR}" _EFFINDOM_HARFBUZZ_SOURCE_DIR_PATH)

set(_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR "${CMAKE_BINARY_DIR}/effindom-harfbuzz-meson-build")
file(TO_CMAKE_PATH "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR}" _EFFINDOM_HARFBUZZ_MESON_BUILD_DIR_PATH)

set(_EFFINDOM_HARFBUZZ_CROSS_FILE "")
if(EMSCRIPTEN)
    find_program(EFFINDOM_HARFBUZZ_EMCC NAMES emcc REQUIRED)
    find_program(EFFINDOM_HARFBUZZ_EMXX NAMES em++ REQUIRED)
    find_program(EFFINDOM_HARFBUZZ_EMAR NAMES emar REQUIRED)
    find_program(EFFINDOM_HARFBUZZ_EMRANLIB NAMES emranlib REQUIRED)
    find_program(EFFINDOM_HARFBUZZ_EMNM NAMES emnm REQUIRED)
    find_program(EFFINDOM_HARFBUZZ_EMSTRIP NAMES emstrip REQUIRED)

    file(TO_CMAKE_PATH "${EFFINDOM_HARFBUZZ_EMCC}" EFFINDOM_HARFBUZZ_EMCC)
    file(TO_CMAKE_PATH "${EFFINDOM_HARFBUZZ_EMXX}" EFFINDOM_HARFBUZZ_EMXX)
    file(TO_CMAKE_PATH "${EFFINDOM_HARFBUZZ_EMAR}" EFFINDOM_HARFBUZZ_EMAR)
    file(TO_CMAKE_PATH "${EFFINDOM_HARFBUZZ_EMRANLIB}" EFFINDOM_HARFBUZZ_EMRANLIB)
    file(TO_CMAKE_PATH "${EFFINDOM_HARFBUZZ_EMNM}" EFFINDOM_HARFBUZZ_EMNM)
    file(TO_CMAKE_PATH "${EFFINDOM_HARFBUZZ_EMSTRIP}" EFFINDOM_HARFBUZZ_EMSTRIP)

    set(EFFINDOM_HARFBUZZ_CPU_FAMILY "wasm32")
    set(EFFINDOM_HARFBUZZ_CPU "wasm32")
    set(_EFFINDOM_HARFBUZZ_C_ARGS "-O3" "-flto")
    set(_EFFINDOM_HARFBUZZ_CPP_ARGS "-O3" "-flto")
    set(_EFFINDOM_HARFBUZZ_C_LINK_ARGS "-O3" "-flto")
    set(_EFFINDOM_HARFBUZZ_CPP_LINK_ARGS "-O3" "-flto")
    if(EFFINDOM_WASM_ARCH_SUFFIX MATCHES "^64")
        set(EFFINDOM_HARFBUZZ_CPU_FAMILY "wasm64")
        set(EFFINDOM_HARFBUZZ_CPU "wasm64")
        list(APPEND _EFFINDOM_HARFBUZZ_C_ARGS "-sMEMORY64=1")
        list(APPEND _EFFINDOM_HARFBUZZ_CPP_ARGS "-sMEMORY64=1")
        list(APPEND _EFFINDOM_HARFBUZZ_C_LINK_ARGS "-sMEMORY64=1")
        list(APPEND _EFFINDOM_HARFBUZZ_CPP_LINK_ARGS "-sMEMORY64=1")
    endif()
    if(EFFINDOM_SIMD)
        list(APPEND _EFFINDOM_HARFBUZZ_C_ARGS "-msimd128")
        list(APPEND _EFFINDOM_HARFBUZZ_CPP_ARGS "-msimd128")
        list(APPEND _EFFINDOM_HARFBUZZ_C_LINK_ARGS "-msimd128")
        list(APPEND _EFFINDOM_HARFBUZZ_CPP_LINK_ARGS "-msimd128")
    endif()
    set(EFFINDOM_HARFBUZZ_WASM_BUILTIN_OPTIONS "")
    if(_EFFINDOM_HARFBUZZ_C_ARGS OR
       _EFFINDOM_HARFBUZZ_CPP_ARGS OR
       _EFFINDOM_HARFBUZZ_C_LINK_ARGS OR
       _EFFINDOM_HARFBUZZ_CPP_LINK_ARGS)
        string(APPEND EFFINDOM_HARFBUZZ_WASM_BUILTIN_OPTIONS "[built-in options]\n")
        string(REPLACE ";" "', '" _EFFINDOM_HARFBUZZ_C_ARGS_JOINED "${_EFFINDOM_HARFBUZZ_C_ARGS}")
        string(REPLACE ";" "', '" _EFFINDOM_HARFBUZZ_CPP_ARGS_JOINED "${_EFFINDOM_HARFBUZZ_CPP_ARGS}")
        string(REPLACE ";" "', '" _EFFINDOM_HARFBUZZ_C_LINK_ARGS_JOINED "${_EFFINDOM_HARFBUZZ_C_LINK_ARGS}")
        string(REPLACE ";" "', '" _EFFINDOM_HARFBUZZ_CPP_LINK_ARGS_JOINED "${_EFFINDOM_HARFBUZZ_CPP_LINK_ARGS}")
        string(APPEND EFFINDOM_HARFBUZZ_WASM_BUILTIN_OPTIONS
            "c_args = ['${_EFFINDOM_HARFBUZZ_C_ARGS_JOINED}']\n"
            "cpp_args = ['${_EFFINDOM_HARFBUZZ_CPP_ARGS_JOINED}']\n"
            "c_link_args = ['${_EFFINDOM_HARFBUZZ_C_LINK_ARGS_JOINED}']\n"
            "cpp_link_args = ['${_EFFINDOM_HARFBUZZ_CPP_LINK_ARGS_JOINED}']\n"
        )
    endif()

    set(_EFFINDOM_HARFBUZZ_CROSS_FILE "${CMAKE_BINARY_DIR}/effindom-harfbuzz-meson-cross.ini")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/EmscriptenMesonCross.ini.in"
        "${_EFFINDOM_HARFBUZZ_CROSS_FILE}"
        @ONLY
    )
elseif(WIN32 AND EFFINDOM_TARGET_ARCH STREQUAL "arm64")
    # Meson otherwise treats this as a native build and tries to execute its
    # ARM64 compiler sanity binary on the x64 build host.
    file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" EFFINDOM_HARFBUZZ_MSVC_COMPILER)
    file(TO_CMAKE_PATH "${CMAKE_AR}" EFFINDOM_HARFBUZZ_MSVC_ARCHIVER)
    set(_EFFINDOM_HARFBUZZ_CROSS_FILE "${CMAKE_BINARY_DIR}/effindom-harfbuzz-meson-cross.ini")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/WindowsArm64MesonCross.ini.in"
        "${_EFFINDOM_HARFBUZZ_CROSS_FILE}"
        @ONLY
    )
elseif(APPLE)
    file(TO_CMAKE_PATH "${CMAKE_C_COMPILER}" EFFINDOM_HARFBUZZ_APPLE_C_COMPILER)
    file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" EFFINDOM_HARFBUZZ_APPLE_CPP_COMPILER)
    file(TO_CMAKE_PATH "${CMAKE_AR}" EFFINDOM_HARFBUZZ_APPLE_ARCHIVER)
    if(EFFINDOM_TARGET_ARCH STREQUAL "arm64")
        set(EFFINDOM_HARFBUZZ_APPLE_ARCH "arm64")
        set(EFFINDOM_HARFBUZZ_APPLE_CPU_FAMILY "aarch64")
        set(EFFINDOM_HARFBUZZ_APPLE_CPU "arm64")
    else()
        set(EFFINDOM_HARFBUZZ_APPLE_ARCH "x86_64")
        set(EFFINDOM_HARFBUZZ_APPLE_CPU_FAMILY "x86_64")
        set(EFFINDOM_HARFBUZZ_APPLE_CPU "x86_64")
    endif()
    set(_EFFINDOM_HARFBUZZ_CROSS_FILE "${CMAKE_BINARY_DIR}/effindom-harfbuzz-meson-cross.ini")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/MacosMesonCross.ini.in"
        "${_EFFINDOM_HARFBUZZ_CROSS_FILE}"
        @ONLY
    )
endif()
file(TO_CMAKE_PATH "${_EFFINDOM_HARFBUZZ_CROSS_FILE}" _EFFINDOM_HARFBUZZ_CROSS_FILE_PATH)

_effindom_resolve_meson_buildtype(_EFFINDOM_HARFBUZZ_MESON_BUILDTYPE)
set(_EFFINDOM_HARFBUZZ_MESON_SETUP_ARGS
    "--buildtype=${_EFFINDOM_HARFBUZZ_MESON_BUILDTYPE}"
    "--default-library=static"
    "-Dbenchmark=disabled"
    "-Dcairo=disabled"
    "-Dchafa=disabled"
    "-Dcoretext=disabled"
    "-Ddirectwrite=disabled"
    "-Ddocs=disabled"
    "-Dfreetype=disabled"
    "-Dgdi=disabled"
    "-Dglib=disabled"
    "-Dgobject=disabled"
    "-Dgraphite=disabled"
    "-Dgraphite2=disabled"
    "-Dicu=disabled"
    "-Dintrospection=disabled"
    "-Dtests=disabled"
    "-Dutilities=disabled"
)
set(EFFINDOM_HARFBUZZ_MESON_SETUP_ARGS_BLOCK "")
foreach(_arg IN LISTS _EFFINDOM_HARFBUZZ_MESON_SETUP_ARGS)
    string(APPEND EFFINDOM_HARFBUZZ_MESON_SETUP_ARGS_BLOCK
        "list(APPEND _setup_args [==[${_arg}]==])\n")
endforeach()

set(EFFINDOM_HARFBUZZ_MESON_EXECUTABLE "${_EFFINDOM_MESON_EXECUTABLE_PATH}")
set(EFFINDOM_HARFBUZZ_NINJA_EXECUTABLE "${_EFFINDOM_HARFBUZZ_NINJA_EXECUTABLE_PATH}")
set(EFFINDOM_HARFBUZZ_SOURCE_DIR "${_EFFINDOM_HARFBUZZ_SOURCE_DIR_PATH}")
set(EFFINDOM_HARFBUZZ_MESON_BUILD_DIR "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR_PATH}")
set(EFFINDOM_HARFBUZZ_CROSS_FILE "${_EFFINDOM_HARFBUZZ_CROSS_FILE_PATH}")
set(_EFFINDOM_HARFBUZZ_BUILD_SCRIPT "${CMAKE_BINARY_DIR}/effindom-build-harfbuzz-meson.cmake")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/BuildHarfBuzzMeson.cmake.in"
    "${_EFFINDOM_HARFBUZZ_BUILD_SCRIPT}"
    @ONLY
)

file(GLOB_RECURSE EFFINDOM_HARFBUZZ_BUILD_SOURCES CONFIGURE_DEPENDS
    "${effindom_skia_pinned_harfbuzz_SOURCE_DIR}/meson.build"
    "${effindom_skia_pinned_harfbuzz_SOURCE_DIR}/meson_options.txt"
    "${effindom_skia_pinned_harfbuzz_SOURCE_DIR}/src/*"
)

# HarfBuzz's Meson static target retains its Unix-style archive name when it
# contains MSVC COFF objects; the file is still a valid MSVC static library.
set(_EFFINDOM_HARFBUZZ_LIBRARY_NAME "libharfbuzz.a")
set(_EFFINDOM_HARFBUZZ_LIBRARY_PATH "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR}/src/${_EFFINDOM_HARFBUZZ_LIBRARY_NAME}")
set(_EFFINDOM_HARFBUZZ_CONFIG_PATH "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR}/config.h")
set(_EFFINDOM_HARFBUZZ_VERSION_HEADER_PATH "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR}/src/hb-version.h")
set(_EFFINDOM_HARFBUZZ_FEATURES_HEADER_PATH "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR}/src/hb-features.h")

add_custom_command(
    OUTPUT
        "${_EFFINDOM_HARFBUZZ_LIBRARY_PATH}"
        "${_EFFINDOM_HARFBUZZ_CONFIG_PATH}"
        "${_EFFINDOM_HARFBUZZ_VERSION_HEADER_PATH}"
        "${_EFFINDOM_HARFBUZZ_FEATURES_HEADER_PATH}"
    COMMAND "${CMAKE_COMMAND}" -P "${_EFFINDOM_HARFBUZZ_BUILD_SCRIPT}"
    DEPENDS
        "${_EFFINDOM_HARFBUZZ_BUILD_SCRIPT}"
        "${_EFFINDOM_HARFBUZZ_CROSS_FILE}"
        ${EFFINDOM_HARFBUZZ_BUILD_SOURCES}
    VERBATIM
)

add_custom_target(effindom_harfbuzz_meson
    DEPENDS
        "${_EFFINDOM_HARFBUZZ_LIBRARY_PATH}"
        "${_EFFINDOM_HARFBUZZ_CONFIG_PATH}"
        "${_EFFINDOM_HARFBUZZ_VERSION_HEADER_PATH}"
        "${_EFFINDOM_HARFBUZZ_FEATURES_HEADER_PATH}"
)

add_library(harfbuzz INTERFACE)
add_dependencies(harfbuzz effindom_harfbuzz_meson)
target_include_directories(harfbuzz SYSTEM INTERFACE
    "${effindom_skia_pinned_harfbuzz_SOURCE_DIR}/src"
    "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR}"
    "${_EFFINDOM_HARFBUZZ_MESON_BUILD_DIR}/src"
)
target_link_libraries(harfbuzz INTERFACE
    "${_EFFINDOM_HARFBUZZ_LIBRARY_PATH}"
)

if(Threads_FOUND)
    target_link_libraries(harfbuzz INTERFACE Threads::Threads)
endif()

find_library(_EFFINDOM_HARFBUZZ_LIBM NAMES m)
if(_EFFINDOM_HARFBUZZ_LIBM)
    target_link_libraries(harfbuzz INTERFACE "${_EFFINDOM_HARFBUZZ_LIBM}")
endif()

FetchContent_Declare(
    effindom_skia_pinned_icu
    GIT_REPOSITORY https://chromium.googlesource.com/chromium/deps/icu.git
    GIT_TAG        ${EFFINDOM_SKIA_PINNED_ICU_REV}
    # EffinDOM owns the ICU source lists and target configuration below.
    SOURCE_SUBDIR  __effindom_source_only
)
FetchContent_MakeAvailable(effindom_skia_pinned_icu)

set(_EFFINDOM_ICU_SOURCE_ROOT "${effindom_skia_pinned_icu_SOURCE_DIR}/source")

file(GLOB EFFINDOM_ICU_COMMON_SOURCES CONFIGURE_DEPENDS
    "${_EFFINDOM_ICU_SOURCE_ROOT}/common/*.c"
    "${_EFFINDOM_ICU_SOURCE_ROOT}/common/*.cpp"
)
file(GLOB EFFINDOM_ICU_STUBDATA_SOURCES CONFIGURE_DEPENDS
    "${_EFFINDOM_ICU_SOURCE_ROOT}/stubdata/*.c"
    "${_EFFINDOM_ICU_SOURCE_ROOT}/stubdata/*.cpp"
)
file(GLOB EFFINDOM_ICU_I18N_SOURCES CONFIGURE_DEPENDS
    "${_EFFINDOM_ICU_SOURCE_ROOT}/i18n/*.c"
    "${_EFFINDOM_ICU_SOURCE_ROOT}/i18n/*.cpp"
)

function(_effindom_configure_icu_target target_name)
    target_compile_definitions(${target_name}
        PUBLIC
            U_NO_DEFAULT_INCLUDE_UTF_HEADERS=1
            U_STATIC_IMPLEMENTATION=1
            U_USING_ICU_NAMESPACE=0
            UNISTR_FROM_CHAR_EXPLICIT=explicit
            UNISTR_FROM_STRING_EXPLICIT=explicit
    )
    target_include_directories(${target_name}
        PUBLIC
            "${_EFFINDOM_ICU_SOURCE_ROOT}/common"
            "${_EFFINDOM_ICU_SOURCE_ROOT}/i18n"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE
            -Wno-deprecated-declarations
            -Wno-macro-redefined
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target_name} PRIVATE
            -Wno-deprecated-declarations
        )
    endif()
    set_target_properties(${target_name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

add_library(effindom_icu_common STATIC ${EFFINDOM_ICU_COMMON_SOURCES})
target_compile_definitions(effindom_icu_common PRIVATE U_COMMON_IMPLEMENTATION=1)
target_link_libraries(effindom_icu_common PRIVATE effindom_wasm_feature_flags)
_effindom_configure_icu_target(effindom_icu_common)

add_library(effindom_icu_stubdata STATIC ${EFFINDOM_ICU_STUBDATA_SOURCES})
target_link_libraries(effindom_icu_stubdata PRIVATE effindom_icu_common)
target_link_libraries(effindom_icu_stubdata PRIVATE effindom_wasm_feature_flags)
_effindom_configure_icu_target(effindom_icu_stubdata)

add_library(effindom_icu_i18n STATIC ${EFFINDOM_ICU_I18N_SOURCES})
target_compile_definitions(effindom_icu_i18n PRIVATE U_I18N_IMPLEMENTATION=1)
target_link_libraries(effindom_icu_i18n PUBLIC effindom_icu_common effindom_icu_stubdata)
target_link_libraries(effindom_icu_i18n PRIVATE effindom_wasm_feature_flags)
_effindom_configure_icu_target(effindom_icu_i18n)
