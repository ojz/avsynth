# Drone Commander Agent Instructions

## Project Direction

Drone Commander has two related but separate implementations:

1. A digital synthesizer used to learn DSP and explore signal paths.
2. A fully analog hardware synthesizer informed by those experiments.

The hardware version must remain fully analog. Do not propose running the digital DSP engine on the hardware. Digital experiments may guide topology, parameter ranges, modulation behavior, and circuit choices, but they are not the hardware implementation.

## Language and Dependencies

- Write the application and DSP engine in readable C17.
- Do not introduce C++.
- Use SDL3 for the window, input, rendering, audio-device access, timing, and application lifecycle.
- Do not introduce SuperCollider, Faust, or another synthesis language.
- Keep FFmpeg optional and outside the real-time DSP path. It may later support recording, encoding, streaming, or export.
- Keep dependencies minimal and justify any new dependency before adding it.
- Use CMake as the build system when the project is scaffolded.

## Architecture

Separate the program into two layers:

- A platform-independent DSP engine written in standard C. It must not depend on SDL or FFmpeg.
- An SDL application shell that owns devices, controls, visualization, and parameter updates.

Represent stateful DSP units with explicit structs and processing functions. Prefer clear names and straightforward data flow over object-oriented patterns or opaque abstractions.

Design the DSP engine so it can render both real-time audio buffers and offline test buffers. Use floating-point samples and keep oscillator phase bounded, normally in the range `[0, 1)`.

## Signal Processing

Implement and explain DSP incrementally. The initial signal path is:

```text
3 oscillators -> mixer -> VCF -> VCA -> output
                       ^      ^
                       |      |
                    square LFO
```

Each oscillator has frequency and amplitude controls. The VCF has cutoff, cutoff-modulation depth, and resonance controls. The VCA has amplitude and amplitude-modulation depth controls. The square LFO has a rate control.

- Begin with a phase-accumulator oscillator and make the implementation educational and easy to inspect.
- Start with explicit processing code rather than a generic node graph or plugin architecture.
- Make topology experiments, including oscillator count, FM, and cross-FM, by editing clear signal-routing code until recurring needs justify an abstraction.
- Treat aliasing as an explicit engineering concern for discontinuous waveforms, FM, feedback, and nonlinear processing. Simple implementations are acceptable while learning, but document their limitations near the relevant work.
- Use `tanhf` as the initial soft-saturation primitive where saturation is desired. Do not describe it as a complete analog model.
- Consider oversampling nonlinear stages only after the basic signal path works and tests or listening demonstrate a need.

## Real-Time Rules

The audio callback must remain deterministic and bounded:

- Do not allocate or free memory.
- Do not perform file or network I/O.
- Do not log.
- Do not call FFmpeg.
- Do not take a mutex that can block.
- Do not rebuild routing or processing structures.

Transfer control changes from the application thread using atomics or a small non-blocking queue. Smooth parameters whose abrupt changes would click or produce zipper noise. Send visualization data to the rendering thread through a bounded buffer; rendering must never control audio timing.

## Development Approach

- Prefer small, audible milestones: one oscillator, gain, saturation, visualization, then additional oscillators, mixing, filtering, and modulation.
- Keep functions and modules narrow enough for a C learner to follow.
- Avoid premature frameworks, generalized graph engines, and hidden code generation.
- Add focused tests for DSP behavior where practical, including bounded output, phase wrapping, parameter limits, and deterministic offline rendering.
- Preserve a direct correspondence between digital blocks and candidate analog concepts, while noting where software behavior cannot be assumed to transfer directly to a circuit.

## Analog Hardware Boundary

When discussing or designing the hardware version, use analog building blocks such as VCOs, op-amp or passive mixers, VCFs, VCAs or OTAs, comparator-based LFOs, and transistor, diode, OTA, or op-amp saturation stages. Account for component tolerances, noise, headroom, tracking, stability, power rails, and safe signal levels.

Do not select circuits solely because they resemble a digital implementation. Circuit choices must be evaluated as analog designs in their own right and validated through simulation, breadboarding, measurement, and listening.