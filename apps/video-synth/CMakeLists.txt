cmake_minimum_required(VERSION 3.20)
project(vsynth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET
  libavfilter libavformat libavcodec libavdevice libavutil)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)

add_executable(vsynth
  src/main.c
  src/window.c
  src/voice.c
  src/rack.c
)

target_include_directories(vsynth PRIVATE src)
target_link_libraries(vsynth PRIVATE
  PkgConfig::AV PkgConfig::SDL2 PkgConfig::SQLITE)

if(MSVC)
  target_compile_options(vsynth PRIVATE /W4)
else()
  target_compile_options(vsynth PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
  # Console subsystem on purpose: stderr is the status line for now.
  target_link_options(vsynth PRIVATE -mconsole)
endif()
