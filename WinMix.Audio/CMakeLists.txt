add_library(winmix_audio STATIC
    src/VolumeCurve.cpp
    src/ProcessInfoCache.cpp
    src/SessionNaming.cpp
    src/AudioSessionService.cpp
    src/AppSessionTracker.cpp
    src/AudioPolicyConfigFactory.cpp
    src/AppOutputRouter.cpp
    src/AppDeviceRouter.cpp
    src/PolicyConfigInterop.cpp
)

target_include_directories(winmix_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(winmix_audio PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NTDDI_VERSION=0x0A000004
)

target_link_libraries(winmix_audio PUBLIC
    ole32
    shlwapi
    version
    propsys
    runtimeobject
)
