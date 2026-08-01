function(effindom_add_native_common_contract_tests native_app_target)
    add_test(
        NAME effindom_v2_native_fui_capability_contract
        COMMAND ${CMAKE_COMMAND}
            -DCONTRACT_FILE=${CMAKE_SOURCE_DIR}/v2/native/common/NativeFuiCapabilityContract.tsv
            -DSTUB_FILE=${CMAKE_SOURCE_DIR}/v2/native/common/src/NativeFuiHostStubs.cpp
            -DREAL_FILE=${CMAKE_SOURCE_DIR}/v2/native/common/src/NativeFuiRuntimeBridge.cpp
            -DFFI_FILE=${CMAKE_SOURCE_DIR}/v2/fui-rs/src/generated/ffi.rs
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/v2/native/common/cmake/VerifyNativeFuiCapabilityContract.cmake
    )
    add_executable(effindom_v2_native_common_contract_tests
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_accessibility.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_custom_drawing.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_platform_factory.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_timer_coordinator.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_worker_coordinator.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_fui_rs_worker_adapter.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_worker_host.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_unsupported_capabilities.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_demo_drawing_showcase.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_window_icon.cpp"
    )
    target_include_directories(effindom_v2_native_common_contract_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/v2/native/common/include"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests"
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/core/tests"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
    )
    target_compile_definitions(effindom_v2_native_common_contract_tests PRIVATE
        EFFINDOM_TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(effindom_v2_native_common_contract_tests PRIVATE
            "$<LINK_GROUP:RESCAN,effindom_v2_native_common,${native_app_target}>"
            Catch2::Catch2WithMain effindom_compile_flags)
    else()
        target_link_libraries(effindom_v2_native_common_contract_tests PRIVATE
            effindom_v2_native_common ${native_app_target}
            Catch2::Catch2WithMain effindom_compile_flags)
    endif()
    if(MSVC)
        target_compile_options(effindom_v2_native_common_contract_tests PRIVATE /wd4251 /wd4275)
    endif()
endfunction()
