# build.ps1 -- 一键编译 x64（VS2022 / MSBuild）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File build.ps1
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Configuration Debug
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Toolset v143 -SdkVersion 10.0.22621.0
#
# 说明：
#   工程文件里写的是需求指定的 v143 / 10.0.22621.0；本脚本会探测本机实际可用的
#   工具集与 Windows SDK 并用 /p: 覆盖，因此在只装了其它版本 VS / SDK 的机器上
#   也能直接编出 x64 程序。

param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Toolset = '',
    [string]$SdkVersion = '',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$sln  = Join-Path $root 'PixelFishTank.sln'

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($p -and (Test-Path $p)) { return $p }
    }
    $guesses = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\*\*\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\MSBuild\Current\Bin\MSBuild.exe",
        'D:\VisualStudio\VSProduct\MSBuild\Current\Bin\MSBuild.exe'
    )
    foreach ($g in $guesses) {
        $hit = Get-ChildItem -Path $g -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw 'Cannot find MSBuild.exe. Install Visual Studio 2022 with Desktop C++ workload, or run msbuild PixelFishTank.sln from a Developer PowerShell.'
}

function Get-VSRoots {
    $list = New-Object System.Collections.ArrayList
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        foreach ($f in (& $vswhere -all -products * -property installationPath)) {
            if ($f) { [void]$list.Add($f) }
        }
    }
    $patterns = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\*\*",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*",
        'D:\VisualStudio\VSProduct'
    )
    foreach ($p in $patterns) {
        foreach ($i in (Get-ChildItem -Path $p -Directory -ErrorAction SilentlyContinue)) {
            [void]$list.Add($i.FullName)
        }
    }
    if ($env:VSINSTALLDIR) { [void]$list.Add($env:VSINSTALLDIR.TrimEnd('\')) }

    $out = New-Object System.Collections.ArrayList
    foreach ($r in $list) {
        if ((Test-Path (Join-Path $r 'VC\Tools\MSVC')) -and ($out -notcontains $r)) { [void]$out.Add($r) }
    }
    return $out
}

function Find-Toolset {
    param($Roots)
    $bestVersion = $null
    $bestName = ''
    foreach ($r in $Roots) {
        $msvcRoot = Join-Path $r 'VC\Tools\MSVC'
        if (-not (Test-Path $msvcRoot)) { continue }
        foreach ($d in (Get-ChildItem $msvcRoot -Directory -ErrorAction SilentlyContinue)) {
            $v = $null
            try { $v = [version]$d.Name } catch { continue }
            if (($null -eq $bestVersion) -or ($v -gt $bestVersion)) {
                $bestVersion = $v
                $bestName = $d.Name
            }
        }
    }
    if (-not $bestName) { return 'v143' }
    $parts = $bestName.Split('.')
    $major = [int]$parts[0]
    $minor = [int]$parts[1]
    if ($major -eq 14 -and $minor -ge 5) { return 'v145' }
    if ($major -eq 14 -and $minor -ge 3) { return 'v143' }
    if ($major -eq 14 -and $minor -ge 2) { return 'v142' }
    return 'v143'
}

function Find-Sdk {
    $roots = New-Object System.Collections.ArrayList
    $kits = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue
    if ($kits.KitsRoot10) { [void]$roots.Add($kits.KitsRoot10) }
    $sdkReg = Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows\v10.0' -ErrorAction SilentlyContinue
    if ($sdkReg.InstallationFolder) { [void]$roots.Add($sdkReg.InstallationFolder) }
    [void]$roots.Add('C:\Program Files (x86)\Windows Kits\10\')
    [void]$roots.Add('D:\Windows Kits\10\')

    $bestVersion = $null
    $bestName = ''
    foreach ($r in $roots) {
        $inc = Join-Path $r 'Include'
        if (-not (Test-Path $inc)) { continue }
        foreach ($d in (Get-ChildItem $inc -Directory -ErrorAction SilentlyContinue)) {
            if ($d.Name -notlike '10.0.*') { continue }
            if ($d.Name -eq '10.0.22621.0') { return '10.0.22621.0' }
            $v = $null
            try { $v = [version]$d.Name.Substring(5) } catch { continue }
            if (($null -eq $bestVersion) -or ($v -gt $bestVersion)) {
                $bestVersion = $v
                $bestName = $d.Name
            }
        }
    }
    if ($bestName) { return $bestName }
    return '10.0.22621.0'
}

$msbuild = Find-MSBuild
$vsRoots = Get-VSRoots

if (-not $Toolset)    { $Toolset    = Find-Toolset -Roots $vsRoots }
if (-not $SdkVersion) { $SdkVersion = Find-Sdk }

Write-Host "MSBuild  : $msbuild"
foreach ($r in $vsRoots) { Write-Host "VS root  : $r" }
Write-Host "Toolset  : $Toolset"
Write-Host "SDK      : $SdkVersion"
Write-Host "Config   : $Configuration|$Platform"
Write-Host ''

& $msbuild $sln /m /nologo /verbosity:minimal "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:PlatformToolset=$Toolset" "/p:WindowsTargetPlatformVersion=$SdkVersion"

if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED, exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

$out = Join-Path $root "build\$Platform\$Configuration"
Write-Host ''
Write-Host "BUILD OK -> $(Join-Path $out 'PixelFishTank.exe')" -ForegroundColor Green
Write-Host "ASSETS   -> $(Join-Path $out 'assets')"
