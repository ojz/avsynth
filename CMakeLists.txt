cmake_minimum_required(VERSION 3.24)

project(avsynth VERSION 0.2.0 LANGUAGES C)

# One C standard for every app and shared library (ROADMAP D5). Extensions stay
# on: the apps use POSIX strdup and strncasecmp, which a strict -std=c17 hides
# behind __STRICT_ANSI__ on both MSYS2 and glibc.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Every executable in one place so the Makefile, the packager and CI all know
# where to look.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# System libraries through pkg-config on every platform (ROADMAP D1): MSYS2
# UCRT64 on Windows, the distribution's packages on Linux. Nothing is vendored
# and nothing is downloaded at configure time, so both machines and CI resolve
# the same libraries the same way.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf)

# SDL3's pkg-config file hands out -mwindows. CMake puts a target's own link
# options before the libraries', so that -mwindows would win over an app's
# -mconsole and take away the console both apps use for stderr. Drop it here
# and let each app state its subsystem.
foreach(_sdl_target PkgConfig::SDL3 PkgConfig::SDL3_TTF)
    get_target_property(_opts ${_sdl_target} INTERFACE_LINK_OPTIONS)
    if(_opts)
        list(REMOVE_ITEM _opts "-mwindows")
        set_target_properties(${_sdl_target} PROPERTIES INTERFACE_LINK_OPTIONS "${_opts}")
    endif()
endforeach()

# One warning set, so an app cannot drift into being the lenient one. No
# -Wpedantic: extensions are deliberately on, see above.
add_library(avsynth_flags INTERFACE)
if(MSVC)
    target_compile_options(avsynth_flags INTERFACE /W4)
else()
    target_compile_options(avsynth_flags INTERFACE
        -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

enable_testing()

add_subdirectory(apps/drone-commander)
add_subdirectory(apps/video-synth)
