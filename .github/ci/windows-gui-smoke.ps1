# Launches the packaged notepadqq.exe like a user would (double click) and checks that it
# actually comes up: one responsive main window, also after being killed (leftover snapshots)
# and when started a second time. On failure it saves a screenshot and the stack of every
# thread (cdb) so hangs can be diagnosed.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$SymbolPath = "",
    [string]$OutDir = "gui-smoke"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$Exe = (Resolve-Path $Exe).Path

Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class NqqWin {
    delegate bool EnumProc(IntPtr hwnd, IntPtr lp);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr hwnd, uint cmd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);
    public static string Text(IntPtr h) { var sb = new StringBuilder(512); GetWindowTextW(h, sb, 512); return sb.ToString(); }
    public static string Class(IntPtr h) { var sb = new StringBuilder(256); GetClassNameW(h, sb, 256); return sb.ToString(); }
    /** Visible, unowned top-level windows of a process: what shows up in the taskbar. */
    public static List<IntPtr> TaskbarWindows(uint pid) {
        var list = new List<IntPtr>();
        EnumWindows((h, lp) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h) && GetWindow(h, 4 /* GW_OWNER */) == IntPtr.Zero) list.Add(h);
            return true;
        }, IntPtr.Zero);
        return list;
    }
    /** True when the window answers a message within the timeout. */
    public static bool Responds(IntPtr h, uint timeoutMs) {
        IntPtr r;
        return SendMessageTimeoutW(h, 0 /* WM_NULL */, IntPtr.Zero, IntPtr.Zero, 0x2 /* SMTO_ABORTIFHUNG */, timeoutMs, out r) != IntPtr.Zero;
    }
}
"@

$script:failures = @()

function Save-Screenshot([string]$name) {
    try {
        $b = [System.Windows.Forms.SystemInformation]::VirtualScreen
        $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
        $bmp.Save((Join-Path $OutDir "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
    } catch { Write-Host "screenshot failed: $_" }
}

function Save-Stacks([System.Diagnostics.Process]$p, [string]$name) {
    $cdb = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $cdb) { Write-Host "cdb not available"; return }
    $sym = if ($SymbolPath) { $SymbolPath } else { Split-Path $Exe }
    $log = Join-Path $OutDir "$name-stacks.txt"
    # -pv: non-invasive attach, works even when the target is hung.
    & $cdb.FullName -pv -p $p.Id -y $sym -lines -c "~*kpn 80; q" 2>&1 | Out-File -Encoding utf8 $log
    Write-Host "===== stacks of $name ====="
    Get-Content $log | Select-Object -Last 250 | ForEach-Object { Write-Host $_ }
}

function Describe([System.Diagnostics.Process]$p) {
    $wins = [NqqWin]::TaskbarWindows([uint32]$p.Id)
    foreach ($h in $wins) {
        Write-Host ("  window 0x{0:x} class='{1}' title='{2}' hung={3}" -f $h.ToInt64(), [NqqWin]::Class($h), [NqqWin]::Text($h), [NqqWin]::IsHungAppWindow($h))
    }
    return $wins
}

function Stop-Nqq {
    Get-Process notepadqq -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

# Starts notepadqq.exe and waits until it shows a responsive main window.
function Start-Nqq([string]$name, [int]$expectedWindows = 1, [string[]]$arguments = @()) {
    Write-Host "----- $name -----"
    $env:QT_FORCE_STDERR_LOGGING = "1"
    $startArgs = @{ FilePath = $Exe; PassThru = $true; RedirectStandardError = (Join-Path $OutDir "$name-stderr.txt") }
    if ($arguments.Count -gt 0) { $startArgs.ArgumentList = $arguments }
    $p = Start-Process @startArgs
    $deadline = (Get-Date).AddSeconds(30)
    $wins = @()
    while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
        Start-Sleep -Milliseconds 500
        $wins = [NqqWin]::TaskbarWindows([uint32]$p.Id)
        if ($wins.Count -ge $expectedWindows) { break }
    }
    # Give it time to finish starting (snapshots, deferred initialisation...), then check again.
    Start-Sleep -Seconds 8
    if ($p.HasExited) {
        $script:failures += "${name}: process exited with code $($p.ExitCode)"
        Save-Screenshot $name
        return $null
    }
    $wins = Describe $p
    $hung = @($wins | Where-Object { -not [NqqWin]::Responds($_, 5000) })
    $ok = $true
    if ($wins.Count -ne $expectedWindows) {
        $script:failures += "${name}: expected $expectedWindows window(s), found $($wins.Count)"
        $ok = $false
    }
    if ($hung.Count -gt 0) {
        $script:failures += "${name}: $($hung.Count) window(s) not responding"
        $ok = $false
    }
    Save-Screenshot $name
    if (-not $ok) { Save-Stacks $p $name }
    return $p
}

Stop-Nqq
$settingsDir = Join-Path $env:APPDATA "Notepadqq"
if (Test-Path $settingsDir) { Remove-Item -Recurse -Force $settingsDir }

# 1. First start on a clean profile.
$p = Start-Nqq "first-start"

# 2. Killed like a hung process / power loss; the next start must restore one window, not more.
Stop-Nqq
$p = Start-Nqq "after-kill-1"
Stop-Nqq
$p = Start-Nqq "after-kill-2"

# 3. A second launch while running hands over to the running instance (no new process window).
if ($p -and -not $p.HasExited) {
    $second = Start-Process -FilePath $Exe -PassThru
    if (-not $second.WaitForExit(15000)) {
        $script:failures += "second-instance: the second process did not exit"
        Save-Stacks $second "second-instance"
        $second.Kill()
    }
    Start-Sleep -Seconds 2
    $wins = Describe $p
    if ($wins.Count -ne 1) { $script:failures += "second-instance: expected 1 window, found $($wins.Count)" }
    Save-Screenshot "second-instance"
}

# 4. Normal close must not leave snapshots behind.
if ($p -and -not $p.HasExited) {
    $p.CloseMainWindow() | Out-Null
    if (-not $p.WaitForExit(15000)) {
        # An unsaved-changes prompt is acceptable here: nothing was typed, so there should be none.
        $script:failures += "close: process did not exit after closing its window"
        Save-Screenshot "close"
        Save-Stacks $p "close"
    }
}
Stop-Nqq
$p = Start-Nqq "after-clean-exit"
Stop-Nqq

Write-Host "settings folder:"
Get-ChildItem -Recurse $settingsDir -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $($_.FullName)" }
Get-ChildItem $OutDir -Filter "*-stderr.txt" | ForEach-Object {
    if ((Get-Item $_.FullName).Length -gt 0) {
        Write-Host "===== $($_.Name) ====="
        Get-Content $_.FullName | Select-Object -Last 60 | ForEach-Object { Write-Host $_ }
    }
}

if ($script:failures.Count -gt 0) {
    Write-Host "GUI smoke test FAILED:"
    $script:failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Write-Host "GUI smoke test passed."
