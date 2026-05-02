param(
    [string]$Version = "1.0.3912.50"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$thirdPartyDir = Join-Path $repoRoot "third_party"
$packageDir = Join-Path $thirdPartyDir "Microsoft.Web.WebView2"
$packageFile = Join-Path $thirdPartyDir "Microsoft.Web.WebView2.nupkg"
$packageZip = Join-Path $thirdPartyDir "Microsoft.Web.WebView2.zip"
$packageUrl = "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$Version"

if (Test-Path (Join-Path $packageDir "build\native\include\WebView2.h")) {
    Write-Host "WebView2 SDK already exists: $packageDir"
    exit 0
}

New-Item -ItemType Directory -Force -Path $thirdPartyDir | Out-Null

Write-Host "Downloading Microsoft.Web.WebView2 $Version..."
Invoke-WebRequest -Uri $packageUrl -OutFile $packageFile

Copy-Item -Path $packageFile -Destination $packageZip -Force

if (Test-Path $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

Expand-Archive -Path $packageZip -DestinationPath $packageDir -Force
Write-Host "WebView2 SDK restored: $packageDir"
