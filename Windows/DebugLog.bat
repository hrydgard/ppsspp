@echo off
set LOGFILE=ppsspplog.txt

del "%LOGFILE%" 2> NUL
if exist PPSSPPWindows64.exe (
    start PPSSPPWindows64.exe --log="%LOGFILE%" -d
    goto exit
)
if exist PPSSPPWindows.exe (
    start PPSSPPWindows.exe --log="%LOGFILE%" -d
    goto exit
)

echo Unable to find PPSSPPWindows.exe.
pause

:exit
