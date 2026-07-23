find_package(PkgConfig REQUIRED)
pkg_check_modules(EFFINDOM_LINUX_FONTCONFIG IMPORTED_TARGET fontconfig)
pkg_check_modules(EFFINDOM_LINUX_DBUS IMPORTED_TARGET dbus-1)
pkg_check_modules(EFFINDOM_LINUX_X11 IMPORTED_TARGET x11 xext)
if(NOT EFFINDOM_LINUX_FONTCONFIG_FOUND OR NOT EFFINDOM_LINUX_DBUS_FOUND)
    message(FATAL_ERROR
        "The Linux native host requires Fontconfig and D-Bus development files. "
        "On Ubuntu/Debian install them with: sudo apt install libfontconfig1-dev libdbus-1-dev")
endif()

set(EFFINDOM_NATIVE_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/native-rust")
set(EFFINDOM_NATIVE_RUST_LIBRARY
    "${EFFINDOM_NATIVE_RUST_TARGET_DIR}/${EFFINDOM_RUST_TARGET}/release/libeffindom_native_fui_rs_app.a")
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
            cargo build --release --target ${EFFINDOM_RUST_TARGET}
            --manifest-path "${EFFINDOM_NATIVE_RUST_MANIFEST}"
        DEPENDS ${EFFINDOM_NATIVE_RUST_SOURCES} "${EFFINDOM_NATIVE_RUST_MANIFEST}"
            "${CMAKE_SOURCE_DIR}/v2/fui-rs/Cargo.toml"
        WORKING_DIRECTORY "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}"
        COMMENT "[EffinDOM Linux native] Building native FUI-RS application"
        VERBATIM
    )
    add_custom_target(effindom_native_fui_rs_app_build DEPENDS "${EFFINDOM_NATIVE_RUST_LIBRARY}")
    add_library(effindom_native_fui_rs_app STATIC IMPORTED GLOBAL)
    set_target_properties(effindom_native_fui_rs_app PROPERTIES
        IMPORTED_LOCATION "${EFFINDOM_NATIVE_RUST_LIBRARY}")
    add_dependencies(effindom_native_fui_rs_app effindom_native_fui_rs_app_build)
endif()

add_library(effindom_v2_linux_native_host STATIC
    src/graphics/LinuxVulkanSurface.cpp
    src/platform/LinuxNativePlatform.cpp
    src/platform/LinuxResizeSyncBridge.cpp
    src/platform/LinuxSystemThemeBridge.cpp
    src/platform/LinuxPlatformFactory.cpp
    src/platform/LinuxPlatformServices.cpp
    src/platform/LinuxAssetEnvironment.cpp
)
target_include_directories(effindom_v2_linux_native_host
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
        "${CMAKE_SOURCE_DIR}/v2/native/common/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
)
target_link_libraries(effindom_v2_linux_native_host
    PUBLIC effindom_v2_native_common
    PRIVATE effindom_compile_flags PkgConfig::EFFINDOM_LINUX_FONTCONFIG
        PkgConfig::EFFINDOM_LINUX_DBUS
)
if(EFFINDOM_LINUX_X11_FOUND)
    target_compile_definitions(effindom_v2_linux_native_host
        PRIVATE EFFINDOM_LINUX_X11_RESIZE_SYNC)
    target_link_libraries(effindom_v2_linux_native_host
        PRIVATE PkgConfig::EFFINDOM_LINUX_X11)
endif()
if(EFFINDOM_NATIVE_GRAPHICS_BACKEND STREQUAL "vulkan")
    include("${CMAKE_SOURCE_DIR}/cmake/PinnedVulkanHeaders.cmake")
    find_package(Vulkan REQUIRED)
    target_compile_definitions(effindom_v2_linux_native_host PRIVATE EFFINDOM_SKIA_VULKAN)
    target_link_libraries(effindom_v2_linux_native_host PRIVATE
        effindom_pinned_vulkan_headers Vulkan::Vulkan)
elseif(NOT EFFINDOM_NATIVE_GRAPHICS_BACKEND STREQUAL "raster")
    message(FATAL_ERROR
        "The Linux native host supports EFFINDOM_NATIVE_GRAPHICS_BACKEND=vulkan or raster")
