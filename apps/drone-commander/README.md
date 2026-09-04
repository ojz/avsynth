# Drone Commander

A C17 synthesizer and signal-path laboratory hosted by SDL3. It is the digital
sketchbook for a later, separate, fully analog hardware synthesizer.

## Safety

**Audio always starts hard-muted.** The SDL audio device is opened paused, and
the audio callback does not run until you deliberately press `Ctrl+Shift+A` or
click the `[HARD MUTED]` button on-screen. Press `Space` or click `[AUDIO LIVE]`
at any time to mute immediately. The visualization runs while muted, so no audio
output is required to explore the controls.

## Build

Drone Commander is built from the repository root along with the rest of the
lab. See the [root README](../../README.md) for the one-time toolchain install,
then:

```sh
make run-drone
```

The executable lands in `build/bin/drone_commander.exe`, and `make test` runs
the DSP tests. Dependencies are SDL3 from pkg-config, CMake, Ninja and a C17
compiler; nothing is downloaded at configure time.

## Controls

- `Mouse`: drag sliders horizontally; click waveform, sync, and anti-alias switches
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

The LFO output is bipolar: the square wave alternates between `-1` and `+1`
before level scaling. Each LFO has a red/green indicator pair; red shows the
negative half-cycle and green shows the positive half-cycle.

LFO 2 can hard-sync to LFO 1, and LFO 3 can hard-sync to LFO 2. A synced LFO
resets its phase whenever the preceding LFO begins a new cycle. This corresponds
to a reset pulse between comparator-based analog LFOs on the eventual hardware.

### Oscilloscope & Metering

- Zero-crossing triggered oscilloscope for a rock-solid, jitter-free visual trace
- Peak VU meter with headroom warning indicator