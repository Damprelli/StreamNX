; StreamNX Installer Script for Inno Setup
; This script configures the installation of StreamNX application

[Setup]
AppName=StreamNX
AppVersion=1.0.0.0
AppPublisher=StreamNX
AppPublisherURL=https://github.com/Damprelli/StreamNX
AppSupportURL=https://github.com/Damprelli/StreamNX
AppUpdatesURL=https://github.com/Damprelli/StreamNX
DefaultDirName={autopf}\StreamNX
DefaultGroupName=StreamNX
AllowNoIcons=yes
LicenseFile=LICENSE
OutputDir=build_pc
OutputBaseFilename=StreamNX-1.0.0-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
//SetupIconFile=resources\img\streamnx.ico
WizardSmallImageFile=resources\img\streamnx.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: "quicklaunchicon"; Description: "Create Quick Launch shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Main executable
Source: "build_pc\StreamNX.exe"; DestDir: "{app}"; Flags: ignoreversion

; All DLL dependencies from build_pc (copies all .dll files)
Source: "build_pc\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; Application resources (fonts, images, configs, etc.)
Source: "resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\StreamNX"; Filename: "{app}\StreamNX.exe"; IconFilename: "{app}\resources\img\borealis_256.png"
Name: "{group}\Uninstall StreamNX"; Filename: "{uninstallexe}"
Name: "{autodesktop}\StreamNX"; Filename: "{app}\StreamNX.exe"; IconFilename: "{app}\resources\img\borealis_256.png"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\StreamNX"; Filename: "{app}\StreamNX.exe"; IconFilename: "{app}\resources\img\borealis_256.png"; Tasks: quicklaunchicon

[Run]
Filename: "{app}\StreamNX.exe"; Description: "Launch StreamNX"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"
