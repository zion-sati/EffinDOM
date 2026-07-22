include(FetchContent)

if(NOT TARGET effindom_pinned_vulkan_headers)
    # Ubuntu 24.04 ships Vulkan-Headers 1.3.275, which predates the ratified
    # FIFO-latest-ready declarations even when the installed driver implements
    # the extension. Headers are build-only; the distro Vulkan loader remains
    # the runtime dependency.
    set(EFFINDOM_VULKAN_HEADERS_REV
        "354fab82dbd91d2526a41ee4dbfff1012a623798"
        CACHE STRING "Pinned Khronos Vulkan-Headers revision")
    FetchContent_Declare(
        effindom_pinned_vulkan_headers_source
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        ${EFFINDOM_VULKAN_HEADERS_REV}
        SOURCE_SUBDIR  __effindom_source_only
    )
    FetchContent_MakeAvailable(effindom_pinned_vulkan_headers_source)

    add_library(effindom_pinned_vulkan_headers INTERFACE)
    target_include_directories(effindom_pinned_vulkan_headers SYSTEM BEFORE INTERFACE
        "${effindom_pinned_vulkan_headers_source_SOURCE_DIR}/include")
endif()
