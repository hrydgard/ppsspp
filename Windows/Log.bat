@echo off
IF Not exist .\User md .\User
IF Not exist .\User\Logs md .\User\Logs
If exist .\User\Logs\ppsspp.log Del .\User\Logs\ppsspp.log
PPSSPPWindows.exe