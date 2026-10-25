cmake_minimum_required(VERSION 3.20)
project(openFences VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT linking so the released .exe is self-contained; recipients do not
# need to install the VC++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_definitions(-DUNICODE -D_UNICODE)

# Sources are UTF-8 (Chinese menu strings); without this MSVC assumes the
# local ANSI code page and misparses multi-byte literals.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# nlohmann/json (header-only, auto-download)
include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Embed every resources/lang/*.json into the exe as an RCDATA resource named
# LANG_<NAME>; the loader (app/src/core/localization.cpp) enumerates them at startup.
file(GLOB OPENFENCES_LANG_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/lang/*.json")
set(OPENFENCES_LANG_RC "${CMAKE_CURRENT_BINARY_DIR}/lang_resources.rc")
set(OPENFENCES_LANG_RC_CONTENT "")
foreach(lang_file ${OPENFENCES_LANG_FILES})
    get_filename_component(lang_name "${lang_file}" NAME_WE)
    string(TOUPPER "${lang_name}" lang_id)
    string(REPLACE "-" "_" lang_id "${lang_id}")
    string(APPEND OPENFENCES_LANG_RC_CONTENT "LANG_${lang_id} RCDATA \"${lang_file}\"
")
endforeach()
file(GENERATE OUTPUT "${OPENFENCES_LANG_RC}" CONTENT "${OPENFENCES_LANG_RC_CONTENT}")
set_source_files_properties("${OPENFENCES_LANG_RC}" PROPERTIES GENERATED TRUE)

add_executable(openfences
    app/src/main.cpp
    app/src/fence/fence_window.cpp
    app/src/fence/fence_layout.cpp
    app/src/fence/icon_grid.cpp
    app/src/fence/icon_sort.cpp
    app/src/fence/fence_selection.cpp
    app/src/fence/fence_sort_filter.cpp
    app/src/fence/fence_source_folder.cpp
    app/src/fence/fence_rename.cpp
    app/src/fence/fence_mouse.cpp
    app/src/fence/fence_keyboard.cpp
    app/src/fence/fence_menu.cpp
    app/src/shell/desktop_host.cpp
    app/src/fence/fence_drop_target.cpp
    app/src/menu/shell_context_menu.cpp
    app/src/render/render.cpp
    app/src/settings/settings_panel.cpp
    app/src/menu/context_menu.cpp
    app/src/render/icon_extract.cpp
    app/src/render/menu_icons.cpp
    app/src/core/config.cpp
    app/src/core/localization.cpp
    app/src/shell/explorer_sort.cpp
    app/src/app.rc
    ${OPENFENCES_LANG_RC}
)

# Merge our application manifest into the linker-generated one. The
# <compatibility> section is load-bearing: without it Windows treats the
# process as pre-Windows-8 and refuses to create WS_EX_LAYERED child windows,
# which the Progman-hosted fence windows are.
target_link_options(openfences PRIVATE
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app/src/app.manifest")

target_include_directories(openfences PRIVATE app/include)
target_link_libraries(openfences PRIVATE
    d2d1 dwrite windowscodecs
    ole32 shlwapi shell32 comctl32 comdlg32
    advapi32
    nlohmann_json::nlohmann_json
)

set_target_properties(openfences PROPERTIES WIN32_EXECUTABLE TRUE)

# Research spikes and diagnostics (see tools/README.md). Not built by default.
option(OPENFENCES_BUILD_TOOLS "Build the diagnostic and spike tools in tools/" OFF)
if(OPENFENCES_BUILD_TOOLS)
    add_executable(diag tools/desktop_diag.cpp)
    target_link_libraries(diag PRIVATE user32 comctl32)

    add_executable(icon_diag tools/icon_diag.cpp app/src/render/icon_extract.cpp)
    target_include_directories(icon_diag PRIVATE app/include)
    target_link_libraries(icon_diag PRIVATE user32 shell32 shlwapi ole32 uuid comctl32)

    add_executable(region_spike tools/region_spike.cpp)
    target_link_libraries(region_spike PRIVATE user32)
    # Layered CHILD windows (the fence analogue probed here) only exist on Win8+;
    # without the supportedOS manifest CreateWindowExW(WS_EX_LAYERED, WS_CHILD) fails.
    target_link_options(region_spike PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app/src/app.manifest")

    add_executable(ctxmenu_spike tools/ctxmenu_spike.cpp)
    target_link_libraries(ctxmenu_spike PRIVATE user32 shell32 shlwapi ole32 uuid)
    # Same supportedOS manifest as the app: without it the process is shimmed to
    # pre-Windows-8 and the shell hides IExplorerCommandProvider (a Win8+ interface).
    target_link_options(ctxmenu_spike PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app/src/app.manifest")

    add_executable(vis_probe tools/vis_probe.cpp)
    target_link_libraries(vis_probe PRIVATE user32 shell32 advapi32)
endif()

# Unit tests (ctest). Off by default.
option(OPENFENCES_BUILD_TESTS "Build the tests in tests/" OFF)
if(OPENFENCES_BUILD_TESTS)
    enable_testing()
    add_executable(l10n_test tests/l10n_test.cpp app/src/core/localization.cpp)
    target_include_directories(l10n_test PRIVATE app/include)
    target_link_libraries(l10n_test PRIVATE shell32 nlohmann_json::nlohmann_json)
    # The loader also reads "<exe dir>\lang\*.json", so the test needs no embedded resources.
    add_custom_command(TARGET l10n_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/lang" "$<TARGET_FILE_DIR:l10n_test>/lang")
    add_test(NAME l10n COMMAND l10n_test)

    add_executable(icon_grid_test tests/icon_grid_test.cpp app/src/fence/icon_grid.cpp)
    target_include_directories(icon_grid_test PRIVATE app/include tests)
    add_test(NAME icon_grid COMMAND icon_grid_test)

    add_executable(icon_sort_test tests/icon_sort_test.cpp app/src/fence/icon_sort.cpp)
    target_include_directories(icon_sort_test PRIVATE app/include tests)
    target_link_libraries(icon_sort_test PRIVATE shlwapi)
    add_test(NAME icon_sort COMMAND icon_sort_test)
endif()
