[Setup]
AppName={cm:AppName}
AppVersion=1.3.0
DefaultDirName={pf}\PPSSPP
; Since no icons will be created in "{group}", we don't need the wizard
; to ask for a Start Menu folder name:
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\PPSSPPWindows.exe
OutputDir=.
UsePreviousLanguage=no

[CustomMessages]
AppName=PPSSPP
LaunchProgram=Start PPSSPP after finishing installation

[Files]
Source: "PPSSPPWindows.exe"; DestDir: "{app}"
Source: "PPSSPPWindows64.exe"; DestDir: "{app}"
Source: "README.md"; DestName: "README.txt"; DestDir: "{app}"; Flags: isreadme
; Special file to signify that we are installed, and not "portable" and should look for
; configuration etc in the user's directory, not in our own subdirectory.
Source: "notinstalled.txt"; DestName: "installed.txt"; DestDir: "{app}";
Source: "assets\*.*"; DestDir: "{app}\assets"
Source: "assets\shaders\*.*"; DestDir: "{app}\assets\shaders"
Source: "assets\lang\*.ini"; DestDir: "{app}\assets\lang"
Source: "flash0\font\*.*"; DestDir: "{app}\flash0\font"

[Run]
Filename: {app}\PPSSPPWindows.exe; Description: {cm:LaunchProgram,{cm:AppName}}; Flags: nowait postinstall skipifsilent

[Icons]
Name: "{commonprograms}\PPSSPP"; Filename: "{app}\PPSSPPWindows.exe"