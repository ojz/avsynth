# vsynth

A video feedback synthesizer. Captures a screen region, runs it through a fixed
rack of libavfilter modules with live knobs, and shows the result in a
borderless window you drag into the capture region. Patches live in a SQLite
project file. See [PRD.md](PRD.md) for the design.

## Build

Dependencies: libavfilter/libavformat/libavcodec/libavdevice/libavutil, SDL2,
SQLite3, CMake, Ninja, pkg-config, a C11 compiler.

**Arch Linux**

```sh
sudo pacman -S gcc cmake ninja pkgconf ffmpeg sdl2 sqlite
cmake -B build -G Ninja && cmake --build build
./build/vsynth
```

**Windows (MSYS2 UCRT64 shell)**

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,ffmpeg,SDL2,sqlite3}
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
vsynth [--project FILE] [--region X,Y,W,H] [--win X,Y,W,H] [--fps N] [--vf CHAIN] [--selftest]
```

The project file defaults to `default.vsynth` in the current directory and is
created on first run with eight starter patches. Capture and window geometry
are remembered in it; CLI flags override. `--vf` runs a raw filter chain
instead of the rack. `--selftest` pushes every knob through the command path
and round-trips a patch, then keeps running.

The window is borderless. Put it inside the capture region for feedback.

| Key | Action |
|---|---|
| left-drag | move window |
| Alt + right-drag | resize window |
| Tab / Shift+Tab | select next / previous control |
| Up / Down (or Right / Left) | nudge selected knob; Shift = fine, Ctrl = coarse |
| Space | bypass / enable the selected module |
| Backspace | reset selected knob to neutral |
| `r` | reset the whole rack |
| `1`–`9`, `0` | load patch slot 1–10 |
| Shift + `1`–`9`, `0` | save current rack to that slot |
| `f` | fullscreen toggle |
| `q` / Esc | quit, saving geometry |

The window title shows the selected control and its value; stderr logs the
same plus fps every two seconds.

## Starter patches

1 init, 2 spin, 3 tunnel, 4 trail, 5 invert, 6 edge, 7 chroma, 8 melt.
Slots 9 and 10 are free. Overwrite any slot with Shift+number.

## Rack

Signal path, in order: rgbashift, crop (zoom), scale (unzoom), rotate, shear,
lenscorrection, lagfun (trail), tmix, tblend (difference), edgedetect, gblur,
unsharp, hue, eq, negate, vignette, noise. Every knob is a libavfilter runtime
command; bypass is the generic `enable` command. Adding a module is one entry
in the table in `src/rack.c`; the project file picks it up on next open.
