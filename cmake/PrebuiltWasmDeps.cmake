function(_effindom_wasm_deps_require relative_path)
    if(NOT EXISTS "${EFFINDOM_WASM_DEPS_ROOT}/${relative_path}")
        message(FATAL_ERROR
            "Prebuilt WASM dependency SDK is incomplete: ${relative_path}\n"
            "Root: ${EFFINDOM_WASM_DEPS_ROOT}")
    endif()
endfunction()

function(_effindom_wasm_add_prebuilt_static target_name library_name)
    _effindom_wasm_deps_require("lanes/${_effindom_wasm_lane}/lib/${library_name}")
    add_library(${target_name} STATIC IMPORTED GLOBAL)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${EFFINDOM_WASM_DEPS_ROOT}/lanes/${_effindom_wasm_lane}/lib/${library_name}")
endfunction()

function(effindom_use_prebuilt_wasm_deps)
    if(NOT EFFINDOM_WASM_DEPS_ROOT)
        message(FATAL_ERROR "EFFINDOM_WASM_DEPS_ROOT is required for prebuilt WASM dependencies.")
    endif()

    if(EFFINDOM_WASM_ARCH_SUFFIX MATCHES "^64")
        set(_effindom_wasm_lane "wasm64")
    else()
        set(_effindom_wasm_lane "wasm32")
    endif()
    if(EFFINDOM_SIMD)
        set(_effindom_wasm_lane "${_effindom_wasm_lane}-simd")
    endif()

    _effindom_wasm_deps_require("lanes/${_effindom_wasm_lane}/skia/libskia.a")
    _effindom_wasm_add_prebuilt_static(yogacore "yoga.a")
    target_include_directories(yogacore SYSTEM INTERFACE "${EFFINDOM_WASM_DEPS_ROOT}/sources/yoga")
    target_link_libraries(yogacore INTERFACE effindom_wasm_feature_flags)

    _effindom_wasm_add_prebuilt_static(effindom_icu_common "icu-common.a")
    _effindom_wasm_add_prebuilt_static(effindom_icu_stubdata "icu-stubdata.a")
    _effindom_wasm_add_prebuilt_static(effindom_icu_i18n "icu-i18n.a")
    foreach(_icu_target IN ITEMS effindom_icu_common effindom_icu_stubdata effindom_icu_i18n)
        target_include_directories(${_icu_target} SYSTEM INTERFACE
            "${EFFINDOM_WASM_DEPS_ROOT}/sources/icu-common"
            "${EFFINDOM_WASM_DEPS_ROOT}/sources/icu-i18n")
        target_compile_definitions(${_icu_target} INTERFACE
            U_NO_DEFAULT_INCLUDE_UTF_HEADERS=1 U_STATIC_IMPLEMENTATION=1
            U_USING_ICU_NAMESPACE=0 UNISTR_FROM_CHAR_EXPLICIT=explicit
            UNISTR_FROM_STRING_EXPLICIT=explicit)
        target_link_libraries(${_icu_target} INTERFACE effindom_wasm_feature_flags)
    endforeach()
    target_link_libraries(effindom_icu_stubdata INTERFACE effindom_icu_common)
    target_link_libraries(effindom_icu_i18n INTERFACE effindom_icu_common effindom_icu_stubdata)

    _effindom_wasm_add_prebuilt_static(effindom_woff2common "woff2-common.a")
    _effindom_wasm_add_prebuilt_static(effindom_woff2dec "woff2-dec.a")
    _effindom_wasm_add_prebuilt_static(brotlicommon "brotli-common.a")
    _effindom_wasm_add_prebuilt_static(brotlidec "brotli-dec.a")
    target_include_directories(effindom_woff2common SYSTEM INTERFACE "${EFFINDOM_WASM_DEPS_ROOT}/sources/woff2")
    target_include_directories(effindom_woff2dec SYSTEM INTERFACE "${EFFINDOM_WASM_DEPS_ROOT}/sources/woff2")
    target_include_directories(brotlicommon SYSTEM INTERFACE "${EFFINDOM_WASM_DEPS_ROOT}/sources/brotli")
    target_include_directories(brotlidec SYSTEM INTERFACE "${EFFINDOM_WASM_DEPS_ROOT}/sources/brotli")
    target_link_libraries(effindom_woff2common INTERFACE effindom_wasm_feature_flags)
    target_link_libraries(effindom_woff2dec INTERFACE effindom_woff2common brotlidec brotlicommon effindom_wasm_feature_flags)
    target_link_libraries(brotlicommon INTERFACE effindom_wasm_feature_flags)
    target_link_libraries(brotlidec INTERFACE effindom_wasm_feature_flags)

    _effindom_wasm_add_prebuilt_static(effindom_harfbuzz "harfbuzz.a")
    add_library(harfbuzz INTERFACE)
    target_include_directories(harfbuzz SYSTEM INTERFACE
        "${EFFINDOM_WASM_DEPS_ROOT}/sources/harfbuzz"
        "${EFFINDOM_WASM_DEPS_ROOT}/lanes/${_effindom_wasm_lane}/generated/harfbuzz")
    target_link_libraries(harfbuzz INTERFACE effindom_harfbuzz)
endfunction()
