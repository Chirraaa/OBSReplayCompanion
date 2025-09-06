cmake_minimum_required(VERSION 3.16)
project(OBSReplayCompanion)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC toolchain configuration for Windows
if(WIN32)
    set(CMAKE_GENERATOR_TOOLSET "v143" CACHE STRING "Platform Toolset" FORCE)
    set(CMAKE_VS_PLATFORM_TOOLSET "v143" CACHE STRING "Platform Toolset" FORCE)
    set(CMAKE_CXX_COMPILER "cl" CACHE STRING "CXX Compiler" FORCE)
    set(CMAKE_C_COMPILER "cl" CACHE STRING "C Compiler" FORCE)
endif()

# Qt6 Configuration - Use MSVC version
unset(CMAKE_PREFIX_PATH CACHE)
set(CMAKE_PREFIX_PATH "")
set(Qt6_DIR "C:/Qt/6.9.1/msvc2022_64/lib/cmake/Qt6" CACHE PATH "Qt6 Directory" FORCE)
set(CMAKE_PREFIX_PATH "C:/Qt/6.9.1/msvc2022_64")

# Verify Qt installation
if(NOT EXISTS "C:/Qt/6.9.1/msvc2022_64")
    message(FATAL_ERROR "Qt MSVC installation not found at C:/Qt/6.9.1/msvc2022_64")
endif()

# Find Qt6 - Added Concurrent module
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent
    PATHS "C:/Qt/6.9.1/msvc2022_64"
    NO_DEFAULT_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
    NO_CMAKE_PATH
    NO_CMAKE_ENVIRONMENT_PATH
)

# Output Directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/Debug")

# OBS Studio Configuration
set(OBS_STUDIO_SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/obs-studio")
set(OBS_STUDIO_BUILD_DIR "${OBS_STUDIO_SOURCE_DIR}/build_x64")

if(NOT EXISTS "${OBS_STUDIO_BUILD_DIR}/CMakeCache.txt")
    message(FATAL_ERROR "OBS Studio build not found in '${OBS_STUDIO_BUILD_DIR}'")
endif()

# Enable Qt MOC processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Source Files
set(APP_SOURCES
    "src/main.cpp"
    "src/MainWindow.cpp"
    "src/MainWindow.h"
    "src/GameCapture.cpp"
    "src/GameCapture.h"
    "src/GlobalHotkey.cpp"
    "src/GlobalHotkey.h"
    "src/KeybindDialog.cpp"
    "src/KeybindDialog.h"
    "src/AudioDeviceFetcher.cpp"
    "src/AudioDeviceFetcher.h"
    "src/AudioVisualizer.cpp"
    "src/AudioVisualizer.h"
    "src/ProcessMonitor.cpp"
    "src/ProcessMonitor.h"
    "src/Logger.cpp"
    "src/Logger.h"
    "src/LogDialog.cpp"
    "src/LogDialog.h"
    "src/gameclip.rc"
)

# Create executable - CONSOLE mode for debugging
add_executable(${PROJECT_NAME} ${APP_SOURCES})

# MSVC settings with console subsystem for debugging
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /Zc:__cplusplus 
        /permissive-
        /W3
        /wd4996
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CONSOLE  # Enable console output
    )
    
    # Set console subsystem for debugging
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()

# Force MOC processing for all Qt-related files
set_target_properties(${PROJECT_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${OBS_STUDIO_SOURCE_DIR}/libobs"
    "${OBS_STUDIO_SOURCE_DIR}/frontend/api"
    "${OBS_STUDIO_SOURCE_DIR}"
    "${OBS_STUDIO_BUILD_DIR}/config"
)

# Find OBS libraries
set(OBS_LIB_PATHS
    "${OBS_STUDIO_BUILD_DIR}/libobs/Debug"
    "${OBS_STUDIO_BUILD_DIR}/libobs/Release"
    "${OBS_STUDIO_BUILD_DIR}/libobs"
)

set(OBS_FRONTEND_LIB_PATHS

    "${OBS_STUDIO_BUILD_DIR}/frontend/api/Debug"
    "${OBS_STUDIO_BUILD_DIR}/frontend/api"
    "${OBS_STUDIO_BUILD_DIR}/frontend"
)

# Find core OBS library
find_library(OBS_LIB
    NAMES obs libobs obs64 libobs64
    PATHS ${OBS_LIB_PATHS}
    NO_DEFAULT_PATH
)

if(NOT OBS_LIB)
    message(FATAL_ERROR "OBS library not found in: ${OBS_LIB_PATHS}")
endif()

# Find OBS Frontend API library
find_library(OBS_FRONTEND_LIB
    NAMES obs-frontend-api libobs-frontend-api obs-frontend-api64 libobs-frontend-api64
    PATHS ${OBS_FRONTEND_LIB_PATHS}
    NO_DEFAULT_PATH
)

if(NOT OBS_FRONTEND_LIB)
    message(WARNING "OBS Frontend API library not found in: ${OBS_FRONTEND_LIB_PATHS}")
    message(STATUS "Will attempt to link without frontend API - some features may not work")
endif()

# System libraries for Windows
set(SYSTEM_LIBS
    psapi
    d3d11
    dxgi
    shell32
    user32
    gdi32
    kernel32
    advapi32
    ole32
    oleaut32
    uuid
    winmm
)

# Link libraries - Added Concurrent
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    ${OBS_LIB}
    ${SYSTEM_LIBS}
)

target_link_libraries(OBSReplayCompanion PRIVATE wbemuuid ole32 oleaut32)

# Add frontend API library if found
if(OBS_FRONTEND_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OBS_FRONTEND_LIB})
    message(STATUS "Linking with OBS Frontend API: ${OBS_FRONTEND_LIB}")
endif()

# Set runtime library
if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Set working directory for debugging
set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>")

# Copy Qt DLLs
if(WIN32)
    get_target_property(QT6_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT6_WINDEPLOYQT_EXECUTABLE ${QT6_QMAKE_EXECUTABLE} PATH)
    set(QT6_WINDEPLOYQT_EXECUTABLE "${QT6_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")

    if(EXISTS ${QT6_WINDEPLOYQT_EXECUTABLE})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${QT6_WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Deploying Qt libraries")
    else()
        # Manual DLL copying fallback
        set(QT_DLLS Qt6Core Qt6Gui Qt6Widgets Qt6Concurrent)
        
        foreach(dll ${QT_DLLS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "C:/Qt/6.9.1/msvc2022_64/bin/${dll}d.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            )
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "C:/Qt/6.9.1/msvc2022_64/bin/${dll}.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            )
        endforeach()
        
        # Copy platform plugins
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/Qt/6.9.1/msvc2022_64/plugins/platforms/qwindowsd.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms/
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/Qt/6.9.1/msvc2022_64/plugins/platforms/qwindows.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms/
        )
    endif()
endif()

# Print debug information
message(STATUS "OBS Library: ${OBS_LIB}")
if(OBS_FRONTEND_LIB)
    message(STATUS "OBS Frontend Library: ${OBS_FRONTEND_LIB}")
endif()
message(STATUS "OBS Source Dir: ${OBS_STUDIO_SOURCE_DIR}")
message(STATUS "OBS Build Dir: ${OBS_STUDIO_BUILD_DIR}")