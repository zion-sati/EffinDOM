# cmake/SkiaWasm.cmake
#
# Exposes staged Skia WASM support directories for the Emscripten targets.
# The expensive Skia compile happens separately in scripts/build_skia_wasm.sh;
# the browser-facing effindom.core/effindom.core64 bundles now statically link
# the staged libskia.a archive instead of dynamically loading a side module.
#
# Expected directory layout for each Skia dir:
#   include/                     Skia public C++ headers
#   modules/skcms/              Support headers referenced by SkColorSpace.h
#   libskia.a                   Staged static archive built for wasm32/wasm64
#
# Build scripts:
#   scripts/build_skia_wasm.sh           → skia/wasm/         (graphite-capable, M136)
#   scripts/build_skia_ganesh_wasm.sh    → skia/wasm-ganesh/  (ganesh-only, future M140)
#
# CMake cache variables (all optional):
#   SKIA_GRAPHITE_WASM_DIR  default: <repo>/skia/wasm
#   SKIA_GANESH_WASM_DIR    default: <repo>/skia/wasm-ganesh (falls back to skia/wasm/)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "SkiaWasm.cmake must only be included in Emscripten builds.")
endif()

find_program(EFFINDOM_WASM_EMCC_EXECUTABLE emcc REQUIRED)
find_program(EFFINDOM_WASM_EMAR_EXECUTABLE emar REQUIRED)

# ── Resolve Graphite Skia dir ─────────────────────────────────────────────────
if(NOT DEFINED SKIA_GRAPHITE_WASM_DIR OR SKIA_GRAPHITE_WASM_DIR STREQUAL "")
    set(SKIA_GRAPHITE_WASM_DIR "${CMAKE_SOURCE_DIR}/skia/wasm"
        CACHE PATH "Skia Graphite WASM dir (output of scripts/build_skia_wasm.sh)")
endif()

# ── Resolve Ganesh Skia dir ───────────────────────────────────────────────────
# Falls back to skia/wasm/ until a dedicated ganesh build is available.
if(NOT DEFINED SKIA_GANESH_WASM_DIR OR SKIA_GANESH_WASM_DIR STREQUAL "")
    if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/skia/wasm-ganesh")
        set(SKIA_GANESH_WASM_DIR "${CMAKE_SOURCE_DIR}/skia/wasm-ganesh"
            CACHE PATH "Skia Ganesh WASM dir (output of scripts/build_skia_ganesh_wasm.sh)")
    else()
        set(SKIA_GANESH_WASM_DIR "${CMAKE_SOURCE_DIR}/skia/wasm"
            CACHE PATH "Skia Ganesh WASM dir (output of scripts/build_skia_ganesh_wasm.sh)")
        message(STATUS "[SkiaWasm] skia/wasm-ganesh/ not found — ganesh bundle reuses the graphite Skia build. Run scripts/build_skia_ganesh_wasm.sh for a lean ganesh-only module.")
    endif()
endif()

