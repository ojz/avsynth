# Drives a running vsynth with posted key messages and grabs screenshots of the
# editor, an applied edit and a parse error. Needs no focus; run from PowerShell
# with the UCRT64 DLLs on PATH. Shift/Ctrl chords do not register (SDL reads
# real modifier state), so preset saving is left to --selftest.
param([string]$Exe = "$PSScriptRoot\..\build\vsynth.exe", [string]$Out = "$env:TEMP\vsynth-uitest")
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
  static bool Ext(uint vk) { return (vk >= 0x21 && vk <= 0x2E) || vk == 0x2D; }
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
function Shot($name) {
  $r = New-Object W32+RECT
  [void][W32]::GetWindowRect($script:hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $bmp.Save("$sp\$name.png")
  $g.Dispose(); $bmp.Dispose()
  "shot $name at $($r.L),$($r.T) ${w}x${h}"
}
function Tap($vk) { [W32]::KeyDown($script:hwnd, $vk); Start-Sleep -Milliseconds 40; [W32]::KeyUp($script:hwnd, $vk); Start-Sleep -Milliseconds 80 }
function Chord($mod, $vk) { [W32]::KeyDown($script:hwnd, $mod); Start-Sleep -Milliseconds 40; Tap $vk; [W32]::KeyUp($script:hwnd, $mod); Start-Sleep -Milliseconds 80 }
function TypeText($s) { foreach ($c in $s.ToCharArray()) { [W32]::Char($script:hwnd, $c); Start-Sleep -Milliseconds 15 } }
$VK = @{ RBRACKET = 0xDD; PGDN = 0x22; E = 0x45; Q = 0x51; RETURN = 0x0D; CONTROL = 0x11; END = 0x23; SHIFT = 0xA0; TWO = 0x32; ESC = 0x1B; N = 0x4E; TAB = 0x09; UP = 0x26 }

Remove-Item "$sp\drive.vsynth" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $Exe -ArgumentList "--project","$sp\drive.vsynth","--region","100,100,640,480","--win","900,200,640,480" -RedirectStandardError "$sp\drive.log" -PassThru -NoNewWindow
Start-Sleep -Seconds 3
$script:hwnd = [W32]::Find([uint32]$p.Id)
"hwnd $($script:hwnd)"

Tap $VK.PGDN; Start-Sleep -Seconds 2
"after first pgdn: " + ((Get-Content "$sp\drive.log" | Select-String "chain 2/3").Count)
Tap $VK.PGDN; Start-Sleep -Seconds 3
Shot "kaleido"
Tap $VK.E; Start-Sleep -Seconds 1
Shot "editor"
Chord $VK.CONTROL $VK.END
TypeText ",negate@neg"; Start-Sleep -Milliseconds 500
Chord $VK.CONTROL $VK.RETURN; Start-Sleep -Seconds 3
Shot "applied"
Chord $VK.CONTROL $VK.END
TypeText ",bogus=1"
Chord $VK.CONTROL $VK.RETURN; Start-Sleep -Seconds 2
Shot "error"
Tap $VK.ESC; Start-Sleep -Seconds 1
Tap $VK.TAB; Tap $VK.UP; Tap $VK.UP; Start-Sleep -Milliseconds 500   # nudge a knob
Chord $VK.SHIFT $VK.TWO; Start-Sleep -Seconds 1                        # save preset 2
Tap $VK.Q; Start-Sleep -Seconds 2
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "had to kill" }
Get-Content "$sp\drive.log" | Select-String -Pattern "chain|applied|error|tap|saved|voice:|preset|= " | Select-Object -First 40
