set(EFFINDOM_V2_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EngineCommand.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EngineRender.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EngineShared.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SvgIntrinsicSize.cpp"
)

add_library(effindom_v2_core STATIC ${EFFINDOM_V2_CORE_SOURCES})
target_include_directories(effindom_v2_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(effindom_v2_core
    PRIVATE effindom_compile_flags
)

if(EMSCRIPTEN)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.txt")
    if(NOT TARGET effindom_skia_graphite OR NOT TARGET effindom_skia_ganesh)
        include("${CMAKE_SOURCE_DIR}/cmake/SkiaWasm.cmake")
    endif()

    set(EFFINDOM_V2_CORE_BROWSER_OUTPUT_DIR
        "${CMAKE_SOURCE_DIR}/public/v2/core"
        CACHE PATH "Output directory for v2 core browser artifacts")
    set(_EFFINDOM_V2_CORE_HTML_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/browser/index.html")
    set(_EFFINDOM_V2_CORE_HTML_OUTPUT "${EFFINDOM_V2_CORE_BROWSER_OUTPUT_DIR}/index.html")
    add_custom_command(
        OUTPUT "${_EFFINDOM_V2_CORE_HTML_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EFFINDOM_V2_CORE_BROWSER_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_EFFINDOM_V2_CORE_HTML_SOURCE}"
            "${_EFFINDOM_V2_CORE_HTML_OUTPUT}"
        DEPENDS "${_EFFINDOM_V2_CORE_HTML_SOURCE}"
        COMMENT "[EffinDom v2 core] Staging browser harness"
    )
    add_custom_target(effindom_v2_core_browser_assets DEPENDS "${_EFFINDOM_V2_CORE_HTML_OUTPUT}")

    target_link_libraries(effindom_v2_core
        PUBLIC effindom_skia_graphite
        PRIVATE effindom_wasm_feature_flags
    )

    add_executable(effindom_v2_core_wasm
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Wasm.cpp"
    )
    add_dependencies(effindom_v2_core_wasm effindom_v2_core_browser_assets)
    target_include_directories(effindom_v2_core_wasm PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(effindom_v2_core_wasm PRIVATE
        effindom_v2_core
        effindom_compile_flags
        effindom_wasm_feature_flags
    )
    set_target_properties(effindom_v2_core_wasm PROPERTIES
        OUTPUT_NAME "effindom-core-v2"
        RUNTIME_OUTPUT_DIRECTORY "${EFFINDOM_V2_CORE_BROWSER_OUTPUT_DIR}"
        SUFFIX ".js"
    )
    target_compile_options(effindom_v2_core_wasm PRIVATE
        --use-port=emdawnwebgpu
    )
    target_link_options(effindom_v2_core_wasm PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXPORT_ALL=1
        -sOFFSCREENCANVAS_SUPPORT=1
        -sMAX_WEBGL_VERSION=2
        -sSUPPORT_LONGJMP=emscripten
        --use-port=emdawnwebgpu
        -sENVIRONMENT=web
        "-sEXPORTED_FUNCTIONS=@${CMAKE_CURRENT_SOURCE_DIR}/exports.txt"
        --emit-symbol-map
    )
    set_property(TARGET effindom_v2_core_wasm APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.txt")
else()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SkiaNative.cmake")
    target_link_libraries(effindom_v2_core
        PUBLIC effindom_v2_skia_native
    )

    add_executable(effindom_v2_core_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_engine.cpp"
    )
    target_include_directories(effindom_v2_core_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(effindom_v2_core_tests PRIVATE
        effindom_v2_core
        Catch2::Catch2WithMain
        effindom_compile_flags
    )
    target_compile_definitions(effindom_v2_core_tests PRIVATE EFFINDOM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    add_executable(effindom_v2_core_generate_goldens
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_goldens.cpp"
    )
    target_include_directories(effindom_v2_core_generate_goldens PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(effindom_v2_core_generate_goldens PRIVATE
        effindom_v2_core
        effindom_compile_flags
    )
    target_compile_definitions(effindom_v2_core_generate_goldens PRIVATE EFFINDOM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    include(Catch)
    catch_discover_tests(effindom_v2_core_tests)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_executable(effindom_v2_core_fuzz
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz_command_buffer.cpp"
        )
        target_include_directories(effindom_v2_core_fuzz PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
        target_link_libraries(effindom_v2_core_fuzz PRIVATE
            effindom_v2_core
            effindom_compile_flags
        )
        target_compile_options(effindom_v2_core_fuzz PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(effindom_v2_core_fuzz PRIVATE -fsanitize=address,undefined)

        include(CTest)
        add_test(
            NAME effindom_v2_core_fuzz_smoke
            COMMAND effindom_v2_core_fuzz --iterations 2500 --max-words 256
        )
    endif()
endif()
