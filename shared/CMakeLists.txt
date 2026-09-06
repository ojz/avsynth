# Shared libraries. Something moves here when a second app needs it, never in
# anticipation (ROADMAP D4).

# The fader and parameter model. Plain C on purpose: a DSP engine may depend on
# it, and it is tested without a window or an audio device.
add_library(avsynth_param param/param.c)
target_include_directories(avsynth_param PUBLIC param)
target_link_libraries(avsynth_param PUBLIC avsynth_flags)
if(NOT MSVC)
    target_link_libraries(avsynth_param PUBLIC m)
endif()

add_executable(param_tests param/tests/test_param.c)
target_link_libraries(param_tests PRIVATE avsynth_param)
add_test(NAME param_tests COMMAND param_tests)

# The control surface. Owns how a fader looks and which gesture does what, so
# every app inherits the same behaviour.
add_library(avsynth_ui ui/ui.c)
target_include_directories(avsynth_ui PUBLIC ui)
target_link_libraries(avsynth_ui PUBLIC avsynth_param avsynth_flags PkgConfig::SDL3)

# The shell (D14). SDL init, the window, the frame loop, the typeface and the
# gesture dispatch, once. An app is a struct plus an AppSpec; the exe stub is
# one call into here. Pulls SDL3_ttf for every app, because the text is the
# lab's, not the app's.
add_library(avsynth_app app/app.c app/text.c)
target_include_directories(avsynth_app PUBLIC app)
target_link_libraries(avsynth_app PUBLIC avsynth_ui avsynth_param avsynth_flags
    PkgConfig::SDL3 PkgConfig::SDL3_TTF)
