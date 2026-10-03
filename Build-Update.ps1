param([Parameter(Mandatory=$true)][string]$InstalledPackage)
$ErrorActionPreference = 'Stop'
$destination = (Resolve-Path -LiteralPath $InstalledPackage).Path
$expectedPackage = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build/package')).TrimEnd('\', '/')
if ($destination.TrimEnd('\', '/') -eq $expectedPackage) {
    throw 'Extract this update into a separate source folder from the working installation.'
}
$manifest = Join-Path $destination 'NeoXR.json'
if (!(Test-Path -LiteralPath $manifest) -or !(Test-Path -LiteralPath (Join-Path $destination 'NeoXR.dll'))) {
    throw 'InstalledPackage must be the existing working NeoXR package folder containing NeoXR.json and NeoXR.dll.'
}
$layer = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($layer.api_layer.name -ne 'XR_APILAYER_AVI_neoxr') { throw 'This folder is not the NeoXR installation.' }
$settings = Join-Path $destination 'NeoXR.ini'
$processName = 'iRacingSim64DX11'
if (Test-Path -LiteralPath $settings) {
    $match = [regex]::Match((Get-Content -LiteralPath $settings -Raw), '(?m)^\s*Process\s*=\s*(.+?)\s*$')
    if ($match.Success) { $processName = [IO.Path]::GetFileNameWithoutExtension($match.Groups[1].Value) }
}
if (Get-Process -Name $processName -ErrorAction SilentlyContinue) { throw 'Close the simulator before updating NeoXR.' }
Push-Location $PSScriptRoot
try {
    & cmake -S . -B build -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. The working installation was not changed.' }
    & ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed. The working installation was not changed.' }
    & cmake --install build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed. The working installation was not changed.' }
    $package = (Resolve-Path -LiteralPath 'build/package').Path
    if ($package -eq $destination) { throw 'Extract this update into a separate source folder from the working installation.' }
    $backup = Join-Path $destination ('backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    Copy-Item -LiteralPath (Join-Path $destination 'NeoXR.dll') -Destination $backup
    if (Test-Path -LiteralPath $settings) { Copy-Item -LiteralPath $settings -Destination $backup }
    $assets = Join-Path $destination 'assets'
    if (Test-Path -LiteralPath $assets) { Copy-Item -LiteralPath $assets -Destination $backup -Recurse }
    if (!(Test-Path -LiteralPath $assets)) { New-Item -ItemType Directory -Path $assets | Out-Null }
    Copy-Item -Path (Join-Path $package 'assets/*') -Destination $assets -Recurse -Force
    # Replace the DLL last, after all assets are in place. Keep INI and registered manifest.
    Copy-Item -LiteralPath (Join-Path $package 'NeoXR.dll') -Destination $destination -Force
    Write-Host 'NeoXR 0.2.1 installed. Existing INI settings and registration were preserved.'
    Write-Host ('Backup: ' + $backup)
    Write-Host 'Launch iRacing in OpenXR and press F8. Set Model=placeholder in the [Wheel] INI section to restore the original visual.'
} finally { Pop-Location }
