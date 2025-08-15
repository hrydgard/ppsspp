@echo off

if "%1"=="" GOTO BADINPUT


REM Make sure to call this with a .zip file extension for the first parameter.
REM If zip.exe is not installed, get it from info-zip.org.

set name=%1

echo Deleting old file %name%
del %name%

echo Adding files to %name%
REM Not distributing the 10 version because it's not compatible with older Windows.
@echo on
zip --recurse-paths %name% assets PPSSPPWindows.exe PPSSPPWindows64.exe README.md
@echo off

echo Done: %name%
goto DONE

:BADINPUT
echo Usage: Windows\zipup.cmd myfile.zip

:DONE