endif()

set(EFFINDOM_NATIVE_OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/output")
set(EFFINDOM_NATIVE_OUTPUT_BIN "${EFFINDOM_NATIVE_OUTPUT_ROOT}/bin")
set(EFFINDOM_NATIVE_OUTPUT_LIB "${EFFINDOM_NATIVE_OUTPUT_ROOT}/lib")
set(EFFINDOM_NATIVE_OUTPUT_RESOURCES "${EFFINDOM_NATIVE_OUTPUT_ROOT}/resources")
option(EFFINDOM_LINUX_STAGE_RUNTIME
    "Require and stage Linux runtime-only assets such as the Cairo libdecor plugin" ON)
if(EFFINDOM_LINUX_STAGE_RUNTIME)
    pkg_get_variable(EFFINDOM_LIBDECOR_LIBDIR libdecor-0 libdir)
    find_file(EFFINDOM_LIBDECOR_CAIRO_PLUGIN
        NAMES libdecor-cairo.so
        HINTS "${EFFINDOM_LIBDECOR_LIBDIR}/libdecor/plugins-1"
        PATH_SUFFIXES libdecor/plugins-1)
    if(NOT EFFINDOM_LIBDECOR_CAIRO_PLUGIN)
        message(FATAL_ERROR
            "The Linux native host requires the Cairo libdecor plugin for decorated Wayland windows. "
            "On Ubuntu/Debian install it with: sudo apt install libdecor-0-plugin-1-cairo")
    endif()
endif()
function(effindom_stage_linux_native_target target_name)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${EFFINDOM_NATIVE_OUTPUT_BIN}"
        BUILD_RPATH "$ORIGIN/../lib"
    )
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${EFFINDOM_NATIVE_OUTPUT_LIB}"
            "${EFFINDOM_NATIVE_OUTPUT_LIB}/libdecor/plugins-1"
            "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}/effindom/fonts"
            "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}/app"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EFFINDOM_LIBDECOR_CAIRO_PLUGIN}"
            "${EFFINDOM_NATIVE_OUTPUT_LIB}/libdecor/plugins-1/libdecor-cairo.so"
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
            "${EFFINDOM_NATIVE_FUI_RS_DEMO_DIR}/resources/app/demo-texture.png"
            "${EFFINDOM_NATIVE_OUTPUT_RESOURCES}/app/demo-texture.png"
        COMMENT "[EffinDOM Linux native] Staging runtime and application assets"
        VERBATIM
    )
endfunction()

if(EFFINDOM_NATIVE_FUI_RS_DEMO_AVAILABLE)
    add_executable(effindom_v2_linux_native
        "${CMAKE_SOURCE_DIR}/v2/native/common/src/NativeApplicationMain.cpp")
    target_link_libraries(effindom_v2_linux_native PRIVATE
        effindom_v2_native_common effindom_v2_linux_native_host
        effindom_native_fui_rs_app effindom_compile_flags)
    effindom_stage_linux_native_target(effindom_v2_linux_native)

    add_executable(effindom_v2_linux_native_tests
        tests/test_linux_native_host.cpp
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_context_menu_coordinator.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_asset_service.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_sdl_event_adapter.cpp"
    )
    target_include_directories(effindom_v2_linux_native_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/v2/native/common/include"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests"
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
    )
    target_link_libraries(effindom_v2_linux_native_tests PRIVATE
        effindom_v2_native_common effindom_v2_linux_native_host effindom_native_fui_rs_app
        Catch2::Catch2WithMain effindom_compile_flags)
    effindom_stage_linux_native_target(effindom_v2_linux_native_tests)

    include("${CMAKE_SOURCE_DIR}/v2/native/common/NativeCommonContractTests.cmake")
    effindom_add_native_common_contract_tests(effindom_native_fui_rs_app)

    include(Catch)
    catch_discover_tests(effindom_v2_linux_native_tests)
    catch_discover_tests(effindom_v2_native_common_contract_tests)
    add_test(NAME effindom_v2_linux_native_cli
        COMMAND effindom_v2_linux_native --hidden --screenshot
            "${CMAKE_CURRENT_BINARY_DIR}/native-fui-rs.png")
endif()
