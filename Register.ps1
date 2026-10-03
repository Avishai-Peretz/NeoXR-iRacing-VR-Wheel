#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run 64-bit PowerShell.' }
$manifest = (Resolve-Path (Join-Path $PSScriptRoot 'NeoXR.json')).Path
if (-not (Test-Path (Join-Path $PSScriptRoot 'NeoXR.dll'))) { throw 'Build and package first. NeoXR.dll is missing.' }
$key = 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name $manifest -Value 0 -PropertyType DWord -Force | Out-Null
Write-Host 'Registered NeoXR. Enabled=0 in NeoXR.ini keeps rendering off. Restart the simulator after changing configuration.'
