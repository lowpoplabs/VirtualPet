# Builds Pet Listener, the Android test app, and optionally installs it on
# a phone plugged in with USB debugging switched on.
#
#   .\android\listener\build.ps1
#   .\android\listener\build.ps1 -Install
#
# Needs Android Studio (for its Java and the Android SDK its setup wizard
# downloads). Gradle fetches itself through the wrapper.
param(
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$studio = 'C:\Program Files\Android\Android Studio\jbr'
if (-not (Test-Path "$studio\bin\java.exe")) { throw "Android Studio's Java was not found at $studio" }
$sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
if (-not (Test-Path $sdk)) { throw "No Android SDK at $sdk. Open Android Studio once and finish its setup wizard." }

$env:JAVA_HOME = $studio
# Keep Gradle's downloads off the system drive when there is somewhere else for them.
if (Test-Path 'D:\Android') { $env:GRADLE_USER_HOME = 'D:\Android\gradle-home' }
Set-Content -Path (Join-Path $here 'local.properties') -Value ("sdk.dir=" + ($sdk -replace '\\', '/')) -Encoding ascii

Push-Location $here
try {
    & (Join-Path $here 'gradlew.bat') --no-daemon -q assembleDebug
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

$apk = Join-Path $here 'app\build\outputs\apk\debug\app-debug.apk'
Write-Host "built: $apk"

if ($Install) {
    $adb = Join-Path $sdk 'platform-tools\adb.exe'
    & $adb install -r $apk
    if ($LASTEXITCODE -ne 0) { throw "install failed: is the phone plugged in, with USB debugging allowed?" }
    Write-Host 'installed: open "Pet Listener" on the phone'
}
