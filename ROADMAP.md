# avsynth roadmap

*v1, 2026-09-04. The decisions below are made. The phases are ordered, not
scheduled. Update this file when a decision changes or a phase lands.*

## 1. Vision

avsynth is a DIY synthesizer lab. Every instrument starts as a small C program
where a topology, a set of ranges and a way of playing it can be tried out
cheaply. The ones worth keeping become hardware.

The hardware side has its own rules, and they shape what the software side
must record:

- **Boards are made for assembly, not for soldering.** PCBs go to JLCPCB (or a
  similar service) as Gerbers plus BOM and pick-and-place files, with every
  SMT part placed by the machine. Hand soldering is the exception, reserved
  for the panel parts that have no SMT equivalent: pots, jacks, switches.
- **Instruments are small desktop boxes.** An empty ammo crate, a folded sheet
  of aluminium, whatever is at hand. Eurorack is allowed for a module that
  really wants to be one, but it is not the standard and nothing depends on it.
- **Power is a daisy chain**, guitar-pedal style: one 9 V or 12 V DC supply,
  barrel jacks, several boxes on one chain. Each board makes whatever rails it
  needs from that.
- **The analog instruments stay analog.** The digital program is the
  sketchbook: it settles the topology, the ranges, the modulation behaviour and
  how the controls should feel. It is not firmware.

vsynth, the video feedback synth, belongs to the same lab even though it has
no analog counterpart yet. It shares the way of working: a text or a table of
parameters, knobs derived from it, presets, a loop you play by ear (or eye).

## 2. What this repository is for

- One directory per instrument under `apps/`. Each is an independent program
  with its own README and, once it earns hardware, a `HARDWARE.md`.
- Shared infrastructure under `shared/`, so that the second, third and tenth
  bench cost a fraction of the first: parameter model, preset storage, the
  on-screen control surface, later MIDI.
- One toolchain, one set of root commands, one CI that builds and packages
  every app.

## 3. Where things stand (2026-09-04, after P1 and P2)

Two apps in one repository, on one toolchain, one C standard and one windowing
API. What is left to unify is the code itself: the parameter model, preset
storage and the control surface are still per app.

| | Drone Commander | vsynth |
|---|---|---|
| Domain | 3-oscillator audio drone synth | screen region through a libavfilter chain, video feedback |
| Standard | C17 | C17 |
| Window / input | SDL3 from pkg-config | SDL3 + SDL3_ttf from pkg-config |
| Other libraries | none | ffmpeg (avfilter, avformat, avcodec, avdevice, avutil), sqlite3 |
| Parameters | a static table of 25 controls (`panel.c`) mirrored by `SynthParameters` | knobs derived at runtime from the chain text (`rack.c`) |
| Presets / persistence | none | SQLite project file: chains, ten presets each, geometry |
| UI | fixed 1180x760 panel, sliders and switches, SDL debug text | modal sheet with F-key tabs, knob rows, text editor, filter browser, glyph atlas from SDL3_ttf |
| Root build | `make run-drone`, in CI and packaging | `make run-vsynth`, in CI and packaging |

The merge on 2026-09-04 moved files but not build systems: each app had grown
whatever toolchain its own machine happened to have, and CI gated only one of
them, so nothing forced them together. That is the part P1 and P2 fixed, and CI
now fails if they drift again. The remaining divergence is deliberate and
scheduled: P3 and P4.

## 4. Decisions

**D1. One toolchain: MSYS2 UCRT64 on Windows, pacman on Arch, system libraries
through pkg-config.** CMake 3.24+, Ninja, presets in `CMakePresets.json`. No
FetchContent for SDL: vsynth needs system ffmpeg anyway, so a self-contained
build was never going to cover both apps, and fetching SDL3 costs two minutes
per fresh configure. CI runs on `windows-latest` through `msys2/setup-msys2`
and installs the same packages a developer does. Release zips ship the exe
plus the DLLs it links, collected from the toolchain by the package step.

**D2. SDL3 everywhere.** vsynth moves from SDL2 and SDL2_ttf to SDL3 and
SDL3_ttf. One windowing API means one shared UI layer. Drone Commander already
targets SDL3, and MSYS2 and Arch both ship SDL3 and SDL3_ttf.

**D3. Root make targets cover every app.** `make` builds everything, `make
run` starts every app, `make run-drone` and `make run-vsynth` start one, `make
test` runs every test, `make package` produces the zips. Executables land in
`build/bin/`. The Makefile has POSIX sh recipes and runs from the MSYS2 shell,
from PowerShell with `C:\msys64\usr\bin` on PATH, and on Linux.

**D4. Shared code lives in `shared/`, and only code both apps use goes
there.** The first three libraries, in the order they are extracted:

1. `shared/param`: the parameter model. Both apps already have one, hidden in
   a static table and in a derived rack respectively.
2. `shared/store`: presets and projects in SQLite. vsynth's `project.c` is the
   starting point; Drone Commander has no persistence and gets presets from
   this.
