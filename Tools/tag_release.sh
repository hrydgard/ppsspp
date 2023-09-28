# Only for use during the process of making official releases

if [ -z "$1" ]; then
	echo "No argument supplied"
	exit 1
fi

VER=$1

git tag -a ${VER} -m '${VER}'; git push --tags origin ${VER}; git push origin master

echo Now run the internal tool:
echo ppsspp-build --commit ${VER} --gold --sign-code

