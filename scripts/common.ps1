# Helpers shared by the local-stack scripts. Dot-source it: . "$PSScriptRoot/common.ps1"

function Get-MariaDbBin([string]$MariaDbHome) {
	$Candidates = @()
	if ($MariaDbHome) { $Candidates += $MariaDbHome }
	if ($env:MARIADB_HOME) { $Candidates += $env:MARIADB_HOME }
	$Scoop = Get-Command scoop -ErrorAction SilentlyContinue
	if ($Scoop) {
		$Prefix = (scoop prefix mariadb 2>$null)
		if ($LASTEXITCODE -eq 0 -and $Prefix) { $Candidates += "$Prefix".Trim() }
	}
	foreach ($c in $Candidates) {
		$Bin = Join-Path $c 'bin'
		if (Test-Path (Join-Path $Bin 'mariadbd.exe')) { return $Bin }
	}
	throw 'MariaDB server not found. Install it (scoop install mariadb) or pass -MariaDbHome / set MARIADB_HOME.'
}

function Test-MariaDbAlive([string]$Bin, [int]$Port) {
	& (Join-Path $Bin 'mariadb-admin.exe') -uroot --host=127.0.0.1 "--port=$Port" --skip-ssl-verify-server-cert --connect-timeout=2 ping 2>$null | Out-Null
	return $LASTEXITCODE -eq 0
}

function Start-MariaDb([string]$Bin, [string]$MyIni, [int]$Port) {
	Write-Host "Starting MariaDB on 127.0.0.1:$Port"
	Start-Process -FilePath (Join-Path $Bin 'mariadbd.exe') -ArgumentList "--defaults-file=`"$MyIni`"" -WindowStyle Hidden | Out-Null
	for ($i = 0; $i -lt 60; $i++) {
		if (Test-MariaDbAlive $Bin $Port) { return }
		Start-Sleep -Milliseconds 500
	}
	throw "MariaDB did not come up on port $Port (see the .err log in the data directory)."
}

# Runs the SQL piped in as root over TCP. -Scalar returns the bare value(s) of the result.
function Invoke-MariaDb {
	param(
		[Parameter(Position = 0)][string]$Bin,
		[Parameter(Position = 1)][int]$Port,
		[string]$Database,
		[switch]$Scalar,
		[Parameter(ValueFromPipeline = $true)][string]$Sql
	)
	begin { $All = New-Object System.Text.StringBuilder }
	process { [void]$All.AppendLine($Sql) }
	end {
		# Root has no password but only exists on loopback; skip the TLS-verification warning.
		$CliArgs = @('-uroot', '--host=127.0.0.1', "--port=$Port", '--skip-ssl-verify-server-cert')
		if ($Scalar) { $CliArgs += @('-N', '-B') }
		if ($Database) { $CliArgs += $Database }
		$Result = $All.ToString() | & (Join-Path $Bin 'mariadb.exe') @CliArgs
		if ($LASTEXITCODE -ne 0) { throw 'SQL execution failed.' }
		return $Result
	}
}

# Parses Massgate's config.ini into a flat hashtable: 'section.key' => value.
function Read-MassgateConfig([string]$Path) {
	$Cfg = @{}
	$Section = ''
	foreach ($Line in Get-Content $Path) {
		if ($Line -match '^\s*\[(.+)\]\s*$') { $Section = $Matches[1] }
		elseif ($Line -match '^\s*([\w.]+)\s*=\s*(.*?)\s*$') { $Cfg["$Section.$($Matches[1])"] = $Matches[2] }
	}
	return $Cfg
}
