# vsynth — a video feedback synthesizer

*PRD v0.1 — 2026-09-02, evening. Working name; rename freely.*

## 1. What this is

A desktop instrument for digital video feedback. It captures a region of the
screen, pushes it through a chain of video filters, and shows the result in a
borderless window that you drag into the capture region. The loop is the
instrument. Turning knobs while the loop runs is playing it.

It replaces a camera pointed at a monitor, and the `x11grab | ffmpeg | ffplay`
shell rig that replaced the camera. Everything the camera rig had (physical
objects, cables, lights, things that can burn down) becomes a project file that
lives in a git repo.

Mental model: a **drum machine**, not a modular. The signal path is fixed. A
patch is a set of knob positions. You never rewire, you only turn knobs and
switch patches. This is what makes live control tractable: libavfilter can
change parameters of a running graph but cannot restructure it, so we never
ask it to.

## 2. Goals and non-goals

**Goals (v1, tonight and the next few evenings)**

- Own the window. Borderless, redraws while being moved or resized, movable
  with Alt+drag and resizable with Alt+right-drag from inside the app, on
  Windows and Linux/X11, without third-party helpers.
- Fixed filter rack with every module at a neutral setting, all knobs
  adjustable live with no restart, every module bypassable live.
- Patches: named snapshots of all knob values. Save, load, switch instantly.
- Projects stored in one SQLite file. A project is a rack definition plus its
  patches. Easy to commit, diff-able via `sqlite3 .dump` if ever needed.
- Cross-platform C. Same source builds with GCC on Arch Linux and on Windows
  through MSYS2/UCRT64. No platform-specific code outside the capture-source
  selection.
- Low latency. The feedback loop is only interesting when the delay between
  capture and display is short and steady. Frames are dropped, never queued.

**Later (explicitly designed for, not built in v1)**

- Morph: interpolate all knobs from patch A to patch B over N seconds.
- Modulators: LFOs and envelopes on any knob, computed in the host.
- **MIDI with MIDI learn.** Select a knob, twist a controller, it is bound.
  Bindings are stored per project in SQLite (`midi_map` table: device,
  channel, CC or note, knob, curve). Also bind patch slots to notes or pads,
  and bypass toggles to buttons. Library: RtMidi (C API, cross-platform,
  Windows MM and ALSA/JACK) or PortMidi. Every knob already has min, max and
  a curve, so a 0..127 CC maps without extra data.
- OSC input on the same binding model, so phones and tablets can be controllers.
- **Multiple outputs.** A voice's output can go to any display: a projector as
  a second SDL window on another monitor, fullscreen, while the control
  surface and the feedback loop stay on the laptop screen. SDL2 gives us
  displays as a list with geometry; a window is created on a chosen display.
  Cross-feeding a projector output back through a camera source is the
  physical loop, digitally patched.
- Multiple voices: several graphs running at once, mixed or cross-fed.
- Recording to file (libavcodec is already linked).
- GPU filters via libplacebo / OpenCL / Vulkan filters when CPU limits bite.
- Other sources: camera, video file, `testsrc`, another voice's output.

**On the UI toolkit question (Qt or not).** The video path, the rack, the
patches, MIDI and multi-output all live in a plain C core with no UI
dependency. The control surface is a separate layer talking to that core.
v1's control surface is the keyboard; the next is an immediate-mode panel
(Nuklear or microui) rendered by SDL in a second window, which is enough for
sliders, bypass buttons, patch grid and MIDI-learn state. Qt becomes worth
its weight only if the app grows a project browser, dockable panels and
native menus. The core-plus-surface split means that decision can be made
late without rewriting anything that touches pixels.

**Non-goals**

- Audio. No sound generation, no audio-reactive filters in v1.
- A generic filter-graph editor. The rack is defined in code and data, not
  drawn with a mouse.
- Wayland-native capture. `x11grab` under XWayland is the Linux path for now.
- Windows `ddagrab`. It fails on this machine; `gdigrab` is fine.

## 3. Vocabulary

