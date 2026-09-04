if(NOT TARGET avsynth_flags)
    message(FATAL_ERROR
        "Configure the repository root, not this folder. From the top of avsynth: "
        "make, or cmake --preset ucrt64.")
endif()

# vsynth's own libraries. SDL3 and SDL3_ttf come from the root.
pkg_check_modules(AV REQUIRED IMPORTED_TARGET
    libavfilter libavformat libavcodec libavdevice libavutil)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)

add_executable(vsynth
    src/main.c
    src/window.c
    src/voice.c
    src/rack.c
    src/project.c
    src/picker.c
    src/hud.c
    src/graph.c
    src/editor.c
    src/help.c
    src/options.c
)

target_include_directories(vsynth PRIVATE src)
target_link_libraries(vsynth PRIVATE
    avsynth_flags PkgConfig::AV PkgConfig::SDL3 PkgConfig::SDL3_TTF PkgConfig::SQLITE)

if(WIN32)
    # Console subsystem on purpose: stderr is the status line for now.
    target_link_options(vsynth PRIVATE -mconsole)
endif()
