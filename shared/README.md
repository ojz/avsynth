# shared/

Code used by more than one app. Something moves here when a second app
genuinely needs it, never in anticipation.

| Library | What it holds | State |
|---|---|---|
| `param` | the fader and parameter model: identity, range, neutral, taper, the three step grains, readout precision, and a set with a selection | in use by Drone Commander; vsynth adopts it in ROADMAP P4 |
| `ui` | the control surface on SDL3: how a fader is laid out and drawn, and which gesture does what | same |
| `store` | presets and projects in SQLite, keyed on the fader address | ROADMAP P5, starting from vsynth's `project.c` |
| `link` | the inter-app clock, named buses and control plane | ROADMAP P6 |

`param` is the specification in [ROADMAP.md](../ROADMAP.md) section 6 turned
into code, and `ui` is the only thing that draws or drives it. An app that
wants a continuous control uses a fader; it does not write its own slider and
it does not invent a gesture.

Rules that hold for anything in here:

- A DSP engine may depend on `param` and nothing else. Never on SDL, SQLite or
  ffmpeg.
- `param` is plain C with no SDL and no SQLite, so it is tested offline.
  `param_tests` runs under `make test` with no window and no audio device.
- `ui` never talks to `store`. The application wires them together.
- Text is passed into `ui` by the app through `UiText`, because the apps do not
  yet agree on a font and the fader does not need them to.
