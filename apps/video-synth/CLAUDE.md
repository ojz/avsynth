# vsynth

C17 video feedback synthesizer: screen region -> user-written libavfilter chain with controls derived
from the text -> borderless SDL3 window you drag into the capture region. Chains and presets in a
SQLite project file. PRD.md is the design doc, README.md has the build steps, chain rules and key
map. Successor to `C:\dev\ffeedback\feedback.ps1` (PowerShell ffmpeg/ffplay one-liner rig; its
CLAUDE.md holds the ffplay-era environment notes).

**This folder (`apps/video-synth/` in the `avsynth` monorepo, github.com/ojz/avsynth) is the project. Do not move or re-create it elsewhere.** Since 2026-09-04 the repository root is the only build entry point: run `make` from `C:\dev\avsynth`, never a `cmake` inside this folder (its CMakeLists refuses to configure standalone). The repo-wide plan is `../../ROADMAP.md`; P1-P4 are done and P5's shell has landed, so this app is C17 on SDL3/SDL3_ttf, its controls are `shared/param` faders drawn by `shared/ui` (`rack.c` derives `Param`s, `hud.c` lays them out one per row and hands them to the shell as a `UiSurface`), and it runs on the shell in `shared/app`: `src/app.c` is the `VSYNTH` AppSpec (create, event, tick, frame), `src/main.c` is a one-line stub, and the shell owns SDL init, the window, the frame loop, the typeface, the fader gestures, F12 and `--screenshot`. P8 replaces `project.c` with `shared/store`.

## Build and run (Windows)

Toolchain is MSYS2 UCRT64 (gcc, cmake, ninja, pkgconf, ffmpeg 9 dev libs, SDL3, SDL3_ttf, sqlite3, plus
the msys `make`). From PowerShell, `make` works directly because the Makefile puts `/ucrt64/bin` on PATH:

```powershell
make build-vsynth    # or: make run-vsynth, make build, make test, make package
```

The Bash tool is Git Bash, not MSYS2, and the MSYS2 login shell starts in its own home, so from Bash
use absolute paths and set PATH yourself:

```sh
/c/msys64/usr/bin/bash -lc 'export PATH=/ucrt64/bin:$PATH; cmake --build /c/dev/avsynth/build --parallel'
```

The exe is `build/bin/vsynth.exe` at the repo root. Running it outside `make` needs
`C:\msys64\ucrt64\bin` on PATH for the DLLs. There is no `python` on this machine; use sed for
scripted edits, but note that sed, awk and perl all mangle backslash-heavy content such as Windows
paths in PowerShell files, so use the Write tool and splice with `head`/`tail` for those. Bash-tool
heredocs containing a single quote fail to parse; use the Write tool for such files. Git identity for
this repo is the personal gmail one, not the Flexso work email.

The default project file lives in `%APPDATA%\vsynth\default.vsynth`. Tests should pass
`--project` with a scratch path so they never touch it.

`tools/uitest.ps1` drives a running instance with PostMessage key events and saves screenshots
(no focus needed; the PowerShell process must be DPI aware, the script does that). Shift/Ctrl
chords do not register through it because SDL reads real modifier state. Warn the user before
running it: the window pops up on their desktop.

## Environment facts (verified)

- One monitor 1920x1080 at 125% scaling, Intel Iris Xe. gdigrab works; ddagrab fails with E_INVALIDARG here.
- Keyboard layout is Belgian AZERTY: the digit row is shifted and `[ ] { }` need AltGr. Bind
  digits by scancode, never by keycode, and avoid bracket keys as shortcuts.
- AltSnap is installed and hooks Alt+mouse globally. Never bind app gestures to Alt+mouse; that is
  why resize is plain right-drag.
- Gyan ffmpeg 9.0.1 CLI (no headers) is on the user PATH via winget; the dev libs are the MSYS2 ones.
- ET Legacy is often running while we work; ignore CPU benchmarks taken then.

## Code rules

- Modules: `graph.c` builds a libavfilter graph from chain text (source, main output, taps, `{W}`/`{H}`
  substitution, error capture); `rack.c` derives modules and knobs from a parsed graph (override
  table for ranges, "written in the text" rule for which options become knobs); `voice.c` capture
  thread with one mailbox per output; `editor.c` text overlay (keyboard only); `help.c` filter
  browser built from `av_filter_iterate` and AVOption tables; `hud.c` panel, tap thumbnails and the
  shared glyph atlas; `window.c` output window + overlay hook; `picker.c` modal region picker;
  `project.c` SQLite (chain / preset / preset_value / preset_enable). Changing the capture region or
  applying chain text restarts the voice (`restart_voice` in main.c) and resends all knobs; a knob
  change never does.
- The UI is modal (`enum Mode` in hud.h: MAIN, PANEL F2, EDIT F3, HELP F1, PROJECT F4). Every mode
  draws inside the shared sheet from `hud_sheet()` (same frame, header with F-key tabs and
  chain/preset/fps, footer via `hud_footer()`); `options.c` is the project mode. One overlay owns
  the keyboard; Esc goes to MAIN (from HELP: back to where it was opened); the active mode's F-key
  also goes to MAIN; PgUp/PgDn and F12 (screenshot) work in every mode; `q` quits only from
  MAIN/PANEL. The mouse always falls through to the window for drag/resize except on panel rows
  with the left button. Add new views as modes on the next F-key, drawn in the sheet.
- A knob exists only for an option the user wrote in the chain text whose value is a plain number
  and whose AVOption carries `AV_OPT_FLAG_RUNTIME_PARAM`. Bypass exists for filters with
  `AVFILTER_FLAG_SUPPORT_TIMELINE`. Do not add hand-written module tables; add an `OVERRIDES` row
  in rack.c when libavfilter's range for an option is useless.
- Knobs go through `avfilter_graph_send_command`; never rebuild the graph for a knob change.
- Chain text is parsed into a throwaway graph (`rack_from_chain`) before it replaces the running
  one, so a typo never kills the picture. Keep it that way.
- Keep the window painting during move/resize: the app owns the drag loop, no OS modal move loop.
- SDL3 conventions since the 2026-09-04 port: layout is integer `SDL_Rect` and `hud_frect()` converts
  at the draw call; text input goes through `hud_text_input()` because SDL3 needs the window;
  tick fields are `Uint64` and compared directly (`SDL_TICKS_PASSED` is gone); most calls return
  `true` on success where SDL2 returned 0; letter keycodes are uppercase (`SDLK_Q`) while preset
  digits stay bound by scancode for AZERTY; vsync and scale mode are renderer and texture
  properties. The root CMake strips SDL3's pkg-config `-mwindows` so `-mconsole` keeps stderr.
- `vsynth --selftest` must keep passing; run it after any change to graph, rack, project, or voice.

## Roadmap (PRD order)

Enum options as named steppers, chain rename/delete in-app, morph between presets, MIDI learn
(RtMidi), multi-output to projector displays, control panel in a second SDL window, Linux build
check on the user's Arch box.
