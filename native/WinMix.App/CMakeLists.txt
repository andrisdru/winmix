add_executable(winmix_app WIN32
    src/WinMain.cpp
    src/MainWindow.cpp
    src/render/DeviceResources.cpp
    src/controls/FaderControl.cpp
    src/controls/PeakMeter.cpp
    src/controls/MuteToggle.cpp
    src/controls/ComboBox.cpp
)

set_target_properties(winmix_app PROPERTIES
    OUTPUT_NAME "WinMixCpp"
)

target_include_directories(winmix_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    winmix_audio
    user32
    gdi32
    d3d11
    dxgi
    d2d1
    dwrite
)
