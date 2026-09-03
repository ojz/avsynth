# vsynth — a video feedback synthesizer

*PRD v0.2 — 2026-09-03. Supersedes v0.1 (2026-09-02), which framed the
instrument as a fixed rack. Working name; rename freely.*

## 1. What this is

A desktop instrument for playing with ffmpeg filter chains on a live video
feedback loop. It captures a region of the screen, pushes it through a
libavfilter graph, and shows the result in a borderless window that you drag
into the capture region. The loop is the instrument. Writing the chain and
turning its knobs while the loop runs is playing it.

It replaces a camera pointed at a monitor, and the `x11grab | ffmpeg | ffplay`
shell rig that replaced the camera. The rig's strength was that a filter chain
is one line of text you can rewrite in seconds; its weakness was that every
rewrite meant a restart and nothing could be changed while it ran. vsynth
keeps the text and adds live control.

Mental model: **the chain text is the instrument.** You write a filtergraph in
plain `ffmpeg -filter_complex` syntax, with splits, branches, merges, blend
modes, whatever libavfilter offers. The app parses it, finds every option you
wrote that the filter accepts at runtime, and gives you a knob for it. A
preset is the knob positions for one chain. Restructuring means editing the
text and applying it; that rebuilds the graph (libavfilter cannot restructure a
running one) and costs a fraction of a second of feedback state, which the
loop re-seeds from the screen.

v0.1 had a fixed rack of seventeen modules defined in C. Two of the user's own
old presets, `mirror` and `kaleido`, use `split` and `hstack` and could not be
expressed in it. That settled the question.

## 2. Goals and non-goals

**Goals (done, 2026-09-02 and 2026-09-03)**

- Own the window. Borderless, redraws while being moved or resized, movable
  with left-drag and resizable with right-drag from inside the app.
- Editable chain. A text editor drawn over the video, in the same style as the
  panel. Apply validates on a throwaway graph first; on failure the
  libavfilter error is shown and the old graph keeps running.
- Derived controls. Every runtime-settable numeric option written in the text
  becomes a knob; every filter with timeline support gets a bypass. No
  hand-maintained module table.
- Taps. Any output label left unconnected becomes a preview thumbnail,
  composited by SDL under the panel, never touching the main picture.
- Presets: ten slots of knob values per chain. Save, load, switch instantly.
- Projects: one SQLite file holding chains and their presets, in the per-user
  app data folder by default.
- Cross-platform C11 with CMake and pkg-config; same source on Arch Linux and
  MSYS2/UCRT64. Platform code only in the capture-source selection.
- Low latency. Frames are dropped, never queued.

**Later (designed for, not built)**

- Enum-valued options (blend modes, edge modes) as named steppers instead of
  bare integers.
- Chain rename and delete in-app; export/import a chain as a text file for git.
- Morph: interpolate all knobs from preset A to preset B over N seconds.
- Modulators: LFOs and envelopes on any knob, computed in the host.
- MIDI with MIDI learn (RtMidi or PortMidi); OSC on the same binding model.
  Every knob already has min, max and step, so a 0..127 CC maps without extra
  data. Bindings per project in SQLite.
- Multiple outputs: a second SDL window on a projector display, fullscreen,
  while the control surface stays on the laptop.
- Multiple voices: several graphs at once, mixed or cross-fed.
- Recording to file (libavcodec is already linked).
- GPU filters via libplacebo / OpenCL / Vulkan when CPU limits bite.
- Other sources: camera, video file, `testsrc`, another voice's output.
- A control surface in a second window so the panel and editor stop being
  part of the feedback. Immediate-mode (Nuklear or microui) over SDL is
  enough; Qt only if a project browser and dockable panels ever appear.

**Non-goals**

- Audio.
- A node-graph editor drawn with the mouse. Text is the editor.
- Our own filters. If libavfilter cannot do it, the answer is a GPU filter
  backend, not C code in this repo.
- Wayland-native capture. `x11grab` under XWayland is the Linux path for now.
- Windows `ddagrab`. It fails on this machine; `gdigrab` is fine.

## 3. Vocabulary

| Term | Meaning |
|---|---|
| **Source** | Where frames come from. v1: a screen region via `gdigrab` (Windows) or `x11grab` (Linux). |
| **Chain** | Filtergraph text in `-filter_complex` syntax, plus its presets. The unit you edit and switch between. |
| **Module** | One filter instance in a chain. Named with `filter@name`, or `Parsed_filter_N` when unnamed. |
| **Knob** | A runtime-settable numeric option written in the text, with min, max, step and the text value as neutral. |
| **Bypass** | A module's on/off switch, the generic `enable` command. `enable=0` in the text starts it off. |
| **Preset** | A snapshot of every knob value and bypass flag of one chain, in slot 1..10. |
| **Tap** | An open output label; shown as a thumbnail. |
| **Voice** | A running graph: source + chain + outputs. v1 has exactly one. |
| **Project** | One SQLite file: chains, presets, current chain, geometry. |

## 4. Architecture

