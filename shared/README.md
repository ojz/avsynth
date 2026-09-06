# shared/

Code used by more than one app. Something moves here when a second app
genuinely needs it, never in anticipation (ROADMAP D4). The libraries, in the
order they are extracted:

| Library | What it holds | State |
|---|---|---|
| `param` | the fader and parameter model: identity, range, neutral, taper, the three step grains, readout precision, and a set with a selection | in use by Drone Commander; vsynth adopts it in ROADMAP P4 |
| `ui` | the control surface on SDL3: how a fader is laid out and drawn, the switch and enum stepper, the framed panel, the lab's one palette, and which gesture does what | same |
| `app` | the shell: SDL boot, the frame loop, the event pump and the gesture dispatcher, behind an `AppSpec` every app fills in (D14) | ROADMAP P5 |
| `bus` | named lock-free in-process buses at audio rate and control rate, how apps send signals to each other inside the launcher (D11) | ROADMAP P6 |
| `store` | presets and projects in SQLite, keyed on the fader address, including generative presets | ROADMAP P8, starting from vsynth's `project.c` |
| `link` | the adapter to the outside world: OSC-compatible UDP for a second machine, MIDI clock and MIDI learn | ROADMAP P9 |

`param` is the specification in [ROADMAP.md](../ROADMAP.md) section 6 turned
into code, and `ui` is the only thing that draws or drives it. An app that
wants a continuous control uses a fader; it does not write its own slider and
it does not invent a gesture.

Rules that hold for anything in here:

- A DSP engine may depend on `param` and nothing else. Never on SDL, SQLite,
  ffmpeg or `ui`.
- `param` is plain C with no SDL and no SQLite, so it is tested offline.
  `param_tests` runs under `make test` with no window and no audio device.
- `ui` never talks to `store`. The shell wires them together; until the shell
  exists, the application does.
- Text is passed into `ui` by the app through `UiText`, because the apps do not
  yet agree on a font and the fader does not need them to. The one typeface
  arrives with the shell (D13, P5).
