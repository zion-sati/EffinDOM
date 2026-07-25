if(NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}")
    message(FATAL_ERROR "Relocation source is missing: ${SOURCE_ROOT}")
endif()
if(NOT DEFINED RELOCATED_ROOT OR RELOCATED_ROOT STREQUAL "")
    message(FATAL_ERROR "Relocation destination was not supplied")
endif()
if(NOT DEFINED EXECUTABLE_RELATIVE OR EXECUTABLE_RELATIVE STREQUAL "")
    message(FATAL_ERROR "Relocated executable path was not supplied")
endif()

file(REMOVE_RECURSE "${RELOCATED_ROOT}")
file(MAKE_DIRECTORY "${RELOCATED_ROOT}")
file(COPY "${SOURCE_ROOT}/" DESTINATION "${RELOCATED_ROOT}")
set(executable "${RELOCATED_ROOT}/${EXECUTABLE_RELATIVE}")
if(NOT EXISTS "${executable}")
    message(FATAL_ERROR "Relocated executable is missing: ${executable}")
endif()

set(working_directory "${RELOCATED_ROOT}.working-directory")
file(REMOVE_RECURSE "${working_directory}")
file(MAKE_DIRECTORY "${working_directory}")
set(screenshot "${working_directory}/relocated-launch.png")
execute_process(
    COMMAND "${executable}" --hidden --screenshot "${screenshot}"
    WORKING_DIRECTORY "${working_directory}"
    RESULT_VARIABLE launch_result
    OUTPUT_VARIABLE launch_output
    ERROR_VARIABLE launch_error)
if(NOT launch_result EQUAL 0)
    message(FATAL_ERROR "Relocated application failed (${launch_result}): ${launch_output}${launch_error}")
endif()
if(NOT EXISTS "${screenshot}")
    message(FATAL_ERROR "Relocated application did not create a screenshot")
endif()
file(SIZE "${screenshot}" screenshot_size)
if(screenshot_size LESS 1024)
    message(FATAL_ERROR "Relocated application screenshot is unexpectedly small: ${screenshot_size} bytes")
endif()
