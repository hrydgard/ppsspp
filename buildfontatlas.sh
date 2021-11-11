./ext/native/tools/build/atlastool atlasfontscript.txt ui 8888 && for f in ui_atlas_luna.*;do ext="${f##*.}"; mv "$f" "font_atlas_luna.$ext"; done && cp font_atlas_luna.zim font_atlas_luna.meta android/assets && cp font_atlas_luna.zim font_atlas_luna.meta assets && mv font_atlas_luna.png font_preview.png && rm font_atlas_luna.*
$SHELL
