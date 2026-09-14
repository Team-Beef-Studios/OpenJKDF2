<#
.SYNOPSIS
    Packages the Windows PCVR (OpenXR) build of OpenJKDF2 / JKDF2-XR into a
    single distributable .zip that end users can extract and drop their game
    files into.

.DESCRIPTION
    Stages the built executable + required runtime DLLs, the engine resource
    folder (shaders/ui/ssl), the pre-tuned VR weapon offsets, the
    Play-JKDF2-XR.bat launcher, and a user instructions file, then zips them up.

      - The ZIP FILE name carries the version (e.g. JKDF2-XR-PCVR-v0.6.0.zip).
      - The folder INSIDE the zip is just "JKDF2-XR" (no version), so updating
        is a clean overwrite for users.
      - A version marker text file (JKDF2-XR-vX.Y.Z.txt) is included so users
        can see which build they extracted.

    No game assets are bundled - users supply their own copy of JKDF2.

.PARAMETER BuildDir
    The CMake build directory containing the PCVR build. Default: build_pcvr

.PARAMETER OutputDir
    Where to write the staging folder and the final zip. Default: <repo>/dist

.PARAMETER WeaponsJson
    The VR weapon-offsets json to bundle. Default: the tuned copy committed
    next to this script (packaging/pcvr/files/jkdf2xr_vr_weapons.json).
    Point this at your live runtime json if you've re-tuned weapons.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\pcvr\package-pcvr.ps1
#>

[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutputDir,
    [string]$WeaponsJson
)

$ErrorActionPreference = 'Stop'

# --- Paths -----------------------------------------------------------------
$scriptDir = $PSScriptRoot
$repo      = (Resolve-Path (Join-Path $scriptDir '..\..')).Path

if (-not $BuildDir)    { $BuildDir    = Join-Path $repo 'build_pcvr' }
if (-not $OutputDir)   { $OutputDir   = Join-Path $repo 'dist' }
if (-not $WeaponsJson) { $WeaponsJson = Join-Path $scriptDir 'files\jkdf2xr_vr_weapons.json' }

$productName = 'JKDF2-XR'
$folderName  = 'JKDF2-XR'          # folder inside the zip - NO version number

# --- Read the VR version from the single source of truth -------------------
$versionCmake = Join-Path $repo 'cmake_modules\version.cmake'
if (-not (Test-Path $versionCmake)) { throw "version.cmake not found at $versionCmake" }

$vrVerMatch = Select-String -Path $versionCmake -Pattern 'OPENJKDF2VR_PROJECT_VERSION\s+([0-9][0-9.]*)'
if (-not $vrVerMatch) { throw "Could not find OPENJKDF2VR_PROJECT_VERSION in $versionCmake" }
$version = $vrVerMatch.Matches[0].Groups[1].Value

$engVerMatch = Select-String -Path $versionCmake -Pattern 'OPENJKDF2_PROJECT_VERSION\s+([0-9][0-9.]*)'
$engineVersion = if ($engVerMatch) { $engVerMatch.Matches[0].Groups[1].Value } else { 'unknown' }

# Git short commit (best effort)
$commit = 'unknown'
try { $commit = (& git -C $repo rev-parse --short=8 HEAD).Trim() } catch { }

$packDate = (Get-Date -Format 'yyyy-MM-dd')

Write-Host "Packaging $productName PCVR v$version (engine $engineVersion, commit $commit)" -ForegroundColor Cyan

# --- Locate build artifacts ------------------------------------------------
$exe = Join-Path $BuildDir 'Release\jkdf2xr.exe'
if (-not (Test-Path $exe)) {
    throw @"
Built executable not found:
    $exe

Build the PCVR target first, e.g.:
    cd $repo
    cmake -S . -B build_pcvr -G "Visual Studio 17 2022" -A x64 -DTARGET_USE_VR=ON
    cmake --build build_pcvr --config Release --target openjkdf2-64
"@
}

# Required runtime DLLs (live in the build_pcvr root)
$dllNames = @('OpenAL32.dll', 'exchndl.dll', 'mgwhelp.dll', 'symsrv.dll')

