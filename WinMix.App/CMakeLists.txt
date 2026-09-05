add_executable(winmix_app WIN32
    src/WinMain.cpp
    src/MainWindow.cpp
    src/DeviceLabels.cpp
    src/TrayIcon.cpp
    src/IconLoader.cpp
    src/Autostart.cpp
    src/render/DeviceResources.cpp
    src/controls/FaderControl.cpp
    src/controls/PeakMeter.cpp
    src/controls/MuteToggle.cpp
    src/controls/ComboBox.cpp
    resources/WinMix.rc
)

set_target_properties(winmix_app PROPERTIES
    OUTPUT_NAME "WinMix"
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h
    @ONLY
)

# WinMix.rc embeds our own hand-authored manifest as resource ID 1
# (CREATEPROCESS_MANIFEST_RESOURCE_ID); without /MANIFEST:NO the linker's own
# auto-generated manifest collides with it at the same resource ID.
target_link_options(winmix_app PRIVATE /MANIFEST:NO)

target_include_directories(winmix_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/generated
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
    comctl32
    gdi32
    shell32
    advapi32
    dwmapi
    d3d11
    dxgi
    d2d1
    dwrite
)
