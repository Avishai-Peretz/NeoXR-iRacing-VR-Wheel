param([string]$AppDirectory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$appPath = Join-Path ([IO.Path]::GetFullPath($AppDirectory)) 'NeoXR-Setup.exe'
if (!(Test-Path -LiteralPath $appPath)) { throw "NeoXR app not found: $appPath" }
$desktopPath = [Environment]::GetFolderPath('Desktop')
$shellLink = New-Object -ComObject WScript.Shell
$shortcut = $shellLink.CreateShortcut((Join-Path $desktopPath 'NeoXR.lnk'))
$shortcut.TargetPath = $appPath
$shortcut.WorkingDirectory = [IO.Path]::GetDirectoryName($appPath)
$shortcut.IconLocation = "$appPath,0"
$shortcut.Description = 'NeoXR — wheel setup and live control'
$shortcut.Save()
Write-Host "Desktop shortcut created: $(Join-Path $desktopPath 'NeoXR.lnk')"
