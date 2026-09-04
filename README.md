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

Both are C17 on SDL3, built by one CMake project against system libraries from
pkg-config. Nothing is vendored or downloaded at configure time, so both
machines and CI resolve the same libraries from the same packages.

## Layout

```text
apps/drone-commander/   audio drone synth
apps/video-synth/       video feedback synth
shared/                 code used by more than one app (empty until roadmap phase 3)
tools/package.sh        release packaging: exe plus the DLLs it links
assets/screenshots/     screenshots per application
.github/workflows/      CI: build, test, artifacts, tagged releases
ROADMAP.md              vision, decisions, phases
AGENTS.md               rules for agents and contributors
```

## Toolchain

Install once. **Windows**, from an MSYS2 UCRT64 shell:

```sh
pacman -S make mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,ffmpeg,sdl3,sdl3-ttf,sqlite3}
```

**Arch Linux**:

```sh
sudo pacman -S make gcc cmake ninja pkgconf ffmpeg sdl3 sdl3-ttf sqlite
```

Nothing else is needed. `make` puts the UCRT64 toolchain on PATH itself, so
every command below works from the MSYS2 shell and from PowerShell alike.

## Build and run

```sh
make              # build every app
make run          # run every app
make run-drone    # Drone Commander only
make run-vsynth   # vsynth only
make test         # every test, through ctest
make package      # zip each app with its DLLs into dist/
make help         # the full list
```

Executables land in `build/bin/`. `make clean` removes build products and keeps
the configuration; `make distclean` removes `build/` and `dist/` entirely.
`BUILD_TYPE`, `GENERATOR`, `BUILD_DIR` and `DIST_DIR` can be overridden.

CMake presets are there too, for editors and for anyone who prefers CMake
directly: `cmake --preset ucrt64` (or `linux`, or `release`), then
`cmake --build build`.

Audio in Drone Commander starts hard-muted and takes a deliberate keypress to
enable. See its README for the controls and the signal path. vsynth's README
has the chain syntax and the key map.

## CI and releases

Every push and pull request builds and tests both apps on Windows in the same
MSYS2 UCRT64 environment a developer uses, then publishes an
`avsynth-windows` artifact holding one zip per app. Tags matching `v*` create a
GitHub release with the same zips. Each zip carries the executable, its README
and the DLLs it links, so it runs on a machine with no toolchain installed.
