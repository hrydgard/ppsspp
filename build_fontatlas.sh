TOOL=./ext/native/tools/build/atlastool
# TOOL=Windows/ARM64/Debug/AtlasTool.exe

$TOOL font_atlasscript.txt font 8888 && mv font_atlas.zim font_atlas.meta assets && rm font_atlas.cpp font_atlas.h
$TOOL asciifont_atlasscript.txt asciifont 8888 && mv asciifont_atlas.zim asciifont_atlas.meta assets && rm asciifont_atlas.cpp asciifont_atlas.h
