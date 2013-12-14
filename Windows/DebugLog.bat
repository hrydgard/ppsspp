@echo off
set LOGFILE=ppsspplog.txt

del "%LOGFILE%" 2> NUL
if exist PPSSPPDebug64.exe (
    PPSSPPDebug64.exe --log="%LOGFILE%"
    goto exit
)
if exist PPSSPPDebug.exe (
    PPSSPPDebug.exe --log="%LOGFILE%"
    goto exit
)

echo Unable to find PPSSPPDebug.exe.
pause

:exit