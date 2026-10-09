# Builds, tests and packages NeoXR, then compiles build\installer\NeoXR-Setup-<version>.exe.
# Requires Visual Studio 2022 (C++), the .NET SDK and Inno Setup 6 (winget install JRSoftware.InnoSetup).
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $version = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(NeoXR VERSION ([\d.]+)').Groups[1].Value
    if (!$version) { throw 'Could not read the version from CMakeLists.txt.' }

    & cmake -S . -B build -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    & cmake --install build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
    & dotnet build setup\NeoXR.Setup.csproj -c Release -nologo "-p:Version=$version"
    if ($LASTEXITCODE -ne 0) { throw 'Setup wizard build failed.' }

    $iscc = @(
        (Get-Command iscc -ErrorAction SilentlyContinue).Source,
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (!$iscc) { throw 'Inno Setup 6 not found. Install it with: winget install JRSoftware.InnoSetup' }
    & $iscc /Qp "/DAppVersion=$version" installer\NeoXR.iss
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
    Write-Host "Installer: $(Resolve-Path "build\installer\NeoXR-Setup-$version.exe")"
} finally { Pop-Location }
