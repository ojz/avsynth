# avsynth agent instructions

Read [ROADMAP.md](ROADMAP.md) first. It holds the vision, the decisions
(numbered D1 to D6) and the phases. Do not re-decide what it decides; if a
decision has to change, change it there in the same commit.

## Git workflow

- Work directly on `main`; no feature branches unless the user asks for one.
- After completing and validating a change, commit it on `main` and push to
  the GitHub remote.
- Use the repository-local Git author configuration (the personal address, not
  the work one). Never force-push or rewrite published history unless asked.

## Repository shape

- One instrument per directory under `apps/`. Each app is self-contained: its
  own README, sources under `src/`, tests under `tests/`.
- `shared/` holds code that at least two apps genuinely use. Extract, do not
  pre-build: the order is `param`, `store`, `ui` (ROADMAP D4).
- Docs live next to what they describe: app READMEs for controls and signal
  paths, `apps/video-synth/PRD.md` for vsynth's design,
  `apps/video-synth/CLAUDE.md` for vsynth's environment and code rules,
  `HARDWARE.md` per app once it goes to hardware.

## Language and dependencies

- C17, readable, for every app and shared library. No C++.
- SDL3 for window, input, rendering, audio device, timing and lifecycle.
  vsynth is on SDL2 until phase 2; do not add new SDL2-only code there.
- CMake with Ninja and pkg-config for system libraries. Toolchain is MSYS2
  UCRT64 on Windows and pacman on Arch (ROADMAP D1).
- Do not introduce SuperCollider, Faust, or another synthesis language.
- Keep dependencies minimal and justify any new one. FFmpeg belongs to vsynth's
  filter path and, later, to recording; it never enters an audio DSP path.
- Write stateful units as explicit structs with processing functions. Clear
  names and straight data flow over object patterns or opaque abstractions.

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
- An SDL application shell that owns devices, controls, visualization and
  parameter updates.
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

## Hardware boundary (all instruments)

When discussing or designing hardware, use analog building blocks: VCOs,
op-amp or passive mixers, VCFs, VCAs or OTAs, comparator-based LFOs,
transistor, diode, OTA or op-amp saturation stages. Account for tolerances,
noise, headroom, tracking, stability, power rails and safe signal levels.

Design for the constraints in ROADMAP section 7: machine-assembled SMT boards,
hand soldering only for panel parts, one daisy-chained 9 V or 12 V DC supply
with rails generated on board, desktop enclosures.

Do not pick a circuit because it resembles the digital implementation. Every
circuit is evaluated as an analog design and validated by simulation,
breadboarding, measurement and listening.