```
 chain text --parse2--> AVFilterGraph --walk options--> knobs (panel rows, preset values)
      ^                     |
      |  editor overlay     +-- [out] / unlabelled end -> scale {W}x{H} -> bgra -> main sink
      |  ctrl+enter         +-- every other open [label] -> bgra -> tap sink
      |
   throwaway graph first; only a graph that configures replaces the voice

 +--------------+  AVPacket  +---------+  AVFrame  +---------------------+
 | libavdevice  |----------->| decoder |---------->| libavfilter graph   |
 | gdigrab /    |            | (raw)   |           | (built by graph.c)  |
 | x11grab      |            +---------+           +----+----------+-----+
 +--------------+                                       | main     | taps
        capture + filter thread                         v          v
 ------------------------------------------------- mailboxes (one slot each)
        main thread (SDL requires it on Windows/macOS)  |          |
 +------------+   commands   +---------------+   +------+----------+------+
 | keyboard,  |------------->| command queue |   | window: frame texture  |
 | panel,     |              | (mutex)       |   | + tap thumbnails       |
 | editor     |              +-------+-------+   | + panel + editor       |
 +------------+                      | drained   +------------------------+
                                     v by filter thread between frames
                    avfilter_graph_send_command(graph, "rotate@rot", "angle", "0.031")
```

**Modules.** `graph.c` turns text into a configured graph and is the one place
that knows the wiring: source, main output, taps, `{W}`/`{H}` substitution,
newline stripping, and capture of libavfilter's error lines. `rack.c` derives
modules and knobs from a parsed graph. `voice.c` runs the capture thread and
one mailbox per output. `editor.c` is the text overlay. `hud.c` draws the
panel and the tap thumbnails and owns the glyph atlas the editor also uses.
`project.c` is SQLite. `main.c` wires it and holds the one `App`.

**Deriving knobs.** After parsing, every non-helper filter in
`graph->filters[]` becomes a module. Its options are walked with
`av_opt_next()`. An option becomes a knob when all of these hold: it carries
`AV_OPT_FLAG_RUNTIME_PARAM`; its current value is a plain number (strings like
`t*0.1` are skipped); and the user wrote it in the text. The last rule is what
keeps the panel the size of the chain: libavfilter marks dozens of options
per filter as runtime-settable, most of them noise. "What you wrote, you can
turn." Positional arguments count as written (`unsharp=5:5:1.5` writes
`lx`, `ly`, `la`).

Ranges come from an override table when libavfilter's bounds are useless
(`rotate angle` is ±DBL_MAX), else from the AVOption when the span is finite
and at most 1000, else from a window around the current value. The table also
defines the one virtual knob: `crop w=iw/Z:h=ih/Z` becomes `zoom`, driving both
expression options. Scale's size options are never knobs; changing frame size
mid-stream breaks downstream filters.

**Live control.** Knobs are `avfilter_graph_send_command` to the instance
name. Bypass is the generic `enable` command on filters with
`AVFILTER_FLAG_SUPPORT_TIMELINE`. Neither touches graph structure. Commands
queued before a graph is up are applied as soon as it is, so a restart followed
by `rack_send_all` restores knob positions.

**Restructuring.** Apply, chain switch and region change all go through
`restart_voice`: stop the thread, start a new one on the current text and
geometry, resend knobs. Apply first derives a rack from the new text on a
throwaway graph; failure leaves everything running and shows the error.

**Window.** `SDL_WINDOW_BORDERLESS`. No OS title bar, so no OS modal move
loop, so no frozen picture. Left-drag moves, right-drag resizes, both from our
own event loop. No Alt gestures: AltSnap eats them. The capture region and the
window are independent; feedback happens when you place one inside the other.

**Storage.** SQLite, one file per project, default in
`SDL_GetPrefPath("", "vsynth")`.

```sql
CREATE TABLE project (id INTEGER PRIMARY KEY CHECK (id = 1), name TEXT, schema_version INTEGER,
                      cap_x INT, cap_y INT, cap_w INT, cap_h INT, cap_fps INT,
                      win_x INT, win_y INT, win_w INT, win_h INT, current_chain INT);
CREATE TABLE chain   (id INTEGER PRIMARY KEY, position INT, name TEXT, text TEXT NOT NULL, created_at TEXT);
CREATE TABLE preset  (id INTEGER PRIMARY KEY, chain_id INT REFERENCES chain ON DELETE CASCADE,
                      slot INT, name TEXT, created_at TEXT, UNIQUE (chain_id, slot));
CREATE TABLE preset_value  (preset_id INT REFERENCES preset ON DELETE CASCADE,
                            target TEXT, option TEXT, value REAL, PRIMARY KEY (preset_id, target, option));
CREATE TABLE preset_enable (preset_id INT REFERENCES preset ON DELETE CASCADE,
                            target TEXT, enabled INT, PRIMARY KEY (preset_id, target));
```

Preset values are keyed by instance name and option name, never by row id,
so a preset survives edits to other parts of the text. A v0.1 file opens fine;
its old tables are ignored and the new ones added beside them.

