; NeoXR installer. Build with Build-Installer.ps1, which passes AppVersion from CMakeLists.txt.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define Package "..\build\package"
#define SetupApp "..\setup\bin\Release"

[Setup]
AppId={{EE96CF22-6781-4D2E-89EE-BE0C36D60FC3}
AppName=NeoXR
AppVersion={#AppVersion}
AppVerName=NeoXR {#AppVersion}
AppPublisher=Avishai Peretz
AppPublisherURL=https://github.com/Avishai-Peretz/NeoXR-iRacing-VR-Wheel
; A folder the user can write to: NeoXR saves calibration and placement into NeoXR.ini at runtime.
DefaultDirName=C:\NeoXR
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
; Registering an OpenXR API layer writes to HKLM.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\build\installer
OutputBaseFilename=NeoXR-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=NeoXR {#AppVersion}
UninstallDisplayIcon={app}\NeoXR-Setup.exe
CloseApplications=yes

[Files]
Source: "{#Package}\NeoXR.dll";             DestDir: "{app}"; Flags: ignoreversion
Source: "{#Package}\NeoXR.json";            DestDir: "{app}"; Flags: ignoreversion
Source: "{#Package}\NeoXR-Input.exe";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#Package}\NeoXR-InputBridge.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Package}\README.md";             DestDir: "{app}"; Flags: ignoreversion
Source: "{#Package}\assets\*";              DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs
Source: "{#SetupApp}\NeoXR-Setup.exe";      DestDir: "{app}"; Flags: ignoreversion
Source: "{#SetupApp}\NeoXR-Setup.exe.config"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Settings belong to the user: install the template only once and keep it on upgrade and uninstall.
Source: "{#Package}\NeoXR.ini";             DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Registry]
Root: HKLM64; Subkey: "SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"; ValueType: dword; ValueName: "{app}\NeoXR.json"; ValueData: 0; Flags: uninsdeletevalue

[Icons]
Name: "{autoprograms}\NeoXR\NeoXR Setup"; Filename: "{app}\NeoXR-Setup.exe"
Name: "{autoprograms}\NeoXR\NeoXR Input Viewer"; Filename: "{app}\NeoXR-Input.exe"
Name: "{autoprograms}\NeoXR\NeoXR folder"; Filename: "{app}"

[Run]
Filename: "{app}\NeoXR-Setup.exe"; Description: "Set up the wheel now"; Flags: postinstall nowait skipifsilent runasoriginaluser

[Code]
const Game = 'iRacingSim64DX11.exe';

function GameRunning(): Boolean;
var Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name="' + Game + '"');
    Result := Processes.Count > 0;
  except
  end;
end;

// The game holds NeoXR.dll open while it runs, so it must be closed to install or remove NeoXR.
function InitializeSetup(): Boolean;
begin
  Result := True;
  while Result and GameRunning() do
    Result := MsgBox('Please close iRacing before installing NeoXR.', mbError, MB_RETRYCANCEL) = IDRETRY;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  while Result and GameRunning() do
    Result := MsgBox('Please close iRacing before removing NeoXR.', mbError, MB_RETRYCANCEL) = IDRETRY;
end;
