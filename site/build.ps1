# Builds the web site into site\out, ready to host as static files.
#
#   .\site\build.ps1                         # firmware from firmware\build-watch
#   .\site\build.ps1 -Version 1.0            # shown on the page and to the installer;
#                                            # without it, firmware\version.txt is used
#   .\site\build.ps1 -Name "Another Name"    # to try the site under a different name
#
# The pages in site\src say {{NAME}} and {{VERSION}} wherever those belong;
# this fills them in. If the watch firmware has been built, its parts are
# copied beside the pages and listed in manifest.json for the browser
# installer. Without a build the pages are still made, and the install
# button will have nothing to install.
#
# The parts are kept separate on purpose. The pet's save lives in flash
# between the partition table and the program; one joined file would write
# blanks over it, and every update would cost the owner their pet.
param(
    [string]$Name = "Baandwidth Farms",
    [string]$Version = ""
)
$ErrorActionPreference = "Stop"
$site = $PSScriptRoot
$root = Split-Path $site -Parent
$src = Join-Path $site "src"
$out = Join-Path $site "out"
$build = Join-Path $root "firmware\build-watch"

# The version is the one the firmware is built with (firmware\version.txt).
$versionFile = Join-Path $root "firmware\version.txt"
if (-not $Version -and (Test-Path $versionFile)) {
    $Version = (Get-Content $versionFile -TotalCount 1).Trim()
}
if (-not $Version) {
    $commit = (& git -C $root rev-parse --short HEAD 2>$null)
    $Version = (Get-Date -Format "yyyy.MM.dd") + $(if ($commit) { " ($commit)" } else { "" })
}

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out, (Join-Path $out "firmware") | Out-Null
Copy-Item (Join-Path $src "*") $out -Recurse

# Fill in the name and version. Written without a byte-order mark.
$utf8 = New-Object System.Text.UTF8Encoding($false)
Get-ChildItem $out -Recurse -Include *.html | ForEach-Object {
    $text = [System.IO.File]::ReadAllText($_.FullName, $utf8)
    $text = $text.Replace("{{NAME}}", $Name).Replace("{{VERSION}}", $Version)
    [System.IO.File]::WriteAllText($_.FullName, $text, $utf8)
}

# The firmware: flash_args lists each part and the address it goes to.
$parts = @()
$flashArgs = Join-Path $build "flash_args"
if (Test-Path $flashArgs) {
    foreach ($line in Get-Content $flashArgs) {
        if ($line -match '^(0x[0-9a-fA-F]+)\s+(\S+)$') {
            $file = Join-Path $build $Matches[2]
            if (-not (Test-Path $file)) { throw "flash_args names $($Matches[2]), which is not in $build" }
            $leaf = Split-Path $file -Leaf
            Copy-Item $file (Join-Path $out "firmware\$leaf")
            $parts += [ordered]@{ path = "firmware/$leaf"; offset = [Convert]::ToInt32($Matches[1], 16) }
        }
    }
}
if ($parts.Count -eq 0) {
    Write-Warning "No watch firmware build found (run firmware\build.ps1 -Board watch). The site was built without it."
} else {
    $parts = @($parts | Sort-Object { $_.offset })
    $size = [math]::Round((Get-ChildItem (Join-Path $out "firmware") | Measure-Object Length -Sum).Sum / 1MB, 1)
    Write-Host "firmware: $($parts.Count) parts, $size MB, version $Version"
}

$manifest = [ordered]@{
    name = $Name
    version = $Version
    new_install_prompt_erase = $true
    builds = @([ordered]@{ chipFamily = "ESP32-S3"; parts = $parts })
}
[System.IO.File]::WriteAllText((Join-Path $out "manifest.json"), ($manifest | ConvertTo-Json -Depth 6), $utf8)

Write-Host "site: $out  (name: $Name)"
