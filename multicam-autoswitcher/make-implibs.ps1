# Genere obs.lib et obs-frontend-api.lib a partir des DLL de l'OBS installe.
# Peut etre lance depuis un PowerShell normal : le script charge tout seul
# l'environnement Visual Studio 2022 (dumpbin / lib).
param(
	[string]$ObsBin = "C:\Program Files\obs-studio\bin\64bit",
	[string]$Out = "$PSScriptRoot\implibs"
)

$ErrorActionPreference = "Stop"

function Enable-VsTools {
	if ((Get-Command dumpbin -ErrorAction SilentlyContinue) -and (Get-Command lib -ErrorAction SilentlyContinue)) { return }

	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (-not (Test-Path $vswhere)) {
		throw "Visual Studio introuvable. Installez Visual Studio 2022 (ou Build Tools) avec la charge 'Developpement Desktop en C++'."
	}
	$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (-not $vs) {
		throw "Outils C++ (MSVC) introuvables. Dans Visual Studio Installer, cochez 'Developpement Desktop en C++'."
	}
	$bat = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
	if (-not (Test-Path $bat)) { throw "vcvars64.bat introuvable : $bat" }

	# Importe l'environnement de vcvars64.bat dans cette session PowerShell
	$envDump = cmd /c "`"$bat`" >nul 2>&1 && set"
	foreach ($line in $envDump) {
		if ($line -match '^([^=]+)=(.*)$') {
			[System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
		}
	}
	if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw "dumpbin toujours introuvable apres chargement de Visual Studio." }
}

Enable-VsTools
New-Item -ItemType Directory -Force $Out | Out-Null

foreach ($name in @("obs", "obs-frontend-api")) {
	$dll = Join-Path $ObsBin "$name.dll"
	if (-not (Test-Path $dll)) { throw "DLL introuvable : $dll" }

	$names = foreach ($l in (& dumpbin /exports $dll)) {
		if ($l -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $Matches[1] }
	}
	if (-not $names) { throw "Aucun export trouve dans $dll" }

	$def = Join-Path $Out "$name.def"
	(@("LIBRARY $name", "EXPORTS") + $names) | Set-Content -Path $def -Encoding ascii
	& lib /nologo "/def:$def" /machine:x64 "/out:$Out\$name.lib"
	if ($LASTEXITCODE -ne 0) { throw "lib.exe a echoue pour $name" }
	Write-Host "OK : $Out\$name.lib ($($names.Count) exports)"
}