| Term | Meaning |
|---|---|
| **Source** | Where frames come from. v1: a screen region via `gdigrab` (Windows) or `x11grab` (Linux). |
| **Module** | One libavfilter instance in the rack, with a stable name (e.g. `rot`). |
| **Knob** | One runtime-settable option of a module, with min, max, neutral and current value. |
| **Bypass** | A module's on/off switch. Implemented with the generic `enable` command. |
| **Rack** | The ordered list of modules. Fixed for a project. Defines the instrument. |
| **Patch** | A snapshot of every knob value and bypass flag in the rack. |
| **Voice** | A running graph: source + rack + output window. v1 has exactly one. |
| **Project** | One SQLite file: rack definition + patches + settings. |

## 4. Architecture

```
 +--------------+  AVPacket  +---------+  AVFrame  +---------------------+
 | libavdevice  |----------->| decoder |---------->| libavfilter graph   |
 | gdigrab /    |            | (raw)   |           | buffer->[rack]->    |
 | x11grab      |            +---------+           | format=bgra->sink   |
 +--------------+                                  +----------+----------+
        capture + filter thread                               | latest frame
 -----------------------------------------------------------------------------
        main thread (SDL requires it on Windows/macOS)        v
 +------------+   commands   +---------------+   +---------------------+
 | input:     |------------->| command queue |   | SDL2 window         |
 | keyboard,  |              | (mutex)       |   | streaming texture   |
 | later MIDI |              +-------+-------+   | borderless          |
 +------------+                      | drained   +---------------------+
                                     v by filter thread
                    avfilter_graph_send_command(graph, module, knob, value)
```

**Threads**

- *Filter thread*: `av_read_frame` -> decode -> `av_buffersrc_add_frame` ->
  `av_buffersink_get_frame` -> swap into a one-slot mailbox. Drains the command
  queue between frames. Never blocks on the renderer.
- *Main thread*: SDL event loop, drag/resize handling, upload the newest frame
  to a streaming texture, present. Runs at display rate. If no new frame,
  re-present the old one.

**Live control**

- Knobs: `avfilter_graph_send_command(graph, "rot", "angle", "0.031", ...)`.
  Every module in the rack is chosen from filters whose options carry the `T`
  (runtime) flag in `ffmpeg -h filter=NAME`.
- Bypass: `avfilter_graph_send_command(graph, "edge", "enable", "0", ...)`.
  Works for any filter with timeline support (the `T` in `ffmpeg -filters`).
  A disabled filter passes frames through untouched at near-zero cost.
- Neither operation touches graph structure. The graph is built once per voice.

**Window**

- `SDL_WINDOW_BORDERLESS`. No OS title bar, so no OS modal move loop, so no
  frozen picture. Alt+left-drag moves the window via `SDL_SetWindowPosition`
  each motion event; Alt+right-drag resizes via `SDL_SetWindowSize`. Plain
  left-drag also moves, since there is nothing else to click on.
- The capture region and the window are independent. Feedback happens when
  you place one inside the other. Both are saved in the project.

**Storage**

SQLite, one file per project.

```sql
CREATE TABLE project   (id INTEGER PRIMARY KEY CHECK (id = 1),
                        name TEXT, schema_version INTEGER,
                        cap_x INT, cap_y INT, cap_w INT, cap_h INT, cap_fps INT,
                        win_x INT, win_y INT, win_w INT, win_h INT);
CREATE TABLE module    (id INTEGER PRIMARY KEY, position INT UNIQUE,
                        name TEXT UNIQUE,      -- instance name used in commands
                        filter TEXT,           -- libavfilter name, e.g. 'rotate'
                        static_args TEXT);     -- fixed at graph build, e.g. 'c=black:ow=iw:oh=ih'
CREATE TABLE knob      (id INTEGER PRIMARY KEY, module_id INT REFERENCES module,
                        option TEXT,           -- option name sent in the command
                        label TEXT, min REAL, max REAL, neutral REAL,
                        curve TEXT DEFAULT 'lin',  -- lin | exp, for UI mapping
                        UNIQUE (module_id, option));
CREATE TABLE patch     (id INTEGER PRIMARY KEY, slot INT UNIQUE, name TEXT,
                        created_at TEXT);
CREATE TABLE patch_knob   (patch_id INT REFERENCES patch, knob_id INT REFERENCES knob,
                           value REAL, PRIMARY KEY (patch_id, knob_id));
CREATE TABLE patch_bypass (patch_id INT REFERENCES patch, module_id INT REFERENCES module,
                           enabled INT, PRIMARY KEY (patch_id, module_id));
```

