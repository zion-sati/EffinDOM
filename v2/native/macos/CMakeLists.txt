set(EFFINDOM_NATIVE_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/native-rust")
set(EFFINDOM_NATIVE_RUST_LIBRARY "${EFFINDOM_NATIVE_RUST_TARGET_DIR}/${EFFINDOM_RUST_TARGET}/release/libeffindom_native_fui_rs_app.a")
set(EFFINDOM_NATIVE_FUI_RS_DEMO_DIR "${CMAKE_SOURCE_DIR}/v2/fui-rs/native-demo")
set(EFFINDOM_NATIVE_RUST_MANIFEST "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/Cargo.toml")
set(EFFINDOM_NATIVE_FUI_RS_DEMO_AVAILABLE OFF)
if(EFFINDOM_BUILD_NATIVE_FUI_RS_DEMO AND EXISTS "${EFFINDOM_NATIVE_RUST_MANIFEST}")
set(EFFINDOM_NATIVE_FUI_RS_DEMO_AVAILABLE ON)
file(GLOB_RECURSE EFFINDOM_NATIVE_RUST_SOURCES CONFIGURE_DEPENDS
    "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/src/*.rs"
    "${CMAKE_SOURCE_DIR}/v2/fui-rs/src/*.rs"
)

add_custom_command(
    OUTPUT "${EFFINDOM_NATIVE_RUST_LIBRARY}"
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${EFFINDOM_NATIVE_RUST_TARGET_DIR}
        cargo build --release --target ${EFFINDOM_RUST_TARGET} --manifest-path "${EFFINDOM_NATIVE_RUST_MANIFEST}"
    DEPENDS ${EFFINDOM_NATIVE_RUST_SOURCES} "${EFFINDOM_NATIVE_RUST_MANIFEST}" "${CMAKE_SOURCE_DIR}/v2/fui-rs/Cargo.toml"
    WORKING_DIRECTORY "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}"
    COMMENT "[EffinDOM native] Building native FUI-RS application"
    VERBATIM
)
add_custom_target(effindom_native_fui_rs_app_build DEPENDS "${EFFINDOM_NATIVE_RUST_LIBRARY}")
add_library(effindom_native_fui_rs_app STATIC IMPORTED GLOBAL)
set_target_properties(effindom_native_fui_rs_app PROPERTIES IMPORTED_LOCATION "${EFFINDOM_NATIVE_RUST_LIBRARY}")
add_dependencies(effindom_native_fui_rs_app effindom_native_fui_rs_app_build)
endif()

add_library(effindom_v2_macos_native_host STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosNativePlatform.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosAccessibilityAdapter.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/MacosMetalSurface.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/input/MacosScrollWheelBridge.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosInputSettings.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosPlatformServices.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosSystemThemeBridge.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosPlatformFactory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosAssetEnvironment.mm"
)
target_include_directories(effindom_v2_macos_native_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/v2/native/common/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
)
target_link_libraries(effindom_v2_macos_native_host
    PUBLIC effindom_v2_native_common
    PRIVATE effindom_compile_flags
)
find_library(EFFINDOM_NATIVE_METAL_FRAMEWORK Metal REQUIRED)
find_library(EFFINDOM_NATIVE_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(EFFINDOM_NATIVE_APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(EFFINDOM_NATIVE_CORETEXT_FRAMEWORK CoreText REQUIRED)
target_link_libraries(effindom_v2_macos_native_host
    PRIVATE
        "${EFFINDOM_NATIVE_METAL_FRAMEWORK}"
        "${EFFINDOM_NATIVE_QUARTZCORE_FRAMEWORK}"
        "${EFFINDOM_NATIVE_APPKIT_FRAMEWORK}"
        "${EFFINDOM_NATIVE_CORETEXT_FRAMEWORK}"
)

set(EFFINDOM_NATIVE_OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/output")
set(EFFINDOM_NATIVE_OUTPUT_BIN "${EFFINDOM_NATIVE_OUTPUT_ROOT}/bin")
set(EFFINDOM_NATIVE_OUTPUT_LIB "${EFFINDOM_NATIVE_OUTPUT_ROOT}/lib")
set(EFFINDOM_NATIVE_OUTPUT_RESOURCES "${EFFINDOM_NATIVE_OUTPUT_ROOT}/resources")
set(EFFINDOM_NATIVE_APP_ROOT "${EFFINDOM_NATIVE_OUTPUT_ROOT}/effindom_v2_macos_native.app")
set(EFFINDOM_NATIVE_APP_BIN "${EFFINDOM_NATIVE_APP_ROOT}/Contents/MacOS")
set(EFFINDOM_NATIVE_APP_FRAMEWORKS "${EFFINDOM_NATIVE_APP_ROOT}/Contents/Frameworks")
set(EFFINDOM_NATIVE_APP_RESOURCES "${EFFINDOM_NATIVE_APP_ROOT}/Contents/Resources")

function(effindom_stage_macos_native_target target_name output_bin output_lib output_resources loader_search_path)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${output_bin}"
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${loader_search_path}"
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${output_lib}"
            "${output_resources}/effindom/fonts"
            "${output_resources}/app"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "${output_lib}/$<TARGET_FILE_NAME:SDL3::SDL3>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:effindom_v2_core>"
            "${output_lib}/$<TARGET_FILE_NAME:effindom_v2_core>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:effindom_v2_ui>"
            "${output_lib}/$<TARGET_FILE_NAME:effindom_v2_ui>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/v2/fonts"
            "${output_resources}/effindom/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/resources/app/demo-texture.png"
            "${output_resources}/app/demo-texture.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/resources/app/application-icon.png"
            "${output_resources}/app/application-icon.png"
        COMMENT "[EffinDOM native] Staging stable, unhashed runtime and application assets"
        VERBATIM
    )
endfunction()

if(EFFINDOM_NATIVE_FUI_RS_DEMO_AVAILABLE)
add_executable(effindom_v2_macos_native "${CMAKE_SOURCE_DIR}/v2/native/common/src/NativeApplicationMain.cpp")
target_link_libraries(effindom_v2_macos_native PRIVATE effindom_v2_macos_native_host effindom_native_fui_rs_app effindom_compile_flags)

effindom_stage_macos_native_target(effindom_v2_macos_native
    "${EFFINDOM_NATIVE_APP_BIN}" "${EFFINDOM_NATIVE_APP_FRAMEWORKS}"
    "${EFFINDOM_NATIVE_APP_RESOURCES}" "@loader_path/../Frameworks")

add_executable(effindom_v2_macos_native_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_macos_native_host.cpp"
    "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_context_menu_coordinator.cpp"
    "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_asset_service.cpp"
    "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_sdl_event_adapter.cpp"
)
target_include_directories(effindom_v2_macos_native_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/v2/native/common/include"
    "${CMAKE_SOURCE_DIR}/v2/native/common/tests"
    "${CMAKE_SOURCE_DIR}/v2/abi/generated"
)
target_link_libraries(effindom_v2_macos_native_tests PRIVATE effindom_v2_macos_native_host effindom_native_fui_rs_app Catch2::Catch2WithMain effindom_compile_flags)
effindom_stage_macos_native_target(effindom_v2_macos_native_tests
    "${EFFINDOM_NATIVE_OUTPUT_BIN}" "${EFFINDOM_NATIVE_OUTPUT_LIB}"
    "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}" "@loader_path/../lib")

include("${CMAKE_SOURCE_DIR}/v2/native/common/NativeCommonContractTests.cmake")
effindom_add_native_common_contract_tests(effindom_native_fui_rs_app)

include(CTest)
include(Catch)
catch_discover_tests(effindom_v2_macos_native_tests)
catch_discover_tests(effindom_v2_native_common_contract_tests)

add_test(NAME effindom_v2_macos_native_cli COMMAND effindom_v2_macos_native --hidden --screenshot "${CMAKE_CURRENT_BINARY_DIR}/native-fui-rs.png")
add_test(
    NAME effindom_v2_macos_native_output
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_ROOT=${EFFINDOM_NATIVE_OUTPUT_ROOT}
        -DAPP_ROOT=${EFFINDOM_NATIVE_APP_ROOT}
        -DEXECUTABLE_NAME=$<TARGET_FILE_NAME:effindom_v2_macos_native>
        -DSDL_LIBRARY_NAME=$<TARGET_FILE_NAME:SDL3::SDL3>
        -DCORE_LIBRARY_NAME=$<TARGET_FILE_NAME:effindom_v2_core>
        -DUI_LIBRARY_NAME=$<TARGET_FILE_NAME:effindom_v2_ui>
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DBUILD_ROOT=${CMAKE_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNativeOutput.cmake
)
add_test(
    NAME effindom_v2_macos_native_relocated_launch
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${EFFINDOM_NATIVE_APP_ROOT}
        -DRELOCATED_ROOT=${CMAKE_CURRENT_BINARY_DIR}/relocated/effindom_v2_macos_native.app
        -DEXECUTABLE_RELATIVE=Contents/MacOS/$<TARGET_FILE_NAME:effindom_v2_macos_native>
        -P ${CMAKE_SOURCE_DIR}/v2/native/common/cmake/VerifyRelocatedNativeLaunch.cmake)
endif()
