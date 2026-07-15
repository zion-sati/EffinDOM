add_subdirectory(core)
add_subdirectory(ui)

if(NOT EMSCRIPTEN)
    add_subdirectory(native/headless)
    if(APPLE)
        add_subdirectory(native/macos)
    endif()
endif()
