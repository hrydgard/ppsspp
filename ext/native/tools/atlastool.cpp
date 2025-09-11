// Sprite packing method borrowed from glorp engine and heavily modified.
// For license safety, just run this as a build tool, don't build it into your game/program.
// https://github.com/zorbathut/glorp

// Horrible build instructions:
// * Download freetype, put in ppsspp/ext as freetype/
// * Open tools.sln
// * In Code Generation on freetype, change from Multithreaded DLL to Multithreaded.
// * Build
// * Move exe file to ext/native/tools/build

// data we need to provide:
// sx, sy
// dx, dy
// ox, oy
// wx

// line height
// dist-per-pixel

#include "Common/Render/AtlasGen.h"
#include <cstdio>
#include <assert.h>
#include <cstring>

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Not enough arguments.\nSee buildatlas.sh for example.\n");
		return 1;
	}
	assert(argc >= 3);

	bool highcolor = false;

	if (argc > 3) {
		if (!strcmp(argv[3], "8888")) {
			highcolor = true;
			printf("RGBA8888 enabled!\n");
		} else {
			printf("Bad third argument\n");
		}
	}
	printf("Reading script %s\n", argv[1]);

	return GenerateFromScript(argv[1], argv[2], highcolor);
}
