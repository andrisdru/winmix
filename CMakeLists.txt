cmake_minimum_required(VERSION 3.26)

# Must precede project(): CMP0091=NEW makes CMAKE_MSVC_RUNTIME_LIBRARY (set
# below) actually control the CRT choice, instead of CMake silently always
# picking the dynamic CRT (/MD) regardless of this variable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Single source of truth for the app version, bumped by the /app-version
# skill -- everything else (the GUI's title bar/tray tooltip/tray menu via
# WinMix.App's generated Version.h) reads PROJECT_VERSION from here rather
# than duplicating the number.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WINMIX_VERSION LIMIT_COUNT 1)

project(WinMix LANGUAGES CXX VERSION ${WINMIX_VERSION})

# Without this, editing VERSION alone doesn't trigger a reconfigure --
# `cmake --build` only reruns configure when it notices CMakeLists.txt
# itself (or another tracked input) changed, so PROJECT_VERSION and the
# generated Version.h would keep silently serving the old number.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WINMIX_BUILD_TOOLS "Build developer smoke-test tools" ON)
option(WINMIX_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(WinMix.Audio)
add_subdirectory(WinMix.App)

if(WINMIX_BUILD_TOOLS)
    add_subdirectory(tools/AudioSmokeTest)
endif()

if(WINMIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
