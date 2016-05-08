[Setup]
AppName={cm:AppName}
AppVersion=0.9.5
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
Source: "README.md"; DestName: "README.txt"; DestDir: "{app}"; Flags: isreadme
Source: "notinstalled.txt"; DestName: "installed.txt"; DestDir: "{app}";
Source: "assets\ppge_atlas.zim"; DestDir: "{app}\assets"
Source: "assets\ui_atlas.zim"; DestDir: "{app}\assets"
Source: "assets\langregion.ini"; DestDir: "{app}\assets"
Source: "assets\compat.ini"; DestDir: "{app}\assets"
Source: "assets\Roboto-Condensed.ttf"; DestDir: "{app}\assets"
Source: "assets\shaders\*.*"; DestDir: "{app}\assets\shaders"
Source: "lang\*.ini"; DestDir: "{app}\lang"
Source: "flash0\font\*.*"; DestDir: "{app}\flash0\font"
Source: "redist/vcredist_x86.exe"; DestDir: {tmp}

[Run]
Filename: {tmp}\vcredist_x86.exe; Parameters: "/passive /Q:a /c:""msiexec /qb /i vcredist.msi"" "; StatusMsg: Installing 2010 RunTime...
; Hm, I wonder if we need to manually delete vcredist_x86.exe as well.
Filename: {app}\PPSSPPWindows.exe; Description: {cm:LaunchProgram,{cm:AppName}}; Flags: nowait postinstall skipifsilent

[Icons]
Name: "{commonprograms}\PPSSPP"; Filename: "{app}\PPSSPPWindows.exe"
