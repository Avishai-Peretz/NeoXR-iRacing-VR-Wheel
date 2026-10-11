@echo off
setlocal
cd /d "%~dp0"
if not exist "setup\bin\Release\NeoXR-Setup.exe" (
  echo Build the control center first: dotnet build setup\NeoXR.Setup.csproj -c Release
  pause
  exit /b 1
)
if not "%~1"=="" (
  start "" "setup\bin\Release\NeoXR-Setup.exe" "%~f1"
) else if exist "C:\NeoXR\NeoXR.ini" (
  start "" "setup\bin\Release\NeoXR-Setup.exe" "C:\NeoXR\NeoXR.ini"
) else (
  start "" "setup\bin\Release\NeoXR-Setup.exe" "%~dp0NeoXR.ini"
)
