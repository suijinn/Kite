cmake_minimum_required(VERSION 3.21)
project(Kite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KITE_BUILD_TESTS "Build the unit tests" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Static CRT: one self-contained exe and a measurably faster cold start.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Shared compile settings
# ---------------------------------------------------------------------------
add_library(kite_options INTERFACE)
target_include_directories(kite_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_definitions(kite_options INTERFACE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    # /utf-8 matters: the built-in Japanese string table lives in the source.
    target_compile_options(kite_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc
    )
endif()

# ---------------------------------------------------------------------------
# kite_core - everything that does not touch the OS.
#
#   core/  no OS calls beyond the five functions in core/base/Platform.h
#   ui/    draws through ui::Renderer only
#
# The unit tests link this and nothing else, which is what keeps the layering
# honest: if a Windows header ever creeps in here, the test build breaks.
# ---------------------------------------------------------------------------
add_library(kite_core STATIC
    src/core/app/App.cpp
    src/core/app/IconProvider.cpp
    src/core/base/Format.cpp
    src/core/base/Ini.cpp
    src/core/base/PathUtil.cpp
    src/core/base/Utf8.cpp
    src/core/fs/DirectoryLoader.cpp
    src/core/i18n/Strings.cpp
    src/core/input/Commands.cpp
    src/core/input/KeyEditor.cpp
    src/core/input/KeyMap.cpp
    src/core/input/Keys.cpp
    src/core/model/Workspace.cpp
    src/core/theme/Theme.cpp
    src/ui/AppUi.cpp
    src/ui/Glyphs.cpp
)
target_link_libraries(kite_core PUBLIC kite_options)

# ---------------------------------------------------------------------------
# kite - the Windows application
# ---------------------------------------------------------------------------
add_executable(kite WIN32
    src/main_win.cpp
    src/platform/win/D2DRenderer.cpp
    src/platform/win/IconHostClient.cpp
    src/platform/win/ShellHostClient.cpp
    src/platform/win/ShellHostProcess.cpp
    src/platform/win/ShellPipe.cpp
    src/platform/win/WinDirectoryWatcher.cpp
    src/platform/win/WinDropTarget.cpp
    src/platform/win/WinFileSystem.cpp
    src/platform/win/WinIconProvider.cpp
    src/platform/win/WinPlatform.cpp
    src/platform/win/WinShell.cpp
    src/platform/win/WinUtf.cpp
    src/platform/win/WinWindow.cpp
)

target_link_libraries(kite PRIVATE
    kite_core
    d2d1
    d3d11
    dxgi
    dwrite
    dwmapi
    shlwapi
    shell32
    ole32
    oleaut32
    imm32
    user32
    gdi32
    mpr          # WNetGetConnection, for mapped network drives
)

if(MSVC)
    target_link_options(kite PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# ---------------------------------------------------------------------------
# kite_shellhost - the process that runs other people's code
#
# Third-party shell extensions (Box, Google Drive, 7-Zip, TortoiseGit ...) are
# instantiated here and nowhere else, so one of them faulting costs a context
# menu or an icon instead of the file manager. ShellMenu.cpp is the IContextMenu
# code and ShellIcons.cpp is what asks for icon overlays; both must stay out of
# the kite target, since linking either one there would put the extensions back
# in Kite's address space, which is the whole thing this avoids.
#
# It does not link kite_core. Nothing in core/ or ui/ is involved in showing a
# menu or reading an icon, and keeping the host small keeps its start-up - which
# the first right-click waits on - short.
# ---------------------------------------------------------------------------
add_executable(kite_shellhost WIN32
    tools/shellhost/main.cpp
    src/platform/win/ShellIcons.cpp
    src/platform/win/ShellMenu.cpp
    src/platform/win/ShellPipe.cpp
    src/platform/win/WinUtf.cpp
)

target_link_libraries(kite_shellhost PRIVATE
    kite_options
    shlwapi
    shell32
    ole32
    oleaut32
    user32
    gdi32
)

if(MSVC)
    target_link_options(kite_shellhost PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# kite.exe looks for the host next to itself, so they have to land in the same
# directory whatever the generator would have done by default.
set_target_properties(kite kite_shellhost PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

get_target_property(KITE_CORE_SOURCES kite_core SOURCES)
get_target_property(KITE_APP_SOURCES kite SOURCES)
get_target_property(KITE_HOST_SOURCES kite_shellhost SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${KITE_CORE_SOURCES} ${KITE_APP_SOURCES} ${KITE_HOST_SOURCES})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(KITE_BUILD_TESTS)
    enable_testing()

    add_executable(kite_tests
        tests/TestFramework.cpp
        tests/FakePlatform.cpp
        tests/test_utf8.cpp
        tests/test_pathutil.cpp
        tests/test_ini.cpp
        tests/test_format.cpp
        tests/test_strings.cpp
        tests/test_keymap.cpp
        tests/test_keyeditor.cpp
        tests/test_iconcache.cpp
        tests/test_theme.cpp
        tests/test_tab.cpp
        tests/test_workspace.cpp
        tests/test_session.cpp
        tests/test_app.cpp
        tests/test_dropcheck.cpp
        tests/test_hostproto.cpp
    )
    target_link_libraries(kite_tests PRIVATE kite_core)

    # One CTest entry per suite so a failure names the area straight away.
    set(KITE_TEST_SUITES
        utf8 path ini format strings keymap keyeditor theme tab workspace session app drop
        iconcache hostproto)

    foreach(suite IN LISTS KITE_TEST_SUITES)
        add_test(NAME ${suite} COMMAND kite_tests --filter ${suite}.)
        set_tests_properties(${suite} PROPERTIES TIMEOUT 60)
    endforeach()
endif()