The graph string is generated from `module` rows at startup:
`buffer -> name@filter=static_args -> ... -> scale -> format=bgra -> buffersink`.
A new project gets the default rack (section 5) inserted by the app.

**Dependencies**

libavfilter, libavutil, libavformat, libavcodec, libavdevice, SDL2, SQLite3.
Build with CMake, find libs via pkg-config. C11. No C++.

| Platform | Toolchain | Deps |
|---|---|---|
| Windows | MSYS2 UCRT64: `pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,ffmpeg,SDL2,sqlite3}` | from pacman |
| Arch Linux | `pacman -S gcc cmake ninja pkgconf ffmpeg sdl2 sqlite` | from pacman |

Verified on this machine: MSYS2 is installed at `C:\msys64` with only base
packages; VS 2022 Build Tools has MSVC but we are not using it. The Gyan
ffmpeg install is the static CLI build with no headers, so it is not a build
dependency. It stays useful for `ffmpeg -h filter=X` lookups.

## 5. Default rack v1

Order matters; this is a signal path. Every knob below is verified as
runtime-settable in ffmpeg 9.0.1. Neutral values marked *verify* need a
one-line test in M2.

| # | Name | Filter | Static args | Knobs (neutral) | Bypass default |
|---|---|---|---|---|---|
| 1 | `shift` | rgbashift | — | rh, rv, gh, gv, bh, bv (0) | on |
| 2 | `zoom` | crop | — | zoom (1.0) drives `w=iw/z`, `h=ih/z`; centered | not bypassable (crop has no timeline) |
| 3 | `unzoom` | scale | capture WxH | none, restores size after the crop | fixed |
| 4 | `rot` | rotate | `c=black:ow=iw:oh=ih` | angle (0) | on |
| 5 | `shear` | shear | `c=black` | shx, shy (0) | on |
| 6 | `lens` | lenscorrection | — | k1, k2 (0) | on |
| 7 | `trail` | lagfun | — | decay (0) | on |
| 8 | `mix` | tmix | `frames=3` | none | **off** |
| 9 | `diff` | tblend | `all_mode=difference` | all_opacity (1) | **off** |
| 10 | `edge` | edgedetect | `mode=colormix` | none | **off** |
| 11 | `blur` | gblur | — | sigma (0.5) | **off** |
| 12 | `sharp` | unsharp | `5:5:1.5` | none | **off** |
| 13 | `hue` | hue | — | h (0), s (1), b (0) | on |
| 14 | `eq` | eq | — | contrast (1), brightness (0), saturation (1), gamma (1) | on |
| 15 | `neg` | negate | — | none | **off** |
| 16 | `vig` | vignette | — | none | **off** |
| 17 | `noise` | noise | `alls=20` | none | **off** |

Verified in M2 (`vsynth --selftest`): this exact chain builds, and every knob
and every `enable` above is accepted as a runtime command on ffmpeg 8
(libavfilter 12). The earlier *verify* items were resolved by defaulting
`mix`, `diff` and `blur` to bypassed rather than trusting a neutral value.

Note on `zoom`: crop to `iw/z x ih/z` followed by `unzoom` scaling back to the
capture size is the classic feedback zoom. The graph runs at capture
resolution throughout; SDL scales the result to the window, so resizing the
window never touches the graph.

The 16 presets in `feedback.ps1` all map onto this rack as patches. Porting
them is a data-entry task, not code.

## 6. Interaction (v1, keyboard only)

The video window is the only window. Keyboard is the controller until a
panel or MIDI arrives.

