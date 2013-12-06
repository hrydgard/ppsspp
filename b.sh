# Strict errors. Any non-zero return exits this script
set -e

cp -r android/assets .
mkdir -p build
if [[ "$1" == "--headless" ]]; then
	HEADLESS="-DHEADLESS=ON"
else
	MAKE_OPT="$1"
fi
pushd build
cmake $HEADLESS .. | (grep -v "^-- " || true)
make -j4 $MAKE_OPT
popd
