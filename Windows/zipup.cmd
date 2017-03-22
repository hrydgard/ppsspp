@echo off

if "%1"=="" GOTO BADINPUT


REM Make sure to call this with a .zip file extension for the first parameter.
REM If zip.exe is not installed, get it from info-zip.org.

set name=%1

echo Deleting old file %name%
del %name%

echo Adding files to %name%
REM Not distributing the 10 version because it's not compatible with older Windows.
cp dx9sdk\8.1\Redist\D3D\x64\d3dcompiler_47.dll .
cp dx9sdk\8.1\Redist\D3D\x86\d3dcompiler_47.dll .\d3dcompiler_47.x86.dll
zip --recurse-paths %name% assets flash0 PPSSPPWindows.exe PPSSPPWindows64.exe d3dcompiler_47.dll d3dcompiler_47.x86.dll README.md
del d3dcompiler_47.dll d3dcompiler_v47.x86.dll

echo Done: %name%
goto DONE

:BADINPUT
echo Usage: Windows\zipup.cmd myfile.zip

:DONE
