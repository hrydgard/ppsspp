TOOL=./ext/native/tools/build/atlastool
# TOOL=Windows/ARM64/Debug/AtlasTool.exe

$TOOL ui_atlasscript.txt ui 8888 && mv ui_atlas.zim ui_atlas.meta assets && rm ui_atlas.cpp ui_atlas.h
