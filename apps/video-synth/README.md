# vsynth

A video feedback synthesizer. Captures a screen region, runs it through a
libavfilter chain you write as text, and shows the result in a borderless
window you drag into the capture region. Knobs are derived from the chain.
Chains and their presets live in a SQLite project file. See [PRD.md](PRD.md)
for the design.

## Build

vsynth is built from the repository root along with the rest of the lab, not
from this folder. See the [root README](../../README.md) for the one-time
toolchain install, then:

```sh
make run-vsynth
```

The executable lands in `build/bin/vsynth.exe`. `make` adds the toolchain to
PATH itself, so this works from the MSYS2 UCRT64 shell and from PowerShell
alike. To pass flags, build once and run it directly:

```sh
make build
./build/bin/vsynth.exe --region 200,200,640,480 --win 1000,300,640,480
```

Dependencies: libavfilter/libavformat/libavcodec/libavdevice/libavutil, SDL3,
SDL3_ttf, SQLite3, CMake, Ninja, pkg-config, a C17 compiler. The root README
has the package lines for MSYS2 and Arch.

## Run

```
vsynth [--project FILE] [--region X,Y,W,H] [--win X,Y,W,H] [--fps N] [--vf CHAIN] [--selftest] [--screenshot FILE.bmp] [--shots DIR]
```

