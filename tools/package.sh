#!/bin/sh
# Package every app for release: its executable, the libraries it links from
# the toolchain prefix, and its README, zipped into DIST_DIR.
#
# Usage: tools/package.sh [BIN_DIR] [DIST_DIR]      (normally: make package)
#
# The apps link the toolchain's SDL3, ffmpeg and SQLite rather than vendoring
# them (ROADMAP D1), so a Windows release has to carry those DLLs next to the
# executable, where the loader looks first. On Linux nothing is bundled: the
# distribution's packages provide it.
#
# Dependencies are read with objdump rather than ldd on purpose. ldd resolves
# by actually loading the image, so it depends on PATH being in a form the
# Windows loader understands, which is not true when make is started from
# PowerShell. objdump reads the import table instead and needs no loader.

set -eu

BIN=${1:-build/bin}
DIST=${2:-dist}

APPS="drone_commander vsynth"

# Where the toolchain keeps its DLLs. A name the import table asks for that
# exists here is ours to ship; anything else belongs to Windows.
LIBDIR=$(dirname "$(command -v cc 2>/dev/null || command -v gcc)")
[ -d "$LIBDIR" ] || LIBDIR=/ucrt64/bin

readme_for() {
    case $1 in
    drone_commander) echo apps/drone-commander/README.md ;;
    vsynth)          echo apps/video-synth/README.md ;;
    *)               echo "" ;;
    esac
}

imports_of() {
    objdump -p "$1" 2>/dev/null | sed -n 's/.*DLL Name: *//p' | tr -d '\r'
}

# Walk the import tree breadth-first, copying each toolchain DLL once. It
# terminates because a round only queues names that were not already copied.
bundle_libs() {
    out=$2
    pending=$(imports_of "$1")
    while [ -n "$pending" ]; do
        next=""
        for name in $pending; do
            if [ -f "$out/$name" ]; then
                continue
            fi
            if [ ! -f "$LIBDIR/$name" ]; then
                continue
            fi
            cp -f "$LIBDIR/$name" "$out/"
            next="$next $(imports_of "$LIBDIR/$name")"
        done
        pending=$next
    done
}

[ -d "$BIN" ] || { echo "package: no such directory: $BIN (run make build first)" >&2; exit 1; }

mkdir -p "$DIST"
packaged=0

for app in $APPS; do
    # Test the .exe name first: MSYS2's test resolves an extensionless path to
    # the .exe anyway, which would hide the suffix and mislabel the platform.
    exe=$BIN/$app.exe
    [ -f "$exe" ] || exe=$BIN/$app
    if [ ! -f "$exe" ]; then
        echo "package: $app not built, skipping" >&2
        continue
    fi

    out=$DIST/$app
    rm -rf "$out"
    mkdir -p "$out"
    cp -f "$exe" "$out/"

    readme=$(readme_for "$app")
    if [ -n "$readme" ] && [ -f "$readme" ]; then
        cp -f "$readme" "$out/"
    fi

    case $exe in
    *.exe) bundle_libs "$exe" "$out"; zip=$app-windows.zip ;;
    *)     zip=$app-linux.zip ;;
    esac

    rm -f "$DIST/$zip"
    (cd "$DIST" && cmake -E tar cf "$zip" --format=zip "$app")
    echo "package: $DIST/$zip  ($(ls -1 "$out" | wc -l | tr -d ' ') files)"
    packaged=$((packaged + 1))
done

[ "$packaged" -gt 0 ] || { echo "package: nothing was packaged" >&2; exit 1; }
