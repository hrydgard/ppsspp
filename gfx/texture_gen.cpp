// Minimal procedural texture generator to generate some usual textures like circles and vignette textures.
// "Gen" textures have filenames like this: "gen:256:256:vignette:0.1"
//
// These must be VERY VERY fast to not slow down loading. Could multicore this in the
// future.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "base/basictypes.h"
#include "gfx/texture.h"


uint8_t *generateTexture(const char *filename, int &bpp, int &w, int &h, bool &clamp) {
	char name_and_params[256];
	// security check :)
	if (strlen(filename) > 200)
		return 0;
	sscanf(filename, "gen:%i:%i:%s", &w, &h, name_and_params);

	uint8_t *data;
	if (!strcmp(name_and_params, "vignette")) {
		bpp = 1;
		data = new uint8_t[w*h];
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; x++) {
				float dx = (float)(x - w/2) / (w/2);
				float dy = (float)(y - h/2) / (h/2);
				float dist = sqrtf(dx * dx + dy * dy);
				dist /= 1.414f;
				float val = 1.0 - powf(dist, 1.4f);
				data[y*w + x] = val * 255;
			}
		}
	} else if (!strcmp(name_and_params, "circle")) {
		bpp = 1;
		// TODO
		data = new uint8_t[w*h];
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; x++) {
				float dx = (float)(x - w/2) / (w/2);
				float dy = (float)(y - h/2) / (h/2);
				float dist = sqrtf(dx * dx + dy * dy);
				dist /= 1.414f;
				float val = 1.0 - powf(dist, 1.4f);
				data[y*w + x] = val * 255;
			}
		}
	} else {
		data = NULL;
	}

	return data;
}
