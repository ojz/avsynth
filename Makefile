CMAKE ?= $(shell where cmake.exe 2>NUL || echo $(LOCALAPPDATA)/Microsoft/WinGet/Packages/Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe/cmake-4.4.3-windows-x86_64/bin/cmake.exe)
BUILD_DIR ?= build
BIN := drone_commander.exe

SRCS := $(wildcard apps/drone-commander/src/*) apps/drone-commander/CMakeLists.txt CMakeLists.txt

.PHONY: all build run test clean

all: build

build: $(BIN)

$(BUILD_DIR)/Makefile: CMakeLists.txt
	@"$(CMAKE)" -S . -B $(BUILD_DIR) -G "MinGW Makefiles"

$(BIN): $(SRCS) $(BUILD_DIR)/Makefile
	@"$(CMAKE)" --build $(BUILD_DIR) --target drone_commander --parallel

run: $(BIN)
	@.\$(BIN)

test: $(BUILD_DIR)/Makefile
	@"$(CMAKE)" --build $(BUILD_DIR) --target dsp_tests --parallel
	@"$(CMAKE)" --build $(BUILD_DIR) --target test

clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(BIN) del /f /q $(BIN)
