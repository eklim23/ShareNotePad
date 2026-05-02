param(
    [Parameter(Mandatory = $true)]
    [string]$HostName,

    [string]$RemoteDir = "~/sharenotepad",

    [string]$SshPath = "C:\Windows\System32\OpenSSH\ssh.exe",

    [string]$ScpPath = "C:\Windows\System32\OpenSSH\scp.exe",

    [switch]$NoInstall
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$relayDir = Join-Path $repoRoot "relay-server"

if (-not (Test-Path $relayDir)) {
    throw "relay-server directory not found: $relayDir"
}

if (-not (Test-Path $SshPath)) {
    throw "ssh executable not found: $SshPath"
}

if (-not (Test-Path $ScpPath)) {
    throw "scp executable not found: $ScpPath"
}

Write-Host "Creating remote directory..."
& $SshPath $HostName "mkdir -p $RemoteDir"

Write-Host "Uploading relay-server..."
& $ScpPath -r $relayDir "${HostName}:$RemoteDir/"

if ($NoInstall) {
    Write-Host "Uploaded only. Skipping install."
    exit 0
}

Write-Host "Running remote install..."
& $SshPath $HostName "cd $RemoteDir/relay-server && chmod +x scripts/install-linux.sh && ./scripts/install-linux.sh"

Write-Host "Done."
