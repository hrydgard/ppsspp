./native/tools/build/atlastool atlasscript.txt ui 8888 && cp ui_atlas.zim assets && mv ui_atlas.zim android/assets && mv ui_atlas.cpp ui_atlas.h UI
./native/tools/build/atlastool atlasscript_nofont.txt ui 8888 && mv ui_atlas.zim assets/ui_atlas_nofont.zim && mv ui_atlas.cpp UI/ui_atlas_nofont.cpp && rm ui_atlas.h
