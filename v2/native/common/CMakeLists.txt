include("${CMAKE_SOURCE_DIR}/cmake/PinnedSdlDeps.cmake")

add_library(effindom_v2_native_common STATIC
    src/NativeAccessibility.cpp
    src/NativeContextMenuCoordinator.cpp
    src/NativeAssetService.cpp
    src/NativeGraphicsCoordinator.cpp
    src/NativeHostCore.cpp
    src/NativeHost.cpp
    src/NativeHttpClient.cpp
    src/NativeFuiRuntimeBridge.cpp
    src/NativeFuiConfig.cpp
    src/NativeFuiRsWorkerAdapter.cpp
    src/NativeFuiHostStubs.cpp
    src/NativeFuiUnsupportedCapabilities.cpp
    src/NativeInputRouter.cpp
    src/NativePageZoomController.cpp
    src/NativeWindowIcon.cpp
    src/NativeRasterSurface.cpp
    src/NativeTimerCoordinator.cpp
    src/NativeTextInputCoordinator.cpp
    src/NativeTouchGestureController.cpp
    src/NativeWorkerCoordinator.cpp
    src/NativeWorkerHost.cpp
    src/SdlDropTarget.cpp
    src/SdlEventAdapter.cpp
    src/SdlFileDialogs.cpp
    src/SdlUiDispatcher.cpp
)
target_include_directories(effindom_v2_native_common
    PUBLIC
        include
    PRIVATE
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
)
target_link_libraries(effindom_v2_native_common
    PUBLIC effindom_v2_core effindom_v2_ui SDL3::SDL3
    PRIVATE effindom_compile_flags cpr::cpr
)
find_package(Threads REQUIRED)
target_link_libraries(effindom_v2_native_common PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(effindom_v2_native_common PRIVATE /wd4251 /wd4275)
endif()

if(BUILD_TESTING)
    add_executable(effindom_v2_native_http_contract_tests
        tests/test_native_http_client.cpp
        tests/test_native_asset_service.cpp
        tests/test_native_asset_callbacks.cpp
        tests/test_native_remote_asset_lifecycle.cpp
        tests/test_sdl_drop_target.cpp)
    target_link_libraries(effindom_v2_native_http_contract_tests PRIVATE
        effindom_v2_native_common Catch2::Catch2WithMain effindom_compile_flags)
    if(MSVC)
        target_compile_options(effindom_v2_native_http_contract_tests PRIVATE /wd4251 /wd4275)
        add_custom_command(TARGET effindom_v2_native_http_contract_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:effindom_v2_core>
                $<TARGET_FILE_DIR:effindom_v2_native_http_contract_tests>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:effindom_v2_ui>
                $<TARGET_FILE_DIR:effindom_v2_native_http_contract_tests>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:effindom_v2_native_http_contract_tests>)
    endif()
    add_test(NAME effindom_v2_native_http_contract COMMAND effindom_v2_native_http_contract_tests)
endif()
