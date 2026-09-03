# vsynth

A video feedback synthesizer. Captures a screen region, runs it through a
libavfilter chain you write as text, and shows the result in a borderless
window you drag into the capture region. Knobs are derived from the chain.
Chains and their presets live in a SQLite project file. See [PRD.md](PRD.md)
for the design.

## Build

Dependencies: libavfilter/libavformat/libavcodec/libavdevice/libavutil, SDL2, SDL2_ttf,
SQLite3, CMake, Ninja, pkg-config, a C11 compiler.

**Arch Linux**

```sh
sudo pacman -S gcc cmake ninja pkgconf ffmpeg sdl2 sdl2_ttf sqlite
cmake -B build -G Ninja && cmake --build build
./build/vsynth
```

**Windows (MSYS2 UCRT64 shell)**

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,ffmpeg,SDL2,SDL2_ttf,sqlite3}
cmake -B build -G Ninja && cmake --build build
./build/vsynth.exe
```

From a normal PowerShell, `C:\msys64\ucrt64\bin` must be on PATH for the DLLs:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
.\build\vsynth.exe --region 200,200,640,480 --win 1000,300,640,480
```

## Run

```
vsynth [--project FILE] [--region X,Y,W,H] [--win X,Y,W,H] [--fps N] [--vf CHAIN] [--selftest] [--screenshot FILE.bmp]
```

The project file defaults to `default.vsynth` in the per-user app data folder
(`%APPDATA%\vsynth\` on Windows, `~/.local/share/vsynth/` on Linux) and is
created on first run with three chains: `rack` (the classic fixed rack with
eight presets), `mirror` and `kaleido`. Capture and window geometry are
remembered in it; CLI flags override. `--vf` runs the given chain text instead
of the project's current one. `--selftest` derives racks from the built-in
chains, pushes every knob through the command path, round-trips a preset, and
then keeps running.

The panel text uses a system monospace font (Consolas on Windows, DejaVu Sans
Mono or Liberation Mono on Linux); without one the panel still shows bars.

The window is borderless. Put it inside the capture region for feedback.

## Vocabulary

| Term | Meaning |
|---|---|
| **chain** | Filtergraph text in `ffmpeg -filter_complex` syntax, plus up to ten presets. Edited in the app. |
| **module** | One filter instance in the chain. Named with `filter@name`; unnamed ones get libavfilter's `Parsed_filter_N`. |
| **knob** | A numeric option you wrote in the chain that the filter accepts at runtime. |
| **preset** | A snapshot of every knob value and bypass flag of a chain, in a slot 1 to 10. |
| **tap** | An output label you leave unconnected. Shown as a thumbnail in the corner. |
| **project** | One SQLite file holding chains, their presets, and the last geometry. |

## Writing chains

Press `e`. The editor is a plain text box over the video. Ctrl+Enter applies:
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

## Controls

| Mouse | Action |
|---|---|
| left-drag on the video | move window |
| right-drag on the video | resize window (drag right/down = bigger) |
| click a panel row | select that knob |
| drag on a row's bar | set the knob |
| click a module name | bypass / enable that module |
| wheel over a row | nudge the knob; Shift = fine, Ctrl = coarse |
| click in the editor | place the cursor; drag selects |

| Key | Action |
|---|---|
| `e` | open the chain editor; Ctrl+Enter applies, Esc closes |
| PageUp / PageDown | previous / next chain in the project |
| Ctrl+`n` | new chain (a copy of the current one), opens the editor |
| `c` | pick the capture region: desktop dims, current region shows in red; drag a new one, or Esc / right-click to keep it |
| Tab / Shift+Tab | select next / previous control |
| Up / Down (or Right / Left) | nudge selected knob; Shift = fine, Ctrl = coarse |
| Space | bypass / enable the selected module |
| Backspace | reset selected knob to the value in the text |
| `r` | reset the whole chain to the text values |
| `x` | randomize: knobs wander a little from neutral, some modules switch on |
| Shift + `x` | randomize wildly (full ranges) |
| `1`–`9`, `0` | load preset slot 1–10 (physical digit row, so it works on AZERTY) |
| Shift + `1`–`9`, `0` | save current knobs to that slot |
| `h` | hide / show the panel and taps |
| `f` | fullscreen toggle |
| `q` / Esc | quit, saving geometry |

Inside the editor: arrows, Home/End, Ctrl+Home/End, Shift-selection,
Ctrl+A/C/X/V, Tab inserts two spaces.

The panel header shows chain name, preset slot, capture region and fps.
stderr logs the same.

## Starter chains

- `rack`: the original fixed rack written out as text, with presets
  1 init, 2 spin, 3 tunnel, 4 trail, 5 invert, 6 edge, 7 chroma, 8 melt.
- `mirror`: `crop,split,hflip,hstack` with hue and rotate knobs.
- `kaleido`: four-way mirror with a `half` tap showing the two-way stage.

## Testing

`vsynth --selftest` is the regression check. `tools/uitest.ps1` (Windows)
drives a running instance with posted key messages and grabs screenshots of
the editor, an applied edit and a parse error; it needs no focus, but SDL
reads real modifier state, so Shift and Ctrl chords do not register from it.
