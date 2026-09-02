# vsynth

C11 video feedback synthesizer: screen region -> libavfilter rack with live knobs -> borderless SDL2
window you drag into the capture region. Patches in a SQLite project file. PRD.md is the design doc,
README.md has the build steps and key map. Successor to `C:\dev\ffeedback\feedback.ps1` (PowerShell
ffmpeg/ffplay one-liner rig; its CLAUDE.md holds the ffplay-era environment notes).

**This folder is the project. Do not move or re-create it elsewhere.**

## Build and run (Windows)

Toolchain is MSYS2 UCRT64 (gcc, cmake, ninja, pkgconf, ffmpeg 8 dev libs, SDL2, SDL2_ttf, sqlite3). The Bash
tool is Git Bash, not MSYS2, and the MSYS2 login shell starts in its own home, so use absolute paths:

```sh
/c/msys64/usr/bin/bash -lc 'export PATH=/ucrt64/bin:$PATH; cmake --build /c/dev/vsynth/build'
PATH="/c/msys64/ucrt64/bin:$PATH" ./build/vsynth.exe --selftest
```

From PowerShell the DLLs need `C:\msys64\ucrt64\bin` on PATH. There is no `python` on this machine;
use sed for scripted edits. Bash-tool heredocs containing a single quote fail to parse; use
the Write tool for such files. Git identity for this repo is the personal gmail one, not the
Flexso work email.

## Environment facts (verified)

- One monitor 1920x1080, Intel Iris Xe. gdigrab works; ddagrab fails with E_INVALIDARG here.
- AltSnap is installed and hooks Alt+mouse globally. Never bind app gestures to Alt+mouse; that is
  why resize is plain right-drag.
- Gyan ffmpeg 9.0.1 CLI (no headers) is on the user PATH via winget; the dev libs are the MSYS2 ones.
- ET Legacy is often running while we work; ignore CPU benchmarks taken then.

## Code rules

- Modules: `window.c` output window + overlay hook, `hud.c` on-screen panel (SDL2_ttf glyph
  atlas, system font lookup), `picker.c` modal region picker, `voice.c` capture thread, `rack.c`
  module table, `project.c` SQLite. Changing the capture region restarts the voice
  (`restart_voice` in main.c); knob changes never do.
- Rack modules live in the table in `src/rack.c`. A filter qualifies only if its options accept
  runtime commands (`T` flag per option in `ffmpeg -h filter=X`) and it supports timeline `enable`
  for bypass. crop has no timeline support.
- Knobs go through `avfilter_graph_send_command`; never rebuild the graph for a knob change.
- Keep the window painting during move/resize: the app owns the drag loop, no OS modal move loop.
- `vsynth --selftest` must keep passing; run it after any change to rack, project, or voice.

## Roadmap (PRD order)

Morph between patches, MIDI learn (RtMidi), multi-output to projector displays, control panel in a
second SDL window, Linux build check on the user's Arch box.
