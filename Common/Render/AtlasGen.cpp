
#include <assert.h>
#include <png.h>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>

#define STB_RECT_PACK_IMPLEMENTATION
#include "ext/stb/stb_rect_pack.h"

#include "Common/StringUtils.h"
#include "Common/Render/TextureAtlas.h"

#include "Common/Data/Format/PNGLoad.h"
#include "Common/Data/Format/ZIMSave.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Convert/ColorConv.h"

#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/FileUtil.h"
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
		printf("Failed to load png from VFS: %s\n", png_name);
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

void Image::ConvertToPremultipliedAlpha() {
	ConvertRGBA8888ToPremulAlpha(dat.data(), dat.data(), w * h);
}

void Image::SavePNG(const char *png_name) {
	pngSave(Path(png_name), dat.data(), w, h, 4);
}

void Image::SaveZIM(const char *zim_name, int zim_format) {
	uint8_t *image_data = new uint8_t[width() * height() * 4];
	for (int y = 0; y < height(); y++) {
		memcpy(image_data + y * width() * 4, (dat.data() + y * w), width() * 4);
	}
	FILE *f = File::OpenCFile(Path(zim_name), "wb");
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

void Bucket::Pack(int image_width) {
	// Place all the little images - whatever they are.
	// Uses greedy fill algorithm. Slow but works surprisingly well, CPUs are fast.
	ImageU8 masq;
	masq.resize(image_width, 1);

	// image_width is set to the square root of the total area of all images.
	// We shouldn't need more than twice that in height (more likely much less).
	const int maxHeight = image_width * 2;

	std::sort(data.begin(), data.end(), CompareByArea);
	for (int i = 0; i < (int)data.size(); i++) {
		if ((i + 1) % 2000 == 0) {
			// printf("Resolving (%i / %i)\n", i, (int)data.size());
		}
		int idx = (int)data[i].w;
		int idy = (int)data[i].h;
		if (idx > 1 && idy > 1) {
			assert(idx <= image_width);
			for (int ty = 0; ty < maxHeight - 1; ty++) {
				if (ty + idy + 1 > (int)masq.height()) {
					// Every 16 lines of new space needed, grow the image.
					masq.resize(image_width, ty + idy + 16);
				}
				// Brute force packing.
				int sz = (int)data[i].w;
				const auto *masq_ty = masq.line(ty);
				const auto *masq_idy = masq.line(ty + idy - 1);
				for (int tx = 0; tx < image_width - sz; tx++) {
					bool valid = !(masq_ty[tx] || masq_idy[tx] || masq_ty[tx + idx - 1] || masq_idy[tx + idx - 1]);
					if (valid) {
						for (int ity = 0; ity < idy && valid; ity++) {
							for (int itx = 0; itx < idx && valid; itx++) {
								if (masq.get(tx + itx, ty + ity)) {
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

	// Sort the data back by ID.
	std::sort(data.begin(), data.end(), CompareByID);

	w = image_width;
	h = masq.height();
}

void Bucket::Pack2(int image_width) {
	// Use stb_rect_pack for packing.
	stbrp_context context;
	// These are just temporary storage (the API is allocation-free otherwise).
	// About one node is needed for each horizontal unit of width.
	std::vector<stbrp_node> nodes(image_width * 2);
	stbrp_init_target(&context, image_width, image_width * 2, nodes.data(), (int)nodes.size());
	// Transfer the rectangles to the rect_pack structs from Data.
	std::vector<stbrp_rect> rects(data.size());
	for (int i = 0; i < data.size(); i++) {
		rects[i].w = (stbrp_coord)data[i].w;
		rects[i].h = (stbrp_coord)data[i].h;
		rects[i].id = i;
	}
	{
		stbrp_pack_rects(&context, rects.data(), (int)rects.size());
	}
	for (int i = 0; i < (int)data.size(); i++) {
		int index = rects[i].id;
		data[index].sx = rects[i].x;
		data[index].sy = rects[i].y;
		data[index].ex = rects[i].x + rects[i].w;
		data[index].ey = rects[i].y + rects[i].h;
	}
	w = image_width;
	h = 0;
	for (int i = 0; i < (int)data.size(); i++) {
		if (data[i].ey > h) {
			h = data[i].ey;
		}
	}
}

std::vector<Data> Bucket::Resolve(Image *dest) {
	dest->resize(w, h);
	// Actually copy the image data in place, after doing the layout.
	for (int i = 0; i < (int)data.size(); i++) {
		dest->copyfrom(images[i], data[i].sx, data[i].sy, data[i].redToWhiteAlpha);
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
	img.w = (int)((float)results[i].w / results[i].scale);
	img.h = (int)((float)results[i].h / results[i].scale);
	truncate_cpy(img.name, name);
	return img;
}

// The below is ChatGPT-generated drop shadow code. Needs optimization!

static std::vector<float> makeGaussianKernel(int radius) {
	const float sigma = radius / 2.0f;
	std::vector<float> kernel(2 * radius + 1);
	float sum = 0.0f;
	for (int i = -radius; i <= radius; i++) {
		float val = std::exp(-(i * i) / (2 * sigma * sigma));
		kernel[i + radius] = val;
		sum += val;
	}
	sum = 1.0f / sum;
	for (float &v : kernel)
		v *= sum;
	return kernel;
}

static void blurAlpha(const std::vector<float> &src, std::vector<float> &dst, int w, int h, int radius) {
	auto kernel = makeGaussianKernel(radius);
	int ksize = (int)kernel.size();
	int kr = radius;

	std::vector<float> tmp(w * h, 0.0f);

	// horizontal
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float sum = 0.0f;
			for (int k = -kr; k <= kr; k++) {
				int xx = std::clamp(x + k, 0, w - 1);
				sum += src[y * w + xx] * kernel[k + kr];
			}
			tmp[y * w + x] = sum;
		}
	}

	// vertical
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float sum = 0.0f;
			for (int k = -kr; k <= kr; k++) {
				int yy = std::clamp(y + k, 0, h - 1);
				sum += tmp[yy * w + x] * kernel[k + kr];
			}
			dst[y * w + x] = sum;
		}
	}
}

static inline uint32_t Over_ABGR(uint32_t front, uint32_t back) {
	const uint32_t fr = front & 0xFFu;
	const uint32_t fg = (front >> 8) & 0xFFu; // green
	const uint32_t fb = (front >> 16) & 0xFFu;
	const uint32_t fa = (front >> 24) & 0xFFu;

	const uint32_t br = back & 0xFFu;
	const uint32_t bg = (back >> 8) & 0xFFu; // green
	const uint32_t bb = (back >> 16) & 0xFFu;
	const uint32_t ba = (back >> 24) & 0xFFu;

	const uint32_t invA = 255u - fa;

	// multiply then divide by 255 with rounding equivalent to (x*invA + 127)/255
	uint32_t outr = fr + (uint32_t)((br * invA + 127u) / 255u);
	uint32_t outg = fg + (uint32_t)((bg * invA + 127u) / 255u);
	uint32_t outb = fb + (uint32_t)((bb * invA + 127u) / 255u);
	uint32_t outa = fa + (uint32_t)((ba * invA + 127u) / 255u);

	// pack back to ABGR
	return (outa << 24) | (outb << 16) | (outg << 8) | outr;
}

void Add1PxTransparentBorder(Image &img) {
	std::vector<u32> newData((img.w + 2) * (img.h + 2), 0);
	for (int y = 0; y < img.h; y++) {
		for (int x = 0; x < img.w; x++) {
			u32 c = img.dat[y * img.w + x];
			newData[(y + 1) * (img.w + 2) + (x + 1)] = c;
		}
	}
	img.dat = std::move(newData);
	img.w += 2;
	img.h += 2;
}

void AddDropShadow(Image &img, int shadowSize, float intensity) {
	int radius = std::max(1, (int)(shadowSize * img.scale));

	// Expand canvas so blur has space on all sides
	int newW = img.w + radius * 2;
	int newH = img.h + radius * 2;

	// Expanded alpha buffer
	std::vector<float> alpha(newW * newH, 0.0f);
	for (int y = 0; y < img.h; y++) {
		for (int x = 0; x < img.w; x++) {
			float a = ((img.dat[y * img.w + x] >> 24) & 0xFF) * (1.0f / 255.0f);
			alpha[(y + radius) * newW + (x + radius)] = a;
		}
	}

	// Blur the expanded alpha
	std::vector<float> blurred(newW * newH, 0.0f);
	blurAlpha(alpha, blurred, newW, newH, radius);

	// Target buffer with transparent background
	std::vector<u32> newData(newW * newH, 0);

	// Draw the computed shadow first (black, blurred alpha - automatically premultiplied).
	for (int y = 0; y < newH; y++) {
		for (int x = 0; x < newW; x++) {
			float a = blurred[y * newW + x];
			if (a > 0.001f) {
				newData[y * newW + x] = ((u32)(a * 255 * intensity) << 24);
			}
		}
	}

	// Composite original image on top (centered in expanded buffer)
	for (int y = 0; y < img.h; y++) {
		for (int x = 0; x < img.w; x++) {
			u32 c = img.dat[y * img.w + x];
			if ((c >> 24) & 0xFF) {
				int nx = x + radius;
				int ny = y + radius;
				newData[ny * newW + nx] = Over_ABGR(c, newData[ny * newW + nx]);
			}
		}
	}

	img.w = newW;
	img.h = newH;
	img.dat = std::move(newData);
}
