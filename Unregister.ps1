#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run 64-bit PowerShell.' }
$manifest = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'NeoXR.json'))
$key = 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
if (Test-Path $key) {
    $item = Get-Item $key
    if ($item.GetValueNames() -contains $manifest) { Remove-ItemProperty -Path $key -Name $manifest }
}
Write-Host 'Removed only this NeoXR registration. Restart the simulator. Other OpenXR layers and the active runtime were not changed.'
