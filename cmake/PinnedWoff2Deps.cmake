include(FetchContent)

set(EFFINDOM_PINNED_BROTLI_REV "v1.1.0")
set(EFFINDOM_PINNED_WOFF2_REV "v1.0.2")

FetchContent_Declare(
    effindom_pinned_brotli
    GIT_REPOSITORY https://github.com/google/brotli.git
    GIT_TAG        ${EFFINDOM_PINNED_BROTLI_REV}
)

FetchContent_GetProperties(effindom_pinned_brotli)
if(NOT effindom_pinned_brotli_POPULATED)
    set(_EFFINDOM_PREV_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(_EFFINDOM_PREV_BROTLI_BUILD_TOOLS "${BROTLI_BUILD_TOOLS}")
    set(_EFFINDOM_PREV_BROTLI_DISABLE_TESTS "${BROTLI_DISABLE_TESTS}")
    set(_EFFINDOM_PREV_BROTLI_BUNDLED_MODE "${BROTLI_BUNDLED_MODE}")

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
    set(BROTLI_BUNDLED_MODE ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(effindom_pinned_brotli)

    if(DEFINED _EFFINDOM_PREV_BUILD_SHARED_LIBS AND NOT _EFFINDOM_PREV_BUILD_SHARED_LIBS STREQUAL "")
        set(BUILD_SHARED_LIBS "${_EFFINDOM_PREV_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
    else()
        unset(BUILD_SHARED_LIBS CACHE)
    endif()
    if(DEFINED _EFFINDOM_PREV_BROTLI_BUILD_TOOLS AND NOT _EFFINDOM_PREV_BROTLI_BUILD_TOOLS STREQUAL "")
        set(BROTLI_BUILD_TOOLS "${_EFFINDOM_PREV_BROTLI_BUILD_TOOLS}" CACHE BOOL "" FORCE)
    else()
        unset(BROTLI_BUILD_TOOLS CACHE)
    endif()
    if(DEFINED _EFFINDOM_PREV_BROTLI_DISABLE_TESTS AND NOT _EFFINDOM_PREV_BROTLI_DISABLE_TESTS STREQUAL "")
        set(BROTLI_DISABLE_TESTS "${_EFFINDOM_PREV_BROTLI_DISABLE_TESTS}" CACHE BOOL "" FORCE)
    else()
        unset(BROTLI_DISABLE_TESTS CACHE)
    endif()
    if(DEFINED _EFFINDOM_PREV_BROTLI_BUNDLED_MODE AND NOT _EFFINDOM_PREV_BROTLI_BUNDLED_MODE STREQUAL "")
        set(BROTLI_BUNDLED_MODE "${_EFFINDOM_PREV_BROTLI_BUNDLED_MODE}" CACHE BOOL "" FORCE)
    else()
        unset(BROTLI_BUNDLED_MODE CACHE)
    endif()
endif()

FetchContent_Declare(
    effindom_pinned_woff2
    GIT_REPOSITORY https://github.com/google/woff2.git
    GIT_TAG        ${EFFINDOM_PINNED_WOFF2_REV}
)

FetchContent_GetProperties(effindom_pinned_woff2)
if(NOT effindom_pinned_woff2_POPULATED)
    FetchContent_Populate(effindom_pinned_woff2)
endif()

add_library(effindom_woff2common STATIC
    "${effindom_pinned_woff2_SOURCE_DIR}/src/table_tags.cc"
    "${effindom_pinned_woff2_SOURCE_DIR}/src/variable_length.cc"
    "${effindom_pinned_woff2_SOURCE_DIR}/src/woff2_common.cc"
)
target_include_directories(effindom_woff2common
    PUBLIC
        "${effindom_pinned_woff2_SOURCE_DIR}/include"
    PRIVATE
        "${effindom_pinned_woff2_SOURCE_DIR}/src"
)
target_link_libraries(effindom_woff2common PRIVATE effindom_wasm_feature_flags)

add_library(effindom_woff2dec STATIC
    "${effindom_pinned_woff2_SOURCE_DIR}/src/woff2_dec.cc"
    "${effindom_pinned_woff2_SOURCE_DIR}/src/woff2_out.cc"
)
target_include_directories(effindom_woff2dec
    PUBLIC
        "${effindom_pinned_woff2_SOURCE_DIR}/include"
    PRIVATE
        "${effindom_pinned_woff2_SOURCE_DIR}/src"
)
target_link_libraries(effindom_woff2dec
    PRIVATE
        effindom_wasm_feature_flags
        effindom_woff2common
        brotlidec
        brotlicommon
)

target_link_libraries(brotlicommon effindom_wasm_feature_flags)
target_link_libraries(brotlidec effindom_wasm_feature_flags)
