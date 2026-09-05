add_executable(winmix_audio_tests
    TestMain.cpp
    VolumeCurveTests.cpp
    SessionNamingTests.cpp
    ProcessInfoCacheTests.cpp
)

target_include_directories(winmix_audio_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/doctest
)

target_link_libraries(winmix_audio_tests PRIVATE winmix_audio)

include(CTest)
add_test(NAME winmix_audio_tests COMMAND winmix_audio_tests)
