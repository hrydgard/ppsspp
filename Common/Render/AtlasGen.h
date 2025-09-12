#pragma once

#include <vector>
#include <algorithm>

#include "Common/CommonTypes.h"

#include "Common/Render/TextureAtlas.h"

constexpr int supersample = 16;
constexpr int distmult = 64 * 3;  // this is "one pixel in the final version equals 64 difference". reduce this number to increase the "blur" radius, increase it to make things "sharper"
constexpr int maxsearch = (128 * supersample + distmult - 1) / distmult;

enum class Effect {
	FX_COPY = 0,
	FX_RED_TO_ALPHA_SOLID_WHITE = 1,   // for alpha fonts
	FX_RED_TO_INTENSITY_ALPHA_255 = 2,
	FX_PREMULTIPLY_ALPHA = 3,
	FX_PINK_TO_ALPHA = 4,   // for alpha fonts
	FX_INVALID = 5,
};

struct ImageU8 {
	std::vector<std::vector<u8>> dat;
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
	void set(int sx, int sy, int ex, int ey, unsigned char fil) {
		for (int y = sy; y < ey; y++)
			fill(dat[y].begin() + sx, dat[y].begin() + ex, fil);
	}
};

struct Image {
	int w;
	int h;

	// WARNING: This only preserves data correctly if w stays the same. Which it does, in our application.
	void resize(int x, int y) {
		w = x;
		h = y;
		dat.resize(w * h);
	}
	int width() const {
		return w;
	}
	int height() const {
		return h;
	}
	void set1(int x, int y, u32 col) {
		dat[y * w + x] = col;
	}
	void fill(u32 col) {
		for (int i = 0; i < w * h; i++) {
			dat[i] = col;
		}
	}
	const u32 *data() const {
		return dat.data();
	}
	u32 get1(int x, int y) const { return dat[y * w + x]; }
	void copyfrom(const Image &img, int ox, int oy, Effect effect);
	void set(int sx, int sy, int ex, int ey, u32 fil);
	bool LoadPNG(const char *png_name);
	void SavePNG(const char *png_name);
	void SaveZIM(const char *zim_name, int zim_format);
private:
	std::vector<u32> dat;
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

struct Bucket {
	std::vector<std::pair<Image, Data> > items;
	void AddItem(const Image &img, const Data &dat) {
		items.push_back(std::make_pair(img, dat));
	}
	std::vector<Data> Resolve(int image_width, Image &dest);
};

inline bool operator<(const Image &lhs, const Image &rhs) {
	return lhs.width() * lhs.height() > rhs.width() * rhs.height();
}

struct ImageDesc {
	std::string name;
	std::string fileName;
	Effect effect;
	int result_index;

	AtlasImage ToAtlasImage(float tw, float th, const std::vector<Data> &results) const;
	void OutputSelf(FILE *fil, float tw, float th, const std::vector<Data> &results) const;
	void OutputHeader(FILE *fil, int index) const;
};

bool LoadImage(const char *imagefile, Effect effect, Bucket *bucket, int &global_id);
