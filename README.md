# avsynth

A DIY synthesizer lab. Every instrument starts here as a small C program that
tries out a topology, its ranges and a way of playing it. The ones worth
keeping become hardware: machine-assembled SMT boards in small desktop boxes on
a daisy-chained 9 V or 12 V supply, guitar-pedal style. The software is the
sketchbook, not the firmware. [ROADMAP.md](ROADMAP.md) has the vision, the
decisions and the plan.

## Applications

| Application | What it is | Docs |
|---|---|---|
| **Drone Commander** (`apps/drone-commander`) | Three-oscillator audio drone synth with FM cascade, state-variable filter, VCA and three synced square LFOs. The digital sketch for a fully analog hardware build. | [README](apps/drone-commander/README.md) |
| **vsynth** (`apps/video-synth`) | Video feedback synth. Captures a screen region, runs it through a libavfilter chain you write as text, shows the result in a borderless window you drag into the capture region. Knobs are derived from the text; presets live in a SQLite project file. | [README](apps/video-synth/README.md), [PRD](apps/video-synth/PRD.md) |

Both are C, both use SDL for window, input and (for audio) the device, both
build with CMake. They were developed separately and merged on 2026-09-04;
unifying their toolchain, SDL version and UI is the current work, see the
roadmap.

## Layout

```text
apps/drone-commander/   audio drone synth (C17, SDL3)
apps/video-synth/       video feedback synth (C11, SDL2, ffmpeg, SQLite)
shared/                 code used by more than one app (empty until phase 3)
assets/screenshots/     screenshots per application
.github/workflows/      CI: Windows build, test, artifacts, tagged releases
ROADMAP.md              vision, decisions, phases
AGENTS.md               rules for agents and contributors
```

## Building today

The only toolchain in use is MSYS2 UCRT64 on Windows (pacman on Arch Linux
works the same way). The unified root commands (`make`, `make run`,
`make run-drone`, `make run-vsynth`) are phase 1 of the roadmap and do not
exist yet; until then the two apps build separately.

Install the toolchain once, from an MSYS2 UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,ffmpeg,SDL2,SDL2_ttf,sqlite3}
```

**Drone Commander**, from the repository root. The first configure fetches and
builds SDL3 from GitHub, which takes a couple of minutes:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./drone_commander.exe
```

**vsynth**, from `apps/video-synth/`:

```sh
cmake -B build -G Ninja
cmake --build build
./build/vsynth.exe
```

From PowerShell, put `C:\msys64\ucrt64\bin` on PATH first so the DLLs are
found. Audio in Drone Commander starts hard-muted; see its README for the
controls.

## CI and releases

Every push and pull request builds and tests Drone Commander on Windows and
publishes a `drone-commander-windows` artifact. Tags matching `v*` create a
GitHub release with the zip. vsynth joins CI in phase 1.
