# Drives a running vsynth with posted key messages and grabs screenshots of each
# mode: panel, editor, help list, help detail, an inserted filter applied, and a
# parse error. Needs no focus; run from PowerShell. Shift/Ctrl+letter chords do
# not register (SDL reads real modifier state), so preset saving is left to
# --selftest. Ctrl+Enter does work.
param([string]$Exe = "$PSScriptRoot\..\..\..\build\bin\vsynth.exe", [string]$Out = "$env:TEMP\vsynth-uitest")
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
New-Item -ItemType Directory -Force $Out | Out-Null
$sp = $Out
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W32 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static IntPtr found = IntPtr.Zero;
  public static IntPtr Find(uint pid) {
    found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h)) {
        var sb = new StringBuilder(64); GetClassName(h, sb, 64);
        if (sb.ToString() == "SDL_app") { found = h; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  static bool Ext(uint vk) { return (vk >= 0x21 && vk <= 0x2E); }   // PgUp..Delete, arrows
  // WM_KEYDOWN / WM_KEYUP with the scancode in lParam, the way real input arrives.
  public static void KeyDown(IntPtr h, uint vk) {
    uint sc = MapVirtualKey(vk, 0);
    PostMessage(h, 0x0100, (IntPtr)vk, (IntPtr)((sc << 16) | 1 | (Ext(vk) ? 0x01000000u : 0)));
  }
  public static void KeyUp(IntPtr h, uint vk) {
    uint sc = MapVirtualKey(vk, 0);
    PostMessage(h, 0x0101, (IntPtr)vk, (IntPtr)((sc << 16) | 1 | 0xC0000000 | (Ext(vk) ? 0x01000000u : 0)));
  }
  public static void Char(IntPtr h, char c) { PostMessage(h, 0x0102, (IntPtr)c, (IntPtr)1); }
}
"@
[void][W32]::SetProcessDPIAware()
$script:hwnd = [IntPtr]::Zero
# F12 makes the app save its own frame buffer (shot-NNN.bmp in --shots), so
# windows sitting on top of it do not matter. Converted to PNG with ffmpeg.
$script:shot_n = 0
function Shot($name) {
  $script:shot_n++
  [W32]::KeyDown($script:hwnd, 0x7B); Start-Sleep -Milliseconds 40; [W32]::KeyUp($script:hwnd, 0x7B)
  Start-Sleep -Milliseconds 400
  $bmp = "$sp\shot-{0:D3}.bmp" -f $script:shot_n
  if (Test-Path $bmp) {
    & ffmpeg -y -loglevel error -i $bmp "$sp\$name.png"
    Remove-Item $bmp
    "shot $name"
  } else { "shot $name MISSING ($bmp)" }
}
function Tap($vk) { [W32]::KeyDown($script:hwnd, $vk); Start-Sleep -Milliseconds 40; [W32]::KeyUp($script:hwnd, $vk); Start-Sleep -Milliseconds 80 }
function Chord($mod, $vk) { [W32]::KeyDown($script:hwnd, $mod); Start-Sleep -Milliseconds 40; Tap $vk; [W32]::KeyUp($script:hwnd, $mod); Start-Sleep -Milliseconds 80 }
function TypeText($s) { foreach ($c in $s.ToCharArray()) { [W32]::Char($script:hwnd, $c); Start-Sleep -Milliseconds 15 } }
$VK = @{ PGDN = 0x22; E = 0x45; H = 0x48; Q = 0x51; RETURN = 0x0D; CONTROL = 0x11; END = 0x23; ESC = 0x1B; TAB = 0x09; UP = 0x26; F1 = 0x70; F2 = 0x71; F3 = 0x72; F4 = 0x73 }

Remove-Item "$sp\drive.vsynth" -ErrorAction SilentlyContinue
Remove-Item "$sp\shot-*.bmp" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $Exe -ArgumentList "--project","$sp\drive.vsynth","--shots","$sp","--region","100,100,640,480","--win","900,200,640,480" -RedirectStandardError "$sp\drive.log" -PassThru -NoNewWindow
Start-Sleep -Seconds 3
$script:hwnd = [W32]::Find([uint32]$p.Id)
"hwnd $($script:hwnd)"

Shot "panel"
Tap $VK.ESC; Start-Sleep -Milliseconds 500
Shot "main"
Tap $VK.PGDN; Start-Sleep -Seconds 2
Tap $VK.PGDN; Start-Sleep -Seconds 3            # kaleido, has a tap
Tap $VK.F2; Start-Sleep -Milliseconds 500
Shot "kaleido"
Tap $VK.F4; Start-Sleep -Milliseconds 500
Shot "project"
Tap $VK.F4; Start-Sleep -Milliseconds 300
Tap $VK.F3; Start-Sleep -Seconds 1
Shot "editor"
Tap $VK.F1; Start-Sleep -Milliseconds 500
TypeText "hue"; Start-Sleep -Milliseconds 500
Shot "help-list"
Tap $VK.RETURN; Start-Sleep -Milliseconds 500
Shot "help-detail"
Tap $VK.RETURN; Start-Sleep -Milliseconds 500   # insert into the editor
Shot "inserted"
Chord $VK.CONTROL $VK.RETURN; Start-Sleep -Seconds 3
Shot "applied"
Chord $VK.CONTROL $VK.END
TypeText ",bogus=1"
Chord $VK.CONTROL $VK.RETURN; Start-Sleep -Seconds 2
Shot "error"
Tap $VK.ESC; Start-Sleep -Milliseconds 500      # back to the picture; the editor keeps the bad text
Shot "main-after"
Tap $VK.F3; Start-Sleep -Milliseconds 500
Shot "editor-dirty"
Tap $VK.ESC; Start-Sleep -Milliseconds 500
Tap $VK.Q; Start-Sleep -Seconds 2
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "had to kill" }
Get-Content "$sp\drive.log" | Select-String -Pattern "chain|applied|error|tap|saved|voice:|preset|inserted" | Select-Object -First 40
