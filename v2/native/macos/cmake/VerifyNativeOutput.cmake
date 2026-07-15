set(required_files
    "${OUTPUT_ROOT}/bin/${EXECUTABLE_NAME}"
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
        message(FATAL_ERROR "Native output is missing ${required_file}")
    endif()
endforeach()

file(GLOB packaged_fonts "${OUTPUT_ROOT}/resources/effindom/fonts/*")
foreach(packaged_font IN LISTS packaged_fonts)
    get_filename_component(packaged_name "${packaged_font}" NAME)
    if(packaged_name MATCHES "\\.[A-Za-z0-9_-]{16,}\\.(ttf|otf|woff2)$")
        message(FATAL_ERROR "Native resource filename must not be content-hashed: ${packaged_name}")
    endif()
endforeach()
