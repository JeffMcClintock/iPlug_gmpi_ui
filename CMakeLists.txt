cmake_minimum_required(VERSION 3.28)
project(IPlugGmpiGain VERSION 1.0.0)

include(FetchContent)

set(GMPI_UI_FOLDER_OVERRIDE "" CACHE PATH "path of a local gmpi_ui repo, or blank to fetch it automatically.")
set(GMPI_SDK_FOLDER_OVERRIDE "" CACHE PATH "path of a local GMPI repo, or blank to fetch it automatically.")
set(IPLUG2_FOLDER_OVERRIDE "" CACHE PATH "path of a local iPlug2 repo, or blank to fetch it automatically.")

# ---------------------------------------------------------------------------
# Dependencies.
#
# SOURCE_SUBDIR points at a directory with no CMakeLists.txt, so FetchContent
# downloads the repo without add_subdirectory'ing it. Without that, iPlug2's
# own CMakeLists would build all of its Examples and Tests along with us.
# ---------------------------------------------------------------------------
if("${GMPI_UI_FOLDER_OVERRIDE}" STREQUAL "")
    message(STATUS "Fetching gmpi_ui from GitHub")
    FetchContent_Declare(
        gmpi_ui
        GIT_REPOSITORY https://github.com/JeffMcClintock/gmpi_ui
        GIT_TAG        origin/main
        SOURCE_SUBDIR  helpers
    )
    FetchContent_MakeAvailable(gmpi_ui)
    set(GMPI_UI_SDK ${gmpi_ui_SOURCE_DIR})
else()
    message(STATUS "Using local gmpi_ui: ${GMPI_UI_FOLDER_OVERRIDE}")
    set(GMPI_UI_SDK ${GMPI_UI_FOLDER_OVERRIDE})
endif()

if("${GMPI_SDK_FOLDER_OVERRIDE}" STREQUAL "")
    message(STATUS "Fetching GMPI from GitHub")
    FetchContent_Declare(
        gmpi
        GIT_REPOSITORY https://github.com/JeffMcClintock/GMPI
        GIT_TAG        origin/main
        SOURCE_SUBDIR  Core
    )
    FetchContent_MakeAvailable(gmpi)
    set(GMPI_SDK ${gmpi_SOURCE_DIR})
else()
    message(STATUS "Using local GMPI: ${GMPI_SDK_FOLDER_OVERRIDE}")
    set(GMPI_SDK ${GMPI_SDK_FOLDER_OVERRIDE})
endif()

# ---------------------------------------------------------------------------
# Linux: editor only.
#
# iPlug2 does not support Linux - Scripts/cmake/IPlug.cmake stops with
# "Error - Linux not yet supported" - so there is no plugin to build. GainView
# is portable though, so build a compile-check of it and stop there. See
# tests/ui_compile_check.cpp.
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    message(STATUS "Linux: iPlug2 is unsupported here, building the UI compile-check only")

    add_executable(ui_compile_check tests/ui_compile_check.cpp src/GainView.h)
    set_target_properties(ui_compile_check PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(ui_compile_check PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GMPI_UI_SDK}
        ${GMPI_UI_SDK}/helpers
        ${GMPI_SDK}
        ${GMPI_SDK}/Core
    )

    return()
endif()

if("${IPLUG2_FOLDER_OVERRIDE}" STREQUAL "")
    message(STATUS "Fetching iPlug2 from GitHub (this one is large - a few minutes on a cold cache)")
    FetchContent_Declare(
        iplug2
        GIT_REPOSITORY https://github.com/iPlug2/iPlug2
        GIT_TAG        origin/master
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  IPlug
    )
    FetchContent_MakeAvailable(iplug2)
    set(IPLUG2_DIR ${iplug2_SOURCE_DIR} CACHE PATH "iPlug2 root directory" FORCE)
else()
    message(STATUS "Using local iPlug2: ${IPLUG2_FOLDER_OVERRIDE}")
    set(IPLUG2_DIR ${IPLUG2_FOLDER_OVERRIDE} CACHE PATH "iPlug2 root directory" FORCE)
endif()

