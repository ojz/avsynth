# vsynth

A video feedback synthesizer. Captures a screen region, runs it through a fixed
rack of libavfilter modules with live knobs, and shows the result in a
borderless window you drag into the capture region. See [PRD.md](PRD.md).

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

From a normal PowerShell, `C:\msys64\ucrt64\bin` must be on PATH for the DLLs.
