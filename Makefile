CMAKE ?= $(shell where cmake.exe 2>NUL || echo $(LOCALAPPDATA)/Microsoft/WinGet/Packages/Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe/cmake-4.4.3-windows-x86_64/bin/cmake.exe)
BUILD_DIR ?= build
BIN := drone_commander.exe
VIDEO_BUILD_DIR ?= build/video-synth
VIDEO_BIN := $(VIDEO_BUILD_DIR)/vsynth.exe

SRCS := $(wildcard apps/drone-commander/src/*) apps/drone-commander/CMakeLists.txt CMakeLists.txt
VIDEO_SRCS := $(wildcard apps/video-synth/src/*) apps/video-synth/CMakeLists.txt

.PHONY: all build build-all build-drone build-vsynth run run-drone run-vsynth test clean

all: build

build: $(BIN)

build-drone: build

build-all: build-drone build-vsynth

$(BUILD_DIR)/Makefile: CMakeLists.txt
	@"$(CMAKE)" -S . -B $(BUILD_DIR) -G "MinGW Makefiles"

$(BIN): $(SRCS) $(BUILD_DIR)/Makefile
	@"$(CMAKE)" --build $(BUILD_DIR) --target drone_commander --parallel

run: $(BIN)
	@.\$(BIN)

run-drone: run

$(VIDEO_BUILD_DIR)/build.ninja: apps/video-synth/CMakeLists.txt
	@"$(CMAKE)" -S apps/video-synth -B $(VIDEO_BUILD_DIR) -G Ninja

$(VIDEO_BIN): $(VIDEO_SRCS) $(VIDEO_BUILD_DIR)/build.ninja
	@"$(CMAKE)" --build $(VIDEO_BUILD_DIR) --parallel

build-vsynth: $(VIDEO_BIN)

run-vsynth: $(VIDEO_BIN)
	@$(VIDEO_BIN)

test: $(BUILD_DIR)/Makefile
	@"$(CMAKE)" --build $(BUILD_DIR) --target dsp_tests --parallel
	@"$(CMAKE)" --build $(BUILD_DIR) --target test

clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(BIN) del /f /q $(BIN)