**Dependencies.** libavfilter, libavutil, libavformat, libavcodec,
libavdevice, SDL2, SDL2_ttf, SQLite3. CMake, pkg-config, C11, no C++.

## 5. Starter chains

A new project gets three chains:

- `rack`: v0.1's fixed rack written out as text, one filter per line, every
  knob named, off-by-default modules with `enable=0`. Its eight presets (init,
  spin, tunnel, trail, invert, edge, chroma, melt) are the old `feedback.ps1`
  presets. It proves nothing was lost.
- `mirror`: `crop=iw/2:ih:0:0,split[a][b];[b]hflip[c];[a][c]hstack` plus hue
  and rotate knobs. The chain v0.1 could not express.
- `kaleido`: the four-way mirror with `split=3[d][e][half]` leaving `[half]`
  open as a tap, so the two-way stage shows in the corner.

## 6. Interaction

The video window is the only window. See README.md for the full key map.
The shape:

- The **panel** (top-left) lists one row per control with a bar; mouse and
  Tab/arrows drive it. `h` hides it, and the taps with it.
- The **editor** (`e`, bottom half) is a plain text box. Ctrl+Enter applies,
  Esc closes. A status line shows the libavfilter error or the key hints.
- **Chains** switch with PageUp/PageDown; Ctrl+N copies the current one into
  a new chain and opens the editor on it.
- **Presets** are the digit row by physical position (the user's layout is
  AZERTY, where digits are shifted); Shift saves.
- **Taps** sit bottom-right, labelled with their output label.

Everything drawn over the video is fed back when the window sits inside the
capture region. That is a known trade-off, to be solved by the second-window
control surface later, not by hiding features now.

## 7. Milestones

| # | Date | Deliverable |
|---|---|---|
| M0 | 2026-09-02 | Toolchain, borderless window with own drag loop, first commit. |
| M1 | 2026-09-02 | gdigrab -> hardcoded graph -> window. Picture keeps moving while dragged. |
| M2 | 2026-09-02 | Fixed rack from a C table, live knobs and bypass over commands, `--selftest`. |
| M3 | 2026-09-02 | SQLite project file, patch slots, geometry persistence. |
| M4 | 2026-09-02 | Region picker, randomize, on-screen panel with mouse. |
| M5 | 2026-09-03 | **Text chains.** `graph.c`, knobs derived from the parsed graph, editor overlay with validate-then-apply, taps as thumbnails, chain/preset/project schema, app-data default location, AZERTY-safe keys. Fixed rack retired to a starter chain. |
| M6 | next | Enum steppers, chain rename/delete, chain export as text. Then morph, then the Linux build check. |

## 8. Decisions log

- **libavfilter in-process, not ffmpeg CLI + zmq.** Owning the process gives
  live knobs, live restructuring by rebuild, taps, and the multi-voice future.
- **Text chain, not fixed rack** (2026-09-03, reversing v0.1). The user's own
  presets needed `split`/`hstack`; the fixed rack was a lite version. The
  reversal removed code: the hand-written module table is gone, knobs come
  from AVOption metadata.
- **Knobs only for written options.** Deriving a knob for every runtime
  option produced 54 rows for the rack chain, most of them noise; the written
  rule gives 29, the same set the hand-written table had. Screen real estate
  matters because the UI is part of the picture.
- **Taps composited by SDL, not in the graph.** Free, toggleable with `h`, and
  the main output stays pure. Compositing in the graph would cost filter time
  and change what gets fed back.
- **Raw ffmpeg syntax verbatim,** with exactly one addition: `{W}`/`{H}`.
  Without it a zoom chain cannot scale back to the capture size across region
  changes. The user chose "verbatim" over sugar; this is the minimum that
  works and is documented as the only exception.
- **Presets belong to a chain, chains to a project.** The user's vocabulary.
  A project-wide chain with presets as values only was the alternative; per-
  chain presets match how the rig's presets were actually organised.
- **Project file in app data, `--project` overrides.** The user's call.
  Export as text is the git-friendly path if wanted later.
- **SDL2, not SDL3. SQLite, not JSON. C11, CMake, pkg-config. Frames dropped,
  not queued.** Unchanged from v0.1.
- **Validate on a throwaway graph before replacing the voice.** A typo in the
  editor must never kill the picture.

## 9. Risks and open questions

- **Positional-argument mapping** in the "written" scan mirrors libavfilter's
  rule (skip constants and aliases by offset). If a filter's option order in
  a future ffmpeg differs from the shorthand order, a knob could go missing;
  the fix is naming the option in the text.
- **Unnamed filters** are keyed `Parsed_filter_N`; inserting a filter earlier
  in the text renumbers them and orphans preset values. Name what you want to
  keep.
- **Frame size changes.** Anything that alters frame size mid-stream after a
  knob turn can break downstream filters. `scale` size options are excluded;
  other size-changing runtime options are not yet known.
- **CPU budget.** All filters are CPU. 800x600 holds 30 fps on the Iris Xe;
  1920x1080 will not. GPU filters are the escape hatch.
- **Wayland.** No capture path without X11. Accepted.
- **Name.** `vsynth` is a placeholder.
