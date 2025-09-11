#pragma once

#include <string>
#include <vector>
#include <set>
#include <cstdio>

#include "Common/CommonTypes.h"

#include "Common/Render/TextureAtlas.h"

struct CharRange : public AtlasCharRange {
	std::set<u16> filter;
};

struct FontReference {
	FontReference(std::string name, std::string file, std::vector<CharRange> ranges, int pixheight, float vertOffset)
		: name_(name), file_(file), ranges_(ranges), size_(pixheight), vertOffset_(vertOffset) {
	}

	std::string name_;
	std::string file_;
	std::vector<CharRange> ranges_;
	int size_;
	float vertOffset_;
};

enum class Effect {
	FX_COPY = 0,
	FX_RED_TO_ALPHA_SOLID_WHITE = 1,   // for alpha fonts
	FX_RED_TO_INTENSITY_ALPHA_255 = 2,
	FX_PREMULTIPLY_ALPHA = 3,
	FX_PINK_TO_ALPHA = 4,   // for alpha fonts
	FX_INVALID = 5,
};

struct Data {
	// item ID
	int id;
	// dimensions of its spot in the world
	int sx, sy, ex, ey;
	// offset from the origin
	float ox, oy;
	float voffset;  // to apply at the end
	// distance to move the origin forward
	float wx;

	int effect;
	int charNum;
};

inline bool operator<(const Data &lhs, const Data &rhs) {
	return lhs.id < rhs.id; // should be unique
}

struct ImageDesc {
	std::string name;
	std::string filename;
	Effect effect;
	int result_index;

	AtlasImage ToAtlasImage(float tw, float th, const std::vector<Data> &results);

	void OutputSelf(FILE *f, float tw, float th, const std::vector<Data> &results);
	void OutputHeader(FILE *f, int index);
};


int GenerateFromScript(const char *script_file, const char *atlas_name, bool highcolor);
