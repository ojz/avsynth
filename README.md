# avsynth

A collection of small audiovisual synthesizers and signal experiments. Each
application is independently buildable while the repository root provides
common build, test, release, and artifact conventions.

## Applications

| Application | Description | Documentation |
| --- | --- | --- |
| Drone Commander | C17/SDL3 three-oscillator drone synthesizer | [apps/drone-commander/README.md](apps/drone-commander/README.md) |
| vsynth | C11/SDL2 video feedback synth: screen region through a libavfilter chain with knobs derived from the text | [apps/video-synth/README.md](apps/video-synth/README.md) |

## Layout

```text
apps/                    Independent synthesizer applications
assets/screenshots/      Repository screenshots grouped by application
dist/                    Local packaged builds (ignored by Git)
.github/workflows/       CI builds and downloadable artifacts
```

## Build

The default root commands build and run Drone Commander. vsynth needs the MSYS2 ffmpeg/SDL2 dev
libs, so build it from `apps/video-synth/` (see its README) or configure the root with
`-DAVSYNTH_BUILD_VIDEO_SYNTH=ON`:

```powershell
make build
make run
make test
```

Audio starts hard-muted. See the application documentation for controls and
signal-path details.

Every push and pull request builds and tests the Windows executable. Successful
workflow runs publish a downloadable `drone-commander-windows` artifact. Version
tags matching `v*` also create a GitHub release with the packaged executable.# Drone Commander

A C17 synthesizer and signal-path laboratory hosted by SDL3. It is the digital
sketchbook for a later, separate, fully analog hardware synthesizer.

## Safety

**Audio always starts hard-muted.** The SDL audio device is opened paused, and
the audio callback does not run until you deliberately press `Ctrl+Shift+A` or
click the `[HARD MUTED]` button on-screen. Press `Space` or click `[AUDIO LIVE]`
at any time to mute immediately. The visualization runs while muted, so no audio
output is required to explore the controls.

## Build

Requirements: CMake 3.24 or newer, Git, and a C17 compiler (MinGW).

You can build and run using `make`:

```powershell
make build
make run
```

Or invoke CMake directly:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
./drone_commander.exe
```

## Controls

- `Mouse`: drag knobs vertically; click waveform, sync, and anti-alias switches
- `R`: reset all parameters to defaults
- `Ctrl+Shift+A` or click banner: deliberately enable audio
- `Space` or click banner: mute audio immediately
- `Escape`: quit

## Signal Path

### Oscillators

3 oscillators with selectable waveforms (Sine, Saw, Square, Triangle), each with:

- Frequency
- Amplitude

### FM Cascade

Linear frequency modulation: Oscillator 1 modulates Oscillator 2, and Oscillator 2
modulates Oscillator 3. When `FM CASCADE` is zero, oscillators remain independent;
turning it up creates classic analog drone sidebands, sub-harmonics, and metallic timbres.

### Anti-Aliasing (PolyBLEP)

Toggleable between naive digital phase accumulation and polynomial band-limited
step (PolyBLEP) correction for saw and square waves. Allows direct comparison of
aliasing noise versus clean analog-like harmonic spectrums.

### Parameter Smoothing

One-pole lowpass filter smoothing on cutoff, resonance, VCA amplitude, drive, and FM depth
to prevent clicks and zipper noise during real-time adjustments.

### Mix

Combines the three oscillators with adjustable `tanhf` soft-saturation drive.

### VCF (Voltage-Controlled Filter)

- Cutoff frequency
- Cutoff frequency depth (modulated by LFO)
- Resonance

### VCA (Voltage-Controlled Amplifier)

- Amplitude
- Amplitude modulation depth (modulated by LFO)

### Modulation (3 x Square LFO)

Each LFO has an independent rate and level. Their level-weighted outputs are
summed and routed to both the VCF and VCA modulation-depth controls.

LFO 2 can hard-sync to LFO 1, and LFO 3 can hard-sync to LFO 2. A synced LFO
resets its phase whenever the preceding LFO begins a new cycle. This corresponds
to a reset pulse between comparator-based analog LFOs on the eventual hardware.

### Oscilloscope & Metering

- Zero-crossing triggered oscilloscope for a rock-solid, jitter-free visual trace
- Peak VU meter with headroom warning indicator