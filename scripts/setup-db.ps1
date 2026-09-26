# Creates (or brings up to date) a local MariaDB instance for Massgate.
#
# - Initializes a private data directory under runtime/ (no Windows service, no admin).
# - Configures it the way Massgate needs: loopback only, legacy sql_mode, enough connections.
# - Starts it if it is not running.
# - Creates the database and the two accounts from config.ini, and loads the schema.
#
# Safe to re-run: every step checks whether it has already been done.
# Requires MariaDB server binaries (e.g. `scoop install mariadb`).

[CmdletBinding()]
param(
	[string]$DataDir,
	[int]$Port = 3306,
	[string]$DatabaseName = 'live',
	[string]$ConfigFile,
	[string]$MariaDbHome
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DataDir) { $DataDir = Join-Path $RepoRoot 'runtime/mariadb/data' }
if (-not $ConfigFile) { $ConfigFile = Join-Path $RepoRoot 'config.ini' }
$Bin = Get-MariaDbBin $MariaDbHome
$DataDir = [IO.Path]::GetFullPath($DataDir)

# 1. Data directory.
$MyIni = Join-Path $DataDir 'my.ini'
if (-not (Test-Path (Join-Path $DataDir 'mysql'))) {
	Write-Host "Initializing MariaDB data directory in $DataDir"
	New-Item -ItemType Directory -Force (Split-Path -Parent $DataDir) | Out-Null
	& (Join-Path $Bin 'mariadb-install-db.exe') "--datadir=$DataDir" "--port=$Port"
	if ($LASTEXITCODE -ne 0) { throw 'mariadb-install-db failed.' }
}

# 2. Server settings Massgate depends on.
$Settings = [ordered]@{
	'bind-address'    = '127.0.0.1'
	# Massgate was written against MySQL 4/5.0: permissive modes, zero dates allowed.
	'sql_mode'        = '""'
	# MMassgateServers -all alone holds ~150 pooled connections.
	'max_connections' = '500'
}
$Ini = Get-Content $MyIni
$Out = New-Object System.Collections.Generic.List[string]
$InMysqld = $false
foreach ($Line in $Ini) {
	if ($Line -match '^\[(.+)\]') {
		if ($InMysqld) { foreach ($k in $Settings.Keys) { $Out.Add("$k=$($Settings[$k])") }; $Settings.Clear() }
		$InMysqld = $Matches[1] -eq 'mysqld'
	}
	elseif ($InMysqld -and $Line -match '^([\w-]+)=' -and $Settings.Contains($Matches[1])) {
		$Out.Add("$($Matches[1])=$($Settings[$Matches[1]])")
		$Settings.Remove($Matches[1])
		continue
	}
	$Out.Add($Line)
}
if ($InMysqld) { foreach ($k in $Settings.Keys) { $Out.Add("$k=$($Settings[$k])") } }
Set-Content -Path $MyIni -Value $Out

# 3. Server process.
if (-not (Test-MariaDbAlive $Bin $Port)) {
	Start-MariaDb $Bin $MyIni $Port
}

# 4. Database and accounts, from the same config.ini Massgate reads.
$Cfg = Read-MassgateConfig $ConfigFile
$Users = @(
	@{ User = $Cfg['writedb.user']; Password = $Cfg['writedb.password'] },
	@{ User = $Cfg['readdb.user'];  Password = $Cfg['readdb.password'] }
)
$Sql = "CREATE DATABASE IF NOT EXISTS ``$DatabaseName`` CHARACTER SET latin1;`n"
foreach ($u in $Users) {
	foreach ($h in 'localhost', '127.0.0.1') {
		$Sql += "CREATE USER IF NOT EXISTS '$($u.User)'@'$h' IDENTIFIED BY '$($u.Password)';`n"
		$Sql += "GRANT ALL ON ``$DatabaseName``.* TO '$($u.User)'@'$h';`n"
	}
}
$Sql | Invoke-MariaDb $Bin $Port

# 5. Schema (only into an empty database).
$TableCount = [int]("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$DatabaseName'" | Invoke-MariaDb $Bin $Port -Scalar)
if ($TableCount -eq 0) {
	Write-Host 'Loading share/sql/databasestructure.sql'
	Get-Content (Join-Path $RepoRoot 'share/sql/databasestructure.sql') -Raw | Invoke-MariaDb $Bin $Port -Database $DatabaseName
}
Get-Content (Join-Path $RepoRoot 'share/sql/fixes.sql') -Raw | Invoke-MariaDb $Bin $Port -Database $DatabaseName
$TableCount = [int]("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$DatabaseName'" | Invoke-MariaDb $Bin $Port -Scalar)
Write-Host "Database '$DatabaseName' ready on 127.0.0.1:$Port with $TableCount tables."
