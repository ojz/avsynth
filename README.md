# avsynth

A DIY synthesizer lab. Every instrument starts here as a small C program that
tries out a topology, its ranges and a way of playing it. An app is a unit that
could plausibly be one box one day, and the ones worth keeping eventually
become hardware — but that is parked, deliberately and for a long time, because
the expensive part of an analog synth is knowing what the circuit has to do,
and that is what playing these apps produces. The software is the sketchbook,
not the firmware. [ROADMAP.md](ROADMAP.md) has the vision, the decisions and
the plan.

## Applications

| Application | What it is | Docs |
|---|---|---|
| **Drone Commander** (`apps/drone-commander`) | Three-oscillator audio drone synth with pulse width, FM cascade, state-variable filter, VCA and three square LFOs, the second and third able to hard-sync to the one before. The digital sketch for a fully analog hardware build. | [README](apps/drone-commander/README.md) |
| **vsynth** (`apps/video-synth`) | Video feedback synth. Captures a screen region, runs it through a libavfilter chain you write as text, shows the result in a borderless window you drag into the capture region. Controls are derived from the text; presets live in a SQLite project file. | [README](apps/video-synth/README.md), [PRD](apps/video-synth/PRD.md) |

Both are C17 on SDL3, built by one CMake project against system libraries from
pkg-config. Nothing is vendored or downloaded at configure time, so both
machines and CI resolve the same libraries from the same packages.

Every continuous control in the lab is meant to be a **fader**: it resets to a
neutral, shows its value at full precision with its unit, and moves at three
grains so a value can actually be dialled in. It carries a stable address, so a
preset, a MIDI CC or a sequencer step can drive it without the app knowing
which. That is the lab's shared control language, specified in
[ROADMAP.md](ROADMAP.md) section 6 and implemented once in `shared/param` and
`shared/ui`. Drone Commander runs on it today. vsynth still draws its own knob
rows and moves onto the fader in phase P4, which is the work in progress.

## Where this is going

The lab will run in **one process, hosted by a launcher** (ROADMAP D11). Each
app stays its own program in its own folder with its own window — start it,
kill it, run two of it — but they share an address space so signals can travel
between them on named buses at audio rate as well as control rate. That is what
makes a sequencer that is also a wavetable player possible, and it is why one
app (MONITOR) will own the only connection to the speakers while every other
app writes to a bus.

None of that exists yet. The next steps are vsynth on the shared fader (P4),
one shared app shell and one typeface (P5), then the launcher and the buses
(P6). Phases are in ROADMAP section 8.

## Layout

```text
apps/drone-commander/   audio drone synth
apps/video-synth/       video feedback synth; PRD.md there is its design
shared/                 code used by more than one app; its README lists the libraries
shared/param/           the fader and parameter model, plain C, tested offline
shared/ui/              the control surface on SDL3: how a fader looks and behaves, one palette
shared/app/             the app shell: SDL boot, frame loop, gesture dispatch (planned, P5)
shared/bus/             named in-process signal buses, audio and control rate (planned, P6)
launcher/               hosts the apps in one process (planned, P6)
tools/package.sh        release packaging: exe plus the DLLs it links
assets/fonts/           the lab's typeface (planned, P5)
assets/screenshots/     screenshots; Drone Commander so far
.github/workflows/      CI: build, test, artifacts, tagged releases
CMakeLists.txt          the one CMake project; CMakePresets.json for editors
Makefile                the root commands, puts the toolchain on PATH itself
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
make              # build every app (also: build, build-all)
make run          # run every app
make build-drone  # build Drone Commander only
make build-vsynth # build vsynth only
make run-drone    # run Drone Commander only
make run-vsynth   # run vsynth only
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
MSYS2 UCRT64 environment a developer uses, then publishes
`drone-commander-windows` and `vsynth-windows` artifacts. Tags matching `v*`
create a GitHub release with the same zips. Each zip carries the executable,
its README and the DLLs it links, so it runs on a machine with no toolchain
installed. Both apps come out of one job on purpose: they share the toolchain
install and are built together, so a divergence fails the build.
