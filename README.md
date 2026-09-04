# avsynth

A DIY synthesizer lab. Every instrument starts here as a small C program that
tries out a topology, its ranges and a way of playing it. The ones worth
keeping become hardware: machine-assembled SMT boards in small desktop boxes on
USB-C 5 V power, with the analog rails each circuit needs generated on-board.
The software is the sketchbook, not the firmware. [ROADMAP.md](ROADMAP.md) has
the vision, the decisions and the plan.

## Applications

| Application | What it is | Docs |
|---|---|---|
| **Drone Commander** (`apps/drone-commander`) | Three-oscillator audio drone synth with FM cascade, state-variable filter, VCA and three synced square LFOs. The digital sketch for a fully analog hardware build. | [README](apps/drone-commander/README.md) |
| **vsynth** (`apps/video-synth`) | Video feedback synth. Captures a screen region, runs it through a libavfilter chain you write as text, shows the result in a borderless window you drag into the capture region. Controls are derived from the text; presets live in a SQLite project file. | [README](apps/video-synth/README.md), [PRD](apps/video-synth/PRD.md) |

Both are C, both use SDL for window, input and (for audio) the device, both
build with CMake. They were developed separately and merged on 2026-09-04;
unifying their toolchain, SDL version and UI is the current work, see the
roadmap.

## Layout

```text
apps/drone-commander/   audio drone synth (C17, SDL3)
apps/video-synth/       video feedback synth (C17, SDL2, ffmpeg, SQLite)
shared/                 code used by more than one app (empty until phase 3)
assets/screenshots/     screenshots per application
.github/workflows/      CI: Windows build, test, artifacts, tagged releases
ROADMAP.md              vision, decisions, phases
AGENTS.md               rules for agents and contributors
```

## Building

Drone Commander builds with the current MinGW/CMake setup. vsynth additionally
needs MSYS2 UCRT64 packages for FFmpeg, SDL2, SDL2_ttf and SQLite. The root
Makefile exposes both apps without pretending the optional dependencies are
installed everywhere.

Install the toolchain once, from an MSYS2 UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,ffmpeg,SDL2,SDL2_ttf,sqlite3}
```

From the repository root:

```sh
make                 # Drone Commander (default)
make run             # Drone Commander
make test            # offline DSP tests
make build-vsynth    # video synth; optional dependencies required
make run-vsynth
make build-all       # both applications
```

The equivalent direct CMake commands remain documented in each app README.
On Windows, run the vsynth targets from an MSYS2 UCRT64 environment, or put
both `C:\msys64\usr\bin` and `C:\msys64\ucrt64\bin` on `PATH` first.

Drone Commander’s first configure fetches and builds SDL3. Audio starts
hard-muted; see its README for controls and signal-path details.

**vsynth direct build**, from `apps/video-synth/`:

```sh
cmake -B build -G Ninja
cmake --build build
./build/vsynth.exe
```

## CI and releases

Every push and pull request builds both apps on Windows. Drone Commander runs
its offline DSP tests. Successful jobs publish `drone-commander-windows` and
`vsynth-windows` artifacts; tagged releases attach the packaged builds.
