cp -r android/assets .
mkdir -p build
if [[ "$1" == "--headless" ]]; then
    HEADLESS="-DHEADLESS=ON"
else
	MAKE_OPT="$1"
fi
(cd build; cmake $HEADLESS .. && make -j3 $MAKE_OPT; cd ..)
