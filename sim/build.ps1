# Builds the desktop simulator with MSVC, using the CMake and Ninja that
# ship with the ESP-IDF tools install. First run downloads SDL2 and LVGL.
#
#   .\sim\build.ps1            build
#   .\sim\build.ps1 -Run       build, then open the watch-sized window
param([switch]$Run)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-Tool($name, $root) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $hit = Get-ChildItem $root -Recurse -Filter "$name.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $hit) { throw "$name not found on PATH or under $root" }
    return $hit.FullName
}

$cmake = Find-Tool cmake 'C:\Espressif\tools\cmake'
$ninja = Find-Tool ninja 'C:\Espressif\tools\ninja'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio Build Tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$build = Join-Path $here 'build'
$cfg = "`"$cmake`" -S `"$here`" -B `"$build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl"
$bld = "`"$cmake`" --build `"$build`""
cmd /c "`"$vcvars`" >nul 2>&1 &&$cfg && $bld"
if ($LASTEXITCODE -ne 0) { throw "simulator build failed ($LASTEXITCODE)" }

if ($Run) { & (Join-Path $build 'virtualpet_sim.exe') }
