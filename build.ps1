<#
    Kite build script.

    Uses the CMake and Ninja that ship inside Visual Studio 2022, so nothing
    extra has to be installed.

        .\build.ps1              # configure + build Release
        .\build.ps1 -Config Debug
        .\build.ps1 -Clean
        .\build.ps1 -Run
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root "build\$Config"

# --- locate the toolchain ---------------------------------------------------
$vsRoot = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath 2>$null

if (-not $vsRoot) {
    foreach ($candidate in @(
            "$env:ProgramFiles\Microsoft Visual Studio\2022\Community",
            "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional",
            "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise",
            "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools")) {
        if (Test-Path (Join-Path $candidate 'VC\Auxiliary\Build\vcvars64.bat')) {
            $vsRoot = $candidate
            break
        }
    }
}
if (-not $vsRoot) { throw 'Visual Studio 2022 with the C++ toolset was not found.' }

$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source }
if (-not $cmake) { throw 'cmake.exe was not found.' }

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

# --- import the MSVC environment once, then run cmake in it -----------------
# vcvars64.bat only exports into a cmd session, so capture and replay its
# environment into this PowerShell process.
$envDump = & "$env:ComSpec" /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
    }
}

$generatorArgs = @()
if (Test-Path $ninja) {
    $generatorArgs = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja")
} else {
    $generatorArgs = @('-G', 'Visual Studio 17 2022', '-A', 'x64')
}

& $cmake -S $root -B $buildDir @generatorArgs "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

& $cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

$exe = Join-Path $buildDir 'kite.exe'
if (-not (Test-Path $exe)) { $exe = Join-Path $buildDir "$Config\kite.exe" }

Write-Host ''
Write-Host "Built: $exe" -ForegroundColor Green
if (Test-Path $exe) {
    Write-Host ("Size:  {0:N0} bytes" -f (Get-Item $exe).Length)
}

if ($Run -and (Test-Path $exe)) { & $exe }
