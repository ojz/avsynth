# shared/

Code used by more than one app. Something moves here when a second app
genuinely needs it, never in anticipation (ROADMAP D4). The libraries, in the
order they are extracted:

| Library | What it holds | State |
|---|---|---|
| `param` | the fader and parameter model: identity, range, neutral, taper, the three step grains, readout precision, and a set with a selection | in use by both apps |
| `ui` | the control surface on SDL3: how a fader is laid out and drawn, as a cell or as a row, the switch and enum stepper, the framed panel, the lab's one palette | in use by both apps |
| `app` | the shell: SDL boot, the window, the frame loop, the event pump, the gesture dispatcher, the typeface and screenshots, behind an `AppSpec` every app fills in (D14) | in use by both apps; the typeface itself is still to be chosen (D13) |
| `bus` | named lock-free in-process buses at audio rate and control rate, how apps send signals to each other inside the launcher (D11) | ROADMAP P6 |
| `store` | presets and projects in SQLite, keyed on the fader address, including generative presets | ROADMAP P8, starting from vsynth's `project.c` |
| `link` | the adapter to the outside world: OSC-compatible UDP for a second machine, MIDI clock and MIDI learn | ROADMAP P9 |

`param` is the specification in [ROADMAP.md](../ROADMAP.md) section 6 turned
into code, `ui` is the only thing that draws it, and `app` is the only thing
that drives it: the shell hit-tests the controls an app lays out and applies
the gestures itself. An app that wants a continuous control uses a fader; it
does not write its own slider, it does not invent a gesture, and it never
writes an event switch for one.

An app is a struct it owns plus an `AppSpec`: create, destroy, event, tick,
frame, and three small callbacks that hand the shell its parameter set, its
laid-out controls and a "changed" notification. The exe stub is one call to
`app_run()`. Drone Commander's spec is about 200 lines; its parameter table,
panel layout and oscilloscope are another 400.

Rules that hold for anything in here:

- A DSP engine may depend on `param` and nothing else. Never on SDL, SQLite,
  ffmpeg, `ui` or `app`.
- `param` is plain C with no SDL and no SQLite, so it is tested offline.
  `param_tests` runs under `make test` with no window and no audio device.
- `ui` never talks to `store`. The shell wires them together.
- Text reaches `ui` through `UiText`, and the shell supplies it: one glyph
  atlas of one typeface. Apps do not open fonts.
