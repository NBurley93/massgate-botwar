# Removes every ghost (simulated ladder player) from the local Massgate database, so the
# next Massgate start creates them again from share/ghosts/callsigns.txt.
#
# Only rows belonging to profiles of the ghost account (ghosts@botwar.invalid) are deleted;
# real players' profiles and stats are left alone. Stop Massgate first: it caches stats.

[CmdletBinding(SupportsShouldProcess)]
param(
	[int]$Port = 3306,
	[string]$DatabaseName = 'live',
	[string]$MariaDbHome
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$Bin = Get-MariaDbBin $MariaDbHome

if (Get-Process MMassgateServers -ErrorAction SilentlyContinue) {
	throw 'Massgate is running; stop it first (it caches player stats and would write ghosts back).'
}

$Ghosts = "SELECT p.profileId FROM Profiles p JOIN Accounts a ON a.accountid = p.accountId WHERE a.email = 'ghosts@botwar.invalid'"
$Count = [int]("SELECT COUNT(*) FROM ($Ghosts) g" | Invoke-MariaDb $Bin $Port -Database $DatabaseName -Scalar)
if ($Count -eq 0) {
	Write-Host 'No ghosts to remove.'
	return
}

if ($PSCmdlet.ShouldProcess("$Count ghost profiles", 'Delete')) {
	# Collect the ids first: MariaDB cannot delete from a table it also selects from.
	@"
CREATE TEMPORARY TABLE GhostIds AS $Ghosts;
DELETE FROM BestOfLadder WHERE profileId IN (SELECT profileId FROM GhostIds);
DELETE FROM PlayerStats WHERE profileId IN (SELECT profileId FROM GhostIds);
DELETE FROM PlayerMedals WHERE profileId IN (SELECT profileId FROM GhostIds);
DELETE FROM PlayerBadges WHERE profileId IN (SELECT profileId FROM GhostIds);
DELETE FROM GhostPlayers WHERE profileId IN (SELECT profileId FROM GhostIds);
DELETE FROM Profiles WHERE profileId IN (SELECT profileId FROM GhostIds);
"@ | Invoke-MariaDb $Bin $Port -Database $DatabaseName | Out-Null
	Write-Host "Removed $Count ghosts. They are created again on the next Massgate start."
}
