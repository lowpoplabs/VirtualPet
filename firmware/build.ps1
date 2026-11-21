# Builds (and optionally flashes) the firmware for one board. Each board
# gets its own build folder and sdkconfig so the two never mix.
#
#   .\firmware\build.ps1 -Board watch
#   .\firmware\build.ps1 -Board square -Flash -Port COM5
#   .\firmware\build.ps1 -Board watch -Flash -Port COM4
#
# -Monitor opens the serial monitor after flashing (Ctrl+] to leave).
param(
    [Parameter(Mandatory = $true)][ValidateSet('watch', 'square')][string]$Board,
    [switch]$Flash,
    [switch]$Monitor,
    [string]$Port
)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $env:IDF_PATH) {
    $cfg = Get-Content 'C:\Espressif\tools\eim_idf.json' -Raw | ConvertFrom-Json
    $idf = $cfg.idfInstalled | Where-Object { $_.id -eq $cfg.idfSelectedId }
    . $idf.activationScript
}

$variant = $Board
$defaults = "sdkconfig.defaults;sdkconfig.defaults.$Board"

$idfArgs = @(
    '-C', $here,
    '-B', (Join-Path $here "build-$variant"),
    '-D', "SDKCONFIG=$here\sdkconfig.$variant",
    '-D', "SDKCONFIG_DEFAULTS=$defaults"
)
if ($Port) { $idfArgs += @('-p', $Port) }
$idfArgs += 'build'
if ($Flash) { $idfArgs += 'flash' }
if ($Monitor) { $idfArgs += 'monitor' }

idf.py @idfArgs
exit $LASTEXITCODE
