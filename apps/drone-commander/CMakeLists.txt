if(NOT TARGET avsynth_flags)
    message(FATAL_ERROR
        "Configure the repository root, not this folder. From the top of avsynth: "
        "make, or cmake --preset ucrt64.")
endif()

# The DSP engine is platform-independent standard C: no SDL, no ffmpeg, no
# SQLite. It renders real-time buffers and offline test buffers alike.
add_library(drone_dsp src/dsp.c)
target_include_directories(drone_dsp PUBLIC src)
target_link_libraries(drone_dsp PUBLIC avsynth_flags)
if(NOT MSVC)
    target_link_libraries(drone_dsp PUBLIC m)
endif()

add_executable(drone_commander src/main.c src/panel.c)
target_link_libraries(drone_commander PRIVATE
    drone_dsp avsynth_param avsynth_ui avsynth_flags PkgConfig::SDL3)

add_executable(dsp_tests tests/test_dsp.c)
target_link_libraries(dsp_tests PRIVATE drone_dsp avsynth_flags)
add_test(NAME dsp_tests COMMAND dsp_tests)
