// Minimal procedural texture generator to generate some useful but unnecessary-to-store textures like circles.
// "Gen" textures have filenames like this: "gen:256:256:4444:vignette:0.1"

#include "gfx/texture.h"

// Returns an OpenGL ID for now.

uint8_t *generateTexture(const char *filename, int &bpp, int &w, int &h, bool &clamp);



