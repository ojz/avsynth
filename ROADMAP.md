# avsynth roadmap

*v3, 2026-09-04. The decisions below are made. The phases are ordered, not
scheduled. Update this file when a decision changes or a phase lands.*

*Changed since v2 (same day): hardware is parked, not cancelled, and the power
question with it. Everything under `apps/` is now called an app. The fader is
the lab's shared control (D8), signal conventions are fixed (D9), and the
apps the lab is heading towards are written down in section 7 so they can be
designed for without being built.*

## 1. Vision

avsynth is a lab of small **apps**. Each one is a C program that tries out a
topology, a set of ranges and a way of playing it. They are meant to be played,
not just run, and eventually to be played together.

The order of work is software first, by a long way. There is a great deal of
experimentation to do before any of it is worth turning into a circuit, so
hardware is parked: section 9 keeps what was already decided about it, and
nothing in the current plan depends on it. The power question in particular is
unsettled and deliberately not being thought about yet.

What every app has in common:

- **Controls are faders** (D8). One control concept, one look, one set of
  gestures, one way of being addressed by a preset or by MIDI, in every app.
- **Signals agree** (D9). Audio and modulation are bipolar, clocks are ramps,
  and connections between apps are named.
- **Nothing is a plugin host.** Each app is its own executable, statically
  reasoned about, started from `make run-<app>`.

## 2. What this repository is for

- One directory per app under `apps/`, an independent program with its own
  README.
- Shared infrastructure under `shared/`, so the second, third and tenth app
  cost a fraction of the first: the fader and parameter model, preset storage,
  the control surface, later MIDI and the inter-app link.
- One toolchain, one set of root commands, one CI that builds and packages
  every app.

## 3. Where things stand (2026-09-04, after P1 and P2)

Two apps, one toolchain, one C standard, one windowing API. What is left to
unify is the code: the parameter model, preset storage and the control surface
are still per app, and the two apps' controls neither look nor behave alike.

| | Drone Commander | vsynth |
|---|---|---|
| Domain | 3-oscillator audio drone synth | screen region through a libavfilter chain, video feedback |
| Standard | C17 | C17 |
| Window / input | SDL3 from pkg-config | SDL3 + SDL3_ttf from pkg-config |
| Other libraries | none | ffmpeg (avfilter, avformat, avcodec, avdevice, avutil), sqlite3 |
| Parameters | a static table of 25 controls (`panel.c`) mirrored by `SynthParameters` | derived at runtime from the chain text (`rack.c`) |
| Presets / persistence | none | SQLite project file: chains, ten presets each, geometry |
| Controls | fixed 1180x760 panel, hand-placed 104 px sliders, absolute drag only | modal sheet, knob rows with a bar, keyboard nudge with fine and coarse |

Drone Commander now meets D8: its controls are faders from `shared/param` and
`shared/ui`, on a grid, with reset, three grains, units and full precision.
vsynth still draws its own knob rows, so the two do not yet look alike. That is
P4.

## 4. Decisions

**D1. One toolchain: MSYS2 UCRT64 on Windows, pacman on Arch, system libraries
through pkg-config.** CMake 3.24+, Ninja, presets in `CMakePresets.json`. No
FetchContent for SDL: vsynth needs system ffmpeg anyway, so a self-contained
build was never going to cover both apps, and fetching SDL3 costs two minutes
per fresh configure. CI runs on `windows-latest` through `msys2/setup-msys2`
and installs the same packages a developer does. Release zips ship the exe
plus the DLLs it links, collected from the toolchain by the package step.

**D2. SDL3 everywhere.** One windowing API means one shared control surface.

**D3. Root make targets cover every app.** `make` builds everything, `make
run` starts every app, `make build-<app>` and `make run-<app>` handle one.
`make test` runs every test, `make package` produces the zips. The per-app
targets select a CMake target inside the one project; they are not second
build trees. Executables land in `build/bin/`.

**D4. Shared code lives in `shared/`, and only code more than one app uses
goes there.** The libraries, in the order they are extracted:

1. `shared/param`: the fader and parameter model (D8). Plain C, no SDL.
2. `shared/ui`: the control surface on SDL3, including how a fader is drawn
   and driven.
