cmake_minimum_required(VERSION 3.24)

project(avsynth VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()
add_subdirectory(apps/drone-commander)
# vsynth needs pkg-config plus the ffmpeg 8, SDL2, SDL2_ttf and sqlite3 dev libs (MSYS2 UCRT64 on
# Windows), so it stays out of the default and CI build. Build it from apps/video-synth/ or turn this on.
option(AVSYNTH_BUILD_VIDEO_SYNTH "Build the video feedback synth (needs ffmpeg/SDL2 dev libs)" OFF)
if(AVSYNTH_BUILD_VIDEO_SYNTH)
    add_subdirectory(apps/video-synth)
endif()
