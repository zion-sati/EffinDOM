include_guard(GLOBAL)

find_package(Git REQUIRED)

set(
    EFFINDOM_FETCHCONTENT_SOURCE_CACHE
    "${CMAKE_SOURCE_DIR}/build/fetchcontent-sources"
    CACHE PATH
    "Shared revision-keyed source cache for EffinDOM FetchContent dependencies")

function(effindom_cache_git_source dependency repository revision)
    string(TOLOWER "${dependency}" _dependency_directory_name)
    string(TOUPPER "${dependency}" _dependency_variable_name)
    string(SHA256 _source_identity "${repository}\n${revision}")
    string(SUBSTRING "${_source_identity}" 0 16 _source_identity_short)

    set(_cache_root "${EFFINDOM_FETCHCONTENT_SOURCE_CACHE}")
    set(_source_dir
        "${_cache_root}/${_dependency_directory_name}-${_source_identity_short}")
    set(_marker "${_source_dir}/.effindom-source-revision")
    set(_expected_marker "${repository}\n${revision}\n")

    file(MAKE_DIRECTORY "${_cache_root}/locks")
    file(
        LOCK "${_cache_root}/locks/${_dependency_directory_name}-${_source_identity_short}.lock"
        GUARD FUNCTION
        TIMEOUT 1800
        RESULT_VARIABLE _lock_result)
    if(NOT _lock_result EQUAL 0)
        message(FATAL_ERROR
            "Timed out waiting for the shared ${dependency} source cache: ${_lock_result}")
    endif()

    set(_source_is_ready OFF)
    if(EXISTS "${_marker}" AND EXISTS "${_source_dir}/.git")
        file(READ "${_marker}" _actual_marker)
        if(_actual_marker STREQUAL _expected_marker)
            set(_source_is_ready ON)
        endif()
    endif()

    if(NOT _source_is_ready)
        set(_temporary_dir "${_source_dir}.tmp")
        file(REMOVE_RECURSE "${_source_dir}" "${_temporary_dir}")
        file(MAKE_DIRECTORY "${_temporary_dir}")

        message(STATUS
            "Preparing shared ${dependency} source ${revision} in ${_source_dir}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" init --quiet "${_temporary_dir}"
            RESULT_VARIABLE _git_result)
        if(NOT _git_result EQUAL 0)
            message(FATAL_ERROR "Failed to initialize the shared ${dependency} source cache")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${_temporary_dir}" remote add origin "${repository}"
            RESULT_VARIABLE _git_result)
        if(NOT _git_result EQUAL 0)
            message(FATAL_ERROR "Failed to configure the shared ${dependency} source remote")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${_temporary_dir}"
                fetch --quiet --depth 1 origin "${revision}"
            RESULT_VARIABLE _git_result)
        if(NOT _git_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to fetch ${dependency} revision ${revision} from ${repository}")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${_temporary_dir}"
                -c advice.detachedHead=false checkout --quiet --detach FETCH_HEAD
            RESULT_VARIABLE _git_result)
        if(NOT _git_result EQUAL 0)
            message(FATAL_ERROR "Failed to check out ${dependency} revision ${revision}")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${_temporary_dir}"
                submodule update --init --recursive --depth 1
            RESULT_VARIABLE _git_result)
        if(NOT _git_result EQUAL 0)
            message(FATAL_ERROR "Failed to initialize ${dependency} submodules")
        endif()

        file(WRITE "${_temporary_dir}/.effindom-source-revision" "${_expected_marker}")
        file(RENAME "${_temporary_dir}" "${_source_dir}")
    endif()

    # FetchContent still owns a target-specific binary and subbuild directory.
    # Only immutable checked-out sources are shared between configurations.
    set("FETCHCONTENT_SOURCE_DIR_${_dependency_variable_name}"
        "${_source_dir}"
        CACHE PATH
        "Shared source checkout for ${dependency}"
        FORCE)
endfunction()