3. `shared/store`: presets and projects in SQLite.
4. `shared/link`: the inter-app clock, buses and control plane (D7, D9).

**D5. C17 for every app and shared library. No C++.** The root sets it once,
with compiler extensions on because the apps use POSIX `strdup` and
`strncasecmp`.

**D6. Everything under `apps/` is an app.** Not an instrument, not a synth,
not a tool. Some make sound, some make pictures, some only route or measure.
They are peers, they are addressed by name, and the vocabulary is uniform in
code, docs and UI.

**D7. Apps share a control plane, not a process or audio callback.** The native
link protocol will use OSC-compatible UDP messages over localhost or LAN with
stable paths and monotonic timestamps. It carries transport state, clock
phase, resets and parameter changes. Each app receives packets on a network
thread and forwards bounded events to its real-time engine; no socket call
occurs on an audio callback. MIDI clock and control, PipeWire and JACK are
adapters at the edge, never the internal protocol.

**D8. The fader is the lab's control.** Every continuous parameter in every
app is a fader, specified in section 6. It resets to a neutral, it shows its
value at full precision, it moves coarse, fine and ultra-fine, and it carries
a stable address so a preset, a MIDI CC or a sequencer step can drive it
without the app knowing which. Switches and enum steppers are the only other
control kinds; there are no bespoke widgets.

**D9. Signal conventions, fixed once for the whole lab.**

- **Audio and modulation are bipolar, nominally -1 to +1.** A level or depth
  control that can invert is a bipolar fader with neutral 0. This is why an
  LFO level runs -1 to +1 rather than 0 to 1: negative is the same amount of
  modulation, inverted.
- **Clocks are ramps, not pulses.** A clock carries a phase that runs 0 to 1
  and wraps. An app that wants an edge finds it by detecting the wrap; an app
  that wants position uses the ramp directly. A missed packet costs precision,
  never a beat.
- **Connections are named.** Signals between apps travel on named buses, not
  on device indices, so a patch is a set of names and can be reasoned about
  in text.

**D10. Hardware is parked.** Nothing in the current plan depends on it, no app
is designed around it, and no phase before it. Section 9 keeps what was
already decided so it is not lost. The power topology is explicitly an open
sore and is not to be designed until the software says what the circuit has to
do.

## 5. Shared architecture

### 5.1 `shared/param`

Plain C, no SDL, no SQLite, unit-testable offline. Holds the fader (section 6),
switches, enum steppers, and a set of them with a selection. The app supplies
an apply callback: Drone Commander writes into `SynthParameters` and publishes
it to the audio thread, vsynth sends an `avfilter_graph_send_command`.

How the current code maps onto it:

| Today | Becomes |
|---|---|
| `Control` rows in `panel.c` | fader and switch definitions, grouped by section |
| `SynthParameters` | stays; it is the DSP engine's view, filled from the set |
| `KnobDef` in `rack.c` | a fader, group = filter instance name, key = option name |
| `ModuleDef.bypassable` / `enabled[]` | a switch per module |

### 5.2 `shared/ui`

SDL3 only. Owns the sheet (frame, header, footer), modes and the rule that one
mode owns the keyboard, the fader widget in both orientations, text through a
glyph atlas, notices, and one palette. Each app keeps its own picture: the
oscilloscope and meter in Drone Commander, the video and taps in vsynth.

### 5.3 `shared/store`

SQLite. vsynth's schema generalized so a preset is keyed by `(group, key)`,
which is the fader's address, and an app is any app:

```
project      id=1, name, schema_version
app_state    app, key, value                     -- geometry, fps, last patch
patch        id, app, position, name, definition -- chain text, or a topology name
preset       id, patch_id, slot 1..10, name
preset_value preset_id, grp, key, value
preset_flag  preset_id, grp, key, on
```

### 5.4 Boundaries that do not move

- A DSP engine depends on nothing in `shared/` except parameter definitions,
  and never on SDL, SQLite or ffmpeg.
- The audio callback rules in `AGENTS.md` apply unchanged.
- `shared/ui` never talks to `shared/store`; the app wires them.

