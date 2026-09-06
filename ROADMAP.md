# avsynth roadmap

*v4, 2026-09-05. The decisions below are made. The phases are ordered, not
scheduled. Update this file when a decision changes or a phase lands.*

*Changed since v3: **the lab runs in one process, hosted by a launcher** (D11),
which amends D7 — the OSC/UDP control plane is demoted from "how apps talk" to
"how the lab talks to the outside". An app's state must live in a struct the
app owns (D12). The lab has one deliberate visual identity, and the user
directs it (D13). There is one shell, `shared/app` (D14). Phases are
renumbered: the shell (P5) and the launcher (P6) come before new apps, and
`shared/store` and `shared/link` move to P8 and P9.*

## 1. Vision

avsynth is a lab of small **apps**. Each one is a C program that tries out a
topology, a set of ranges and a way of playing it. They are meant to be played,
not just run, and to be played together.

An app is a **unit that could plausibly be one box one day**. That constraint
is doing real work even while hardware is parked: it is what stops an app from
growing a tab bar and quietly becoming a suite. If it would need two front
panels, it is two apps.

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
- **One shell** (D14). Window, event loop, gesture dispatch, text and palette
  come from `shared/app`. An app supplies its parameters and its picture.
- **State is owned** (D12). Everything an app knows lives in a struct it owns,
  so the launcher can create it, destroy it, and run two of it.

## 2. What this repository is for

- One directory per app under `apps/`, an independent program with its own
  README.
- Shared infrastructure under `shared/`, so the second, third and tenth app
  cost a fraction of the first: the fader and parameter model, the shell, the
  in-process buses, preset storage, and later MIDI and the external link.
- `launcher/`, the one program that is not an app: it hosts them.
- One toolchain, one set of root commands, one CI that builds and packages
  every app.

## 3. Where things stand (2026-09-06, after P4)

Two apps, one toolchain, one C standard, one windowing API, and one control
concept implemented once and used twice. About 6,700 lines, of which `shared/`
is 678.

| | Drone Commander | vsynth |
|---|---|---|
| Domain | 3-oscillator audio drone synth | screen region through a libavfilter chain, video feedback |
| Standard | C17 | C17 |
| Window / input | SDL3 from pkg-config | SDL3 + SDL3_ttf from pkg-config |
| Other libraries | none | ffmpeg (avfilter, avformat, avcodec, avdevice, avutil), sqlite3 |
| Parameters | `shared/param`, a static table of definitions | derived at runtime from the chain text (`rack.c`) |
| Presets / persistence | none | SQLite project file: chains, ten presets each, geometry |
| Controls | `shared/param` + `shared/ui` faders on a grid | `shared/param` + `shared/ui` faders, one per row, derived by `rack.c` |
| Text | `SDL_RenderDebugText`, 8 px | a glyph atlas over a hunted system font |

What is honestly not done yet, and why it blocks cranking out apps:

1. ~~**`shared/ui` has exactly one consumer.**~~ Resolved by P4: vsynth runs on
   the fader with its parameters derived at runtime, in a row form the library
   gained for it. Two consumers, two layouts, one gesture table.
2. ~~**There is no shell.**~~ Resolved by P5: `shared/app` owns SDL boot, the
   window, the frame loop, the event pump and the one gesture table. Neither
   app has an event switch for a fader any more; each is a struct plus an
   `AppSpec`, and its executable is a one-line stub.
3. **Text is a placeholder.** Half resolved: the shell renders one typeface
   through one atlas for both apps, from `assets/fonts/` when a face is there.
   No face is there yet, so a system monospace stands in. Choosing it is the
   user's (D13); it is the open half of P5.
