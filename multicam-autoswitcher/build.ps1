# Compile le plugin (Windows). Exemple :
#   .\build.ps1 -ObsSource "C:/chemin/obs-studio-32.2.2" -QtPrefix "C:/Qt/6.11.1/msvc2022_64"
param(
	[Parameter(Mandatory = $true)][string]$ObsSource,
	[Parameter(Mandatory = $true)][string]$QtPrefix,
	[string]$ObsBin = "C:\Program Files\obs-studio\bin\64bit"
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path "$PSScriptRoot\implibs\obs.lib")) {
	& "$PSScriptRoot\make-implibs.ps1" -ObsBin $ObsBin
}

cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -G "Visual Studio 17 2022" -A x64 "-DOBS_SOURCE_DIR=$ObsSource" "-DCMAKE_PREFIX_PATH=$QtPrefix"
if ($LASTEXITCODE -ne 0) { throw "La configuration CMake a echoue" }

cmake --build "$PSScriptRoot\build" --config Release
if ($LASTEXITCODE -ne 0) { throw "La compilation a echoue" }

Write-Host ""
Get-ChildItem "$PSScriptRoot\build\Release\*.dll" | ForEach-Object { Write-Host "OK : $($_.FullName)" }
