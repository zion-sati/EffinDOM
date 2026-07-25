if(NOT DEFINED READELF_EXECUTABLE OR NOT EXISTS "${READELF_EXECUTABLE}")
    message(FATAL_ERROR "readelf was not supplied or does not exist: ${READELF_EXECUTABLE}")
endif()
if(NOT DEFINED EXECUTABLE OR NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Linux native executable is missing: ${EXECUTABLE}")
endif()

execute_process(
    COMMAND "${READELF_EXECUTABLE}" -d "${EXECUTABLE}"
    RESULT_VARIABLE readelf_result
    OUTPUT_VARIABLE dynamic_section
    ERROR_VARIABLE readelf_error)
if(NOT readelf_result EQUAL 0)
    message(FATAL_ERROR "readelf failed for ${EXECUTABLE}: ${readelf_error}")
endif()
if(NOT dynamic_section MATCHES "(RPATH|RUNPATH).*[[][$]ORIGIN/../lib[]]")
    message(FATAL_ERROR "Linux application is missing $ORIGIN/../lib RPATH/RUNPATH")
endif()
foreach(forbidden_path IN ITEMS "${SOURCE_ROOT}" "${BUILD_ROOT}")
    string(FIND "${dynamic_section}" "${forbidden_path}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
        message(FATAL_ERROR "Linux application contains build-time loader path ${forbidden_path}")
    endif()
endforeach()

foreach(required_library IN ITEMS "${SDL_LIBRARY_NAME}" "${CORE_LIBRARY_NAME}" "${UI_LIBRARY_NAME}")
    if(NOT EXISTS "${LIBRARY_DIR}/${required_library}")
        message(FATAL_ERROR "Linux application is missing staged library ${required_library}")
    endif()
endforeach()
