cmake_minimum_required(VERSION 3.26)
project(WinMixCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WINMIX_BUILD_TOOLS "Build developer smoke-test tools" ON)
option(WINMIX_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(WinMix.App)
