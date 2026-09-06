if(NOT TARGET avsynth_flags)
    message(FATAL_ERROR
        "Configure the repository root, not this folder. From the top of avsynth: "
        "make, or cmake --preset ucrt64.")
endif()

# The engine on its own, so the tests can link it without SDL.
add_library(drone_dsp src/dsp.c)
target_include_directories(drone_dsp PUBLIC src)
target_link_libraries(drone_dsp PUBLIC avsynth_flags)
if(NOT MSVC)
    target_link_libraries(drone_dsp PUBLIC m)
endif()

# The app as a library plus a thin exe stub (ROADMAP D11, P6): the launcher
# will link drone_app and host DRONE_COMMANDER; run alone, main.c is one call.
add_library(drone_app src/app.c src/panel.c)
target_include_directories(drone_app PUBLIC src)
target_link_libraries(drone_app PUBLIC drone_dsp avsynth_app avsynth_param avsynth_ui avsynth_flags)

add_executable(drone_commander src/main.c)
target_link_libraries(drone_commander PRIVATE drone_app)

add_executable(dsp_tests tests/test_dsp.c)
target_link_libraries(dsp_tests PRIVATE drone_dsp avsynth_flags)
add_test(NAME dsp_tests COMMAND dsp_tests)
