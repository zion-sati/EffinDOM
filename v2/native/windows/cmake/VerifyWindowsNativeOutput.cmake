set(required_files
    "${OUTPUT_ROOT}/bin/${EXECUTABLE_NAME}"
    "${OUTPUT_ROOT}/bin/${SDL_LIBRARY_NAME}"
    "${OUTPUT_ROOT}/bin/${CORE_LIBRARY_NAME}"
    "${OUTPUT_ROOT}/bin/${UI_LIBRARY_NAME}"
    "${OUTPUT_ROOT}/lib/${SDL_LIBRARY_NAME}"
    "${OUTPUT_ROOT}/lib/${CORE_LIBRARY_NAME}"
    "${OUTPUT_ROOT}/lib/${UI_LIBRARY_NAME}"
    "${OUTPUT_ROOT}/resources/effindom/fonts/NotoSans-Regular.ttf"
    "${OUTPUT_ROOT}/resources/effindom/fonts/NotoSans-Bold.ttf"
    "${OUTPUT_ROOT}/resources/effindom/fonts/NotoSansMono-Regular.ttf"
    "${OUTPUT_ROOT}/resources/app/demo-texture.png"
)

foreach(required_file IN LISTS required_files)
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Windows native output is missing ${required_file}")
    endif()
endforeach()

if(REQUIRE_SYMBOLS)
    foreach(symbol_file IN ITEMS "${EXECUTABLE_PDB_NAME}" "${CORE_PDB_NAME}" "${UI_PDB_NAME}")
        if(symbol_file STREQUAL "" OR NOT EXISTS "${OUTPUT_ROOT}/symbols/${symbol_file}")
            message(FATAL_ERROR "Windows native output is missing required PDB: ${symbol_file}")
        endif()
    endforeach()
endif()

file(GLOB_RECURSE packaged_files RELATIVE "${OUTPUT_ROOT}" "${OUTPUT_ROOT}/*")
foreach(packaged_file IN LISTS packaged_files)
    get_filename_component(packaged_name "${packaged_file}" NAME)
    if(packaged_name MATCHES "\\.[A-Za-z0-9_-]{16,}\\.(ttf|otf|woff2)$")
        message(FATAL_ERROR "Windows native resource filename must not be content-hashed: ${packaged_name}")
    endif()
    if(packaged_file MATCHES "(^|/)(CMakeFiles|_deps|native-rust|Testing)(/|$)" OR
       packaged_name MATCHES "^(CMakeCache\\.txt|build\\.ninja|compile_commands\\.json)$")
        message(FATAL_ERROR "Build cache leaked into Windows native output: ${packaged_file}")
    endif()
endforeach()

if(RUN_EXECUTABLE)
    set(relocated_root "${CMAKE_CURRENT_BINARY_DIR}/relocated-windows-output")
    file(REMOVE_RECURSE "${relocated_root}")
    file(MAKE_DIRECTORY "${relocated_root}")
    file(COPY "${OUTPUT_ROOT}/" DESTINATION "${relocated_root}")
    set(screenshot "${relocated_root}/native-screenshot.png")
    execute_process(
        COMMAND "${relocated_root}/bin/${EXECUTABLE_NAME}" --hidden --screenshot "${screenshot}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_output
        ERROR_VARIABLE run_error)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "Relocated Windows native executable failed (${run_result}): ${run_output}${run_error}")
    endif()
    if(NOT EXISTS "${screenshot}")
        message(FATAL_ERROR "Relocated Windows native executable did not create its screenshot")
    endif()
    file(SIZE "${screenshot}" screenshot_size)
    if(screenshot_size LESS 1024)
        message(FATAL_ERROR "Relocated Windows native screenshot is unexpectedly small: ${screenshot_size} bytes")
    endif()
endif()
