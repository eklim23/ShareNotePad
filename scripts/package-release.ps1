param(
    [string]$Configuration = "Release",
    [string]$OutputRoot = "dist",
    [string]$PackageName = "ShareNotepad"
)

$ErrorActionPreference = "Stop"

$pathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
if (-not $pathValue) {
    $pathValue = [Environment]::GetEnvironmentVariable("PATH", "Process")
}
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$buildDir = Join-Path $repoRoot "build"
$sourceDir = Join-Path $buildDir $Configuration
$outputRootPath = Join-Path $repoRoot $OutputRoot
$packageDir = Join-Path $outputRootPath $PackageName

Write-Host "Building ShareNotepad ($Configuration)..."
cmake --build $buildDir --config $Configuration --target ShareNotepad

$exePath = Join-Path $sourceDir "ShareNotepad.exe"
$loaderPath = Join-Path $sourceDir "WebView2Loader.dll"
$uiPath = Join-Path $sourceDir "ui"
$readmeTemplate = Join-Path $repoRoot "packaging\README.txt"

foreach ($required in @($exePath, $loaderPath, $uiPath)) {
    if (-not (Test-Path $required)) {
        throw "Required build output is missing: $required"
    }
}

if (-not (Test-Path $readmeTemplate)) {
    throw "README template is missing: $readmeTemplate"
}

New-Item -ItemType Directory -Force -Path $outputRootPath | Out-Null

$resolvedRoot = [System.IO.Path]::GetFullPath($repoRoot)
$resolvedPackage = [System.IO.Path]::GetFullPath($packageDir)
if (-not $resolvedPackage.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean a package directory outside the repository: $resolvedPackage"
}

if (Test-Path $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

Copy-Item -Path $exePath -Destination (Join-Path $packageDir "ShareNotepad.exe") -Force
Copy-Item -Path $loaderPath -Destination (Join-Path $packageDir "WebView2Loader.dll") -Force
Copy-Item -Path $uiPath -Destination (Join-Path $packageDir "ui") -Recurse -Force
Copy-Item -Path $readmeTemplate -Destination (Join-Path $packageDir "README.txt") -Force
Set-Content -Path (Join-Path $packageDir "ShareNotepad.portable") -Value "portable-self-install" -Encoding ASCII

$version = @"
name=ShareNotepad
configuration=$Configuration
created_at=$(Get-Date -Format "yyyy-MM-dd HH:mm:ss K")
"@

Set-Content -Path (Join-Path $packageDir "VERSION.txt") -Value $version -Encoding UTF8

Write-Host "Package created:"
Write-Host $packageDir