# ── Validate a Skia dir and create an INTERFACE target ───────────────────────
function(effindom_add_skia_target TARGET_NAME SKIA_DIR)
    if(NOT IS_DIRECTORY "${SKIA_DIR}")
        message(FATAL_ERROR
            "[SkiaWasm] ${TARGET_NAME}: directory does not exist: ${SKIA_DIR}\n"
            "  Run the appropriate Skia build script first.")
    endif()

    set(_archive "${SKIA_DIR}/libskia.a")
    if(NOT EXISTS "${_archive}")
        message(FATAL_ERROR
            "[SkiaWasm] ${TARGET_NAME}: staged archive not found: ${_archive}")
    endif()

    set(_svg_archive "${SKIA_DIR}/libsvg.a")
    set(_skshaper_archive "${SKIA_DIR}/libskshaper.a")
    set(_archives "${_archive}")
    if(EXISTS "${_svg_archive}")
        list(APPEND _archives "${_svg_archive}")
    endif()
    if(EXISTS "${_skshaper_archive}")
        list(APPEND _archives "${_skshaper_archive}")
    endif()

    set(_expat_header "${SKIA_DIR}/third_party/externals/expat/expat/lib/expat.h")
    set(_expat_config "${SKIA_DIR}/third_party/expat/include/expat_config/expat_config.h")
    if(EXISTS "${_expat_header}" AND EXISTS "${_expat_config}")
        set(_EFFINDOM_WASM_EXPAT_CFLAGS "")
        if(CMAKE_C_FLAGS)
            separate_arguments(_EFFINDOM_WASM_EXPAT_CFLAGS NATIVE_COMMAND "${CMAKE_C_FLAGS}")
        endif()
        if(CMAKE_C_FLAGS_RELEASE)
            separate_arguments(_EFFINDOM_WASM_EXPAT_RELEASE_CFLAGS NATIVE_COMMAND "${CMAKE_C_FLAGS_RELEASE}")
            list(APPEND _EFFINDOM_WASM_EXPAT_CFLAGS ${_EFFINDOM_WASM_EXPAT_RELEASE_CFLAGS})
        endif()
        if(EFFINDOM_SIMD)
            list(APPEND _EFFINDOM_WASM_EXPAT_CFLAGS -O3 -msimd128)
        endif()
        set(_expat_build_dir "${CMAKE_BINARY_DIR}/${TARGET_NAME}_expat")
        set(_expat_archive "${_expat_build_dir}/libexpat.a")
        set(_expat_xmlparse_o "${_expat_build_dir}/xmlparse.o")
        set(_expat_xmlrole_o "${_expat_build_dir}/xmlrole.o")
        set(_expat_xmltok_o "${_expat_build_dir}/xmltok.o")
        add_custom_command(
            OUTPUT
                "${_expat_archive}"
                "${_expat_xmlparse_o}"
                "${_expat_xmlrole_o}"
                "${_expat_xmltok_o}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_expat_build_dir}"
            COMMAND "${EFFINDOM_WASM_EMCC_EXECUTABLE}"
                -c
                -DXML_STATIC
                ${_EFFINDOM_WASM_EXPAT_CFLAGS}
                -I${SKIA_DIR}/third_party/externals/expat/expat/lib
                -I${SKIA_DIR}/third_party/expat/include/expat_config
                ${SKIA_DIR}/third_party/externals/expat/expat/lib/xmlparse.c
                -o ${_expat_xmlparse_o}
            COMMAND "${EFFINDOM_WASM_EMCC_EXECUTABLE}"
                -c
                -DXML_STATIC
                ${_EFFINDOM_WASM_EXPAT_CFLAGS}
                -I${SKIA_DIR}/third_party/externals/expat/expat/lib
                -I${SKIA_DIR}/third_party/expat/include/expat_config
                ${SKIA_DIR}/third_party/externals/expat/expat/lib/xmlrole.c
                -o ${_expat_xmlrole_o}
            COMMAND "${EFFINDOM_WASM_EMCC_EXECUTABLE}"
                -c
                -DXML_STATIC
                ${_EFFINDOM_WASM_EXPAT_CFLAGS}
                -I${SKIA_DIR}/third_party/externals/expat/expat/lib
                -I${SKIA_DIR}/third_party/expat/include/expat_config
                ${SKIA_DIR}/third_party/externals/expat/expat/lib/xmltok.c
                -o ${_expat_xmltok_o}
            COMMAND "${EFFINDOM_WASM_EMAR_EXECUTABLE}" rcs
                ${_expat_archive}
                ${_expat_xmlparse_o}
                ${_expat_xmlrole_o}
                ${_expat_xmltok_o}
            DEPENDS
                "${SKIA_DIR}/third_party/externals/expat/expat/lib/xmlparse.c"
                "${SKIA_DIR}/third_party/externals/expat/expat/lib/xmlrole.c"
                "${SKIA_DIR}/third_party/externals/expat/expat/lib/xmltok.c"
                "${_expat_header}"
                "${_expat_config}"
            VERBATIM
        )
        add_custom_target(${TARGET_NAME}_expat_build
            DEPENDS
                "${_expat_archive}"
                "${_expat_xmlparse_o}"
                "${_expat_xmlrole_o}"
                "${_expat_xmltok_o}"
        )
        add_library(${TARGET_NAME}_expat STATIC IMPORTED GLOBAL)
        set_target_properties(${TARGET_NAME}_expat PROPERTIES IMPORTED_LOCATION "${_expat_archive}")
        add_dependencies(${TARGET_NAME}_expat ${TARGET_NAME}_expat_build)
        list(APPEND _archives ${TARGET_NAME}_expat)
    endif()

    message(STATUS "[SkiaWasm] ${TARGET_NAME}: ${SKIA_DIR}")

    # Use SKIA_DIR as the include root (not SKIA_DIR/include): Skia headers
    # cross-reference each other as "include/gpu/graphite/Context.h", so the
    # compiler must see the directory that *contains* include/.
    add_library(${TARGET_NAME} INTERFACE)
    target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${SKIA_DIR}")
    target_link_libraries(${TARGET_NAME} INTERFACE ${_archives})
endfunction()

effindom_add_skia_target(effindom_skia_graphite "${SKIA_GRAPHITE_WASM_DIR}")

effindom_add_skia_target(effindom_skia_ganesh "${SKIA_GANESH_WASM_DIR}")
