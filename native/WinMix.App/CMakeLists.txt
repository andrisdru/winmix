add_executable(winmix_app WIN32 src/WinMain.cpp)

set_target_properties(winmix_app PROPERTIES
    OUTPUT_NAME "WinMixCpp"
)

target_compile_definitions(winmix_app PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NTDDI_VERSION=0x0A000004
)

target_link_libraries(winmix_app PRIVATE
    user32
    gdi32
)
