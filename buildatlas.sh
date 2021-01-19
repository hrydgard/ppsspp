./ext/native/tools/build/atlastool atlasscript.txt ui 8888 && cp ui_atlas.zim ui_atlas.meta assets && cp ui_atlas.zim ui_atlas.meta android/assets && rm ui_atlas.cpp ui_atlas.h

mv assets/ui_atlas.zim assets/ui_atlas_luna.zim
mv assets/ui_atlas.meta assets/ui_atlas_luna.meta
mv android/assets/ui_atlas.zim android/assets/ui_atlas_luna.zim
mv android/assets/ui_atlas.meta android/assets/ui_atlas_luna.meta

rm ui_atlas.zim ui_atlas.meta
pause
$SHELL
