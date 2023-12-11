# Note that we do not copy the big font atlas to Android assets. No longer needed!

./ext/native/tools/build/atlastool ui_atlasscript.txt ui 8888 && cp ui_atlas.zim ui_atlas.meta assets && rm ui_atlas.cpp ui_atlas.h
#./ext/native/tools/build/atlastool font_atlasscript.txt font 8888 && cp font_atlas.zim font_atlas.meta assets && rm font_atlas.cpp font_atlas.h
#./ext/native/tools/build/atlastool asciifont_atlasscript.txt asciifont 8888 && cp asciifont_atlas.zim asciifont_atlas.meta assets && rm asciifont_atlas.cpp asciifont_atlas.h

rm ui_atlas.zim ui_atlas.meta
#rm font_atlas.zim font_atlas.meta
#rm asciifont_atlas.zim asciifont_atlas.meta
