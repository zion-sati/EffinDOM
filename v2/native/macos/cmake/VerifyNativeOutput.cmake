set(required_files
    "${APP_ROOT}/Contents/MacOS/${EXECUTABLE_NAME}"
    "${APP_ROOT}/Contents/Frameworks/${SDL_LIBRARY_NAME}"
    "${APP_ROOT}/Contents/Frameworks/${CORE_LIBRARY_NAME}"
    "${APP_ROOT}/Contents/Frameworks/${UI_LIBRARY_NAME}"
    "${APP_ROOT}/Contents/Resources/effindom/fonts/NotoSans-Regular.ttf"
    "${APP_ROOT}/Contents/Resources/effindom/fonts/NotoSans-Bold.ttf"
    "${APP_ROOT}/Contents/Resources/effindom/fonts/NotoSansMono-Regular.ttf"
    "${APP_ROOT}/Contents/Resources/app/demo-texture.png"
)

foreach(required_file IN LISTS required_files)
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Native output is missing ${required_file}")
    endif()
endforeach()

file(GLOB packaged_fonts "${APP_ROOT}/Contents/Resources/effindom/fonts/*")
foreach(packaged_font IN LISTS packaged_fonts)
    get_filename_component(packaged_name "${packaged_font}" NAME)
    if(packaged_name MATCHES "\\.[A-Za-z0-9_-]{16,}\\.(ttf|otf|woff2)$")
        message(FATAL_ERROR "Native resource filename must not be content-hashed: ${packaged_name}")
    endif()
endforeach()

set(app_executable "${APP_ROOT}/Contents/MacOS/${EXECUTABLE_NAME}")
execute_process(
    COMMAND otool -l "${app_executable}"
    RESULT_VARIABLE otool_result
    OUTPUT_VARIABLE load_commands
    ERROR_VARIABLE otool_error)
if(NOT otool_result EQUAL 0)
    message(FATAL_ERROR "otool failed for ${app_executable}: ${otool_error}")
endif()
if(NOT load_commands MATCHES "path @loader_path/../Frameworks")
    message(FATAL_ERROR "macOS application is missing @loader_path/../Frameworks")
endif()
string(REGEX MATCHALL "path [^\n]+" loader_paths "${load_commands}")
foreach(forbidden_path IN ITEMS "${SOURCE_ROOT}" "${BUILD_ROOT}")
    string(FIND "${loader_paths}" "${forbidden_path}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
        message(FATAL_ERROR "macOS application contains build-time loader path ${forbidden_path}")
    endif()
endforeach()
