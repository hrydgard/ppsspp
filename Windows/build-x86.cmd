@echo off
rem // Copyright (c) 2012- PPSSPP Project.

rem // This program is free software: you can redistribute it and/or modify
rem // it under the terms of the GNU General Public License as published by
rem // the Free Software Foundation, version 2.0 or later versions.

rem // This program is distributed in the hope that it will be useful,
rem // but WITHOUT ANY WARRANTY; without even the implied warranty of
rem // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
rem // GNU General Public License 2.0 for more details.

rem // A copy of the GPL 2.0 should have been included with the program.
rem // If not, see http://www.gnu.org/licenses/

rem // Official git repository and contact information can be found at
rem // https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

if not defined vs120comntools (
  echo "Visual Studio 2013 doesn't appear to be installed properly. Quitting."
  goto quit
  ) else (
    call "%vs120comntools%\vsvars32.bat" x86_amd64
)

set PPSSPP_ROOT=%CD%\..
set RELEASE_DIR=%CD%\bin-release
set RELEASE_X86=PPSSPPWindows.exe
set DEBUG_X86=PPSSPPDebug.exe

set RLS_PPSSPP=/m /p:Configuration=Release;Platform=win32
set DBG_PPSSPP=/m /p:Configuration=Debug;Platform=win32

call msbuild PPSSPP.sln /t:Clean %RLS_PPSSPP%
call msbuild PPSSPP.sln /t:Build %RLS_PPSSPP%
if not exist %PPSSPP_ROOT%\%RELEASEX86% (
    echo Release build failed.
    goto Quit
)
call msbuild PPSSPP.sln /t:Clean %DBG_PPSSPP% /m
call msbuild PPSSPP.sln /t:Build %DBG_PPSSPP% /m
if not exist %PPSSPP_ROOT%\%DEBUGX86% (
    echo Debug build failed.
    goto Quit
)

if not exist %RELEASE_DIR%\\. (
  mkdir %RELEASE_DIR%
) else (
  rmdir /S /Q %RELEASE_DIR%
  mkdir %RELEASE_DIR%
)

cd /d %RELEASE_DIR%

xcopy "%PPSSPP_ROOT%\assets" ".\assets\*" /S
xcopy "%PPSSPP_ROOT%\flash0" ".\flash0\*" /S
xcopy "%PPSSPP_ROOT%\lang" ".\lang\*" /S

copy %PPSSPP_ROOT%\LICENSE.txt /Y
copy %PPSSPP_ROOT%\README.md /Y

copy %PPSSPP_ROOT%\%DEBUG_X86% /Y

copy %PPSSPP_ROOT%\%RELEASE_X86% /Y

:Quit
pause
