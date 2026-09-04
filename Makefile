# avsynth root targets. A thin wrapper over CMake so the same commands work on
# every machine and in CI. See ROADMAP.md (D1, D3) and README.md.
#
# Requires GNU make and the MSYS2 UCRT64 toolchain on Windows, or the
# equivalent distribution packages on Linux. README.md has the pacman line.

BUILD_DIR  ?= build
DIST_DIR   ?= dist
BUILD_TYPE ?= RelWithDebInfo
GENERATOR  ?= Ninja

BIN_DIR := $(BUILD_DIR)/bin

# The UCRT64 toolchain is not on the Windows PATH, so `make` started from
# PowerShell would find neither cmake, ninja and gcc nor the DLLs the apps link
# at run time. Put it in front when it is installed.
ifneq ($(wildcard /ucrt64/bin/cmake.exe),)
export PATH := /ucrt64/bin:$(PATH)
endif

ifeq ($(OS),Windows_NT)
EXE := .exe
endif

CMAKE ?= cmake
CTEST ?= ctest

DRONE  := $(BIN_DIR)/drone_commander$(EXE)
VSYNTH := $(BIN_DIR)/vsynth$(EXE)

.PHONY: all configure build run run-drone run-vsynth test package clean distclean help

all: build

$(BUILD_DIR)/CMakeCache.txt:
	@$(CMAKE) -S . -B $(BUILD_DIR) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

configure: $(BUILD_DIR)/CMakeCache.txt

build: configure
	@$(CMAKE) --build $(BUILD_DIR) --parallel

# Every synth at once. Each owns its window; ctrl+c here stops both, and
# closing one window leaves the other running.
run: build
	@echo "starting drone_commander and vsynth  (ctrl+c stops both)"
	@$(DRONE) & $(VSYNTH) & wait

run-drone: build
	@$(DRONE)

run-vsynth: build
	@$(VSYNTH)

test: build
	@$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

package: build
	@sh tools/package.sh $(BIN_DIR) $(DIST_DIR)

clean:
	@$(CMAKE) --build $(BUILD_DIR) --target clean

distclean:
	@rm -rf $(BUILD_DIR) $(DIST_DIR)

help:
	@echo "avsynth targets:"
	@echo "  make              build every app"
	@echo "  make run          run every app"
	@echo "  make run-drone    run Drone Commander only"
	@echo "  make run-vsynth   run vsynth only"
	@echo "  make test         run every test through ctest"
	@echo "  make package      zip each app with its DLLs into $(DIST_DIR)/"
	@echo "  make clean        remove build products, keep the configuration"
	@echo "  make distclean    remove $(BUILD_DIR)/ and $(DIST_DIR)/ entirely"
	@echo
	@echo "Variables: BUILD_DIR BUILD_TYPE GENERATOR DIST_DIR"