## 6. The fader

One control, shared by every app. This section is the specification; `shared/param`
and `shared/ui` implement it and nothing else defines a continuous control.

**Identity.** A fader is addressed by `group` and `key`, both stable strings.
That address is what a preset row, a MIDI binding and a sequencer target all
use, so none of them needs to know the app's internals or the fader's screen
position.

**Value.** `min`, `max`, and a `neutral`. The neutral is what reset returns to:
the value that makes the control stop acting where there is one, such as a
bipolar depth's zero, and the patch default where there is not, such as a
frequency. A bipolar fader's bar fills outward from its neutral so an inverted
setting reads at a glance; every other fader fills from its minimum and marks
the neutral with a tick, because filling a frequency outward from 110 Hz would
read as nonsense.

**Taper.** How the track position maps to the value.

- *Linear* for levels, depths and anything already perceptually even.
- *Exponential* for frequencies, so a track is useful at both ends. This is
  what makes a 20 Hz to 12 kHz cutoff dialable instead of a cliff.
- *Bipolar* for signed controls: symmetric about the neutral, with the two
  halves tapered independently so both directions feel the same.

**Movement, three grains.** Every fader has a coarse, a fine and an ultra-fine
step, defaulted from the range to round numbers and overridable per fader.
The gestures are the same everywhere:

| Gesture | Effect |
|---|---|
| Drag the track | absolute, follows the pointer, the coarse grain |
| Wheel | one fine step |
| Ctrl + wheel | one coarse step |
| Shift + wheel | one ultra-fine step |
| Middle click, or double click | reset to neutral |
| Arrows, when selected | one fine step; Ctrl coarse, Shift ultra-fine |
| Backspace, when selected | reset to neutral |

**Readout.** The value is always visible, with its unit, at a precision derived
from the ultra-fine step, so the smallest possible movement is always visible
in the number. No fader ever shows fewer digits than it can be moved by.

**Designed for, not built yet.** The address plus min, max and taper is
everything a 7-bit MIDI CC or a 14-bit NRPN needs to map onto a fader, and
everything a sequencer step needs to write into one. A fader does not know
whether a human, a CC or a step moved it.

## 7. Apps the lab is heading towards

Direction, not a work queue. Written down because the shared pieces have to be
designed with these in mind, and because an earlier SuperCollider version of
this lab was lost. None of them is scheduled.

- **MONITOR.** The only app that talks to the speakers. A level fader, a
  visualizer, and recording to file. Everything else outputs to a named bus
  and is silent until MONITOR is running. This is what makes named buses
  (D9) worth having and keeps output level in exactly one place.
- **CLOCK.** A master BPM with derived faster and slower ramps, each a named
  bus carrying phase 0 to 1 per D9. Tempo can be nudged while running so it
  can be synced up by ear against something already playing. No pulses.
- **SEQUENCER.** A row of vertical faders, each a bipolar value, which is a
  window into a buffer rather than a fixed number of steps: the count can be
  doubled or halved. Takes a clock ramp in and outputs the fader under the
  playhead. Because the output is just a signal, the same app is a step
  sequencer for a cutoff at slow rates and a wavetable or sample player at
  audio rates. This is the strongest argument for the fader being one shared,
  addressable, MIDI-mappable thing rather than per-app widgets.
- **A filter app** in the spirit of the parts of a Sherman Filterbank that
  actually got used: gain into a high-pass into a low-pass. No amplitude
  envelope, no cutoff envelope.
- **A kick drum app.** Crude on purpose.

Why not SuperCollider, where much of this existed before: deployment is a
single static executable here, the result is stable, and writing the DSP
directly is now practical.

## 8. Phases

Each phase ends in a commit on `main` that leaves every app building and its
tests passing.

**P0. Decisions and documents. Done 2026-09-04.**

**P1. One toolchain, one set of root commands. Done 2026-09-04.**
- `CMakePresets.json`, one root CMake project building both apps, C17 and SDL3
  from pkg-config for everything, executables in `build/bin/`.