| Key | Action |
|---|---|
| Alt+left-drag / left-drag | move window |
| Alt+right-drag | resize window |
| `1`–`9`, `0` | load patch slot 1–10 |
| Shift+`1`–`0` | save current knobs to slot |
| Tab / Shift+Tab | select next / previous knob |
| Up / Down | nudge selected knob (fine with Shift, coarse with Ctrl) |
| Space | toggle bypass of selected knob's module |
| Backspace | reset selected knob to neutral |
| `r` | reset all knobs (init patch) |
| `c` | re-select capture region (drag on a translucent overlay) |
| `f` | toggle fullscreen |
| Esc / `q` | quit, saving window and capture geometry |

Feedback to the player: window title shows `module.knob = value` for the
selected knob, and a one-line log on stderr. On-screen text needs a font
renderer and is deferred to the panel milestone.

## 7. Milestones for tonight

Time-boxed. Each ends in something runnable. Stop at any boundary and the
repo is still coherent.

| # | Time | Deliverable | Done when |
|---|---|---|---|
| M0 | 20 min | Toolchain and skeleton | `pacman` deps installed, CMake project, borderless SDL window with self-implemented Alt-drag and Alt-resize, `.gitignore`, first commit. |
| M1 | 60 min | The loop | gdigrab region -> hardcoded graph (the `tunnel` preset) -> window. Picture keeps moving while the window is dragged. Feature parity with the pwsh rig. |
| M2 | 60 min | The rack | Default rack from a C table, all modules at neutral. Tab/arrows send commands. Space bypasses. Verify the four *verify* neutrals. |
| M3 | 45 min | Patches | SQLite project file, default rack inserted on create, save/load slots, geometry persisted. Port 4–5 presets as patches. |
| M4 | rest | Stretch | Morph between two slots over 2 s. Then Linux build check on the Arch box. |

## 8. Decisions log

- **libavfilter in-process, not ffmpeg CLI + zmq.** zmq would give live knobs
  but leaves the window problem, the restart-on-restructure problem, and the
  multi-voice future unsolved. Owning the process solves all three.
- **Fixed rack, not a graph editor.** See section 1. Also cuts UI scope by an
  order of magnitude.
- **SDL2, not SDL3.** MSYS2 and Arch both ship SDL2 today and ffmpeg's own
  ffplay uses it. SDL3 port later is mechanical.
- **SQLite, not JSON.** The user's call. Also gives us patch history and
  multiple racks per project without inventing a format.
- **C11, CMake, pkg-config.** Lowest-friction path that is identical on
  Arch and MSYS2. MSVC is out; vcpkg's ffmpeg build takes longer than M1.
- **Frames dropped, not queued.** One-slot mailbox between threads. Latency
  beats smoothness in a feedback loop.
- **Keyboard first, panel later.** A Nuklear or microui panel in a second SDL
  window is the planned UI. Not tonight.

## 9. Risks and open questions

- **MSYS2 ffmpeg version** differs from the Gyan CLI build (its package db
  currently says 6.1.1; after `pacman -Syu` likely newer). The avfilter API we
  use (`avfilter_graph_parse_ptr`, buffersrc/sink, `send_command`) is stable
  across 6–9. Runtime-settable flags were checked against 9.0.1; re-check any
  knob that misbehaves.
- **gdigrab from inside our process** should behave like the CLI. Needs
  `avdevice_register_all()` and the same options (`offset_x`, `video_size`,
  `framerate`, `draw_mouse=0`).
- **CPU budget.** All v1 filters are CPU. 16 modules on a 1920x1080 region
  will not hold 30 fps on an Iris Xe; 800x600 should. Bypassed modules cost a
  frame copy at most. Measure in M2; GPU filters are the escape hatch.
- **Neutral values** for tmix weights, tblend opacity, gblur sigma=0 are
  unverified. Fallback for any that isn't truly neutral: default that module
  to bypassed.
- **Wayland.** No capture path without X11. Accepted for v1.
- **Name.** `vsynth` is a placeholder.
