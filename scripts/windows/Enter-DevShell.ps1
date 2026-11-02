<#
.SYNOPSIS
    Puts the current PowerShell session into a Visual Studio x64 developer environment.
.DESCRIPTION
    Finds the newest Visual Studio with the C++ x64 toolset through vswhere, so builds work with
    any installed Visual Studio version. Run it inside "Developer PowerShell for VS" too: it clears
    the Platform variable that breaks "dotnet build" of the solution.

    When vswhere lists no installation although Visual Studio is on disk (the installer's
    instance records under %ProgramData%\Microsoft\VisualStudio\Packages\_Instances are missing),
    it falls back to the newest vcvars64.bat under the standard install folders.

    Running it again in a session that already has the environment changes nothing: every run of
    vcvars64.bat lengthens PATH, and past cmd.exe's line limit vcvars64.bat fails.
#>

# The script is dot-sourced, so the work runs in a child scope: a 'Stop' preference left in the
# caller's session would turn harmless stderr output of later native commands into errors.
& {
    $ErrorActionPreference = 'Stop'

    if ($env:VSCMD_VER -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Remove-Item Env:Platform -ErrorAction SilentlyContinue
        Write-Host "Visual Studio developer environment already set up ($env:VSCMD_VER)"
        return
    }

    function Enter-ThroughVsWhere {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path $vswhere)) {
            return $false
        }
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $installPath) {
            return $false
        }
        Import-Module (Join-Path $installPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
        Enter-VsDevShell -VsInstallPath $installPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
        Write-Host "Visual Studio developer environment: $installPath"
        return $true
    }

    function Enter-ThroughVcVars {
        # Enter-VsDevShell also depends on the installer's records, so import vcvars64.bat directly.
        $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | ForEach-Object { Join-Path $_ 'Microsoft Visual Studio' }
        $candidates = foreach ($root in $roots) {
            if (-not (Test-Path $root)) { continue }
            Get-ChildItem $root -Directory | Get-ChildItem -Directory | ForEach-Object {
                $vcvars = Join-Path $_.FullName 'VC\Auxiliary\Build\vcvars64.bat'
                $msvc = Join-Path $_.FullName 'VC\Tools\MSVC'
                if ((Test-Path $vcvars) -and (Test-Path $msvc)) {
                    $newest = Get-ChildItem $msvc -Directory | ForEach-Object { [version]$_.Name } |
                        Sort-Object -Descending | Select-Object -First 1
                    if ($newest) {
                        [pscustomobject]@{ InstallPath = $_.FullName; VcVars = $vcvars; Msvc = $newest }
                    }
                }
            }
        }
        $chosen = $candidates | Sort-Object Msvc -Descending | Select-Object -First 1
        if (-not $chosen) {
            return $false
        }

        # vcvars64.bat calls vswhere itself and prints its failure; only the resulting variables
        # matter, and its own output is shown only when it fails.
        $output = Join-Path $env:TEMP 'osdrop-vcvars.log'
        $environment = & cmd.exe /c "call `"$($chosen.VcVars)`" >`"$output`" 2>&1 && set"
        if ($LASTEXITCODE -ne 0) {
            $reason = (Get-Content $output -ErrorAction SilentlyContinue | Select-Object -Last 5) -join "`n"
            throw "vcvars64.bat failed: $($chosen.VcVars)`n$reason`nOpen a new PowerShell window and run this script again."
        }
        foreach ($line in $environment) {
            if ($line -match '^([^=]+)=(.*)$') {
                Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
            }
        }

        # vcvars64.bat does not add the CMake and Ninja that ship with Visual Studio; without them
        # an unrelated cmake earlier on PATH is used.
        $cmakeTools = Join-Path $chosen.InstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
        $env:PATH = (Join-Path $cmakeTools 'CMake\bin') + ';' + (Join-Path $cmakeTools 'Ninja') + ';' + $env:PATH

        Write-Host "Visual Studio developer environment (vcvars64.bat, the installer lists no instances): $($chosen.InstallPath)"
        return $true
    }

    if (-not (Enter-ThroughVsWhere) -and -not (Enter-ThroughVcVars)) {
        throw ('No Visual Studio with the "Desktop development with C++" workload was found. Install it, ' +
            'or run Repair from the Visual Studio Installer if it is installed but not detected.')
    }

    # The developer environment sets Platform=x64, which makes "dotnet build Osdrop.slnx" look for
    # a "Release|x64" solution configuration that does not exist. CMake and Ninja do not use it.
    Remove-Item Env:Platform -ErrorAction SilentlyContinue
}
