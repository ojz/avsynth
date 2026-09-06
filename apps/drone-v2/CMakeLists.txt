if(NOT TARGET avsynth_flags)
    message(FATAL_ERROR
        "Configure the repository root, not this folder. From the top of avsynth: "
        "make, or cmake --preset ucrt64.")
endif()

# The app as a library plus a thin exe stub (ROADMAP D11, P6).
add_library(dronev2_app src/app.c)
target_include_directories(dronev2_app PUBLIC src)
target_link_libraries(dronev2_app PUBLIC avsynth_app avsynth_param avsynth_ui avsynth_flags)
if(NOT MSVC)
    target_link_libraries(dronev2_app PUBLIC m)
endif()

add_executable(drone_v2 src/main.c)
target_link_libraries(drone_v2 PRIVATE dronev2_app)
