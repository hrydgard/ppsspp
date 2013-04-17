#pragma once

struct AtlasChar {
  // texcoords
  float sx, sy, ex, ey;
  // offset from the origin
  float ox, oy;
  // distance to move the origin forward
  float wx;
  // size in pixels
  unsigned short pw, ph;
};

struct AtlasCharRange {
	int start;
	int end;
	int start_index;
};

struct AtlasFont {
  float padding;
  float height;
  float ascend;
  float distslope;
  const AtlasChar *charData;
	const AtlasCharRange *ranges;
	int numRanges;
	const char *name;

	// Returns 0 on no match.
	const AtlasChar *getChar(int utf32) const ;
};

struct AtlasImage {
  float u1, v1, u2, v2;
  int w, h;
	const char *name;
};

struct Atlas {
  const char *filename;
  const AtlasFont **fonts;
  int num_fonts;
  const AtlasImage *images;
  int num_images;

	// These are inefficient linear searches, try not to call every frame.
	const AtlasFont *getFontByName(const char *name) const;
	const AtlasImage *getImageByName(const char *name) const;
};