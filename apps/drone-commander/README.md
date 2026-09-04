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

![The panel](../../assets/screenshots/drone-commander.png)

Every continuous control is a fader, the lab's shared control, so these
gestures are the same in every app (see [ROADMAP](../../ROADMAP.md) section 6):

| Gesture | Effect |
|---|---|
| Drag the track | absolute, follows the pointer |
| Wheel | one fine step |
| Ctrl + wheel | one coarse step |
| Shift + wheel | one ultra-fine step |
| Middle click, or double click | reset to the neutral |
| `Tab`, `Shift+Tab` | select the next or previous control |
| Arrows | nudge the selected control; Ctrl coarse, Shift ultra-fine |
| `Backspace` | reset the selected control to its neutral |

A fader shows its value at full precision with its unit, and the faint tick on
the track is its neutral, so you can see where a reset will land. Frequencies
are on an exponential track, which is what makes a 20 Hz to 12 kHz cutoff
dialable at both ends; the ultra grain reaches 1 Hz, so an exact 440 Hz is a
few scroll clicks rather than a pixel hunt. The window title always names the
selected control and its value.

Switches and enum steppers (wave, anti-alias, LFO sync) advance on click or
wheel and wrap.

The rest:

- `R`: back to the default patch
- `Ctrl+Shift+A` or click the banner: deliberately enable audio
- `Space` or click the banner: mute audio immediately
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

Each LFO has an independent rate and level. Their outputs are summed and routed
to both the VCF and VCA modulation-depth controls.

**Level runs -1 to +1.** The square wave itself alternates between `-1` and
`+1`, and the level scales it, so a negative level is the same amount of
modulation with its sign flipped. Zero, in the middle of the track, is no
modulation at all and is where a reset lands. Two LFOs at opposite levels
cancel; one inverted against another at the same rate is a way to get a
narrower sweep than either alone.

The sum is normalised by the total of the levels' **magnitudes**, so inverting
one LFO changes the shape of the modulation without changing its overall depth.

Each LFO shows its phase as a single bar, split where the sign changes: red on
the negative half, green on the positive, with the live half lit and a marker
riding the current phase.

LFO 2 can hard-sync to LFO 1, and LFO 3 can hard-sync to LFO 2. A synced LFO
resets its phase whenever the preceding LFO begins a new cycle. This corresponds
to a reset pulse between comparator-based analog LFOs on the eventual hardware.

### Oscilloscope & Metering

- Zero-crossing triggered oscilloscope for a rock-solid, jitter-free visual trace
- Peak VU meter with headroom warning indicator