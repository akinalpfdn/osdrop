<#
.SYNOPSIS
    Builds the Windows installer, windows\build\installer\Osdrop-Setup.exe.
.DESCRIPTION
    Builds the engine, the input process and the service with the release preset and runs their
    tests, publishes the app self-contained, stages the license texts, and compiles
    windows\installer\Osdrop.iss with Inno Setup.

    Run it from the repository root in a shell set up by scripts\windows\Enter-DevShell.ps1.
    Needs the .NET SDK from windows\app\global.json and Inno Setup 6 or 7 (winget install
    JRSoftware.InnoSetup). The release workflow runs this same script; see docs/RELEASING.md.
.PARAMETER SkipTests
    Skips the native tests, for a quick local rebuild of the installer only.
#>
[CmdletBinding()]
param(
    [switch] $SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$windows = Join-Path $repo 'windows'
$nativeBin = Join-Path $windows 'build\release\bin'
$output = Join-Path $windows 'build\installer'
$stage = Join-Path $output 'stage'

# Native commands do not stop the script on failure in Windows PowerShell 5.1.
function Invoke-Native {
    param([string] $Command, [string[]] $Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Find-InnoSetupCompiler {
    $command = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    # The script works with both; the release workflow pins Inno Setup 6.
    $candidates = foreach ($folder in @('Inno Setup 7', 'Inno Setup 6')) {
        Join-Path ${env:ProgramFiles(x86)} "$folder\ISCC.exe"
        Join-Path $env:ProgramFiles "$folder\ISCC.exe"
        Join-Path $env:LOCALAPPDATA "Programs\$folder\ISCC.exe"
    }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw 'Inno Setup was not found. Install it with: winget install JRSoftware.InnoSetup'
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'The MSVC compiler is not on PATH. Run this first: . .\scripts\windows\Enter-DevShell.ps1'
}
$iscc = Find-InnoSetupCompiler

# One version for the whole release: the Windows app's, which scripts/release_version.py keeps
# equal to the Mac app's and the tag.
$version = ([xml](Get-Content (Join-Path $windows 'app\Directory.Build.props') -Raw)).Project.PropertyGroup.Version
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "windows\app\Directory.Build.props has no X.Y.Z <Version> (found '$version')"
}
Write-Host "==> Osdrop $version"

Write-Host '==> Engine, input process and service (release preset)'
Push-Location $windows
try {
    Invoke-Native cmake @('--preset', 'release')
    Invoke-Native cmake @('--build', '--preset', 'release')
    if (-not $SkipTests) {
        Invoke-Native ctest @('--preset', 'release')
    }
}
finally {
    Pop-Location
}

if (Test-Path $stage) {
    Remove-Item $stage -Recurse -Force
}
$appStage = Join-Path $stage 'app'
$licenseStage = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force -Path $appStage, $licenseStage | Out-Null

Write-Host '==> App (self-contained, win-x64)'
Invoke-Native dotnet @(
    'publish', (Join-Path $windows 'app\Osdrop.App\Osdrop.App.csproj'),
    '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true',
    "-p:OsdropNativeBin=$nativeBin", '-o', $appStage
)
# The project copies the native files only when they exist, so a missing one must fail here.
foreach ($file in @('Osdrop.exe', 'OsdropEngine.dll', 'OsdropInput.exe', 'OsdropService.exe')) {
    if (-not (Test-Path (Join-Path $appStage $file))) {
        throw "The published app has no $file"
    }
}
# A developer tool that types and clicks; it is not for users.
Remove-Item (Join-Path $appStage 'OsdropInputTool.exe') -ErrorAction SilentlyContinue

Write-Host '==> Licenses'
$dotnetRoot = Split-Path (Get-Command dotnet).Source
$licenses = [ordered]@{
    'LICENSE.txt'                  = Join-Path $repo 'LICENSE'
    'Monocypher-LICENSE.txt'       = Join-Path $repo 'core\third_party\monocypher\LICENCE.md'
    'Asio-LICENSE.txt'             = Join-Path $repo 'core\third_party\asio\LICENSE_1_0.txt'
    'dotnet-LICENSE.txt'           = Join-Path $dotnetRoot 'LICENSE.txt'
    'dotnet-ThirdPartyNotices.txt' = Join-Path $dotnetRoot 'ThirdPartyNotices.txt'
}
foreach ($entry in $licenses.GetEnumerator()) {
    if (-not (Test-Path $entry.Value)) {
        throw "License text not found: $($entry.Value)"
    }
    Copy-Item $entry.Value (Join-Path $licenseStage $entry.Key)
}
Copy-Item (Join-Path $windows 'installer\licenses\*.txt') $licenseStage

Write-Host '==> Installer'
Invoke-Native $iscc @(
    '/Qp', "/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$output",
    (Join-Path $windows 'installer\Osdrop.iss')
)
$installer = Join-Path $output 'Osdrop-Setup.exe'
# The installer is not code-signed, so the published hash is how a download can be checked.
$installerHash = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Path "$installer.sha256" -Value "$installerHash  Osdrop-Setup.exe" -Encoding ascii
Write-Host "Built $installer"
Write-Host "SHA-256 $installerHash"
