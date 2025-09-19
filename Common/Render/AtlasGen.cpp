
#include <assert.h>
#include <png.h>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>

#include "Common/StringUtils.h"
#include "Common/Render/TextureAtlas.h"

#include "Common/Data/Format/PNGLoad.h"
#include "Common/Data/Format/ZIMSave.h"

#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Render/AtlasGen.h"

typedef unsigned short u16;

void Image::copyfrom(const Image &img, int ox, int oy, bool redToWhiteAlpha) {
	assert(img.width() + ox <= width());
	assert(img.height() + oy <= height());
	for (int y = 0; y < (int)img.height(); y++) {
		for (int x = 0; x < (int)img.width(); x++) {
			if (!redToWhiteAlpha) {
				set1(x + ox, y + oy, img.get1(x, y));
			} else {
				set1(x + ox, y + oy, 0x00FFFFFF | (img.get1(x, y) << 24));
			}
		}
	}
}

bool Image::LoadPNG(const char *png_name) {
	size_t sz;
	const uint8_t *file_data = g_VFS.ReadFile(png_name, &sz);
	if (!file_data) {
		printf("Failed to load png from VFS");
		return false;
	}

	unsigned char *img_data;
	int w, h;
	if (1 != pngLoadPtr(file_data, sz, &w, &h, &img_data)) {
		delete[] file_data;
		printf("Failed to load %s\n", png_name);
		return false;
	}
	delete[] file_data;
	resize(w, h);
	memcpy(dat.data(), img_data, 4 * w * h);
	free(img_data);
	return true;
}

void Image::SavePNG(const char *png_name) {
	pngSave(Path(png_name), dat.data(), w, h, 4);
}

void Image::SaveZIM(const char *zim_name, int zim_format) {
	uint8_t *image_data = new uint8_t[width() * height() * 4];
	for (int y = 0; y < height(); y++) {
		memcpy(image_data + y * width() * 4, (dat.data() + y * w), width() * 4);
	}
	FILE *f = fopen(zim_name, "wb");
	// SaveZIM takes ownership over image_data, there's no leak.
	::SaveZIM(f, width(), height(), width() * 4, zim_format | ZIM_DITHER, image_data);
	fclose(f);
}

void Bucket::AddImage(Image &&img, int id) {
	Data dat{};
	dat.id = id;
	dat.sx = 0;
	dat.sy = 0;
	dat.ex = (int)img.width();
	dat.ey = (int)img.height();
	dat.w = dat.ex;
	dat.h = dat.ey;
	dat.scale = img.scale;
	dat.redToWhiteAlpha = false;
	images.emplace_back(std::move(img));
	data.push_back(dat);
}

inline bool CompareByID(const Data &lhs, const Data &rhs) {
	return lhs.id < rhs.id; // should be unique
}

inline bool CompareByArea(const Data& lhs, const Data& rhs) {
	return lhs.w * lhs.h > rhs.w * rhs.h;
}

std::vector<Data> Bucket::Resolve(int image_width, Image &dest) {
	// Place all the little images - whatever they are.
	// Uses greedy fill algorithm. Slow but works surprisingly well, CPUs are fast.
	ImageU8 masq;
	masq.resize(image_width, 1);
	dest.resize(image_width, 1);
	std::sort(data.begin(), data.end(), CompareByArea);
	for (int i = 0; i < (int)data.size(); i++) {
		if ((i + 1) % 2000 == 0) {
			printf("Resolving (%i / %i)\n", i, (int)data.size());
		}
		int idx = (int)data[i].w;
		int idy = (int)data[i].h;
		if (idx > 1 && idy > 1) {
			assert(idx <= image_width);
			for (int ty = 0; ty < 2047; ty++) {
				if (ty + idy + 1 > (int)dest.height()) {
					// Every 16 lines of new space needed, grow the image.
					masq.resize(image_width, ty + idy + 16);
					dest.resize(image_width, ty + idy + 16);
				}
				// Brute force packing.
				int sz = (int)data[i].w;
				auto &masq_ty = masq.dat[ty];
				auto &masq_idy = masq.dat[ty + idy - 1];
				for (int tx = 0; tx < image_width - sz; tx++) {
					bool valid = !(masq_ty[tx] || masq_idy[tx] || masq_ty[tx + idx - 1] || masq_idy[tx + idx - 1]);
					if (valid) {
						for (int ity = 0; ity < idy && valid; ity++) {
							for (int itx = 0; itx < idx && valid; itx++) {
								if (masq.dat[ty + ity][tx + itx]) {
									goto skip;
								}
							}
						}
						masq.set(tx, ty, tx + idx + 1, ty + idy + 1, 255);

						data[i].sx = tx;
						data[i].sy = ty;

						data[i].ex = tx + idx;
						data[i].ey = ty + idy;

						// printf("Placed %d at %dx%d-%dx%d\n", items[i].second.id, tx, ty, tx + idx, ty + idy);
						goto found;
					}
				skip:
					;
				}
			}
		found:
			;
		}
	}

	if ((int)dest.width() > image_width * 2) {
		printf("PACKING FAIL : height=%i", (int)dest.width());
		exit(1);
	}

	// Sort the data back by ID.
	std::sort(data.begin(), data.end(), CompareByID);

	// Actually copy the image data in place, after doing the layout.
	for (int i = 0; i < (int)data.size(); i++) {
		dest.copyfrom(images[i], data[i].sx, data[i].sy, data[i].redToWhiteAlpha);
	}

	return data;
}

AtlasImage ToAtlasImage(int id, std::string_view name, float tw, float th, const std::vector<Data> &results) {
	AtlasImage img{};
	const int i = id;
	const float toffx = 0.5f / tw;
	const float toffy = 0.5f / th;
	img.u1 = results[i].sx / tw + toffx;
	img.v1 = results[i].sy / th + toffy;
	img.u2 = results[i].ex / tw - toffx;
	img.v2 = results[i].ey / th - toffy;
	// The w and h here is the UI-pixels width/height. So if we rasterized at another DPI than 1.0f, we need to scale here.
	img.w = results[i].w / results[i].scale;
	img.h = results[i].h / results[i].scale;
	truncate_cpy(img.name, name);
	return img;
}
