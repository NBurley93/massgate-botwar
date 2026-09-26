# Registers this machine's World in Conflict CD key with the local Massgate database.
#
# Massgate derives each session's encryption key from the client's CD key, so a key has
# to exist in the CdKeys table before the game (or a dedicated server using it) can log in.
# The key is read from the registry, validated and encoded by MMassgateServers.exe itself,
# and written to CdKeys. It is never printed.

[CmdletBinding()]
param(
	# Register this key instead of the one in the registry (e.g. a dedicated server's key).
	[string]$Key,
	[string]$DatabaseName = 'live',
	[int]$Port = 3306,
	# 255 = every flag the seeded keys have, including isRankedServer (needed for a ranked DS).
	[int]$GroupMembership = 255,
	[string]$MariaDbHome
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Bin = Get-MariaDbBin $MariaDbHome

if (-not $Key) {
	$RegPath = 'HKCU:\Software\Massive Entertainment AB\World In Conflict'
	$Key = (Get-ItemProperty -Path $RegPath -Name CDKEY -ErrorAction SilentlyContinue).CDKEY
	if (-not $Key) { throw "No CDKEY value under $RegPath. Start World in Conflict once so it stores its key." }
}
# The tool's command-line parser treats '-' as the start of a new option.
$Key = ($Key -replace '[-\s]', '').ToUpperInvariant()

$Exe = @('build/bin/Release', 'build/bin/Debug') |
	ForEach-Object { Join-Path $RepoRoot "$_/MMassgateServers.exe" } |
	Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Exe) { throw 'MMassgateServers.exe not found; build it first (see README).' }

# The tool needs a working directory with config.ini and leaves log files behind.
$WorkDir = Join-Path ([IO.Path]::GetTempPath()) "massgate-keytool-$PID"
New-Item -ItemType Directory -Force $WorkDir | Out-Null
try {
	Copy-Item (Join-Path $RepoRoot 'config.ini') $WorkDir
	Push-Location $WorkDir
	try { $Output = & $Exe -dbname $DatabaseName -keysequence $Key 2>&1 | Out-String }
	finally { Pop-Location }
}
finally {
	Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
}
$Key = $null

if ($Output -match 'invalid checksum') { throw 'Massgate does not recognize this CD key (invalid checksum).' }
if ($Output -notmatch 'Key has sequence number (\d+)') { throw 'Could not validate the CD key; is MariaDB running and config.ini correct?' }
$Sequence = [uint32]$Matches[1]
$BatchId = if ($Output -match 'Key has batch id (\d+)') { [uint32]$Matches[1] } else { 0 }
$ProductId = if ($Output -match 'Key has product id (\d+)') { [uint32]$Matches[1] } else { 0 }
if ($Output -notmatch 'Encrypted key: ([A-Z0-9]+)') { throw 'Could not encode the CD key.' }
$Encoded = $Matches[1]
$Output = $null

@"
INSERT INTO CdKeys (sequenceNumber, cdKey, batchId, numAccountCreations, maxAccountCreations, groupMembership)
VALUES ($Sequence, '$Encoded', $BatchId, 0, 2147483647, $GroupMembership)
ON DUPLICATE KEY UPDATE cdKey = VALUES(cdKey), isBanned = 0, maxAccountCreations = VALUES(maxAccountCreations), groupMembership = VALUES(groupMembership);
"@ | Invoke-MariaDb $Bin $Port -Database $DatabaseName | Out-Null

Write-Host "Registered CD key: sequence number $Sequence, product id $ProductId, group membership $GroupMembership."
