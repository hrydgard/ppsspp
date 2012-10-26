#include <string.h>
#include "gfx/texture_atlas.h"

const AtlasFont *Atlas::getFontByName(const char *name) const
{
	for (int i = 0; i < num_fonts; i++) {
		if (!strcmp(name, fonts[i]->name))
			return fonts[i];
	}
}

const AtlasImage *Atlas::getImageByName(const char *name) const
{
	for (int i = 0; i < num_images; i++) {
		if (!strcmp(name, images[i].name))
			return &images[i];
	}
}
