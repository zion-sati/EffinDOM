set(EFFINDOM_V2_UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Api.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeBoxModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeCallbacks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeClipboard.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeCrossSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiDebugTreeProjector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiScrollCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiEventSink.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiPlatformHost.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiFocusCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiFixedPitchTabs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiFrameCommitCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiInputRouter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiSelectionCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiTextEditingCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiLayoutCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiNodeVisualEncoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiNodeStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiSceneTraversal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiSceneGeometryResolver.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiTextPaintEncoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiTreePainter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeEditing.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeEditingTextState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeEditingGeometry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeGrid.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeInput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeNodes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeText.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeTextLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeTextNonWrap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeTextShaping.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiSemanticProjector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeViewport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiVisibilityResolver.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiRuntimeWrappedText.cpp"
)

if(EMSCRIPTEN)
    set(EFFINDOM_V2_UI_LIBRARY_TYPE STATIC)
else()
    set(EFFINDOM_V2_UI_LIBRARY_TYPE SHARED)
endif()
add_library(effindom_v2_ui ${EFFINDOM_V2_UI_LIBRARY_TYPE} ${EFFINDOM_V2_UI_SOURCES})
set_target_properties(effindom_v2_ui PROPERTIES
    OUTPUT_NAME "effindom_ui")
if(WIN32 AND EFFINDOM_V2_UI_LIBRARY_TYPE STREQUAL "SHARED")
    # CMake derives this list from EffinDOM's own object files; linked static
    # dependency archives are not added to the generated .def file.
    set_target_properties(effindom_v2_ui PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_definitions(effindom_v2_ui PRIVATE EFFINDOM_V2_UI_BUILDING_DLL=1)
    target_compile_options(effindom_v2_ui PRIVATE /wd4251 /wd4275)
endif()
target_include_directories(effindom_v2_ui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/v2/core/include"
)
target_link_libraries(effindom_v2_ui
    PRIVATE effindom_compile_flags
    PRIVATE effindom_woff2dec
    PUBLIC harfbuzz
    PUBLIC effindom_icu_i18n
    PUBLIC effindom_icu_common
    PUBLIC effindom_icu_stubdata
    PUBLIC yogacore
)

# All statically linked dependencies are implementation details. In addition
# to hidden-by-default compilation, prevent archive members with their own
# default visibility from entering the ELF dynamic symbol table.
if(UNIX AND NOT APPLE AND EFFINDOM_V2_UI_LIBRARY_TYPE STREQUAL "SHARED")
    target_link_options(effindom_v2_ui PRIVATE
        "LINKER:--exclude-libs,ALL"
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/native-exports.map")
    set_property(TARGET effindom_v2_ui APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/native-exports.map")
elseif(APPLE AND EFFINDOM_V2_UI_LIBRARY_TYPE STREQUAL "SHARED")
    target_link_options(effindom_v2_ui PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/native-exports.apple")
    set_property(TARGET effindom_v2_ui APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/native-exports.apple")
endif()

if(EMSCRIPTEN)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.txt")
    set(EFFINDOM_V2_UI_BROWSER_OUTPUT_DIR
        "${CMAKE_SOURCE_DIR}/public/v2/ui"
        CACHE PATH "Output directory for v2 ui browser artifacts")
    set(_EFFINDOM_V2_UI_HTML_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/browser/index.html")
    set(_EFFINDOM_V2_UI_HTML_OUTPUT "${EFFINDOM_V2_UI_BROWSER_OUTPUT_DIR}/index.html")
    add_custom_command(
        OUTPUT "${_EFFINDOM_V2_UI_HTML_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EFFINDOM_V2_UI_BROWSER_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_EFFINDOM_V2_UI_HTML_SOURCE}"
            "${_EFFINDOM_V2_UI_HTML_OUTPUT}"
        DEPENDS "${_EFFINDOM_V2_UI_HTML_SOURCE}"
        COMMENT "[EffinDom v2 ui] Staging browser harness"
    )
    add_custom_target(effindom_v2_ui_browser_assets DEPENDS "${_EFFINDOM_V2_UI_HTML_OUTPUT}")

    add_executable(effindom_v2_ui_wasm
        "${CMAKE_CURRENT_SOURCE_DIR}/src/WasmMain.cpp"
    )
    add_dependencies(effindom_v2_ui_wasm effindom_v2_ui_browser_assets)
    target_include_directories(effindom_v2_ui_wasm PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(effindom_v2_ui_wasm PRIVATE
        effindom_v2_ui
        effindom_compile_flags
        effindom_wasm_feature_flags
    )
    set_target_properties(effindom_v2_ui_wasm PROPERTIES
        OUTPUT_NAME "effindom-ui-v2"
        RUNTIME_OUTPUT_DIRECTORY "${EFFINDOM_V2_UI_BROWSER_OUTPUT_DIR}"
        SUFFIX ".js"
    )
    target_link_options(effindom_v2_ui_wasm PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXPORT_ALL=1
        -sMODULARIZE=1
        -sEXPORT_NAME=EffinDomUiV2ModuleFactory
        -sENVIRONMENT=web
        "-sEXPORTED_FUNCTIONS=@${CMAKE_CURRENT_SOURCE_DIR}/exports.txt"
        --emit-symbol-map
    )
    set_property(TARGET effindom_v2_ui_wasm APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.txt")
else()
    add_executable(effindom_v2_ui_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestUiSupport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_command_builder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_commit_frame.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_focus_coordinator.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_platform_host.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_fixed_pitch_tabs.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_node_store_access.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_layout_text.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_portals.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_input.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_runtime_api.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_scroll.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_debug_tree.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_misc.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_cross_selection.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_editing.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_semantic.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_text_layout.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_text_edit_descriptor.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_shaping_resources.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_text_selection.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_text_wrapped.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_text_input.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_grid.cpp"
    )
    target_include_directories(effindom_v2_ui_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/v2/core/include"
    )
    target_link_libraries(effindom_v2_ui_tests PRIVATE
        effindom_v2_ui
        Catch2::Catch2WithMain
        effindom_compile_flags
    )
    if(MSVC)
        target_compile_options(effindom_v2_ui_tests PRIVATE /wd4251 /wd4275)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
        target_link_libraries(effindom_v2_ui_tests PRIVATE
            "-Wl,--start-group"
            effindom_icu_i18n
            effindom_icu_common
            effindom_icu_stubdata
            "-Wl,--end-group"
        )
    endif()
    target_compile_definitions(effindom_v2_ui_tests PRIVATE EFFINDOM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    if(EFFINDOM_TARGET_RUNNABLE)
        include(Catch)
        catch_discover_tests(effindom_v2_ui_tests)
    endif()
endif()