# iPlug2 vendors RTAudio/RTMidi but not the VST3 SDK - normally you run
# Dependencies/IPlug/download-vst3-sdk.sh by hand. Do it here instead, into the
# exact path iPlug2 hardcodes, so a clean checkout configures in one step.
# vstgui4 is deliberately not fetched: it is large and iPlug2 never uses it.
set(VST3_SDK_DIR ${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK)
if(NOT EXISTS ${VST3_SDK_DIR}/pluginterfaces)
    message(STATUS "Fetching the VST3 SDK into ${VST3_SDK_DIR}")
    file(REMOVE_RECURSE ${VST3_SDK_DIR})
    FetchContent_Declare(
        vst3sdk
        GIT_REPOSITORY  https://github.com/steinbergmedia/vst3sdk
        GIT_TAG         v3.7.9_build_61
        GIT_SHALLOW     TRUE
        GIT_SUBMODULES  "pluginterfaces;base;public.sdk"
        SOURCE_DIR      ${VST3_SDK_DIR}
        SOURCE_SUBDIR   no_cmake_here
    )
    FetchContent_MakeAvailable(vst3sdk)
endif()

set(IPLUG2_CXX_STANDARD 20 CACHE STRING "" FORCE)
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# ---------------------------------------------------------------------------
# STEP 1a of 5 - compile GMPI-UI.
#
# GMPI-UI is header-only apart from its platform drawing backends, which have
# to be compiled by whoever hosts the window. This is that list; it is short,
# and it is the whole of GMPI-UI's build footprint.
#
# Why a separate STATIC library rather than adding these to the plugin target?
# Because GMPI-UI's Win32 code is a UNICODE build and iPlug2's standalone-app
# code is ANSI - put them in one target and whichever define you choose breaks
# the other. Keeping UNICODE PRIVATE to this library lets both compile
# together. (On macOS there is no such conflict; the library is just tidier.)
#
# Our own bridge from step 2 is compiled in here too, for the same reason: it
# is the file that includes <Windows.h>.
# ---------------------------------------------------------------------------
set(GMPI_UI_SRC
    ${GMPI_UI_SDK}/backends/DrawingFrameCommon.cpp
    ${GMPI_UI_SDK}/helpers/Timer.cpp
)

if(WIN32)
    list(APPEND GMPI_UI_SRC
        src/GmpiPlugFrame_win.cpp
        ${GMPI_UI_SDK}/backends/DirectXGfx.cpp
        ${GMPI_UI_SDK}/backends/DrawingFrameWin.cpp
    )
elseif(APPLE)
    list(APPEND GMPI_UI_SRC
        src/GmpiPlugFrame_mac.mm
        ${GMPI_UI_SDK}/backends/DrawingFrameMac.mm
    )
endif()

add_library(gmpi_ui STATIC ${GMPI_UI_SRC} src/GmpiPlugFrame.h)

set_target_properties(gmpi_ui PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# GMPI-UI's headers, plus the GMPI core repo it depends on for a handful of
# shared interface definitions. PUBLIC so the plugin target inherits them.
target_include_directories(gmpi_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GMPI_UI_SDK}
    ${GMPI_UI_SDK}/helpers
    ${GMPI_SDK}
    ${GMPI_SDK}/Core
)

if(WIN32)
    target_compile_definitions(gmpi_ui PRIVATE UNICODE _UNICODE)
    target_link_libraries(gmpi_ui PUBLIC
        Dwmapi dxguid dxgi d3d11 d2d1 dwrite windowscodecs
    )
elseif(APPLE)
    target_link_libraries(gmpi_ui PUBLIC
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework ImageIO"
        "-framework Accelerate"
        # For UTType, used by GMPI-UI's file dialog. Weak, because the
        # framework does not exist before macOS 11 and GMPI-UI guards its use
        # with @available - a hard link would stop the plugin loading there
        # instead of taking the fallback path.
        "-weak_framework UniformTypeIdentifiers"
    )
endif()

# ---------------------------------------------------------------------------
# STEP 1b of 5 - tell iPlug2 you are not using IGraphics.
#
# These two settings are the entire "opt out of IGraphics" switch, and the
# hinge the whole integration turns on:
#
#   UI NONE              stops iPlug2 compiling and linking IGraphics
#   DEFINES NO_IGRAPHICS makes IPlugDelegate_select.h pick plain
#                        IEditorDelegate as the plugin's base class, whose
#                        editor contract is just OpenWindow/CloseWindow
#
# UI NONE alone is not enough - it drops the library but leaves the headers
# expecting IGEditorDelegate. You want both.
#
# LINK gmpi_ui brings in the library from step 1a, with its include paths.
# ---------------------------------------------------------------------------
iplug_add_plugin(${PROJECT_NAME}
    SOURCES
        src/IPlugGmpiGain.cpp
        src/IPlugGmpiGain.h
        src/GainView.h
        resources/resource.h
    UI NONE
    DEFINES NO_IGRAPHICS
    LINK gmpi_ui
    FORMATS APP VST3 AU
)
