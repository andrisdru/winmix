cmake_minimum_required(VERSION 3.26)

# Must precede project(): CMP0091=NEW makes CMAKE_MSVC_RUNTIME_LIBRARY (set
# below) actually control the CRT choice, instead of CMake silently always
# picking the dynamic CRT (/MD) regardless of this variable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(WinMix LANGUAGES CXX)

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
