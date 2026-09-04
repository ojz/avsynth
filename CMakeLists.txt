cmake_minimum_required(VERSION 3.24)

project(drone_commander VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

add_library(drone_dsp src/dsp.c)
target_include_directories(drone_dsp PUBLIC src)
if(NOT MSVC)
    target_link_libraries(drone_dsp PUBLIC m)
endif()

add_executable(drone_commander
    src/main.c
    src/panel.c
)
target_link_libraries(drone_commander PRIVATE drone_dsp SDL3::SDL3)
set_target_properties(drone_commander PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(drone_dsp PRIVATE /W4)
    target_compile_options(drone_commander PRIVATE /W4)
else()
    target_compile_options(drone_dsp PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(drone_commander PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(dsp_tests tests/test_dsp.c)
target_link_libraries(dsp_tests PRIVATE drone_dsp)
add_test(NAME dsp_tests COMMAND dsp_tests)