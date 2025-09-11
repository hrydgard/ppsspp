#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
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

// Use the result array, and recorded data, to generate C++ tables for everything.
struct FontDesc {
	std::string name;

	int first_char_id = -1;

	float ascend = 0.0f;
	float descend = 0.0f;
	float height = 0.0f;

	float metrics_height = 0.0f;

	std::vector<CharRange> ranges;

	void ComputeHeight(const std::vector<Data> &results, float distmult);

	void OutputSelf(FILE *fil, float tw, float th, const std::vector<Data> &results) const;
	void OutputIndex(FILE *fil) const;
	void OutputHeader(FILE *fil, int index) const;
	AtlasFontHeader GetHeader() const;

	std::vector<AtlasCharRange> GetRanges() const;
	std::vector<AtlasChar> GetChars(float tw, float th, const std::vector<Data> &results) const;
};

struct Image {
	std::vector<std::vector<u32>> dat;
	void resize(int x, int y) {
		dat.resize(y);
		for (int i = 0; i < y; i++)
			dat[i].resize(x);
	}
	int width() const {
		return (int)dat[0].size();
	}
	int height() const {
		return (int)dat.size();
	}
	void copyfrom(const Image &img, int ox, int oy, Effect effect);
	void set(int sx, int sy, int ex, int ey, unsigned char fil);
	bool LoadPNG(const char *png_name);
	void SavePNG(const char *png_name);
	void SaveZIM(const char *zim_name, int zim_format);
};

inline bool operator<(const Image &lhs, const Image &rhs) {
	return lhs.dat.size() * lhs.dat[0].size() > rhs.dat.size() * rhs.dat[0].size();
}


constexpr int AtlasSupersample = 16;
constexpr int AtlasDistMult = 64 * 3;  // this is "one pixel in the final version equals 64 difference". reduce this number to increase the "blur" radius, increase it to make things "sharper"
constexpr int AtlasMaxSearch = (128 * AtlasSupersample + AtlasDistMult - 1) / AtlasDistMult;

typedef std::vector<FontReference> FontReferenceList;
bool GetLocales(const char *locales, std::vector<CharRange> &ranges);
void LoadAndResolve(std::vector<ImageDesc> &images, std::map<std::string, FontReferenceList> &fontRefs, std::vector<FontDesc> &fonts, int image_width, std::vector<Data> &results, Image &dest);
