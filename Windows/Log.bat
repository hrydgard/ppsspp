@echo off
IF Not exist .\User md .\User
IF Not exist .\User\Logs md .\User\Logs
IF exist .\User\Logs\ppsspp.log Del .\User\Logs\ppsspp.log
IF exist .\PPSSPPWindows.exe PPSSPPWindows.exe
IF exist .\PPSSPPWindows64.exe PPSSPPWindows64.exe