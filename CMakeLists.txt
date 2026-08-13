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
# gmpi_ui's platform backends, plus our window-hosting glue, as a static lib.
#
# A separate target rather than plain sources on the plugin, because gmpi_ui's
# Win32 code needs UNICODE while iPlug2's standalone-app code is ANSI. Keeping
# UNICODE PRIVATE here lets both compile in one build.
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
    )
endif()

# ---------------------------------------------------------------------------
# The plugin.
#
# UI NONE + NO_IGRAPHICS makes iPlug2 skip IGraphics entirely, leaving
# IEditorDelegate::OpenWindow() for us to implement with a gmpi_ui frame.
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
