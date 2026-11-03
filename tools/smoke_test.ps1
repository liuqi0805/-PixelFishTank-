# tools/smoke_test.ps1 -- 冒烟测试：启动 -> 截图 -> 关闭 -> 检查 state.json
# 用法：powershell -ExecutionPolicy Bypass -File tools\smoke_test.ps1
param(
    [string]$Exe = '',
    [string]$ShotPath = '',
    [int]$WaitMs = 3500
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe)      { $Exe = Join-Path $root 'build\x64\Release\PixelFishTank.exe' }
if (-not $ShotPath) { $ShotPath = Join-Path $root '_preview\window.png' }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);

    public static IntPtr FindByClass(uint target, string clsName) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h,l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid == target) {
                var cls = new StringBuilder(256); GetClassName(h, cls, 256);
                if (cls.ToString() == clsName) { found = h; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

[void][W]::SetProcessDPIAware()

$stateFile = Join-Path (Split-Path -Parent $Exe) 'state.json'
if (Test-Path $stateFile) { Remove-Item $stateFile -Force }

Get-Process PixelFishTank -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds $WaitMs

if ($p.HasExited) { throw "进程提前退出，退出码 $($p.ExitCode)" }

$h = [IntPtr]::Zero
for ($i = 0; $i -lt 50 -and $h -eq [IntPtr]::Zero; $i++) {
    $h = [W]::FindByClass([uint32]$p.Id, 'PixelFishTankWindow')
    if ($h -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
}
if ($h -eq [IntPtr]::Zero) { throw '找不到 PixelFishTankWindow 窗口' }
Write-Host "hwnd = $h"

$r = New-Object W+RECT
[void][W]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
Write-Host "rect = ($($r.Left),$($r.Top)) ${w}x${ht}"

# 多截几张，便于看到动画
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ShotPath) | Out-Null
for ($k = 1; $k -le 2; $k++) {
    Start-Sleep -Milliseconds 900
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
    $g.Dispose()
    $out = $ShotPath -replace '\.png$', "_$k.png"
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "shot -> $out"
}

# 模拟右键菜单：向其窗口发送 WM_RBUTTONDOWN/UP（客户区坐标 60,60）
[void][W]::PostMessage($h, 0x0204, [IntPtr]0, [IntPtr](60 -bor (60 -shl 16)))
[void][W]::PostMessage($h, 0x0205, [IntPtr]0, [IntPtr](60 -bor (60 -shl 16)))
Start-Sleep -Milliseconds 600
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$g.Dispose()
$menuShot = $ShotPath -replace '\.png$', '_menu.png'
$bmp.Save($menuShot, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "shot -> $menuShot"

[void][W]::PostMessage($h, 0x0010, [IntPtr]0, [IntPtr]0)   # WM_CLOSE
Start-Sleep -Milliseconds 800

# 现在的默认行为是「关闭 = 收进托盘」（settings.closeToTray = true），
# 所以 WM_CLOSE 之后进程应该还活着、窗口应该已经隐藏 —— 这才是正确表现。
$stillAlive = -not $p.HasExited
$stillVisible = [W]::IsWindowVisible($h)
Write-Host "after WM_CLOSE: alive = $stillAlive, window visible = $stillVisible"
if (-not $stillAlive) { throw '关闭后进程不该直接退出（closeToTray 默认开启）' }
if ($stillVisible) { throw '关闭后窗口应该已隐藏到托盘' }
$p.Kill()
$p.WaitForExit(3000) | Out-Null
Write-Host "hidden to tray OK, process killed by test"

if (Test-Path $stateFile) {
    Write-Host "state.json OK ($((Get-Item $stateFile).Length) bytes)"
    Get-Content $stateFile -Raw | Select-Object -First 1
} else {
    throw '没有生成 state.json'
}
