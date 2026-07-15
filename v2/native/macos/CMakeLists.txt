include(FetchContent)
enable_language(OBJCXX)

if(NOT TARGET SDL3::SDL3)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

set(EFFINDOM_NATIVE_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/native-rust")
set(EFFINDOM_NATIVE_RUST_LIBRARY "${EFFINDOM_NATIVE_RUST_TARGET_DIR}/release/libeffindom_native_fui_rs_app.a")
set(EFFINDOM_NATIVE_RUST_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/fui-rs-app/Cargo.toml")
file(GLOB_RECURSE EFFINDOM_NATIVE_RUST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/fui-rs-app/src/*.rs"
    "${CMAKE_SOURCE_DIR}/v2/fui-rs/src/*.rs"
)

add_custom_command(
    OUTPUT "${EFFINDOM_NATIVE_RUST_LIBRARY}"
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${EFFINDOM_NATIVE_RUST_TARGET_DIR}
        cargo build --release --manifest-path "${EFFINDOM_NATIVE_RUST_MANIFEST}"
    DEPENDS ${EFFINDOM_NATIVE_RUST_SOURCES} "${EFFINDOM_NATIVE_RUST_MANIFEST}" "${CMAKE_SOURCE_DIR}/v2/fui-rs/Cargo.toml"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fui-rs-app"
    COMMENT "[EffinDOM native] Building native FUI-RS application"
    VERBATIM
)
add_custom_target(effindom_native_fui_rs_app_build DEPENDS "${EFFINDOM_NATIVE_RUST_LIBRARY}")
add_library(effindom_native_fui_rs_app STATIC IMPORTED GLOBAL)
set_target_properties(effindom_native_fui_rs_app PROPERTIES IMPORTED_LOCATION "${EFFINDOM_NATIVE_RUST_LIBRARY}")
add_dependencies(effindom_native_fui_rs_app effindom_native_fui_rs_app_build)

add_library(effindom_v2_macos_native_host STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MacosNativeHost.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/MacosMetalSurface.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/input/MacosInputRouter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/input/MacosScrollWheelBridge.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosUiDispatcher.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosPlatformServices.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosFileDialogs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/MacosDropTarget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/NativeAssetLoader.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NativeFuiHostServices.cpp"
)
target_include_directories(effindom_v2_macos_native_host
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
)
target_link_libraries(effindom_v2_macos_native_host
    PUBLIC effindom_v2_core effindom_v2_ui SDL3::SDL3
    PRIVATE effindom_native_fui_rs_app effindom_compile_flags
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

add_executable(effindom_v2_macos_native "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(effindom_v2_macos_native PRIVATE effindom_v2_macos_native_host effindom_native_fui_rs_app effindom_compile_flags)

set(EFFINDOM_NATIVE_OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/output")
set(EFFINDOM_NATIVE_OUTPUT_BIN "${EFFINDOM_NATIVE_OUTPUT_ROOT}/bin")
set(EFFINDOM_NATIVE_OUTPUT_LIB "${EFFINDOM_NATIVE_OUTPUT_ROOT}/lib")
set(EFFINDOM_NATIVE_OUTPUT_RESOURCES "${EFFINDOM_NATIVE_OUTPUT_ROOT}/resources")
function(effindom_stage_macos_native_target target_name)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${EFFINDOM_NATIVE_OUTPUT_BIN}"
        BUILD_RPATH "@executable_path/../lib"
    )
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${EFFINDOM_NATIVE_OUTPUT_LIB}"
            "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}/effindom/fonts"
            "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}/app"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "${EFFINDOM_NATIVE_OUTPUT_LIB}/$<TARGET_FILE_NAME:SDL3::SDL3>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:effindom_v2_core>"
            "${EFFINDOM_NATIVE_OUTPUT_LIB}/$<TARGET_FILE_NAME:effindom_v2_core>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:effindom_v2_ui>"
            "${EFFINDOM_NATIVE_OUTPUT_LIB}/$<TARGET_FILE_NAME:effindom_v2_ui>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/v2/fonts"
            "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}/effindom/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/fui-rs-app/resources/app/demo-texture.png"
            "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}/app/demo-texture.png"
        COMMENT "[EffinDOM native] Staging stable, unhashed runtime and application assets"
        VERBATIM
    )
endfunction()

effindom_stage_macos_native_target(effindom_v2_macos_native)

add_executable(effindom_v2_macos_native_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_macos_native_host.cpp")
target_include_directories(effindom_v2_macos_native_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(effindom_v2_macos_native_tests PRIVATE effindom_v2_macos_native_host effindom_native_fui_rs_app Catch2::Catch2WithMain effindom_compile_flags)
effindom_stage_macos_native_target(effindom_v2_macos_native_tests)

include(CTest)
include(Catch)
catch_discover_tests(effindom_v2_macos_native_tests)

add_test(NAME effindom_v2_macos_native_cli COMMAND effindom_v2_macos_native --hidden --screenshot "${CMAKE_CURRENT_BINARY_DIR}/native-fui-rs.png")
add_test(
    NAME effindom_v2_macos_native_output
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_ROOT=${EFFINDOM_NATIVE_OUTPUT_ROOT}
        -DEXECUTABLE_NAME=$<TARGET_FILE_NAME:effindom_v2_macos_native>
        -DSDL_LIBRARY_NAME=$<TARGET_FILE_NAME:SDL3::SDL3>
        -DCORE_LIBRARY_NAME=$<TARGET_FILE_NAME:effindom_v2_core>
        -DUI_LIBRARY_NAME=$<TARGET_FILE_NAME:effindom_v2_ui>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNativeOutput.cmake
)
