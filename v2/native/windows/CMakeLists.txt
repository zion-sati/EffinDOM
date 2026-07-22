set(EFFINDOM_NATIVE_FUI_RS_DEMO_DIR "${CMAKE_SOURCE_DIR}/v2/fui-rs/native-demo")
set(EFFINDOM_NATIVE_RUST_MANIFEST "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/Cargo.toml")
set(EFFINDOM_WINDOWS_DEMO_AVAILABLE OFF)
if(EFFINDOM_BUILD_NATIVE_FUI_RS_DEMO AND EXISTS "${EFFINDOM_NATIVE_RUST_MANIFEST}")
    set(EFFINDOM_WINDOWS_DEMO_AVAILABLE ON)
    set(EFFINDOM_NATIVE_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/native-rust")
    set(EFFINDOM_NATIVE_RUST_PROFILE release)
    set(EFFINDOM_NATIVE_RUST_BUILD_ARGS --release)
    set(EFFINDOM_NATIVE_RUST_ENV CARGO_TARGET_DIR=${EFFINDOM_NATIVE_RUST_TARGET_DIR})
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(EFFINDOM_NATIVE_RUST_PROFILE debug)
        set(EFFINDOM_NATIVE_RUST_BUILD_ARGS)
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        list(APPEND EFFINDOM_NATIVE_RUST_ENV CARGO_PROFILE_RELEASE_DEBUG=1)
    endif()
    set(EFFINDOM_NATIVE_RUST_LIBRARY
        "${EFFINDOM_NATIVE_RUST_TARGET_DIR}/${EFFINDOM_RUST_TARGET}/${EFFINDOM_NATIVE_RUST_PROFILE}/effindom_native_fui_rs_app.lib")
    file(GLOB_RECURSE EFFINDOM_NATIVE_RUST_SOURCES CONFIGURE_DEPENDS
        "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/src/*.rs"
        "${CMAKE_SOURCE_DIR}/v2/fui-rs/src/*.rs"
    )
    add_custom_command(
        OUTPUT "${EFFINDOM_NATIVE_RUST_LIBRARY}"
        COMMAND ${CMAKE_COMMAND} -E env ${EFFINDOM_NATIVE_RUST_ENV}
            cargo build ${EFFINDOM_NATIVE_RUST_BUILD_ARGS} --target ${EFFINDOM_RUST_TARGET} --manifest-path "${EFFINDOM_NATIVE_RUST_MANIFEST}"
        DEPENDS ${EFFINDOM_NATIVE_RUST_SOURCES} "${EFFINDOM_NATIVE_RUST_MANIFEST}" "${CMAKE_SOURCE_DIR}/v2/fui-rs/Cargo.toml"
        WORKING_DIRECTORY "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}"
        COMMENT "[EffinDOM native] Building Windows ${EFFINDOM_TARGET_ARCH} FUI-RS application"
        VERBATIM
    )
    add_custom_target(effindom_native_fui_rs_app_build DEPENDS "${EFFINDOM_NATIVE_RUST_LIBRARY}")
    add_library(effindom_native_fui_rs_app STATIC IMPORTED GLOBAL)
    set_target_properties(effindom_native_fui_rs_app PROPERTIES
        IMPORTED_LOCATION "${EFFINDOM_NATIVE_RUST_LIBRARY}"
        INTERFACE_LINK_LIBRARIES "ws2_32;ntdll;userenv")
    add_dependencies(effindom_native_fui_rs_app effindom_native_fui_rs_app_build)
endif()

add_library(effindom_v2_windows_native_host STATIC
    src/platform/WindowsNativePlatform.cpp
    src/platform/WindowsAccessibilityAdapter.cpp
    src/graphics/WindowsGpuSurface.cpp
    src/input/WindowsScrollWheelBridge.cpp
    src/platform/WindowsPlatformServices.cpp
    src/platform/WindowsSystemThemeBridge.cpp
    src/platform/WindowsPlatformFactory.cpp
)
target_include_directories(effindom_v2_windows_native_host
    PUBLIC
        include
        "${CMAKE_SOURCE_DIR}/v2/native/common/include"
    PRIVATE
        src
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
)
target_link_libraries(effindom_v2_windows_native_host
    PUBLIC effindom_v2_native_common
    PRIVATE effindom_compile_flags shell32 dwrite ole32 comctl32 dwmapi uiautomationcore
)
target_compile_definitions(effindom_v2_windows_native_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(effindom_v2_windows_native_host PRIVATE /wd4251 /wd4275)
if(EFFINDOM_NATIVE_GRAPHICS_BACKEND STREQUAL "d3d")
    target_compile_definitions(effindom_v2_windows_native_host PRIVATE EFFINDOM_SKIA_DIRECT3D SK_GANESH SK_DIRECT3D)
    target_link_libraries(effindom_v2_windows_native_host PRIVATE d3d12 dxgi d3dcompiler)
endif()

if(EFFINDOM_WINDOWS_DEMO_AVAILABLE)
    set(EFFINDOM_WINDOWS_OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/output")
    set(EFFINDOM_WINDOWS_OUTPUT_BIN "${EFFINDOM_WINDOWS_OUTPUT_ROOT}/bin")
    set(EFFINDOM_WINDOWS_OUTPUT_LIB "${EFFINDOM_WINDOWS_OUTPUT_ROOT}/lib")
    set(EFFINDOM_WINDOWS_OUTPUT_RESOURCES "${EFFINDOM_WINDOWS_OUTPUT_ROOT}/resources")
    set(EFFINDOM_WINDOWS_OUTPUT_SYMBOLS "${EFFINDOM_WINDOWS_OUTPUT_ROOT}/symbols")

    function(effindom_stage_windows_native_target target_name)
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${EFFINDOM_WINDOWS_OUTPUT_BIN}")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${EFFINDOM_WINDOWS_OUTPUT_BIN}"
                "${EFFINDOM_WINDOWS_OUTPUT_LIB}"
                "${EFFINDOM_WINDOWS_OUTPUT_RESOURCES}/effindom/fonts"
                "${EFFINDOM_WINDOWS_OUTPUT_RESOURCES}/app"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:effindom_v2_core>"
                "${EFFINDOM_WINDOWS_OUTPUT_BIN}/$<TARGET_FILE_NAME:effindom_v2_core>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:effindom_v2_ui>"
                "${EFFINDOM_WINDOWS_OUTPUT_BIN}/$<TARGET_FILE_NAME:effindom_v2_ui>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "${EFFINDOM_WINDOWS_OUTPUT_BIN}/$<TARGET_FILE_NAME:SDL3::SDL3>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:effindom_v2_core>"
                "${EFFINDOM_WINDOWS_OUTPUT_LIB}/$<TARGET_FILE_NAME:effindom_v2_core>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:effindom_v2_ui>"
                "${EFFINDOM_WINDOWS_OUTPUT_LIB}/$<TARGET_FILE_NAME:effindom_v2_ui>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "${EFFINDOM_WINDOWS_OUTPUT_LIB}/$<TARGET_FILE_NAME:SDL3::SDL3>"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/v2/fonts"
                "${EFFINDOM_WINDOWS_OUTPUT_RESOURCES}/effindom/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/resources/app/demo-texture.png"
                "${EFFINDOM_WINDOWS_OUTPUT_RESOURCES}/app/demo-texture.png"
            COMMENT "[EffinDOM native] Staging Windows runtime and application assets"
            VERBATIM)

        if(CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${EFFINDOM_WINDOWS_OUTPUT_SYMBOLS}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_PDB_FILE:${target_name}>"
                    "${EFFINDOM_WINDOWS_OUTPUT_SYMBOLS}/$<TARGET_PDB_FILE_NAME:${target_name}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_PDB_FILE:effindom_v2_core>"
                    "${EFFINDOM_WINDOWS_OUTPUT_SYMBOLS}/$<TARGET_PDB_FILE_NAME:effindom_v2_core>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_PDB_FILE:effindom_v2_ui>"
                    "${EFFINDOM_WINDOWS_OUTPUT_SYMBOLS}/$<TARGET_PDB_FILE_NAME:effindom_v2_ui>"
                COMMENT "[EffinDOM native] Staging Windows program databases"
                VERBATIM)
        endif()
    endfunction()

    add_executable(effindom_v2_windows_native "${CMAKE_SOURCE_DIR}/v2/native/common/src/NativeApplicationMain.cpp")
    target_link_libraries(effindom_v2_windows_native PRIVATE
        effindom_v2_windows_native_host effindom_native_fui_rs_app effindom_compile_flags)

    add_executable(effindom_v2_windows_native_tests
        tests/test_windows_native_host.cpp
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_asset_service.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_context_menu_coordinator.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_sdl_event_adapter.cpp")
    target_include_directories(effindom_v2_windows_native_tests PRIVATE
        src
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests")
    target_link_libraries(effindom_v2_windows_native_tests PRIVATE
        effindom_v2_windows_native_host effindom_native_fui_rs_app Catch2::Catch2WithMain effindom_compile_flags)
    target_compile_options(effindom_v2_windows_native_tests PRIVATE /wd4251 /wd4275)
    if(EFFINDOM_NATIVE_GRAPHICS_BACKEND STREQUAL "d3d")
        target_compile_definitions(effindom_v2_windows_native_tests PRIVATE EFFINDOM_TEST_DIRECT3D)
    endif()

    include("${CMAKE_SOURCE_DIR}/v2/native/common/NativeCommonContractTests.cmake")
    effindom_add_native_common_contract_tests(effindom_native_fui_rs_app)

    add_custom_command(TARGET effindom_v2_native_common_contract_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:effindom_v2_core>"
            "$<TARGET_FILE_DIR:effindom_v2_native_common_contract_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:effindom_v2_ui>"
            "$<TARGET_FILE_DIR:effindom_v2_native_common_contract_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:effindom_v2_native_common_contract_tests>"
        COMMENT "[EffinDOM native] Staging Windows contract-test runtime libraries"
        VERBATIM)

    effindom_stage_windows_native_target(effindom_v2_windows_native)
    effindom_stage_windows_native_target(effindom_v2_windows_native_tests)

    include(Catch)
    if(EFFINDOM_TARGET_RUNNABLE)
        catch_discover_tests(effindom_v2_windows_native_tests)
        catch_discover_tests(effindom_v2_native_common_contract_tests)
    endif()

    set(EFFINDOM_WINDOWS_REQUIRE_SYMBOLS OFF)
    set(EFFINDOM_WINDOWS_EXPECT_DEBUG_CRT OFF)
    if(CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
        set(EFFINDOM_WINDOWS_REQUIRE_SYMBOLS ON)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(EFFINDOM_WINDOWS_EXPECT_DEBUG_CRT ON)
    endif()

    add_test(
        NAME effindom_v2_windows_native_output
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT_ROOT=${EFFINDOM_WINDOWS_OUTPUT_ROOT}
            -DEXECUTABLE_NAME=$<TARGET_FILE_NAME:effindom_v2_windows_native>
            -DSDL_LIBRARY_NAME=$<TARGET_FILE_NAME:SDL3::SDL3>
            -DCORE_LIBRARY_NAME=$<TARGET_FILE_NAME:effindom_v2_core>
            -DUI_LIBRARY_NAME=$<TARGET_FILE_NAME:effindom_v2_ui>
            -DREQUIRE_SYMBOLS=${EFFINDOM_WINDOWS_REQUIRE_SYMBOLS}
            -DEXECUTABLE_PDB_NAME=$<TARGET_PDB_FILE_NAME:effindom_v2_windows_native>
            -DCORE_PDB_NAME=$<TARGET_PDB_FILE_NAME:effindom_v2_core>
            -DUI_PDB_NAME=$<TARGET_PDB_FILE_NAME:effindom_v2_ui>
            -DRUN_EXECUTABLE=${EFFINDOM_TARGET_RUNNABLE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyWindowsNativeOutput.cmake)

    find_program(EFFINDOM_DUMPBIN_EXECUTABLE dumpbin REQUIRED)
    add_test(
        NAME effindom_v2_windows_native_dependencies
        COMMAND ${CMAKE_COMMAND}
            -DDUMPBIN_EXECUTABLE=${EFFINDOM_DUMPBIN_EXECUTABLE}
            -DBINARY_DIR=${EFFINDOM_WINDOWS_OUTPUT_BIN}
            -DEXPECTED_ARCH=${EFFINDOM_TARGET_ARCH}
            -DEXPECT_DEBUG_CRT=${EFFINDOM_WINDOWS_EXPECT_DEBUG_CRT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyWindowsNativeDependencies.cmake)

    if(EFFINDOM_TARGET_RUNNABLE)
        add_test(
            NAME effindom_v2_windows_native_shell_launch
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyWindowsShellLaunch.ps1
                -OutputRoot ${EFFINDOM_WINDOWS_OUTPUT_ROOT}
                -ExecutableName $<TARGET_FILE_NAME:effindom_v2_windows_native>
                -RelocatedRoot ${CMAKE_CURRENT_BINARY_DIR}/shell-relocated-windows-output)
    endif()
endif()