The project file defaults to `default.vsynth` in the per-user app data folder
(`%APPDATA%\vsynth\` on Windows, `~/.local/share/vsynth/` on Linux) and is
created on first run with three chains: `rack` (the classic fixed rack with
eight presets), `mirror` and `kaleido`. Capture and window geometry are
remembered in it; CLI flags override. `--vf` runs the given chain text instead
of the project's current one. `--selftest` derives racks from the built-in
chains, rejects a deliberately broken one, checks the enum readout, pushes
every knob through the command path, round-trips a preset and a chain rename
and delete, then resets the capture region to 640x480 and keeps running.
`--screenshot FILE.bmp` saves the window two seconds after start and keeps
running; `--shots DIR` is where `F12` screenshots go.

The panel text uses a system monospace font: Consolas, Lucida Console or
Courier New on Windows; DejaVu Sans Mono, Liberation Mono or Noto Sans Mono on
Linux. Without one the panel still shows bars. One typeface shipped with the
lab replaces this hunt in ROADMAP P5.

The window is borderless and opens in knobs mode. Put it inside the capture
region for feedback.

## Vocabulary

| Term | Meaning |
|---|---|
| **chain** | Filtergraph text in `ffmpeg -filter_complex` syntax, plus up to ten presets. Edited in the app. |
| **module** | One filter instance in the chain. Named with `filter@name`; unnamed ones get libavfilter's `Parsed_filter_N`. |
| **knob** | A numeric option you wrote in the chain that the filter accepts at runtime. |
| **preset** | A snapshot of every knob value and bypass flag of a chain, in a slot 1 to 10. |
| **tap** | An output label you leave unconnected. Shown as a thumbnail: bottom-right on the bare picture, top-right of the sheet in knobs and chain mode. |
| **project** | One SQLite file holding chains, their presets, and the last geometry. |

## Writing chains

Press `F3`. The editor is a plain text box over the video. Ctrl+Enter applies:
the text is parsed into a throwaway graph first, and only if that succeeds is
the running graph replaced. A parse error shows libavfilter's message in red
under the text and the old picture keeps running. Esc closes.

Rules of the road:

- **Plain ffmpeg syntax.** `,` chains filters, `;` separates branches,
  `[labels]` connect them. `split`, `blend`, `overlay`, `hstack`, `tblend`
  and friends all work. Line breaks are allowed; they are removed before parsing.
- **What you write, you can turn.** Every option you wrote that the filter
  accepts at runtime becomes a knob, with the value in the text as its neutral.
  Options you did not write stay hidden. `gblur=sigma=2` gives one knob;
  `gblur=sigma=2:steps=1` gives two. Expression values like `t*0.1` or `iw/2`
  stay in the text and get no knob.
- **Name your filters** (`rotate@rot=...`) if you want presets to survive
  re-ordering; unnamed filters are keyed by position.
- **Bypass** works on every filter with timeline support; `enable=0` in the
  text starts it bypassed.
- **`{W}` and `{H}`** are replaced by the capture width and height before
  parsing. That is the only addition to ffmpeg's syntax; it lets
  `crop@zoom=w=iw/1.02:h=ih/1.02,scale={W}:{H}` survive a region change.
- **Taps:** any output label you do not consume becomes a preview thumbnail.
  `split=3[a][b][peek]` with `[peek]` left dangling shows that stage in the
  corner while `[a]` and `[b]` continue.
- **Output:** the label `[out]` if present, else the unlabelled end of the
  chain, is what fills the window. It is scaled to the capture size and
  converted to BGRA for you.

Only the `crop` zoom trick is special-cased: `w=iw/Z:h=ih/Z` yields one `zoom`
knob that drives both.

## Modes

The window is modal. One sheet, drawn in the same place with the same header in
every mode, and one mode at a time owns the keyboard. The header shows the
F-key tabs with the active one highlighted, then chain, preset and fps; the
footer shows the keys that matter in that mode.

| Mode | Key | What you see |
|---|---|---|
| **picture** | Esc | just the video, plus a short notice now and then |
| **help** | `F1` (or Ctrl+`h`) | libavfilter's filter list and option tables |
| **knobs** | `F2` (or `h`) | one row per knob, presets, taps; the mouse works on the rows |
| **chain** | `F3` (or `e`) | the chain text; Ctrl+Enter applies |
| **project** | `F4` | the chains in the project (switch, new, rename, delete), capture region and fps |

Esc returns to the picture; so does the F-key of the mode you are in. Help is
a detour: Esc there returns to wherever you opened it from. `q` quits from
picture and knobs; in the text modes it is a letter. The mouse always belongs
to the window: left-drag moves, right-drag resizes, in every mode, even over
the editor or help. Taps are visible on the picture and in knobs and chain
mode; help and project hide them. If the running chain fails, the app switches
to the chain editor by itself with libavfilter's error in red.

Keys that work in every mode:

| Key | Action |
|---|---|
| `F1` `F2` `F3` `F4` | switch mode; the active mode's key returns to the picture |
| PageUp / PageDown | previous / next chain in the project (unapplied edits are dropped) |
| `F12` | save a screenshot of the window (picture plus overlays) as `shot-NNN.bmp` in the app data folder, or in `--shots DIR` |

## Controls

Picture and knobs share these keys:

| Key | Action |
|---|---|
| Ctrl+`n` | new chain (a copy of the current one), opens the editor |
| `c` | pick the capture region: desktop dims, current region shows in red; drag a new one, or Esc / right-click to keep it |
| Tab / Shift+Tab | select next / previous control |
| Up / Down (or Right / Left) | nudge the selected fader one fine step; Ctrl = coarse, Shift = ultra-fine |
| Space | bypass / enable the selected module |
| Backspace | reset selected knob to the value in the text |
| `r` | reset the whole chain to the text values; the preset indicator clears |
| `x` | randomize: knobs wander a little from neutral, some modules switch on; the preset indicator clears |
| Shift + `x` | randomize wildly (full ranges) |
| `1`–`9`, `0` | load preset slot 1–10 (physical digit row, so it works on AZERTY) |
| Shift + `1`–`9`, `0` | save current knobs to that slot |
| `f` | fullscreen toggle |
| `q` | quit, saving geometry |

Every knob is a **fader**, the lab's shared control from `shared/param` and
`shared/ui`, laid out one per row: module name, option, track, readout. The
gestures are the lab's (ROADMAP section 6), the same as in Drone Commander:
drag the track to set, wheel one fine step, Ctrl + wheel coarse, Shift + wheel
ultra-fine, middle click or double click to reset to the value in the text.
The faint tick on the track is that text value. A fader whose range crosses
zero fills outward from it, so a shift left and a shift right read as what
they are. Click the module name to bypass the module. Right-click over the
rows still resizes the window. When a chain has more faders than fit, the rows
scroll to follow the selection and the footer shows `n/total`. Enum options
(blend modes, edge modes) show the constant's name and step through the
constants on click, wheel or arrow.

Chain: arrows, Home/End, Ctrl+Home/End, Shift-selection, Ctrl+A/C/X/V, Tab
inserts two spaces, Ctrl+Enter applies and leaves the cursor where it is,
`F1` opens help on the filter under the cursor. Unapplied edits survive
leaving and re-entering the mode (the footer says `modified`); switching
chains drops them.

Help: type to narrow the list (name matches first, then descriptions), Up/Down
to move, Enter shows the filter's options: type, default, range, and a green
`T` on options that can be changed live. Enter again inserts
`filter@filter=opt=default:...` with its live numeric options into the editor
at the cursor, so the new module arrives with knobs. Backspace or Left goes
back to the list.

Project: Up/Down over the capture row and the chains. On the capture row,
Left/Right change the fps by 5 and Enter (or `c`) picks the region. On a
chain, Enter switches to it, `n` adds a new chain (copy of the current one),
`r` renames it (type, Enter), Delete pressed twice deletes it. The last chain
cannot be deleted.

The window title shows the chain name, the last preset slot and the selected
knob with its value; stderr prints the key map once at start and the selected
knob as it changes.

## Starter chains

- `rack`: the original fixed rack written out as text, with presets
  1 init, 2 spin, 3 tunnel, 4 trail, 5 invert, 6 edge, 7 chroma, 8 melt.
- `mirror`: `crop,split,hflip,hstack` with hue and rotate knobs.
- `kaleido`: four-way mirror with a `half` tap showing the two-way stage, plus
  a trail (`lagfun`) and contrast and saturation knobs (`eq`).

## Testing

`vsynth --selftest` is the regression check. `tools/uitest.ps1` (Windows)
drives a running instance with posted key messages and grabs screenshots of
the editor, an applied edit and a parse error; it needs no focus, but SDL
reads real modifier state, so Shift and Ctrl chords do not register from it.
