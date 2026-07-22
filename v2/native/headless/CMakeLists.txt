set(EFFINDOM_V2_HEADLESS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/HeadlessConformanceHost.cpp"
)

add_library(effindom_v2_headless_host STATIC ${EFFINDOM_V2_HEADLESS_SOURCES})
target_include_directories(effindom_v2_headless_host
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
)
target_link_libraries(effindom_v2_headless_host
    PUBLIC effindom_v2_core
    PUBLIC effindom_v2_ui
    PRIVATE effindom_compile_flags
)
target_compile_definitions(effindom_v2_headless_host PRIVATE EFFINDOM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(effindom_v2_headless_host PRIVATE /wd4251 /wd4275)
endif()

function(effindom_configure_headless_target target_name)
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
    )
    target_link_libraries(${target_name} PRIVATE
        effindom_v2_headless_host
        effindom_compile_flags
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
        target_link_libraries(${target_name} PRIVATE
            "-Wl,--start-group"
            effindom_icu_i18n
            effindom_icu_common
            effindom_icu_stubdata
            "-Wl,--end-group"
        )
    endif()
    target_compile_definitions(${target_name} PRIVATE EFFINDOM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /wd4251 /wd4275)
    endif()
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:effindom_v2_core>
                $<TARGET_FILE_DIR:${target_name}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:effindom_v2_ui>
                $<TARGET_FILE_DIR:${target_name}>
            VERBATIM
        )
    endif()
endfunction()

add_executable(effindom_v2_headless_conformance
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)
effindom_configure_headless_target(effindom_v2_headless_conformance)

add_executable(effindom_v2_headless_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_headless_conformance.cpp"
)
target_link_libraries(effindom_v2_headless_tests PRIVATE Catch2::Catch2WithMain)
effindom_configure_headless_target(effindom_v2_headless_tests)

include(CTest)
if(EFFINDOM_TARGET_RUNNABLE)
    include(Catch)
    catch_discover_tests(effindom_v2_headless_tests)

    set(EFFINDOM_V2_HEADLESS_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    add_test(
        NAME effindom_v2_headless_conformance_cli
        COMMAND effindom_v2_headless_conformance
            --output "${EFFINDOM_V2_HEADLESS_ARTIFACT_DIR}"
            --verify "${CMAKE_CURRENT_SOURCE_DIR}/goldens"
    )
endif()