3. `shared/ui`: the control surface drawn with SDL3: the sheet, its modes,
   knob rows, text, notices.

Storage was the suggested first cut. Extracting it forces the parameter model
out first, because a preset is a list of parameter values, so param and store
land together.

**D5. C17 for every app and shared library. No C++.** vsynth's move from C11
to C17 is a flag change.

**D6. Hardware handoff is a document, not code.** When an instrument is
chosen for hardware, its app gets a `HARDWARE.md`: the topology as blocks, the
parameter ranges the software settled on, candidate circuits per block, the
power budget, and the parts strategy (see section 7). The software never grows
a firmware target.

## 5. Shared architecture

### 5.1 `shared/param`

Plain C, no SDL, no SQLite, unit-testable offline.

```c
typedef enum { PARAM_NUMBER, PARAM_ENUM, PARAM_SWITCH } ParamKind;

typedef struct Param {
    char        group[32];   /* module or section: "osc1", "rot"            */
    char        key[32];     /* stable id inside the group: "freq", "angle"  */
    char        label[24];   /* what the player reads                        */
    ParamKind   kind;
    double      min, max, step, neutral;
    const char *unit;        /* "Hz", "", or NULL                            */
    const char *const *names;/* enum value names, kind == PARAM_ENUM         */
    int         nnames;
} Param;

typedef struct ParamSet {
    Param  *defs; double *values; int *enabled; int n;
    int     sel;
} ParamSet;
```

Operations both apps implement today and will call instead: select next and
previous, nudge with fine and coarse factors, reset one and reset all,
randomize with a depth, format a value (enum name or number), describe the
selection in one line. The app supplies an `apply(group, key, value)` callback:
Drone Commander writes into `SynthParameters` and publishes it to the audio
thread, vsynth sends an `avfilter_graph_send_command`.

How the current code maps onto it:

| Today | Becomes |
|---|---|
| `Control` rows in `panel.c` (id, label, min, max, step) | `Param` rows, group = oscillator or section |
| `SynthParameters` | stays; it is the DSP engine's view, filled from the ParamSet |
| `KnobDef` in `rack.c` (label, opt, min, max, neutral, step, enum unit) | `Param`, group = filter instance name, key = option name |
| `ModuleDef.bypassable` / `enabled[]` | `PARAM_SWITCH` per module, or the `enabled` array |

### 5.2 `shared/store`

SQLite through the system library. vsynth's schema, generalized so a preset is
keyed by `(group, key)` and an instrument is any app:

```
project      id=1, name, schema_version
instrument   id, app, position, name, definition   -- definition: chain text for vsynth,
                                                   -- empty or a topology name for drone
preset       id, instrument_id, slot 1..10, name
preset_value preset_id, grp, key, value
preset_enable preset_id, grp, enabled
setting      app, key, value                       -- geometry, fps, last instrument
```

The default project file stays per app in the per-user data folder
(`%APPDATA%\avsynth\<app>.db`, `~/.local/share/avsynth/<app>.db`), one file
per app, one schema. vsynth's existing `default.vsynth` files migrate on open.

### 5.3 `shared/ui`

SDL3 only. Owns the sheet (frame, header with F-key tabs and status, footer
hints), modes and the rule that one mode owns the keyboard, knob rows with
bars driven by a ParamSet, text through a glyph atlas (SDL3_ttf, system
monospace font, debug-text fallback), notices, a shared palette. Each app
keeps its own picture: the oscilloscope and VU meter in Drone Commander, the
video and tap thumbnails in vsynth. Apps add modes for what is theirs (chain
editor, filter browser, project view).

### 5.4 Boundaries that do not move

- A DSP engine depends on nothing in `shared/` except `param` definitions, and
  never on SDL, SQLite or ffmpeg.
- The audio callback rules in `AGENTS.md` apply unchanged: no allocation, I/O,
  logging, locking or graph rebuilding on the audio thread.
- `shared/ui` never talks to `shared/store`; the app wires them.

## 6. Phases

Each phase ends in a commit on `main` that leaves both apps building and
their tests passing.

**P0. Decisions and documents. Done 2026-09-04.** This roadmap; a root README
that describes the repository instead of pasting an app's README; repo-wide
`AGENTS.md`.

**P1. One toolchain, one set of root commands. Done 2026-09-04.**
- `CMakePresets.json` with `ucrt64`, `linux` and `release` presets, Ninja.
- Root `CMakeLists.txt` builds both apps unconditionally, sets C17 for
  everything, finds SDL3 and SDL3_ttf once through pkg-config, shares one
  warning set through the `avsynth_flags` interface target, and puts every
  executable in `build/bin/`. Drone Commander's FetchContent of SDL3 is gone.
  SDL3's pkg-config `-mwindows` is stripped so each app picks its subsystem.
- Root Makefile per D3, which prepends `/ucrt64/bin` to PATH so the same
  commands work from PowerShell and from the MSYS2 shell.
