#define ApplicationName 'PPSSPP'
#define ApplicationVersion GetFileVersion('PPSSPPWindows.exe')

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ar"; MessagesFile: "compiler:Languages\Armenian.islu"
Name: "br"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "ca"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "co"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "cz"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "da"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "du"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "fi"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "ge"; MessagesFile: "compiler:Languages\German.isl"
Name: "gr"; MessagesFile: "compiler:Languages\Greek.isl"
Name: "he"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "hu"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "ne"; MessagesFile: "compiler:Languages\Nepali.islu"
Name: "no"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "po"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "sc"; MessagesFile: "compiler:Languages\ScottishGaelic.isl"
Name: "sec"; MessagesFile: "compiler:Languages\SerbianCyrillic.isl"
Name: "ser"; MessagesFile: "compiler:Languages\SerbianLatin.isl"
Name: "sl"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "sp"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"
; more languages are available at: http://www.jrsoftware.org/files/istrans/
; but those need to be mirrored on PPSSPP repo somehow

[Setup]
; Installer - name for title and wizard pages
AppName={#ApplicationName}
; Installer - version
VersionInfoVersion={#ApplicationVersion}

; Programs and Features - Name
AppVerName={#ApplicationName}
; Programs and Features - Version
AppVersion={#ApplicationVersion}
; Programs and Features - Publisher
AppPublisher=PPSSPP Team
; Programs and Features - Help link
AppSupportURL=https://forums.ppsspp.org
; Programs and Features - Support link
AppPublisherURL=https://www.ppsspp.org
; Programs and Features - Update information
AppUpdatesURL=https://www.ppsspp.org/downloads.html

DefaultDirName={pf}\PPSSPP
; Since no icons will be created in "{group}", we don't need the wizard
; to ask for a Start Menu folder name:
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\PPSSPPWindows.exe
; Detect the correct directory for Program Files x86/64
ArchitecturesInstallIn64BitMode=x64

; Save the installer as ...
OutputBaseFilename={#ApplicationName}_{#ApplicationVersion}_Setup
; ... in the same directory
OutputDir=.

[Files]
Source: "PPSSPPWindows.exe"; DestDir: "{app}"
Source: "PPSSPPWindows64.exe"; DestDir: "{app}"; Check: IsWin64
Source: "README.md"; DestName: "README.txt"; DestDir: "{app}"; Flags: isreadme
; Special file to signify that we are installed, and not "portable" and should look for
; configuration etc in the user's directory, not in our own subdirectory.
Source: "notinstalled.txt"; DestName: "installed.txt"; DestDir: "{app}";
Source: "assets\*.*"; DestDir: "{app}\assets"
Source: "assets\shaders\*.*"; DestDir: "{app}\assets\shaders"
Source: "assets\themes\*.*"; DestDir: "{app}\assets\themes"
Source: "assets\debugger\*"; DestDir: "{app}\assets\debugger"; Flags: recursesubdirs
Source: "assets\lang\*.ini"; DestDir: "{app}\assets\lang"
Source: "assets\flash0\font\*.*"; DestDir: "{app}\assets\flash0\font"
Source: "assets\vfpu\*.*"; DestDir: "{app}\assets\vfpu"
Source: "dx9sdk\8.1\Redist\D3D\x64\d3dcompiler_47.dll"; DestDir: "{app}"
Source: "dx9sdk\8.1\Redist\D3D\x86\d3dcompiler_47.dll"; DestName: "d3dcompiler_47.x86.dll"; DestDir: "{app}"

[Run]
Filename: {app}\PPSSPPWindows.exe; Description: {cm:LaunchProgram,{#ApplicationName}}; Flags: nowait postinstall skipifsilent; Check: not IsWin64
Filename: {app}\PPSSPPWindows64.exe; Description: {cm:LaunchProgram,{#ApplicationName}}; Flags: nowait postinstall skipifsilent; Check: IsWin64

[Icons]
Name: "{commonprograms}\PPSSPP"; Filename: "{app}\PPSSPPWindows.exe"; Check: not IsWin64
Name: "{commonprograms}\PPSSPP"; Filename: "{app}\PPSSPPWindows64.exe"; Check: IsWin64

