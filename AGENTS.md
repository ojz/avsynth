# avsynth agent instructions

Read [ROADMAP.md](ROADMAP.md) first. It holds the vision, the decisions
(numbered D1 to D14) and the phases (P0 to P9). Do not re-decide what it
decides; if a decision has to change, change it there in the same commit.

## How the user directs this work

This matters as much as the technical rules, so it is first.

- **The user does not read these documents.** ROADMAP.md, AGENTS.md and the
  per-app docs are written for agents, and for the user's future self only when
  something has gone wrong. They are the project's memory, not its interface.
- **The interface is the conversation. Explain everything, fully, in chat.**
  Never answer a question with "it is in the roadmap", never assume a decision
  is understood because it is written down, and never present a plan only as a
  file diff. When a phase, a trade-off or a piece of architecture comes up,
  explain it in plain language in the reply itself, including the reasoning and
  what it costs. Assume nothing was read.
- **The user knows C conceptually, not syntactically.** They studied it about
  twenty years ago and do not write it day to day. So: explain *what a piece of
  code does and why that shape was chosen*, not what the syntax means. Do not
  dumb down architecture, signal flow, real-time constraints or trade-offs —
  those land. Do not expect them to spot a bug by reading a diff.
- **The user directs by taste and intent; the agent supplies the
  implementation.** Their input is about what it should feel like, sound like
  and look like, and which idea is worth having. Take that as authoritative
  even when it arrives loosely worded, and turn it into code without asking
  them to specify it in engineering terms.
- **Disagree when there is a reason to.** They ask directly for the
  recommendation and expect a real one, with the argument attached. Agreeing
  by default is worse than useless here. But once they have decided, build it.
- **Aesthetic direction is theirs** (D13). Fonts, palette, generative ornament:
  propose options, show them, let the user choose. Do not silently pick a look.

## The decisions that shape almost every change

- **Everything under `apps/` is an app** (D6). Not an instrument, not a synth,
  not a tool. Uniform vocabulary in code, docs and UI. An app is a unit that
  could plausibly be one box one day; if it would need two front panels, it is
  two apps. `launcher/` is the one program that is not an app.
- **Every continuous control is a fader** (D8), specified in ROADMAP section 6
  and implemented once in `shared/param` and `shared/ui`. Never write a bespoke
  slider, and never invent a gesture: a fader resets to its neutral, shows its
  value at full precision with its unit, and moves coarse on drag, fine on the
  wheel, coarse on ctrl+wheel and ultra-fine on shift+wheel.
- **Signals agree** (D9). Audio and modulation are bipolar, nominally -1 to +1,
  so a depth that can invert is a bipolar fader with neutral 0. Clocks are
  ramps of phase 0 to 1, never pulses. Connections between apps are named.
- **The lab runs in one process, hosted by a launcher** (D11). Apps are
  libraries plus a thin exe stub, each with its own window, startable, killable
  and instantiable more than once. Signals travel on named in-process buses.
  OSC/UDP survives as `shared/link`, the adapter to the outside world, not as
  the way two apps in the same lab talk to each other.
- **An app's state lives in a struct the app owns** (D12). No mutable
  file-scope state: no `static` arrays, caches, layout flags or RNG seeds
  outside the instance. `static const` tables are fine. This is not style — the
  launcher runs two instances of the same app, and shared statics make that
  silently wrong.
- **One shell** (D14). `shared/app` owns SDL init, the window, the frame loop,
  the event pump and the D8 gesture dispatch. An app supplies parameters,
  layout, its picture and its engine. An app never writes its own event switch
  for faders.
- **Hardware is parked** (D10). Do not design circuits, do not propose a power
  topology, and do not let an app's design depend on either.

## Git workflow

- Work directly on `main`; no feature branches unless the user asks for one.
- After completing and validating a change, commit it on `main` and push to
  the GitHub remote.
- Another machine pushes to this repo too. Fetch before pushing and keep the
  other session's decisions; never resolve a conflict by taking one whole side.
- Use the repository-local Git author configuration (the personal address, not
  the work one). Never force-push or rewrite published history unless asked.

## Repository shape

- One app per directory under `apps/`. Each app is self-contained: its own
  README, sources under `src/`, tests under `tests/`.
- `shared/` holds code that more than one app genuinely uses. Extract, do not
  pre-build: the order is `param`, `ui`, `app`, `bus`, `store`, `link`
  (ROADMAP D4). `shared/param` is plain C and testable offline; keep it that
  way.
- `launcher/` hosts apps. It never draws inside an app's window.
- `assets/fonts/` holds the lab's typeface, shipped in the repo. No app hunts
  for a system font (D13).
- Docs live next to what they describe: app READMEs for controls and signal
  paths, `apps/video-synth/PRD.md` for vsynth's design,
  `apps/video-synth/CLAUDE.md` for vsynth's environment and code rules,
  `HARDWARE.md` per app once it goes to hardware.

## Cross-app connectivity

- Keep audio/video transport separate from control synchronization.
- **Inside the lab, signals travel on named in-process buses** (`shared/bus`,
  D11): a single-writer many-reader ring per bus, carrying float samples under
  D9's conventions. Audio rate and control rate are the same mechanism at
  different block sizes; a clock is a bus carrying a phase ramp. This is what
  makes an audio-rate SEQUENCER and a summing MONITOR possible, and it is why
  the lab is one process.
