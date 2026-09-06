# Drone V2

Five triangle-wave voices, each with a frequency and a level, summed. No
modulation, no FM, no filter. Ten faders and nothing else.

It is the first app written on the lab's finished shell (ROADMAP P5), and the
proof that a new app costs about two hundred lines: `src/app.c` is the whole
instrument, sound and panel included.

## Safety

**Audio starts hard-muted**, like every app in the lab. Press `Ctrl+Shift+A` or
click the red `HARD MUTED` banner to enable it; `Space` or a click on the green
`AUDIO LIVE` banner mutes immediately.

## Build

From the repository root, after the one-time toolchain install in the
[root README](../../README.md):

```sh
make run-drone2
```

## Controls

Every control is a fader, the lab's shared control, so the gestures are the
lab's: drag the track to set, wheel one fine step, Ctrl + wheel coarse,
Shift + wheel ultra-fine, middle or double click to reset, Tab to select,
arrows to nudge, Backspace to reset the selected one.

- Frequency, exponential from 20 Hz to 2 kHz. Each voice's neutral is a note
  of a harmonic stack on A: 55, 82.41, 110, 164.81, 220 Hz, so reset lands on
  a chord.
- Level, 0 to 1, neutral 0.5.
- `R` returns every fader to that chord.
- `F12` saves a screenshot; `--screenshot FILE.bmp` saves one two seconds after
  start. Both come from the shell.
- `Escape` quits.

## Sound

Each voice is a naive triangle from a phase accumulator. Frequency and level
glide towards the fader with a one-pole smoother so a move never clicks. The
five are summed and divided by five, so five voices at full level meet at
exactly full scale. It opens its own audio device for now; P6 moves every
app onto a bus and gives MONITOR the one device.
