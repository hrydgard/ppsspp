./native/tools/build/atlastool atlasscript.txt ui 8888 && cp ui_atlas.zim assets && cp ui_atlas.zim android/assets && cp ui_atlas.zim.png ui_atlas_high.zim.png && mv ui_atlas.cpp ui_atlas_highmem.cpp && mv ui_atlas.h UI
./native/tools/build/atlastool atlasscript_lowmem.txt ui 8888 && mv ui_atlas.zim assets/ui_atlas_lowmem.zim && mv ui_atlas.cpp ui_atlas_lowmem.cpp
diff -I'^//.*' -D USING_QT_UI ui_atlas_highmem.cpp ui_atlas_lowmem.cpp > UI/ui_atlas.cpp
rm ui_atlas_highmem.cpp ui_atlas_lowmem.cpp ui_atlas.h
