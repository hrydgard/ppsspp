@echo off
IF exist .\ppsspp.log Del .\ppsspp.log
IF exist .\PPSSPPWindows.exe PPSSPPWindows.exe  --log=ppsspp.log
IF exist .\PPSSPPWindows64.exe PPSSPPWindows64.exe --log=ppsspp.log