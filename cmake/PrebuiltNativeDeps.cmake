function(_effindom_prebuilt_require relative_path)
    if(NOT EXISTS "${EFFINDOM_NATIVE_DEPS_ROOT}/${relative_path}")
        message(FATAL_ERROR
            "Prebuilt native dependency SDK is incomplete: ${relative_path}\n"
            "Root: ${EFFINDOM_NATIVE_DEPS_ROOT}\n"
            "Run the exported platform build script without bypassing its dependency preparation step.")
    endif()
endfunction()

function(_effindom_add_prebuilt_static target_name library_name)
    _effindom_prebuilt_require("lib/${library_name}")
    add_library(${target_name} STATIC IMPORTED GLOBAL)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${EFFINDOM_NATIVE_DEPS_ROOT}/lib/${library_name}")
endfunction()

function(effindom_use_prebuilt_native_deps)
    if(NOT EFFINDOM_NATIVE_DEPS_ROOT)
        message(FATAL_ERROR
            "EFFINDOM_NATIVE_DEPS_MODE=prebuilt requires EFFINDOM_NATIVE_DEPS_ROOT.\n"
            "Use an exported build-native-runtime-* script so it can run scripts/prepare-native-deps.mjs.")
    endif()

    _effindom_prebuilt_require("skia/include/core/SkCanvas.h")
    _effindom_prebuilt_require("include/cpr/cprver.h")
    if(WIN32)
        set(_effindom_library_prefix "")
        set(_effindom_library_suffix ".lib")
    else()
        set(_effindom_library_prefix "lib")
        set(_effindom_library_suffix ".a")
    endif()
    foreach(_library IN ITEMS skia svg skshaper)
        _effindom_prebuilt_require("skia/${_effindom_library_prefix}${_library}${_effindom_library_suffix}")
    endforeach()

    add_library(effindom_v2_skia_native INTERFACE)
    target_include_directories(effindom_v2_skia_native SYSTEM INTERFACE
        "${EFFINDOM_NATIVE_DEPS_ROOT}/skia"
        "${EFFINDOM_NATIVE_DEPS_ROOT}/skia/modules/skcms")
    target_link_libraries(effindom_v2_skia_native INTERFACE
        "${EFFINDOM_NATIVE_DEPS_ROOT}/skia/${_effindom_library_prefix}svg${_effindom_library_suffix}"
        "${EFFINDOM_NATIVE_DEPS_ROOT}/skia/${_effindom_library_prefix}skshaper${_effindom_library_suffix}"
        "${EFFINDOM_NATIVE_DEPS_ROOT}/skia/${_effindom_library_prefix}skia${_effindom_library_suffix}")
    if(APPLE)
        target_compile_definitions(effindom_v2_skia_native INTERFACE SK_GANESH SK_METAL)
        foreach(_framework IN ITEMS ApplicationServices CoreFoundation CoreGraphics CoreText CoreServices ImageIO Foundation)
            find_library(_effindom_framework_${_framework} NAMES ${_framework} REQUIRED)
            target_link_libraries(effindom_v2_skia_native INTERFACE "${_effindom_framework_${_framework}}")
        endforeach()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(effindom_v2_skia_native INTERFACE SK_GANESH SK_VULKAN)
        find_package(Vulkan REQUIRED)
        target_link_libraries(effindom_v2_skia_native INTERFACE Vulkan::Vulkan)
    endif()
    find_package(Threads QUIET)
    if(Threads_FOUND)
        target_link_libraries(effindom_v2_skia_native INTERFACE Threads::Threads)
    endif()
    find_library(_effindom_prebuilt_libm NAMES m)
    if(_effindom_prebuilt_libm)
        target_link_libraries(effindom_v2_skia_native INTERFACE "${_effindom_prebuilt_libm}")
    endif()
    find_library(_effindom_prebuilt_expat NAMES expat)
    if(_effindom_prebuilt_expat)
        target_link_libraries(effindom_v2_skia_native INTERFACE "${_effindom_prebuilt_expat}")
    endif()

    _effindom_add_prebuilt_static(yogacore "yoga${_effindom_library_suffix}")
    target_include_directories(yogacore SYSTEM INTERFACE "${EFFINDOM_NATIVE_DEPS_ROOT}/include")

    _effindom_add_prebuilt_static(effindom_icu_common "icu-common${_effindom_library_suffix}")
    _effindom_add_prebuilt_static(effindom_icu_stubdata "icu-stubdata${_effindom_library_suffix}")
    _effindom_add_prebuilt_static(effindom_icu_i18n "icu-i18n${_effindom_library_suffix}")
    foreach(_icu_target IN ITEMS effindom_icu_common effindom_icu_stubdata effindom_icu_i18n)
        target_include_directories(${_icu_target} SYSTEM INTERFACE
            "${EFFINDOM_NATIVE_DEPS_ROOT}/include/icu/common"
            "${EFFINDOM_NATIVE_DEPS_ROOT}/include/icu/i18n")
        target_compile_definitions(${_icu_target} INTERFACE
            U_NO_DEFAULT_INCLUDE_UTF_HEADERS=1 U_STATIC_IMPLEMENTATION=1
            U_USING_ICU_NAMESPACE=0 UNISTR_FROM_CHAR_EXPLICIT=explicit
            UNISTR_FROM_STRING_EXPLICIT=explicit)
    endforeach()
    target_link_libraries(effindom_icu_stubdata INTERFACE effindom_icu_common)
    target_link_libraries(effindom_icu_i18n INTERFACE effindom_icu_common effindom_icu_stubdata)

    _effindom_add_prebuilt_static(effindom_woff2common "woff2-common${_effindom_library_suffix}")
    _effindom_add_prebuilt_static(effindom_woff2dec "woff2-dec${_effindom_library_suffix}")
    _effindom_add_prebuilt_static(effindom_brotlicommon "brotli-common${_effindom_library_suffix}")
    _effindom_add_prebuilt_static(effindom_brotlidec "brotli-dec${_effindom_library_suffix}")
    target_include_directories(effindom_woff2common SYSTEM INTERFACE "${EFFINDOM_NATIVE_DEPS_ROOT}/include")
    target_include_directories(effindom_woff2dec SYSTEM INTERFACE "${EFFINDOM_NATIVE_DEPS_ROOT}/include")
    target_link_libraries(effindom_woff2dec INTERFACE effindom_woff2common effindom_brotlidec effindom_brotlicommon)

    _effindom_add_prebuilt_static(effindom_harfbuzz "harfbuzz${_effindom_library_suffix}")
    add_library(harfbuzz INTERFACE)
    target_include_directories(harfbuzz SYSTEM INTERFACE
        "${EFFINDOM_NATIVE_DEPS_ROOT}/include/harfbuzz"
        "${EFFINDOM_NATIVE_DEPS_ROOT}/include/harfbuzz-generated")
    target_link_libraries(harfbuzz INTERFACE effindom_harfbuzz)

    _effindom_add_prebuilt_static(effindom_curl "curl${_effindom_library_suffix}")
    _effindom_add_prebuilt_static(effindom_cpr "cpr${_effindom_library_suffix}")
    add_library(cpr::cpr ALIAS effindom_cpr)
    target_include_directories(effindom_curl SYSTEM INTERFACE "${EFFINDOM_NATIVE_DEPS_ROOT}/include")
    target_include_directories(effindom_cpr SYSTEM INTERFACE "${EFFINDOM_NATIVE_DEPS_ROOT}/include")
    target_link_libraries(effindom_cpr INTERFACE effindom_curl)
    if(WIN32)
        target_link_libraries(effindom_curl INTERFACE ws2_32 crypt32 secur32 bcrypt)
    elseif(APPLE)
        foreach(_framework IN ITEMS CoreFoundation Security SystemConfiguration)
            find_library(_effindom_curl_framework_${_framework} NAMES ${_framework} REQUIRED)
            target_link_libraries(effindom_curl INTERFACE "${_effindom_curl_framework_${_framework}}")
        endforeach()
    else()
        _effindom_add_prebuilt_static(effindom_ssl "ssl${_effindom_library_suffix}")
        _effindom_add_prebuilt_static(effindom_crypto "crypto${_effindom_library_suffix}")
        target_link_libraries(effindom_crypto INTERFACE z)
        target_link_libraries(effindom_curl INTERFACE effindom_ssl effindom_crypto dl)
    endif()

    if(WIN32)
        _effindom_prebuilt_require("lib/SDL3.lib")
        _effindom_prebuilt_require("bin/SDL3.dll")
        add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
        set_target_properties(SDL3::SDL3 PROPERTIES
            IMPORTED_IMPLIB "${EFFINDOM_NATIVE_DEPS_ROOT}/lib/SDL3.lib"
            IMPORTED_LOCATION "${EFFINDOM_NATIVE_DEPS_ROOT}/bin/SDL3.dll")
    elseif(APPLE)
        _effindom_prebuilt_require("lib/libSDL3.0.dylib")
        add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
        set_target_properties(SDL3::SDL3 PROPERTIES IMPORTED_LOCATION "${EFFINDOM_NATIVE_DEPS_ROOT}/lib/libSDL3.0.dylib")
    else()
        _effindom_prebuilt_require("lib/libSDL3.so")
        add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
        set_target_properties(SDL3::SDL3 PROPERTIES
            IMPORTED_LOCATION "${EFFINDOM_NATIVE_DEPS_ROOT}/lib/libSDL3.so"
            IMPORTED_SONAME "libSDL3.so.0")
    endif()
    target_include_directories(SDL3::SDL3 SYSTEM INTERFACE "${EFFINDOM_NATIVE_DEPS_ROOT}/include")
endfunction()
