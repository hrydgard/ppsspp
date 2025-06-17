TARGETDIR=$1
VERSION=$2

TARGETPATH=$1/$2

echo "Copying to $TARGETPATH"

mkdir -p $TARGETPATH
cp PPSSPPWindows.exe $TARGETPATH/
cp PPSSPPWindows64.exe $TARGETPATH/
cp -r assets $TARGETPATH/
cp README.md $TARGETPATH/
rm $TARGETPATH/assets/lang/.git
