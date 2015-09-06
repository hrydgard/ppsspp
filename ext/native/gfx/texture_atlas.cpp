#include <string.h>
#include "gfx/texture_atlas.h"

const AtlasFont *Atlas::getFontByName(const char *name) const
{
	for (int i = 0; i < num_fonts; i++) {
		if (!strcmp(name, fonts[i]->name))
			return fonts[i];
	}
	return 0;
}

const AtlasImage *Atlas::getImageByName(const char *name) const
{
	for (int i = 0; i < num_images; i++) {
		if (!strcmp(name, images[i].name))
			return &images[i];
	}
	return 0;
}

const AtlasChar *AtlasFont::getChar(int utf32) const {
	for (int i = 0; i < numRanges; i++) {
		if (utf32 >= ranges[i].start && utf32 < ranges[i].end) {
			const AtlasChar *c = &charData[ranges[i].start_index + utf32 - ranges[i].start];
			if (c->ex == 0 && c->ey == 0)
				return 0;
			else
				return c;
		}
	}
	return 0;
}