4. ~~**Both apps keep mutable file-scope state.**~~ Resolved by P5: the panel's
   arrays live in `PanelState`, vsynth's `App` is allocated by `create()`, the
   rack's scratch is on the heap. Two documented exceptions remain in vsynth,
   both library-global by nature: libavfilter's log callback (`graph.c`) and
   libavdevice registration (`voice.c`).

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
run` starts the launcher, `make build-<app>` and `make run-<app>` handle one.
`make test` runs every test, `make package` produces the zips. The per-app
targets select a CMake target inside the one project; they are not second
build trees. Executables land in `build/bin/`.

**D4. Shared code lives in `shared/`, and only code more than one app uses
goes there.** The libraries, in the order they are extracted:

1. `shared/param`: the fader and parameter model (D8). Plain C, no SDL. *Done.*
2. `shared/ui`: how a fader is drawn and driven, on SDL3. *Done, two consumers.*
3. `shared/app`: the shell (D14). Window, frame loop, gesture dispatch, text,
   palette, and the app ABI the launcher later loads. *Done, both apps on it;
   the typeface file itself is still to be chosen.*
4. `shared/bus`: named in-process signal buses (D11, D9). Lock-free rings.
5. `shared/store`: presets and projects in SQLite, including generative ones.
6. `shared/link`: the external adapter — OSC/UDP to another machine, and MIDI.

**D5. C17 for every app and shared library. No C++.** The root sets it once,
with compiler extensions on because the apps use POSIX `strdup` and
`strncasecmp`.

**D6. Everything under `apps/` is an app.** Not an instrument, not a synth,
not a tool. Some make sound, some make pictures, some only route or measure.
They are peers, they are addressed by name, and the vocabulary is uniform in
code, docs and UI. `launcher/` is the one program that is not an app.

**D7. Apps do not share an audio callback, and no app is a plugin host.**
*Amended by D11: they do share a process.* What survives unchanged is the
real-time discipline. Each app owns its own engine state. Only MONITOR opens a
playback device (section 7); every other app writes to a named bus. No socket
call, no allocation and no lock that can block ever occurs on an audio
callback, whatever process the app is running in.

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
  that wants position uses the ramp directly. A missed sample costs precision,
  never a beat.
- **Connections are named.** Signals between apps travel on named buses, not
  on device indices, so a patch is a set of names and can be reasoned about
  in text.

**D10. Hardware is parked.** Nothing in the current plan depends on it, no app
is designed around it, and no phase touches it. Section 9 keeps what was
already decided so it is not lost. The power topology is explicitly an open
sore and is not to be designed until the software says what the circuit has to
do.

**D11. The lab runs in one process, hosted by a launcher.** This amends D7.

*Why.* Section 7's SEQUENCER is meant to be one app that is a step sequencer
at slow rates and a wavetable or sample player at audio rates — the same fader
row, read faster. That is the strongest idea in the lab and **it cannot work
over UDP**: an audio-rate signal between two apps needs shared memory, not
packets. MONITOR summing every other app's output has the same requirement.
The moment those two apps exist, an out-of-process design has to be undone.

*Shape.*

- Each app builds twice from the same sources: a **shared library** exporting a
  small ABI (create, event, frame, audio, destroy), and a **thin executable
  stub** that hosts exactly one instance. `make run-drone` keeps working and
  still opens one window.
- The launcher loads app libraries, runs **one SDL event loop**, and dispatches
  by window ID. Every app keeps its own window, its own title bar and its own
  everything. To the user they remain separate programs; the launcher is a
  thing that starts them.
- An app can be started, killed and **started more than once**. Two PINGs and
  three SEQUENCERs is a normal patch, which named UDP addressing would have
  needed a whole instance-naming scheme to express.
- Signals travel on named in-process buses (`shared/bus`), lock-free rings, one
  writer and many readers, carrying audio-rate and control-rate signals alike
  under D9's conventions.
- MONITOR owns the only playback device and sums the buses in its callback.

*Costs, accepted knowingly.* One crash domain: a faulting app takes down the
lab. Acceptable for a one-user lab of small programs, and the exe stub means
any app can still be run alone to isolate it. And every app must obey D12.

*What this does not change.* Apps stay in their own folders, keep their own
READMEs, and are still reasoned about one at a time. Nothing becomes a plugin
format, nothing gets a tab bar, and there is no host window that owns the
apps' pixels.

**D12. An app's state lives in a struct the app owns.** No mutable file-scope
state — no `static` arrays, caches, layout flags or RNG seeds outside the
instance. The launcher creates and destroys instances at will and may run two
of the same app, which must share nothing. Constant tables (`static const`)
are fine and encouraged.

**D13. The lab has one visual identity, and the user directs it.** One
typeface, shipped in `assets/fonts/` and loaded from there rather than hunted
from system paths, at one set of sizes. One palette, in `shared/ui`. Apps do
not each choose a look; an app's own picture (an oscilloscope, a video frame,
a phase bar) is its own, but the chrome around it is the lab's.

Generative and ornamental elements in the interface are wanted, not tolerated:
they are part of what the lab is for. They come from a shared vocabulary in
`shared/ui` so that ten apps look like one family. **The aesthetic direction is
the user's to give and is not to be invented by an agent** — propose options,
show them, and let the user choose.

**D14. One shell: `shared/app`.** SDL init, window creation, the frame loop,
the event pump, the D8 gesture dispatcher, the text callback and the palette
live in exactly one place. An app supplies its parameter definitions, its
layout, its picture and its engine. An app never writes its own event switch
for faders, because a hand-transcribed gesture table is how the one gesture
table stops being one.

## 5. Shared architecture

### 5.1 `shared/param`

Plain C, no SDL, no SQLite, unit-testable offline. Holds the fader (section 6),
switches, enum steppers, and a set of them with a selection. The app supplies
an apply callback: Drone Commander writes into `SynthParameters` and publishes
it to the audio thread, vsynth sends an `avfilter_graph_send_command`.

### 5.2 `shared/ui`

SDL3 only. Owns the fader widget in both orientations, the stepper, the panel
frame, text through a `UiText` callback, notices, and one palette (D13). Each
app keeps its own picture: the oscilloscope and meter in Drone Commander, the
video and taps in vsynth.

### 5.3 `shared/app`

The shell (D14), and the seam the launcher needs. An app is a struct plus a
table of functions:

```c
typedef struct AppSpec {
    const char *name, *title;                /* "vsynth": data folder, log prefix */
    int   window_w, window_h;
    SDL_WindowFlags window_flags;            /* e.g. SDL_WINDOW_BORDERLESS */
    SDL_InitFlags   init_flags;              /* e.g. SDL_INIT_AUDIO */
    void *(*create)(AppHost *host, int argc, char **argv);  /* the instance, or NULL */
    void  (*destroy)(void *self);
    bool  (*event)(void *self, const SDL_Event *ev);    /* see the order below */
    void  (*tick)(void *self);                          /* per loop, before frame */
    void  (*frame)(void *self, SDL_Renderer *r);        /* the whole window */
    ParamSet        *(*params)(void *self);             /* the shell drives these */
    const UiSurface *(*surface)(void *self);            /* the laid-out controls */
    void  (*changed)(void *self, int param);            /* a value or the selection moved */
} AppSpec;
```

The shell owns the gesture dispatch. The app lays out its controls into a
`UiSurface` (which parameter, which box, which fader geometry) and the shell
hit-tests it: drag sets, wheel nudges by `ui_grain`, middle or double click
resets, a click on a switch or enum steps it, Tab selects, arrows nudge,
Backspace resets. After any of these it calls `changed`. The event order is
fixed: **mouse events go to the shell first** and only a hit on a control is
consumed, so an app can still use the rest of the window (vsynth's borderless
drag and its module-name column); **keyboard events go to the app first**, so a
mode that owns the keyboard (vsynth's chain editor) sees every key, and only
what the app declines reaches the shell. Mouse events reach the app already in
renderer coordinates.

The shell also owns what every app wants and none should write twice: the
typeface (one glyph atlas, from `assets/fonts/`), `--screenshot FILE.bmp` and
`F12` with `--shots DIR`, stripped from argv before the app sees it. `AppHost`
is how an app reaches its window, its renderer, the lab's text and its data
folder, and later its buses and its store — supplied by the exe stub when run
alone, by the launcher when hosted. The `audio` callback in the first sketch
is gone: an app that makes sound writes to a bus (P6), and only MONITOR opens
a device.

### 5.4 `shared/bus`

Named signal buses in one process (D11). A single-writer, many-reader ring per
bus, published by name, carrying float samples under D9's conventions. Control
rate and audio rate are the same mechanism at different block sizes; a clock is
a bus carrying a phase ramp. No locks on the reader or the writer path.

### 5.5 `shared/store`

SQLite. vsynth's schema generalized so a preset is keyed by `(group, key)`,
which is the fader's address, and an app is any app:

```
project      id=1, name, schema_version
app_state    app, key, value                     -- geometry, fps, last patch
patch        id, app, position, name, definition -- chain text, or a topology name
preset       id, patch_id, slot 1..10, name
preset_value preset_id, grp, key, value
preset_flag  preset_id, grp, key, on
generator    preset_id, kind, seed, constraint   -- section 7.1
```

### 5.6 `shared/link`

Demoted by D11 from the internal protocol to the **external adapter**: an
OSC-compatible UDP surface so a second machine, a phone or another program can
drive fader addresses and follow the clock, plus MIDI clock and CC. Messages
carry monotonic timestamps and stable addresses. Socket I/O runs on its own
thread and publishes bounded events; it never touches an audio callback.

### 5.7 Boundaries that do not move

- A DSP engine depends on nothing except `shared/param` definitions, and never
  on SDL, SQLite, ffmpeg or `shared/ui`.
- The audio callback rules in `AGENTS.md` apply unchanged, in-process or not.
- `shared/ui` never talks to `shared/store`; the shell wires them.
- The launcher never draws inside an app's window.

## 6. The fader

One control, shared by every app. This section is the specification;
`shared/param` and `shared/ui` implement it and nothing else defines a
continuous control.

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

Direction, not a work queue, except where a phase names one. Written down
because the shared pieces have to be designed with these in mind, and because
an earlier SuperCollider version of this lab was lost.

- **MONITOR.** The only app that talks to the speakers. A level fader, a
  visualizer, and recording to file. Everything else outputs to a named bus
  and is silent until MONITOR is running. This is what makes named buses
  (D9, D11) worth having and keeps output level in exactly one place.
- **CLOCK.** A master BPM with derived faster and slower ramps, each a named
  bus carrying phase 0 to 1 per D9. Tempo can be nudged while running so it
  can be synced up by ear against something already playing. No pulses.
- **SEQUENCER.** A row of vertical faders, each a bipolar value, which is a
  window into a buffer rather than a fixed number of steps: the count can be
  doubled or halved. Takes a clock ramp in and outputs the fader under the
  playhead. Because the output is just a signal, the same app is a step
  sequencer for a cutoff at slow rates and a wavetable or sample player at
  audio rates. This is the strongest argument both for the fader being one
  shared addressable thing (D8) and for the lab being one process (D11).
- **A filter app** in the spirit of the parts of a Sherman Filterbank that
  actually got used: gain into a high-pass into a low-pass. No amplitude
  envelope, no cutoff envelope.
- **SHELVES.** A low-pass, then a bank of band-passes at harmonically related
  frequencies, then a high-pass, with the band-passes' bandwidth as the main
  control. Named after window louvres: turning the plates lets in more or less
  light, and here more or less of the spectrum. A resonator bank whose one
  gesture opens and closes the whole thing.
- **PING.** Several low-frequency square waves into a resonant band-pass, which
  pings. A technique the user also patches on their modular, and it needs the
  squares' phase and pulse width, not just their rate. This is the app that
  most wants generative presets, below.
- **PROBE.** Shows the camera, takes an x and a y in, and outputs the
  luminosity of that pixel. A sequencer whose pattern is a physical object:
  draw on paper, point the camera at it, and read the drawing as a signal.
  Wants vsynth's capture path and the fader's address model, and nothing else.
- **MOSAIC.** Cut a familiar record into micro-fragments, cut a live input into
  micro-fragments, and reconstruct the input out of the record's pieces.
  Concatenative resynthesis. The one idea here that never got built before.
- **A kick drum app.** Crude on purpose.

### 7.1 Generative presets

The most portable idea from the lost version, and it belongs in the shared
layer rather than in one app. A preset was not a set of stored values but an
**algorithm that produced values**, with different algorithms embodying
different constraints. One kept the base frequencies harmonically related, so
every roll of the dice was in tune with itself; another was free.

That is a different thing from randomising a knob, and it generalises: a fader
already carries range, taper, neutral and a stable address, which is most of
what a constrained generator needs. vsynth's `x` and `X` randomize are the
crude seed of it. `shared/store` (P8) holds a generator alongside stored values
rather than being retrofitted for it later, because this is the answer to
"presets are boring".

### 7.2 What this tells the software

These describe the kind of sound and the kind of experimentation the lab is
for. Read together they say: drones and resonance rather than beats and songs;
slow signals that modulate other signals; physical and visual inputs treated as
control voltage; constrained randomness preferred over recall; and every
parameter reachable while the thing is running. Drone Commander and vsynth
already represent that fairly well, which is why they are the two that exist.

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
- Oscillators gained **pulse width**: duty 0.02 to 0.98, neutral 0.5, with the
  PolyBLEP falling edge moved to the duty rather than fixed at half a cycle.
  The DC offset it introduces (2 x duty - 1) is left in, because an analog VCO
  has it too and it biases the saturation stage asymmetrically, which is part
  of what pulse width sounds like. `dsp_tests` measures the duty directly.
  No phase control: a free-running drone oscillator's absolute phase is
  inaudible, and phase only becomes a control when something retriggers it,
  which is PING's problem in section 7, not this app's.
- The old panel's specific faults, for the record: the FM cascade slider
  crossed the oscillator section border, the anti-alias label overflowed it,
  five sections each had their own title colour, the VCA and output sections
  held one control apiece in a mostly empty box, and a 104 px track spanning
  20 Hz to 12 kHz put 115 Hz in a pixel with no reset and no fine grain.

**P4. vsynth on the shared fader. Done 2026-09-06.** `rack.c` produces `Param`s
and `hud.c` draws them with `shared/ui`, so both apps' controls look and behave
identically. This was the test of the abstraction: vsynth's parameters are
discovered at runtime from the chain text. What the test found:
- The fader model held. Address, range, neutral, taper and the three grains
  all had a natural source in the chain text: the written value is the
  neutral, a range crossing zero is bipolar, an integer option has no grain
  below one.
- The *layout* needed a second form. A list of controls of unknown length
  cannot use the three-line cell Drone Commander's panel uses, so `shared/ui`
  gained `ui_fader_layout_row` and `ui_stepper_draw_row`: the same drawing
  and the same gestures on one line. Two forms, one fader.
- An enum's fader value is an index into its names; libavfilter wants the
  constant. The rack converts at the command and at the preset file, so
  project files written before the fader still load.
- A struct holding a `ParamSet` holds pointers into itself, so it cannot be
  copied by assignment. `rack_adopt()` relinks. This is an argument for D12
  and P5: state an app owns on the heap, handed around by pointer, not by
  value.
- Also done here because the file was open: `rack.c`'s `rng_state` moved
  into the `Rack` (D12), and vsynth's palette took the shared theme's values.

**P5. `shared/app`: the shell, the identity, and owned state.** The phase that
makes a new app cheap. **Shell and owned state done 2026-09-06; the typeface
and the identity are open, waiting on the user (D13).**
- ~~Extract the SDL boot, the frame loop, the event pump and the D8 gesture
  dispatcher out of `panel.c` and `hud.c` into one shell (D14), behind the
  `AppSpec` of section 5.3. Both existing apps move onto it.~~ Done. Each app
  is now a library plus a one-line exe stub, which is the shape P6 needs.
  Drone Commander's spec is about 200 lines; vsynth's is its old `main.c`
  reorganised into create, event, tick and frame, the same length with the
  window, screenshot and fader-key code gone and the shell plumbing added.
- One typeface, chosen by the user, shipped in `assets/fonts/`. The mechanism
  is done: the shell loads the first `.ttf` or `.otf` it finds there (or in
  `fonts/` next to a packaged exe), renders one atlas, and hands the same
  `UiText` to `shared/ui` and to both apps; the package step ships the folder.
  The file is not: a system monospace stands in and the log says so. One
  palette is in `shared/ui` and both apps use its values. A first pass at the
  lab's visual identity, presented as options (D13), is the remaining work.
- ~~Kill mutable file-scope state in both apps (D12).~~ Done, with two
  documented exceptions that are library-global by nature (`graph.c`'s log
  capture, `voice.c`'s device registration).
- *Exit:* a new app reaches a first window with working faders in roughly 200
  lines of its own code, and neither existing app writes an event switch for a
  fader. The second half holds today; the first is to be proven by MONITOR in
  P6.

**P6. The launcher and `shared/bus`.** D11 made real.
- Each app builds as a library plus a thin exe stub; `make run-<app>` is
  unchanged from the user's side.
- `launcher/`: loads app libraries, one SDL event loop dispatching by window
  ID, start and kill, and more than one instance of an app.
- `shared/bus`: named lock-free rings, audio rate and control rate.
- MONITOR is written here as the proof, because it is the app that needs it:
  it owns the only playback device. Drone Commander stops opening one and
  writes to a named bus instead.
- *Exit:* Drone Commander and MONITOR in the launcher, sound coming out of
  MONITOR, either killable without touching the other, and two Drone Commanders
  at once making two independent drones.

**P7. The first apps cranked out.** CLOCK, SEQUENCER, and the Sherman-style
filter, in that order — CLOCK because it defines what a clock bus carries,
SEQUENCER because it is the app the whole design was bent around, the filter
because it is the first one whose value is purely in how it sounds. Each is
built on the finished shell and judged by playing it, not by a test.
*Exit:* a patch worth recording, made of at least four apps.

**P8. `shared/store`.** vsynth's `project.c` becomes `shared/store` with the
generalized schema keyed on the fader address, plus a migration for existing
project files. Every app gains a project file and ten preset slots on the digit
row. Generative presets (7.1) land here, not later: a preset slot may hold a
generator instead of values. *Exit:* every app saves and recalls through the
same code, and at least one app has a constrained generator worth rolling.

**P9. `shared/link`, the outside world.** OSC-compatible UDP for a second
machine, MIDI clock in, and MIDI learn onto fader addresses. Followers run
their own clocks and correct phase from timestamped updates. *Exit:* a MIDI
controller moves a fader in any app without that app knowing about MIDI.

**Later, in no order.** The remaining apps in section 7; host-side modulators
for vsynth; a control surface in a second window; an Arch build check;
recording to file in MONITOR.

## 9. Parked: hardware

Kept so it is not lost. Nothing depends on it and no phase touches it. Revisit
only when the software has settled what a circuit would have to do.

The intent was: boards made for assembly rather than soldering, sent to a PCBA
service with every SMT part machine-placed and hand soldering reserved for
panel parts; small desktop boxes rather than a rack standard; and the analog
versions staying fully analog, with the software as the sketchbook that settles
topology, ranges and feel but never becoming firmware.

The order stays software first for a reason worth restating: the expensive part
of an analog synth is not the layout, it is knowing what the circuit has to do
and what its ranges should be. That is exactly what playing the apps produces.
Which app gets converted first is a decision to make *after* there are several
to choose between, and it will be made by ear.

**Power is unresolved and disliked.** A USB-C 5 V input with analog rails made
on board was the last idea, and it is not settled. Do not design it, do not
propose a topology, and do not let an app's design depend on it.

## 10. Open questions

To settle before the phase that needs them:

1. Which typeface, and how much generative ornament the interface carries. The
   user directs this (D13); P5 is when it is asked.
2. Whether the launcher shows a list of apps, a patch view of the buses, or
   nothing but a menu. Not needed before P6; the answer probably arrives from
   playing.
3. Whether per-app project files merge into one lab-wide file. Not needed
   before P8 ships one schema.
4. What the sequencer's buffer window means at audio rate: sample count per
   fader, interpolation between faders, and where a buffer comes from.
   Needed before P7's SEQUENCER.
5. Whether Drone Commander keeps a spatially arranged panel or moves to a
   row-based sheet. P3 assumed it keeps a panel, because the oscilloscope and
   the section grouping carry meaning that rows would lose. Still assumed.
