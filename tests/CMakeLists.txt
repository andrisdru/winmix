add_executable(winmix_audio_tests
    TestMain.cpp
    VolumeCurveTests.cpp
    SessionNamingTests.cpp
    ProcessInfoCacheTests.cpp
    AppSessionTrackerTests.cpp
    DeviceLabelsTests.cpp
    ${CMAKE_SOURCE_DIR}/WinMix.App/src/DeviceLabels.cpp
)

target_include_directories(winmix_audio_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/doctest
    ${CMAKE_SOURCE_DIR}/WinMix.App/src
)

target_link_libraries(winmix_audio_tests PRIVATE winmix_audio)

include(CTest)
add_test(NAME winmix_audio_tests COMMAND winmix_audio_tests)
