# Starts the local Massgate stack, each part in its own console window:
#   MariaDB (127.0.0.1:3306), the patch/news web server (127.0.0.1:80), and Massgate (port 3001).
# Run scripts/setup-db.ps1 once before the first start. Close a window to stop that part.

[CmdletBinding()]
param(
	[ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
	[int]$DbPort = 3306,
	[int]$WebPort = 80,
	[int]$MassgatePort = 3001,
	[string]$MariaDbHome
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Bin = Get-MariaDbBin $MariaDbHome

$MyIni = Join-Path $RepoRoot 'runtime/mariadb/data/my.ini'
if (-not (Test-Path $MyIni)) { throw 'No database yet; run scripts/setup-db.ps1 first.' }
if (Test-MariaDbAlive $Bin $DbPort) {
	Write-Host "MariaDB already running on $DbPort."
} else {
	Start-MariaDb $Bin $MyIni $DbPort
}

$WwwRoot = Join-Path $RepoRoot 'share/www-root'
Start-Process -FilePath python -ArgumentList '-m', 'http.server', $WebPort, '--bind', '127.0.0.1', '--directory', "`"$WwwRoot`"" -WindowStyle Minimized
Write-Host "Web server starting on 127.0.0.1:$WebPort."

$Exe = Join-Path $RepoRoot "build/bin/$Configuration/MMassgateServers.exe"
if (-not (Test-Path $Exe)) { throw "$Exe not found; build it first (see README)." }
# Massgate writes its logs into its working directory.
$WorkDir = Join-Path $RepoRoot 'runtime/massgate'
New-Item -ItemType Directory -Force $WorkDir | Out-Null
Copy-Item (Join-Path $RepoRoot 'config.ini') $WorkDir -Force
Start-Process -FilePath $Exe -ArgumentList 'live', '-noboom', '-all', '-dbname', 'live', '-massgateport', $MassgatePort -WorkingDirectory $WorkDir
Write-Host "Massgate starting on port $MassgatePort (logs in runtime/massgate)."
