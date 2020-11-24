@echo off
set LOGFILE=ppsspplog.txt

del "%LOGFILE%" 2> NUL
if exist PPSSPPWindows64.exe (
    PPSSPPWindows64.exe --log="%LOGFILE%"
    goto exit
)
if exist PPSSPPWindows.exe (
    PPSSPPWindows.exe --log="%LOGFILE%"
    goto exit
)

echo Unable to find PPSSPPWindows.exe.
pause

:exit