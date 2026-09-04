# shared/

Code used by more than one application. Empty on purpose: something moves here
when a second app genuinely needs it, never in anticipation.

Planned, in this order (see [ROADMAP.md](../ROADMAP.md) D4 and phases 3 and 4):

| Library | What it holds | Extracted from |
|---|---|---|
| `param` | the parameter model: definitions with range, step, neutral and kind; select, nudge, reset, randomize, format | Drone Commander's control table in `panel.c` and vsynth's derived knobs in `rack.c` |
| `store` | presets and projects in SQLite, keyed by (group, key) so a preset survives edits elsewhere | vsynth's `project.c` |
| `ui` | the control surface on SDL3: the sheet, its modes, knob rows, glyph atlas, notices | vsynth's `hud.c` |

Rules that hold for anything in here:

- A DSP engine may depend on `param` definitions and nothing else. Never on
  SDL, SQLite or ffmpeg.
- `ui` never talks to `store`. The application wires them together.
- `param` is plain C with no SDL and no SQLite, so it can be tested offline.
