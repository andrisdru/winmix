add_executable(audio_smoke_test main.cpp)

target_link_libraries(audio_smoke_test PRIVATE winmix_audio)

set_target_properties(audio_smoke_test PROPERTIES
    OUTPUT_NAME "AudioSmokeTest"
)
