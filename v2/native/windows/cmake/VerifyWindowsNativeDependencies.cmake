if(NOT DEFINED DUMPBIN_EXECUTABLE OR NOT EXISTS "${DUMPBIN_EXECUTABLE}")
    message(FATAL_ERROR "dumpbin was not supplied or does not exist: ${DUMPBIN_EXECUTABLE}")
endif()
if(NOT DEFINED BINARY_DIR OR NOT IS_DIRECTORY "${BINARY_DIR}")
    message(FATAL_ERROR "Windows native binary directory is missing: ${BINARY_DIR}")
endif()

file(GLOB binaries "${BINARY_DIR}/*.exe" "${BINARY_DIR}/*.dll")
if(NOT binaries)
    message(FATAL_ERROR "No Windows PE binaries found in ${BINARY_DIR}")
endif()

set(allowed_application_dlls effindom_core.dll effindom_ui.dll sdl3.dll sdl3d.dll)
set(debug_runtime_regex "^(ucrtbased|vcruntime[0-9_]*d|msvcp[0-9_]*d|concrt[0-9_]*d)\\.dll$")
set(release_runtime_regex "^(vcruntime|msvcp|concrt)[0-9_]+\\.dll$")
set(system_dependency_regex "^(api-ms-win-|ext-ms-win-).+\\.dll$|^(advapi32|bcryptprimitives|comctl32|d3d12|d3dcompiler_47|dwmapi|dwrite|dxgi|gdi32|imm32|kernel32|ntdll|ole32|oleaut32|setupapi|shell32|uiautomationcore|user32|userenv|version|winmm|ws2_32)\\.dll$|^(msvcp|vcruntime|concrt)[0-9_]+\\.dll$")

if(EXPECTED_ARCH STREQUAL "x64")
    set(expected_machine "machine \\(x64\\)")
elseif(EXPECTED_ARCH STREQUAL "x86")
    set(expected_machine "machine \\(x86\\)")
elseif(EXPECTED_ARCH STREQUAL "arm64")
    set(expected_machine "machine \\(ARM64\\)")
else()
    message(FATAL_ERROR "Unsupported EXPECTED_ARCH: ${EXPECTED_ARCH}")
endif()

foreach(binary IN LISTS binaries)
    execute_process(
        COMMAND "${DUMPBIN_EXECUTABLE}" /dependents "${binary}"
        RESULT_VARIABLE dependents_result
        OUTPUT_VARIABLE dependents_output
        ERROR_VARIABLE dependents_error)
    if(NOT dependents_result EQUAL 0)
        message(FATAL_ERROR "dumpbin /dependents failed for ${binary}: ${dependents_error}")
    endif()

    string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.[dD][lL][lL]" dependencies "${dependents_output}")
    foreach(dependency IN LISTS dependencies)
        string(TOLOWER "${dependency}" dependency_lower)
        if(NOT EXPECT_DEBUG_CRT AND dependency_lower MATCHES "${debug_runtime_regex}")
            message(FATAL_ERROR "${binary} depends on debug CRT ${dependency}")
        elseif(EXPECT_DEBUG_CRT AND dependency_lower MATCHES "${release_runtime_regex}")
            message(FATAL_ERROR "${binary} mixes release CRT ${dependency} into a Debug package")
        endif()
        if(dependency_lower MATCHES "${debug_runtime_regex}" OR dependency_lower MATCHES "${release_runtime_regex}")
            # Accepted above according to the selected build configuration.
        elseif(dependency_lower MATCHES "^(effindom_|sdl3d?\\.dll)")
            list(FIND allowed_application_dlls "${dependency_lower}" allowed_index)
            if(allowed_index EQUAL -1)
                message(FATAL_ERROR "${binary} has an unstaged application dependency: ${dependency}")
            endif()
            if(NOT EXISTS "${BINARY_DIR}/${dependency}")
                message(FATAL_ERROR "${binary} depends on ${dependency}, but it is absent beside the executable")
            endif()
        elseif(NOT dependency_lower MATCHES "${system_dependency_regex}")
            message(FATAL_ERROR "${binary} has a non-system, unstaged dependency: ${dependency}")
        endif()
    endforeach()

    execute_process(
        COMMAND "${DUMPBIN_EXECUTABLE}" /headers "${binary}"
        RESULT_VARIABLE headers_result
        OUTPUT_VARIABLE headers_output
        ERROR_VARIABLE headers_error)
    if(NOT headers_result EQUAL 0 OR NOT headers_output MATCHES "${expected_machine}")
        message(FATAL_ERROR "${binary} does not have expected ${EXPECTED_ARCH} PE machine type: ${headers_error}")
    endif()
endforeach()

message(STATUS "Verified ${EXPECTED_ARCH} PE architecture, staged application DLLs, and configuration-correct CRT dependencies")