- Root Makefile per D3, prepending `/ucrt64/bin` to PATH so the same commands
  work from PowerShell and the MSYS2 shell.
- `tools/package.sh` zips each app with the DLLs it links, read from the
  import table with `objdump` rather than `ldd`.
- CI on `msys2/setup-msys2`, building, testing and packaging both apps.

**P2. vsynth on SDL3. Done 2026-09-04.** Integer `SDL_Rect` layout converted at
the draw call, text input through the renderer's window, `Uint64` ticks,
per-texture scale mode, bool returns, uppercase letter keycodes with preset
digits still by scancode. `vsynth --selftest` reports OK with 0 failures.
A later pass made the hit-testing pixel-density independent.

**P3. The fader, and Drone Commander redesigned on it. Done 2026-09-04.**
- `shared/param`: the fader per section 6, plus switches and enum steppers.
  `param_tests` covers taper round-trips, the geometric midpoint of the
  exponential taper, bipolar symmetry, snapping onto the neutral, the three
  grains, clamping, NaN, readout precision and the set operations. It runs
  under `make test` with no window and no audio device.
- `shared/ui`: the fader on SDL3, horizontal, with the vertical orientation
  implemented for the sequencer to come, and the gesture table in one place.
  Text arrives through a `UiText` callback so the apps need not agree on a font.
- Drone Commander: LFO level is bipolar -1 to +1 per D9, and the summing now
  weights by magnitude, which it had to before a negative level was reachable.
  The panel is on a two-row grid with one accent colour; every continuous
  control is a fader with a unit, a neutral tick and an exponential taper where
  it is a frequency. `assets/screenshots/drone-commander.png` is the result.
- The old panel's specific faults, for the record: the FM cascade slider
  crossed the oscillator section border, the anti-alias label overflowed it,
  five sections each had their own title colour, the VCA and output sections
  held one control apiece in a mostly empty box, and a 104 px track spanning
  20 Hz to 12 kHz put 115 Hz in a pixel with no reset and no fine grain.

**P4. vsynth on the shared fader.** `rack.c` produces faders instead of
`KnobDef`s and `hud.c` draws them with `shared/ui`, so both apps' controls
look and behave identically. Exit: the two apps side by side are recognisably
the same instrument family, and `--selftest` still passes.

**P5. `shared/store`.** vsynth's `project.c` becomes `shared/store` with the
generalized schema keyed on the fader address, plus a migration for existing
project files. Drone Commander gains a project file and ten preset slots on
the digit row by scancode. Exit: both apps save and load presets through the
same code.

**P6. `shared/link`, the clock and buses.** Define the OSC-compatible message
schema and the monotonic timestamp model, then a UDP control thread and
bounded event queue. Clocks are ramps per D9. First acceptance test: two apps
share transport and resets without phase depending on packet arrival jitter.

**Later, in no order.** MIDI learn onto fader addresses; the apps in section 7;
host-side modulators for vsynth; a control surface in a second window; an Arch
build check.

## 9. Parked: hardware

Kept so it is not lost. Nothing depends on it and no phase before P6 touches
it. Revisit only when the software has settled what a circuit would have to do.

The intent was: boards made for assembly rather than soldering, sent to a PCBA
service with every SMT part machine-placed and hand soldering reserved for
panel parts; small desktop boxes rather than a rack standard; and the analog
versions staying fully analog, with the software as the sketchbook that settles
topology, ranges and feel but never becoming firmware.

**Power is unresolved and disliked.** A USB-C 5 V input with analog rails made
on board was the last idea, and it is not settled. Do not design it, do not
propose a topology, and do not let an app's design depend on it.

## 10. Open questions

To settle before the phase that needs them:

1. Whether Drone Commander keeps a spatially arranged panel after P3 or moves
   to vsynth's row-based sheet. P3 assumes it keeps a panel, because the
   oscilloscope and the section grouping carry meaning that rows would lose.
2. Whether per-app project files merge into one lab-wide file. Not needed
   before P5 ships one schema.
3. What the sequencer's buffer window means at audio rate: sample count per
   fader, interpolation between faders, and where a buffer comes from.
   Needed before section 7's SEQUENCER, not before.
