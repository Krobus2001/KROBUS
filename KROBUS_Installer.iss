
; ============================================
; KROBUS WINDOWS INSTALLER
; Version 1.0.0
; ============================================

#define MyAppName "KROBUS"
#define MyAppVersion "1.0.0"
#define MyAppExeName "KROBUS.exe"

[Setup]

; Application details
AppId=KROBUS.DesktopCompanion
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=KROBUS

; Installation location
DefaultDirName={localappdata}\Programs\KROBUS
DefaultGroupName=KROBUS

; Install for current Windows user
PrivilegesRequired=lowest

; Windows 64-bit
SetupArchitecture=x64

; Strawberry KROBUS icon
SetupIconFile=C:\Projects\KROBUS\KROBUS.ico
UninstallDisplayIcon={app}\KROBUS.exe

; Installer output
OutputDir=C:\Projects\KROBUS\installer_output
OutputBaseFilename=KROBUS_Setup_1.0.0

; Compression
Compression=lzma2
SolidCompression=yes

; Modern installer appearance
WizardStyle=modern

; Handle running application
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

; ============================================
; OPTIONAL INSTALLATION FEATURES
; ============================================

[Tasks]

; Optional desktop shortcut
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional options:"; Flags: unchecked

; Optional startup
Name: "autostart"; Description: "Start KROBUS when Windows starts"; GroupDescription: "Additional options:"; Flags: unchecked

; ============================================
; APPLICATION FILES
; ============================================

[Files]

; Include the complete working standalone app
; Qt libraries, plugins, sprites, and KROBUS.exe
Source: "C:\Projects\KROBUS\dist\KROBUS\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; ============================================
; WINDOWS SHORTCUTS
; ============================================

[Icons]

; Start menu shortcut
Name: "{userprograms}\KROBUS"; Filename: "{app}\KROBUS.exe"; WorkingDir: "{app}"

; Desktop shortcut
Name: "{userdesktop}\KROBUS"; Filename: "{app}\KROBUS.exe"; WorkingDir: "{app}"; Tasks: desktopicon

; Start with Windows
Name: "{userstartup}\KROBUS"; Filename: "{app}\KROBUS.exe"; WorkingDir: "{app}"; Tasks: autostart

; ============================================
; LAUNCH AFTER INSTALLATION
; ============================================

[Run]

Filename: "{app}\KROBUS.exe"; Description: "Launch KROBUS now"; Flags: nowait postinstall skipifsilent
