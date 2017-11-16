CONVERT=/c/batch/convert.exe
ICON=../source_assets/image/icon_regular.png
ASSET_DIR=AssetsNormal

mkdir -p $ASSET_DIR
$CONVERT ${ICON} -resize 48x48 $ASSET_DIR/LockScreenLogo.scale-200.png
$CONVERT ${ICON} -resize 88x88 $ASSET_DIR/Square44x44Logo.scale-200.png
$CONVERT ${ICON} -resize 24x24 $ASSET_DIR/Square44x44Logo.targetsize-24_altform-unplated.png
$CONVERT ${ICON} -resize 300x300 $ASSET_DIR/Square150x150Logo.scale-200.png
$CONVERT ${ICON} -resize 50x50 $ASSET_DIR/StoreLogo.png
$CONVERT ${ICON} -resize 1240x600 -background none -gravity center -extent 1240x600 $ASSET_DIR/SplashScreen.scale-200.png



ICON=../source_assets/image/icon_gold.png
ASSET_DIR=AssetsGold

mkdir -p $ASSET_DIR
$CONVERT ${ICON} -resize 48x48 $ASSET_DIR/LockScreenLogo.scale-200.png
$CONVERT ${ICON} -resize 88x88 $ASSET_DIR/Square44x44Logo.scale-200.png
$CONVERT ${ICON} -resize 24x24 $ASSET_DIR/Square44x44Logo.targetsize-24_altform-unplated.png
$CONVERT ${ICON} -resize 300x300 $ASSET_DIR/Square150x150Logo.scale-200.png
$CONVERT ${ICON} -resize 50x50 $ASSET_DIR/StoreLogo.png
$CONVERT ${ICON} -resize 1240x600 -background none -gravity center -extent 1240x600 $ASSET_DIR/SplashScreen.scale-200.png



