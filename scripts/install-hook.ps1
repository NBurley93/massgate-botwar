# Installs the botwar hook (build/bin/<config>/hook/dbghelp.dll) into the World in Conflict
# folder, or removes it with -Uninstall.
#
# The game's own dbghelp.dll is kept as dbghelp.dll.botwar-backup and restored on uninstall.
# The hook only patches wic_ds.exe; wic.exe and the modkit just get dbghelp forwarded to Windows.

[CmdletBinding(SupportsShouldProcess)]
param(
	# Defaults to the "World in Conflict" link in the repo root.
	[string]$GameDir,
	[ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
	[switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $GameDir) { $GameDir = Join-Path $RepoRoot 'World in Conflict' }
if (-not (Test-Path (Join-Path $GameDir 'wic_ds.exe'))) { throw "No wic_ds.exe in '$GameDir'; pass -GameDir." }
$GameDir = (Get-Item $GameDir).FullName

$Target = Join-Path $GameDir 'dbghelp.dll'
$Backup = Join-Path $GameDir 'dbghelp.dll.botwar-backup'

function Test-BotwarHook([string]$Path) {
	(Test-Path $Path) -and [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path)).Contains('botwar hook')
}

if (Get-Process wic, wic_ds, wic_online -ErrorAction SilentlyContinue) {
	throw 'World in Conflict or its dedicated server is running; close it first.'
}

if ($Uninstall) {
	if (-not (Test-BotwarHook $Target)) {
		Write-Host 'The hook is not installed.'
		return
	}
	if (-not (Test-Path $Backup)) {
		throw "No backup of the game's dbghelp.dll ($Backup). Verify the game files in Steam to restore it."
	}
	if ($PSCmdlet.ShouldProcess($Target, "Restore the game's dbghelp.dll")) {
		Move-Item $Backup $Target -Force
		Write-Host "Restored the game's dbghelp.dll."
	}
	return
}

$Source = Join-Path $RepoRoot "build/bin/$Configuration/hook/dbghelp.dll"
if (-not (Test-Path $Source)) { throw "$Source not found; build it first (see README)." }

if ((Test-Path $Target) -and -not (Test-BotwarHook $Target)) {
	if (Test-Path $Backup) {
		throw "Both $Target and $Backup exist and neither is the hook; sort them out by hand."
	}
	if ($PSCmdlet.ShouldProcess($Target, "Back up the game's dbghelp.dll")) {
		Move-Item $Target $Backup
		Write-Host "Backed up the game's dbghelp.dll to $Backup."
	}
}
if ($PSCmdlet.ShouldProcess($Target, "Install the hook ($Configuration)")) {
	Copy-Item $Source $Target -Force
	Write-Host "Installed the hook. wic_ds.exe writes botwar_hook.log in $GameDir."
}

# Bots are named from the ghost ladder's callsigns. An existing setting is left alone.
$Ini = Join-Path $GameDir 'botwar_hook.ini'
$Callsigns = (Resolve-Path (Join-Path $RepoRoot 'share/ghosts/callsigns.txt')).Path
if (-not ((Test-Path $Ini) -and (Select-String -Path $Ini -Pattern '^\s*callsigns\s*=' -Quiet))) {
	if ($PSCmdlet.ShouldProcess($Ini, 'Name bots from share/ghosts/callsigns.txt')) {
		Add-Content -Path $Ini -Value "[bots]`r`ncallsigns=$Callsigns" -Encoding ascii
		Write-Host "Bots will be named from $Callsigns (see $Ini)."
	}
}
