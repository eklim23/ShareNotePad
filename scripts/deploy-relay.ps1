param(
    [Parameter(Mandatory = $true)]
    [string]$HostName,

    [string]$RemoteDir = "~/sharenotepad",

    [switch]$NoInstall
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$relayDir = Join-Path $repoRoot "relay-server"

if (-not (Test-Path $relayDir)) {
    throw "relay-server directory not found: $relayDir"
}

Write-Host "Creating remote directory..."
ssh $HostName "mkdir -p $RemoteDir"

Write-Host "Uploading relay-server..."
scp -r $relayDir "${HostName}:$RemoteDir/"

if ($NoInstall) {
    Write-Host "Uploaded only. Skipping install."
    exit 0
}

Write-Host "Running remote install..."
ssh $HostName "cd $RemoteDir/relay-server && chmod +x scripts/install-linux.sh && ./scripts/install-linux.sh"

Write-Host "Done."