$resourceSrc = Join-Path $repo 'resource'
if (-not (Test-Path $resourceSrc)) { throw "Engine resource folder not found at $resourceSrc" }
if (-not (Test-Path $WeaponsJson)) { throw "Weapons json not found at $WeaponsJson" }

$howToPlay = Join-Path $scriptDir 'files\HOW-TO-PLAY.txt'
$launcherBat = Join-Path $scriptDir 'files\Play-JKDF2-XR.bat'

# --- Stage -----------------------------------------------------------------
$staging = Join-Path $OutputDir $folderName
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# Executable
Copy-Item $exe (Join-Path $staging 'jkdf2xr.exe')

# DLLs
foreach ($dll in $dllNames) {
    $src = Join-Path $BuildDir $dll
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $staging $dll)
    } else {
        Write-Warning "DLL not found in build dir, skipping: $dll  ($src)"
    }
}

# Engine resources (shaders/ui/ssl/patrons.txt) - NOT the user's game GOBs
Copy-Item -Recurse $resourceSrc (Join-Path $staging 'resource')

# Pre-tuned VR weapon offsets
Copy-Item $WeaponsJson (Join-Path $staging 'jkdf2xr_vr_weapons.json')

# Mysteries of the Sith lives in its own folder - running with -motsCompat chdirs into it, so
# it needs its own copy of the offsets (the file covers both games' bin ranges).
$motsDir = Join-Path $staging 'mots'
New-Item -ItemType Directory -Force -Path $motsDir | Out-Null
Copy-Item $WeaponsJson (Join-Path $motsDir 'jkdf2xr_vr_weapons.json')
@(
    "Put your Mysteries of the Sith files in this folder."
    ""
    "You need these, copied from your MotS install:"
    "    Episode\JKM.GOO, JKM_KFY.GOO, JKM_MP.GOO, JKM_SABER.GOO"
    "    Resource\JKMRES.GOO, JKMsndLO.goo, JK_.CD"
    "    Resource\VIDEO\   (cutscenes, optional)"
    "    MUSIC\            (optional)"
    ""
    "Then run Play-JKDF2-XR.bat in the folder above and choose Mysteries of the Sith."
    "(or launch jkdf2xr.exe -motsCompat by hand)"
) | Set-Content -Encoding ascii (Join-Path $motsDir 'PUT-MOTS-FILES-HERE.txt')

# User instructions
if (Test-Path $howToPlay) { Copy-Item $howToPlay (Join-Path $staging 'HOW-TO-PLAY.txt') }

# Launcher - detects which games the user supplied and offers a chooser when both are present
if (Test-Path $launcherBat) {
    Copy-Item $launcherBat (Join-Path $staging 'Play-JKDF2-XR.bat')
} else {
    Write-Warning "Launcher batch file not found, skipping: $launcherBat"
}

# Version marker file (name + version visible at a glance)
$marker = Join-Path $staging "$productName-v$version.txt"
@(
    "$productName  -  OpenJKDF2 PCVR (OpenXR)"
    ""
    "Version:   v$version"
    "Engine:    OpenJKDF2 $engineVersion"
    "Commit:    $commit"
    "Packaged:  $packDate"
    ""
    "Star Wars: Jedi Knight Dark Forces II in VR."
    "See HOW-TO-PLAY.txt to add your game files and start."
) | Set-Content -Encoding ascii $marker

# --- Zip -------------------------------------------------------------------
$zipName = "$productName-PCVR-v$version.zip"
$zipPath = Join-Path $OutputDir $zipName
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

# Compress the staging folder itself so the archive contains a single
# top-level "JKDF2-XR/" directory.
Compress-Archive -Path $staging -DestinationPath $zipPath -CompressionLevel Optimal

$zipSizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "  Staging : $staging"
Write-Host "  Zip     : $zipPath  ($zipSizeMB MB)"
Write-Host ""
Write-Host "Users: extract, copy game files into the JKDF2-XR folder, run Play-JKDF2-XR.bat."
