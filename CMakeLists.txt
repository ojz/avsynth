cmake_minimum_required(VERSION 3.24)

project(avsynth VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()
add_subdirectory(apps/drone-commander)