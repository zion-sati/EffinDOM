function(effindom_add_native_common_contract_tests native_app_target)
    add_executable(effindom_v2_native_common_contract_tests
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_accessibility.cpp"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests/test_native_platform_factory.cpp"
    )
    target_include_directories(effindom_v2_native_common_contract_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/v2/native/common/include"
        "${CMAKE_SOURCE_DIR}/v2/native/common/tests"
        "${CMAKE_SOURCE_DIR}/v2/abi/generated"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
        "${CMAKE_SOURCE_DIR}/v2/core/src"
        "${CMAKE_SOURCE_DIR}/v2/ui/include"
        "${CMAKE_SOURCE_DIR}/v2/ui/src"
    )
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
