# Fetches and builds the third-party dependencies that are not vendored in 3rdparty/.
#
# Currently: MariaDB Connector/C, built as a 32-bit DLL. Massgate is x86-only for now
# (inline __asm in MCommon2), and MariaDB only ships Connector/C for Windows as source.
#
# Output: deps/mariadb-connector-c-x86 (picked up by the CMake presets via MYSQL_ROOT).

[CmdletBinding()]
param(
	[switch]$Force
)

$ErrorActionPreference = 'Stop'

$ConnectorVersion = '3.4.9'
$ConnectorSha256  = 'db9bc2886615856d71f8b87a3f53fbc8f3a3fb4e32184424199b97993a355755'

$RepoRoot   = Split-Path -Parent $PSScriptRoot
$DepsDir    = Join-Path $RepoRoot 'deps'
$SrcDir     = Join-Path $DepsDir 'src'
$BuildDir   = Join-Path $DepsDir 'build/mariadb-connector-c'
$InstallDir = Join-Path $DepsDir 'mariadb-connector-c-x86'

if ((Test-Path (Join-Path $InstallDir 'lib/mariadb/libmariadb.dll')) -and -not $Force) {
	Write-Host "MariaDB Connector/C already installed in $InstallDir (use -Force to rebuild)."
	return
}

New-Item -ItemType Directory -Force $SrcDir | Out-Null

$ZipName = "mariadb-connector-c-$ConnectorVersion-src.zip"
$ZipPath = Join-Path $SrcDir $ZipName
if (-not (Test-Path $ZipPath)) {
	$Url = "https://downloads.mariadb.org/rest-api/connector-c/$ConnectorVersion/$ZipName"
	Write-Host "Downloading $Url"
	Invoke-WebRequest -Uri $Url -OutFile $ZipPath -UseBasicParsing
}

$ActualSha256 = (Get-FileHash -Algorithm SHA256 $ZipPath).Hash.ToLowerInvariant()
if ($ActualSha256 -ne $ConnectorSha256) {
	Remove-Item $ZipPath
	throw "SHA-256 mismatch for $ZipName (got $ActualSha256). Deleted the download."
}

$ExtractedDir = Join-Path $SrcDir "mariadb-connector-c-$ConnectorVersion-src"
if (-not (Test-Path $ExtractedDir)) {
	Expand-Archive -Path $ZipPath -DestinationPath $SrcDir
}

cmake -S $ExtractedDir -B $BuildDir -G 'Visual Studio 17 2022' -A Win32 `
	"-DCMAKE_INSTALL_PREFIX=$InstallDir" `
	-DWITH_UNIT_TESTS=OFF `
	-DWITH_CURL=OFF `
	-DWITH_SSL=SCHANNEL
if ($LASTEXITCODE -ne 0) { throw 'Configuring MariaDB Connector/C failed.' }

cmake --build $BuildDir --config Release --target install -- -m
if ($LASTEXITCODE -ne 0) { throw 'Building MariaDB Connector/C failed.' }

Write-Host "MariaDB Connector/C $ConnectorVersion installed in $InstallDir"