- `tools/package.sh` zips each app with the DLLs it links. It reads the import
  table with `objdump`, not `ldd`: ldd resolves by loading the image, so it
  depends on PATH being in a form the Windows loader accepts, which is not
  true when make is started from PowerShell.
- CI on `msys2/setup-msys2` in the UCRT64 environment, building, testing and
  packaging both apps; the release job uploads both zips.
- Verified: `make`, `make help`, `make test` and `make package` from
  PowerShell; the packaged vsynth passes `--selftest` on a PATH holding only
  the Windows system directories, so the bundle is self-contained.

**P2. vsynth on SDL3. Done 2026-09-04.** `window.c`, `hud.c`, `editor.c`,
`help.c`, `picker.c`, `options.c`, `main.c` and `voice.c` moved to SDL3 and
SDL3_ttf. What the port turned on:
- Layout stays in integer `SDL_Rect`; `hud_frect()` converts at the draw call,
  because SDL3 renders in floats.
- Text input needs a window, so it goes through `hud_text_input()`, which asks
  the renderer for its window.
- `SDL_TICKS_PASSED` is gone and ticks are 64-bit: tick fields are `Uint64`
  and comparisons are plain.
- vsync is a renderer property (`SDL_SetRenderVSync`), scale quality is a
  texture property, fullscreen is a bool, displays are enumerated by id, and
  `SDL_RenderReadPixels` returns a new surface.
- Letter keycodes are uppercase (`SDLK_Q`). Digit presets still bind by
  scancode, so AZERTY keeps working.
- Verified: `vsynth --selftest` reports `OK (0 failures)`, covering derived
  knobs, the enum knob, rejection of a broken chain, preset round-trip,
  chain rename and delete, and a voice restart on a new capture size.
  `tools/uitest.ps1` now defaults to `build/bin/vsynth.exe`; it has not been
  re-run since the port.

**P3. `shared/param` and `shared/store`.**
- Extract the parameter model; `rack.c` derives Params from the graph,
  `panel.c` declares Params in its table. Nudge, reset, randomize and format
  come from `shared/param`. Unit tests in `shared/param/tests`.
- Move `project.c` to `shared/store` with the generalized schema and a
  migration for existing project files.
- Drone Commander gets a project file and ten preset slots on the digit row
  (scancodes, so AZERTY works), save with Shift, like vsynth.
- Exit: both apps save and load presets through the same code; vsynth
  selftest still round-trips a preset.

**P4. `shared/ui`.** Move the sheet, modes, knob rows, glyph atlas and
notices out of vsynth's `hud.c`. Drone Commander's panel becomes knob rows in
the sheet plus its own oscilloscope area, with the same keys as vsynth: Tab to
select, arrows to nudge, Space to toggle, F-keys for modes, F12 screenshot.
Exit: both apps look and drive the same, per-app code is only what differs.

**P5. Hardware handoff.** `HARDWARE.md` template and the first one for Drone
Commander: blocks (3 VCO, mixer with soft clip, VCF, VCA, 3 square LFO with
sync), ranges taken from the Param table, candidate circuits, power budget on
a 9 V or 12 V chain, JLCPCB part picks. Also the point where the parts
strategy in section 7 gets tested against a real BOM.

**Later, in no order.** MIDI learn on the Param model (RtMidi or PortMidi;
every Param already has min, max and step, so a 7-bit CC maps with no extra
data), host-side modulators for vsynth, a control surface in a second window,
new benches for new instrument ideas, an Arch build check.

## 7. Hardware constraints to design against

Starting rules, to be corrected by the first real board:

- **Assembly service:** JLCPCB PCBA. Prefer parts from their Basic and
  Preferred libraries to avoid per-part feeder fees; check availability before
  committing a topology to a part.
- **Hand soldering budget:** panel parts only. Pots, jacks and switches are
  through-hole or panel-mount and wired; everything else is SMT on the board.
- **Power:** one DC barrel input per box, 9 V or 12 V, daisy-chainable.
  Pick one polarity convention for the whole lab and protect every board
  against the other one. Bipolar rails, when a circuit wants them, come from
  an on-board charge pump or inverter, never from a second supply.
- **Enclosures:** desktop boxes. Panel layout and board outline are designed
  per box, not per rack standard. A eurorack variant is a different panel and
  power header on the same board, if ever.
- **Signal levels:** decide once (line level for desktop, eurorack level only
  on a eurorack variant) and document it in each `HARDWARE.md`.

## 8. Open questions

To settle with the user before the phase that needs them:

1. Supply voltage and polarity convention for the daisy chain (9 V
   centre-negative like pedals, or 12 V). Needed by P5.
2. Which instrument goes to hardware first. Drone Commander is the obvious
   candidate; needed by P5.
3. Whether the per-app project files should merge into one lab-wide file
   later. Not needed before P3 ships one schema.
4. Whether Drone Commander keeps its fixed panel geometry as a second mode
   after P4, or the sheet replaces it entirely.