- **Outside the lab, `shared/link` is the adapter**: OSC-compatible UDP over
  localhost or LAN for another machine, plus MIDI clock and CC. Messages carry
  monotonic timestamps and stable fader addresses. Socket I/O runs on an
  application-owned network thread and publishes bounded events to real-time
  code through a non-blocking queue; audio callbacks never call networking
  APIs. Followers run their own clocks and correct phase from timestamped
  updates; packet arrival time is not the clock.
- Only MONITOR opens a playback device. Every other app that makes sound writes
  to a named bus and is silent on its own. Output level lives in one place.
- MIDI, PipeWire and JACK are adapters for external systems, never the
  repository's internal mechanism.

## Language and dependencies

- C17, readable, for every app and shared library. No C++. Compiler extensions
  are on (gnu17) because the apps use POSIX `strdup` and `strncasecmp`.
- SDL3 for window, input, rendering, audio device, timing and lifecycle. Both
  apps are on SDL3; never reintroduce SDL2, and remember SDL3 draws in floats
  (`SDL_FRect`), needs the window for text input, and returns `true` on success
  where SDL2 returned 0.
- CMake with Ninja and pkg-config for system libraries. Toolchain is MSYS2
  UCRT64 on Windows and pacman on Arch (ROADMAP D1). Nothing is vendored or
  fetched at configure time, and the root is the only build entry point: build
  and run through `make` from the repository root, never from an app folder.
- Do not introduce SuperCollider, Faust, or another synthesis language.
- Keep dependencies minimal and justify any new one. FFmpeg belongs to vsynth's
  filter path and, later, to recording; it never enters an audio DSP path.
- Write stateful units as explicit structs with processing functions, allocated
  and owned by the app instance (D12). Clear names and straight data flow over
  object patterns or opaque abstractions.

## Drone Commander

Two related but separate implementations exist:

1. The digital synthesizer in `apps/drone-commander`, used to learn DSP and
   explore signal paths.
2. A fully analog hardware synthesizer informed by those experiments.

The hardware stays fully analog. Never propose running the DSP engine on the
hardware. Digital experiments guide topology, parameter ranges, modulation
behaviour and circuit choices; they are not the hardware.

Architecture:

- A platform-independent DSP engine in standard C (`dsp.c`). It must not
  depend on SDL, FFmpeg, SQLite or `shared/ui`.
- An app shell from `shared/app` that owns the window, controls and events.
- The engine renders both real-time audio buffers and offline test buffers.
  Floating-point samples, oscillator phase kept in `[0, 1)`.

Signal path, extended in small audible steps:

```text
3 oscillators -> mixer (tanhf drive) -> VCF -> VCA -> output
                                         ^      ^
                                    3 square LFOs, hard-sync chain
```

- Phase-accumulator oscillators, explicit routing code, no generic node graph
  until a recurring need earns one.
- Aliasing is an explicit concern for discontinuous waveforms, FM, feedback and
  nonlinearities. Simple implementations are fine while learning; document
  their limits next to the code.
- `tanhf` is the soft-saturation primitive. Do not call it an analog model.
- Oversample nonlinear stages only after tests or listening show the need.
- An LFO level is a bipolar fader running -1 to +1, so a negative level is the
  same depth inverted. Where LFO outputs are summed, weight them by magnitude:
  summing signed levels lets two opposed LFOs cancel the normalising weight and
  leave the modulation at twice its intended depth.
- Each square LFO shows its phase as one bar split where the sign changes, red
  on the negative half and green on the positive, with the live half lit.

Real-time rules for the audio callback, without exception:

- No allocation or freeing, no file or network I/O, no logging, no FFmpeg,
  no mutex that can block, no rebuilding of routing or processing structures.
- Control changes arrive through atomics or a small non-blocking queue.
  Parameters that would click or zipper are smoothed.
- Visualization data leaves through a bounded buffer; rendering never controls
  audio timing.

Tests: bounded output, phase wrapping, parameter limits, deterministic offline
rendering. Keep functions narrow enough for a C learner to follow.

## vsynth

Follow `apps/video-synth/CLAUDE.md` for build commands, environment facts and
code rules. The rules that must survive any refactor:

- A knob exists only for an option the user wrote in the chain text, whose
  value is a plain number and whose AVOption is runtime-settable. No
  hand-written module tables; use the `OVERRIDES` row in `rack.c` for bad
  ranges.
- Knob changes go through `avfilter_graph_send_command`; the graph is never
  rebuilt for a knob.
- Chain text is parsed into a throwaway graph before it replaces the running
  one. A typo never kills the picture.
- The window keeps painting during move and resize; the app owns the drag loop.
- `vsynth --selftest` must keep passing. Run it after touching graph, rack,
  project or voice code, with `--project` pointing at a scratch file.

## Hardware boundary

Hardware is parked (ROADMAP D10 and section 9). There is a lot of software
experimentation to do first, nothing in the plan depends on a circuit, and the
power question is explicitly unsettled and not to be worked on.

So: do not design circuits, do not propose a power topology, do not add a
firmware target, and do not let an app's design bend around an imagined board.
If hardware comes up, say it is parked and carry on with the software.

When it is eventually unparked, the rules already recorded apply: analog
building blocks evaluated as analog designs in their own right, validated by
simulation, breadboarding, measurement and listening, never chosen because
they resemble the digital implementation.
