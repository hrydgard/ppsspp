
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
#include "Common/Render/AtlasGen.h"

using namespace std;

typedef unsigned short u16;

void Image::copyfrom(const Image &img, int ox, int oy, Effect effect) {
	assert(img.width() + ox <= width());
	assert(img.height() + oy <= height());
	for (int y = 0; y < (int)img.height(); y++) {
		for (int x = 0; x < (int)img.width(); x++) {
			switch (effect) {
			case Effect::FX_COPY:
				set1(x + ox, y + oy, img.get1(x, y));
				break;
			case Effect::FX_RED_TO_ALPHA_SOLID_WHITE:
				set1(x + ox, y + oy, 0x00FFFFFF | (img.get1(x, y) << 24));
				break;
			case Effect::FX_RED_TO_INTENSITY_ALPHA_255:
			{
				u32 val = img.get1(x, y) & 0xFF;
				set1(x + ox, y + oy, 0xFF000000 | val | (val << 8) | (val << 16));
				break;
			}
			case Effect::FX_PREMULTIPLY_ALPHA:
			{
				unsigned int color = img.get1(x, y);
				unsigned int a = color >> 24;
				unsigned int r = (color & 0xFF) * a >> 8, g = (color & 0xFF00) * a >> 8, b = (color & 0xFF0000) * a >> 8;
				color = (color & 0xFF000000) | (r & 0xFF) | (g & 0xFF00) | (b & 0xFF0000);
				// Simulate 4444
				color = color & 0xF0F0F0F0;
				color |= color >> 4;
				set1(x + ox, y + oy, color);
				break;
			}
			case Effect::FX_PINK_TO_ALPHA:
			{
				u32 val = img.get1(x, y);
				set1(x + ox, y + oy, ((val & 0xFFFFFF) == 0xFF00FF) ? 0x00FFFFFF : (val | 0xFF000000));
				break;
			}
			default:
				set1(x + ox, y + oy, 0xFFFF00FF);
				break;
			}
		}
	}
}

void Image::set(int sx, int sy, int ex, int ey, u32 fil) {
	for (int y = sy; y < ey; y++) {
		for (int x = sx; x < ex; x++) {
			dat[y * w + x] = fil;
		}
	}
}

bool Image::LoadPNG(const char *png_name) {
	unsigned char *img_data;
	int w, h;
	if (1 != pngLoad(png_name, &w, &h, &img_data)) {
		printf("Failed to load %s\n", png_name);
		return false;
	}
	resize(w, h);
	for (int y = 0; y < h; y++) {
		memcpy(dat.data() + y * w, img_data + 4 * y * w, 4 * w);
	}
	free(img_data);
	return true;
}

void Image::SavePNG(const char *png_name) {
	// Save PNG
	FILE *fil = fopen(png_name, "wb");
	png_structp  png_ptr;
	png_infop  info_ptr;
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	assert(png_ptr);
	info_ptr = png_create_info_struct(png_ptr);
	assert(info_ptr);
	png_init_io(png_ptr, fil);
	//png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
	png_set_IHDR(png_ptr, info_ptr, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png_ptr, info_ptr);
	for (int y = 0; y < height(); y++) {
		png_write_row(png_ptr, (png_byte*)(dat.data() + y * w));
	}
	png_write_end(png_ptr, NULL);
	png_destroy_write_struct(&png_ptr, &info_ptr);
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

std::vector<Data> Bucket::Resolve(int image_width, Image &dest) {
	// Place all the little images - whatever they are.
	// Uses greedy fill algorithm. Slow but works surprisingly well, CPUs are fast.
	ImageU8 masq;
	masq.resize(image_width, 1);
	dest.resize(image_width, 1);
	sort(items.begin(), items.end());
	for (int i = 0; i < (int)items.size(); i++) {
		if ((i + 1) % 2000 == 0) {
			printf("Resolving (%i / %i)\n", i, (int)items.size());
		}
		int idx = (int)items[i].first.width();
		int idy = (int)items[i].first.height();
		if (idx > 1 && idy > 1) {
			assert(idx <= image_width);
			for (int ty = 0; ty < 2047; ty++) {
				if (ty + idy + 1 > (int)dest.height()) {
					// Every 16 lines of new space needed, grow the image.
					masq.resize(image_width, ty + idy + 16);
					dest.resize(image_width, ty + idy + 16);
				}
				// Brute force packing.
				int sz = (int)items[i].first.width();
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
						dest.copyfrom(items[i].first, tx, ty, (Effect)items[i].second.effect);
						masq.set(tx, ty, tx + idx + 1, ty + idy + 1, 255);

						items[i].second.sx = tx;
						items[i].second.sy = ty;

						items[i].second.ex = tx + idx;
						items[i].second.ey = ty + idy;

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

	// Output the glyph data.
	vector<Data> dats;
	for (int i = 0; i < (int)items.size(); i++)
		dats.push_back(items[i].second);
	return dats;
}

bool LoadImage(const char *imagefile, Effect effect, Bucket *bucket, int &global_id) {
	Image img;

	bool success = false;
	if (!strcmp(imagefile, "white.png")) {
		img.resize(16, 16);
		img.fill(0xFFFFFFFF);
		success = true;
	} else {
		success = img.LoadPNG(imagefile);
		// printf("loaded image: %ix%i\n", (int)img.dat[0].size(), (int)img.dat.size());
	}
	if (!success) {
		return false;
	}

	Data dat;
	memset(&dat, 0, sizeof(dat));
	dat.id = global_id++;
	dat.sx = 0;
	dat.sy = 0;
	dat.ex = (int)img.width();
	dat.ey = (int)img.height();
	dat.effect = (int)effect;
	bucket->AddItem(img, dat);
	return true;
}

AtlasImage ImageDesc::ToAtlasImage(float tw, float th, const vector<Data> &results) const {
	AtlasImage img{};
	int i = result_index;
	float toffx = 0.5f / tw;
	float toffy = 0.5f / th;
	img.u1 = results[i].sx / tw + toffx;
	img.v1 = results[i].sy / th + toffy;
	img.u2 = results[i].ex / tw - toffx;
	img.v2 = results[i].ey / th - toffy;
	img.w = results[i].ex - results[i].sx;
	img.h = results[i].ey - results[i].sy;
	truncate_cpy(img.name, name);
	return img;
}

void ImageDesc::OutputSelf(FILE *fil, float tw, float th, const vector<Data> &results) const {
	int i = result_index;
	float toffx = 0.5f / tw;
	float toffy = 0.5f / th;
	fprintf(fil, "  {%ff, %ff, %ff, %ff, %d, %d, \"%s\"},\n",
		results[i].sx / tw + toffx,
		results[i].sy / th + toffy,
		results[i].ex / tw - toffx,
		results[i].ey / th - toffy,
		results[i].ex - results[i].sx,
		results[i].ey - results[i].sy,
		name.c_str());
}

void ImageDesc::OutputHeader(FILE *fil, int index) const {
	fprintf(fil, "#define %s %i\n", name.c_str(), index);
}
