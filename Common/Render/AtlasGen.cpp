
#include <assert.h>
#include <libpng17/png.h>
#include <ft2build.h>
#include <freetype/ftbitmap.h>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>

#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/Render/TextureAtlas.h"

#include "Common/Data/Format/PNGLoad.h"
#include "Common/Data/Format/ZIMSave.h"

#include "Common/Render/kanjifilter.h"
// extracted only JIS Kanji on the CJK Unified Ideographs of UCS2. Cannot reading BlockAllocator. (texture size over)
//#define USE_KANJI KANJI_STANDARD | KANJI_RARELY_USED | KANJI_LEVEL4
// daily-use character only. However, it is too enough this.
//#define USE_KANJI KANJI_STANDARD (texture size over)
// Shift-JIS filtering. (texture size over)5
//#define USE_KANJI KANJI_SJIS_L1 | KANJI_SJIS_L2
// more conpact daily-use character. but, not enough this.
// if when you find the unintelligible sequence of characters,
// add kanjiFilter Array with KANJI_LEARNING_ORDER_ADDTIONAL.
#define USE_KANJI KANJI_LEARNING_ORDER_ALL

#include "Common/Data/Encoding/Utf8.h"

#include "Common/Render/AtlasGen.h"

using namespace std;

static int global_id;

void Image::copyfrom(const Image &img, int ox, int oy, Effect effect) {
	assert(img.dat[0].size() + ox <= dat[0].size());
	assert(img.dat.size() + oy <= dat.size());
	for (int y = 0; y < (int)img.dat.size(); y++) {
		for (int x = 0; x < (int)img.dat[y].size(); x++) {
			switch (effect) {
			case Effect::FX_COPY:
				dat[y + oy][ox + x] = img.dat[y][x];
				break;
			case Effect::FX_RED_TO_ALPHA_SOLID_WHITE:
				dat[y + oy][ox + x] = 0x00FFFFFF | (img.dat[y][x] << 24);
				break;
			case Effect::FX_RED_TO_INTENSITY_ALPHA_255:
				dat[y + oy][ox + x] = 0xFF000000 | img.dat[y][x] | (img.dat[y][x] << 8) | (img.dat[y][x] << 16);
				break;
			case Effect::FX_PREMULTIPLY_ALPHA:
			{
				unsigned int color = img.dat[y][x];
				unsigned int a = color >> 24;
				unsigned int r = (color & 0xFF) * a >> 8, g = (color & 0xFF00) * a >> 8, b = (color & 0xFF0000) * a >> 8;
				color = (color & 0xFF000000) | (r & 0xFF) | (g & 0xFF00) | (b & 0xFF0000);
				// Simulate 4444
				color = color & 0xF0F0F0F0;
				color |= color >> 4;
				dat[y + oy][ox + x] = color;
				break;
			}
			case Effect::FX_PINK_TO_ALPHA:
				dat[y + oy][ox + x] = ((img.dat[y][x] & 0xFFFFFF) == 0xFF00FF) ? 0x00FFFFFF : (img.dat[y][x] | 0xFF000000);
				break;
			default:
				dat[y + oy][ox + x] = 0xFFFF00FF;
				break;
			}
		}
	}
}
void Image::set(int sx, int sy, int ex, int ey, unsigned char fil) {
	for (int y = sy; y < ey; y++)
		fill(dat[y].begin() + sx, dat[y].begin() + ex, fil);
}
bool Image::LoadPNG(const char *png_name) {
	unsigned char *img_data;
	int w, h;
	if (1 != pngLoad(png_name, &w, &h, &img_data)) {
		printf("Failed to load %s\n", png_name);
		exit(1);
		return false;
	}
	dat.resize(h);
	for (int y = 0; y < h; y++) {
		dat[y].resize(w);
		memcpy(&dat[y][0], img_data + 4 * y * w, 4 * w);
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
	png_set_IHDR(png_ptr, info_ptr, (uint32_t)dat[0].size(), (uint32_t)dat.size(), 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png_ptr, info_ptr);
	for (int y = 0; y < (int)dat.size(); y++) {
		png_write_row(png_ptr, (png_byte*)&dat[y][0]);
	}
	png_write_end(png_ptr, NULL);
	png_destroy_write_struct(&png_ptr, &info_ptr);
}
void Image::SaveZIM(const char *zim_name, int zim_format) {
	uint8_t *image_data = new uint8_t[width() * height() * 4];
	for (int y = 0; y < height(); y++) {
		memcpy(image_data + y * width() * 4, &dat[y][0], width() * 4);
	}
	FILE *f = fopen(zim_name, "wb");
	// SaveZIM takes ownership over image_data, there's no leak.
	::SaveZIM(f, width(), height(), width() * 4, zim_format | ZIM_DITHER, image_data);
	fclose(f);
}

int NextPowerOf2(int x) {
	int powof2 = 1;
	// Double powof2 until >= val
	while (powof2 < x) powof2 <<= 1;
	return powof2;
}

struct Bucket {
	vector<pair<Image, Data> > items;
	void AddItem(const Image &img, const Data &dat) {
		items.push_back(make_pair(img, dat));
	}

	// TODO: use stb_rectpack
	vector<Data> Resolve(int image_width, Image &dest) {
		// Place all the little images - whatever they are.
		// Uses greedy fill algorithm. Slow but works surprisingly well, CPUs are fast.
		Image masq;
		masq.resize(image_width, 1);
		dest.resize(image_width, 1);
		sort(items.begin(), items.end());
		for (int i = 0; i < (int)items.size(); i++) {
			if ((i + 1) % 2000 == 0) {
				printf("Resolving (%i / %i)\n", i, (int)items.size());
			}
			int idx = (int)items[i].first.dat[0].size();
			int idy = (int)items[i].first.dat.size();
			if (idx > 1 && idy > 1) {
				assert(idx <= image_width);
				for (int ty = 0; ty < 2047; ty++) {
					if (ty + idy + 1 > (int)dest.dat.size()) {
						masq.resize(image_width, ty + idy + 16);
						dest.resize(image_width, ty + idy + 16);
					}
					// Brute force packing.
					int sz = (int)items[i].first.dat[0].size();
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

		if ((int)dest.dat.size() > image_width * 2) {
			printf("PACKING FAIL : height=%i", (int)dest.dat.size());
			exit(1);
		}
		dest.resize(image_width, (int)dest.dat.size());

		// Output the glyph data.
		vector<Data> dats;
		for (int i = 0; i < (int)items.size(); i++)
			dats.push_back(items[i].second);
		return dats;
	}
};

struct Closest {
	FT_Bitmap bmp;
	Closest(FT_Bitmap bmp) : bmp(bmp) {}
	float find_closest(int x, int y, char search) {
		int best = 1 << 30;
		for (int i = 1; i <= AtlasMaxSearch; i++) {
			if (i * i >= best)
				break;
			for (int f = -i; f < i; f++) {
				int dist = i * i + f * f;
				if (dist >= best) continue;
				if (safe_access(x + i, y + f) == search || safe_access(x - f, y + i) == search || safe_access(x - i, y - f) == search || safe_access(x + f, y - i) == search)
					best = dist;
			}
		}
		return sqrt((float)best);
	}
	char safe_access(int x, int y) {
		if (x < 0 || y < 0 || x >= (int)bmp.width || y >= (int)bmp.rows)
			return 0;
		return bmp.buffer[x + y * bmp.width];
	}
};

typedef vector<FT_Face> FT_Face_List;

inline vector<CharRange> merge(const vector<CharRange> &a, const vector<CharRange> &b) {
	vector<CharRange> result = a;
	for (size_t i = 0, in = b.size(); i < in; ++i) {
		bool insert = true;
		for (size_t j = 0, jn = a.size(); j < jn; ++j) {
			// Should never overlap, so same start is always a duplicate.
			if (b[i].start == a[j].start) {
				insert = false;
				break;
			}
		}

		if (insert) {
			result.push_back(b[i]);
		}
	}

	return result;
}

void RasterizeFonts(const FontReferenceList &fontRefs, vector<CharRange> &ranges, float *metrics_height, Bucket *bucket) {
	FT_Library freetype = 0;
	if (FT_Init_FreeType(&freetype) != 0) {
		printf("ERROR: Failed to init freetype\n");
		exit(1);
	}

	vector<FT_Face> fonts;
	fonts.resize(fontRefs.size());

	// The ranges may overlap, so build a list of fonts per range.
	map<int, FT_Face_List> fontsByRange;
	// TODO: Better way than average?
	float totalHeight = 0.0f;

	for (size_t i = 0, n = fontRefs.size(); i < n; ++i) {
		FT_Face &font = fonts[i];
		int err = FT_New_Face(freetype, fontRefs[i].file_.c_str(), 0, &font);
		if (err != 0) {
			printf("Failed to load font file %s (%d)\n", fontRefs[i].file_.c_str(), err);
			printf("bailing");
			exit(1);
		}
		printf("TTF info: %d glyphs, %08x flags, %d units, %d strikes\n", (int)font->num_glyphs, (int)font->face_flags, (int)font->units_per_EM, (int)font->num_fixed_sizes);

		if (FT_Set_Pixel_Sizes(font, 0, fontRefs[i].size_ * AtlasSupersample) != 0) {
			printf("ERROR: Failed to set font size\n");
			exit(1);
		}

		ranges = merge(ranges, fontRefs[i].ranges_);
		for (size_t r = 0, rn = fontRefs[i].ranges_.size(); r < rn; ++r) {
			const CharRange &range = fontRefs[i].ranges_[r];
			fontsByRange[range.start].push_back(fonts[i]);
		}

		totalHeight += font->size->metrics.height;
	}

	// Wait what - how does this make sense?
	*metrics_height = totalHeight / (float)fontRefs.size();

	size_t missing_chars = 0;

	// Convert all characters to bitmaps.
	for (size_t r = 0, rn = ranges.size(); r < rn; r++) {
		FT_Face_List &tryFonts = fontsByRange[ranges[r].start];
		ranges[r].result_index = global_id;
		for (int kar = ranges[r].start; kar < ranges[r].end; kar++) {
			bool filtered = false;
			if (ranges[r].filter.size()) {
				if (ranges[r].filter.find((u16)kar) == ranges[r].filter.end())
					filtered = true;
			}

			FT_Face font = nullptr;
			bool foundMatch = false;
			float vertOffset = 0;
			for (size_t i = 0, n = tryFonts.size(); i < n; ++i) {
				font = tryFonts[i];
				vertOffset = fontRefs[i].vertOffset_;
				if (FT_Get_Char_Index(font, kar) != 0) {
					foundMatch = true;
					break;
				}
			}
			if (!foundMatch) {
				// fprintf(stderr, "WARNING: No font contains character %x.\n", kar);
				missing_chars++;
			}

			Image img;
			if (!foundMatch || filtered || 0 != FT_Load_Char(font, kar, FT_LOAD_RENDER | FT_LOAD_MONOCHROME)) {
				img.resize(1, 1);
				Data dat;

				dat.id = global_id++;

				dat.sx = 0;
				dat.sy = 0;
				dat.ex = 0;
				dat.ey = 0;
				dat.ox = 0;
				dat.oy = 0;
				dat.wx = 0;
				dat.voffset = 0;
				dat.charNum = kar;
				dat.effect = (int)Effect::FX_RED_TO_ALPHA_SOLID_WHITE;
				bucket->AddItem(img, dat);
				continue;
			}

			// printf("%dx%d %p\n", font->glyph->bitmap.width, font->glyph->bitmap.rows, font->glyph->bitmap.buffer);
			const int bord = (128 + AtlasDistMult - 1) / AtlasDistMult + 1;
			if (font->glyph->bitmap.buffer) {
				FT_Bitmap tempbitmap;
				FT_Bitmap_New(&tempbitmap);
				FT_Bitmap_Convert(freetype, &font->glyph->bitmap, &tempbitmap, 1);
				Closest closest(tempbitmap);

				// No resampling, just sets the size of the image.
				img.resize((tempbitmap.width + AtlasSupersample - 1) / AtlasSupersample + bord * 2, (tempbitmap.rows + AtlasSupersample - 1) / AtlasSupersample + bord * 2);
				int lmx = (int)img.dat[0].size();
				int lmy = (int)img.dat.size();

				// AA by finding distance to character. Probably a fairly decent approximation but why not do it right?
				for (int y = 0; y < lmy; y++) {
					int cty = (y - bord) * AtlasSupersample + AtlasSupersample / 2;
					for (int x = 0; x < lmx; x++) {
						int ctx = (x - bord) * AtlasSupersample + AtlasSupersample / 2;
						float dist;
						if (closest.safe_access(ctx, cty)) {
							dist = closest.find_closest(ctx, cty, 0);
						} else {
							dist = -closest.find_closest(ctx, cty, 1);
						}
						dist = dist / AtlasSupersample * AtlasDistMult + 127.5f;
						dist = floor(dist + 0.5f);
						if (dist < 0) dist = 0;
						if (dist > 255) dist = 255;

						// Only set the red channel. We process when adding the image.
						img.dat[y][x] = (unsigned char)dist;
					}
				}
				FT_Bitmap_Done(freetype, &tempbitmap);
			} else {
				img.resize(1, 1);
			}

			Data dat;

			dat.id = global_id++;

			dat.sx = 0;
			dat.sy = 0;
			dat.ex = (int)img.dat[0].size();
			dat.ey = (int)img.dat.size();
			dat.ox = (float)font->glyph->metrics.horiBearingX / 64 / AtlasSupersample - bord;
			dat.oy = -(float)font->glyph->metrics.horiBearingY / 64 / AtlasSupersample - bord;
			dat.voffset = vertOffset;
			dat.wx = (float)font->glyph->metrics.horiAdvance / 64 / AtlasSupersample;
			dat.charNum = kar;

			dat.effect = (int)Effect::FX_RED_TO_ALPHA_SOLID_WHITE;
			bucket->AddItem(img, dat);
		}
	}

	if (missing_chars) {
		printf("Chars not found in any font: %d\n", (int)missing_chars);
	}

	for (size_t i = 0, n = fonts.size(); i < n; ++i) {
		FT_Done_Face(fonts[i]);
	}
	FT_Done_FreeType(freetype);
}

bool LoadImage(const char *imagefile, Effect effect, Bucket *bucket) {
	Image img;

	bool success = false;
	if (!strcmp(imagefile, "white.png")) {
		img.dat.resize(16);
		for (int i = 0; i < 16; i++) {
			img.dat[i].resize(16);
			for (int j = 0; j < 16; j++) {
				img.dat[i][j] = 0xFFFFFFFF;
			}
		}
		success = true;
	} else {
		success = img.LoadPNG(imagefile);
		// printf("loaded image: %ix%i\n", (int)img.dat[0].size(), (int)img.dat.size());
	}
	if (!success) {
		return false;
	}

	Data dat{};
	dat.id = global_id++;
	dat.sx = 0;
	dat.sy = 0;
	dat.ex = (int)img.dat[0].size();
	dat.ey = (int)img.dat.size();
	dat.effect = (int)effect;
	bucket->AddItem(img, dat);
	return true;
}

void FontDesc::ComputeHeight(const vector<Data> &results, float distmult) {
	ascend = 0;
	descend = 0;
	for (size_t r = 0; r < ranges.size(); r++) {
		for (int i = ranges[r].start; i < ranges[r].end; i++) {
			int idx = i - ranges[r].start + ranges[r].result_index;
			ascend = max(ascend, -results[idx].oy);
			descend = max(descend, results[idx].ey - results[idx].sy + results[idx].oy);
		}
	}

	height = metrics_height / 64.0f / AtlasSupersample;
}

void FontDesc::OutputSelf(FILE *fil, float tw, float th, const vector<Data> &results) const {
	// Dump results as chardata.
	fprintf(fil, "const AtlasChar font_%s_chardata[] = {\n", name.c_str());
	int start_index = 0;
	for (size_t r = 0; r < ranges.size(); r++) {
		fprintf(fil, "// RANGE: 0x%x - 0x%x, start %d, result %d\n", ranges[r].start, ranges[r].end, start_index, ranges[r].result_index);
		for (int i = ranges[r].start; i < ranges[r].end; i++) {
			int idx = i - ranges[r].start + ranges[r].result_index;
			fprintf(fil, "    {%ff, %ff, %ff, %ff, %1.4ff, %1.4ff, %1.4ff, %i, %i},  // %04x\n",
				/*results[i].id, */
				results[idx].sx / tw,
				results[idx].sy / th,
				results[idx].ex / tw,
				results[idx].ey / th,
				results[idx].ox,
				results[idx].oy + results[idx].voffset,
				results[idx].wx,
				results[idx].ex - results[idx].sx, results[idx].ey - results[idx].sy,
				results[idx].charNum);
		}
		start_index += ranges[r].end - ranges[r].start;
	}
	fprintf(fil, "};\n");

	fprintf(fil, "const AtlasCharRange font_%s_ranges[] = {\n", name.c_str());
	// Write range information.
	start_index = 0;
	for (size_t r = 0; r < ranges.size(); r++) {
		int first_char_id = ranges[r].start;
		int last_char_id = ranges[r].end;
		fprintf(fil, "  { %i, %i, %i },\n", first_char_id, last_char_id, start_index);
		start_index += last_char_id - first_char_id;
	}
	fprintf(fil, "};\n");

	fprintf(fil, "const AtlasFont font_%s = {\n", name.c_str());
	fprintf(fil, "  %ff, // padding\n", height - ascend - descend);
	fprintf(fil, "  %ff, // height\n", ascend + descend);
	fprintf(fil, "  %ff, // ascend\n", ascend);
	fprintf(fil, "  %ff, // distslope\n", AtlasDistMult / 256.0);
	fprintf(fil, "  font_%s_chardata,\n", name.c_str());
	fprintf(fil, "  font_%s_ranges,\n", name.c_str());
	fprintf(fil, "  %i,\n", (int)ranges.size());
	fprintf(fil, "  \"%s\", // name\n", name.c_str());
	fprintf(fil, "};\n");
}

void FontDesc::OutputIndex(FILE *fil) const {
	fprintf(fil, "  &font_%s,\n", name.c_str());
}

void FontDesc::OutputHeader(FILE *fil, int index) const {
	fprintf(fil, "#define %s %i\n", name.c_str(), index);
}

AtlasFontHeader FontDesc::GetHeader() const {
	int numChars = 0;
	for (size_t r = 0; r < ranges.size(); r++) {
		numChars += ranges[r].end - ranges[r].start;
	}
	AtlasFontHeader header{};
	header.padding = height - ascend - descend;
	header.height = ascend + descend;
	header.ascend = ascend;
	header.distslope = AtlasDistMult / 256.0;
	truncate_cpy(header.name, name);
	header.numChars = numChars;
	header.numRanges = (int)ranges.size();
	return header;
}

vector<AtlasCharRange> FontDesc::GetRanges() const {
	int start_index = 0;
	vector<AtlasCharRange> out_ranges;
	for (size_t r = 0; r < ranges.size(); r++) {
		int first_char_id = ranges[r].start;
		int last_char_id = ranges[r].end;
		AtlasCharRange range;
		range.start = first_char_id;
		range.end = last_char_id;
		range.result_index = start_index;
		start_index += last_char_id - first_char_id;
		out_ranges.push_back(range);
	}
	return out_ranges;
}

vector<AtlasChar> FontDesc::GetChars(float tw, float th, const vector<Data> &results) const {
	vector<AtlasChar> chars;
	for (size_t r = 0; r < ranges.size(); r++) {
		for (int i = ranges[r].start; i < ranges[r].end; i++) {
			int idx = i - ranges[r].start + ranges[r].result_index;
			AtlasChar c;
			c.sx = results[idx].sx / tw;  // sx, sy, ex, ey
			c.sy = results[idx].sy / th;
			c.ex = results[idx].ex / tw;
			c.ey = results[idx].ey / th;
			c.ox = results[idx].ox; // ox, oy
			c.oy = results[idx].oy + results[idx].voffset;
			c.wx = results[idx].wx; //wx
			c.pw = results[idx].ex - results[idx].sx; // pw, ph
			c.ph = results[idx].ey - results[idx].sy;
			chars.push_back(c);
		}
	}
	return chars;
}

AtlasImage ImageDesc::ToAtlasImage(float tw, float th, const vector<Data> &results) {
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

void ImageDesc::OutputSelf(FILE *fil, float tw, float th, const vector<Data> &results) {
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

void ImageDesc::OutputHeader(FILE *fil, int index) {
	fprintf(fil, "#define %s %i\n", name.c_str(), index);
}

CharRange range(int start, int end, const std::set<u16> &filter) {
	CharRange r;
	r.start = start;
	r.end = end + 1;
	r.result_index = 0;
	r.filter = filter;
	return r;
}

CharRange range(int start, int end) {
	CharRange r;
	r.start = start;
	r.end = end + 1;
	r.result_index = 0;
	return r;
}

inline bool operator <(const CharRange &a, const CharRange &b) {
	// These ranges should never overlap so this should be enough.
	return a.start < b.start;
}

void LearnFile(const char *filename, const char *desc, std::set<u16> &chars, uint32_t lowerLimit, uint32_t upperLimit) {
	FILE *f = File::OpenCFile(Path(filename), "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		size_t sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		char *data = new char[sz + 1];
		fread(data, 1, sz, f);
		fclose(f);
		data[sz] = 0;

		UTF8 utf(data);
		int learnCount = 0;
		while (!utf.end()) {
			uint32_t c = utf.next();
			if (c >= lowerLimit && c <= upperLimit) {
				if (chars.find(c) == chars.end()) {
					learnCount++;
					chars.insert(c);
				}
			}
		}
		delete[] data;
		printf("%d %s characters learned.\n", learnCount, desc);
	}
}

bool GetLocales(const char *locales, std::vector<CharRange> &ranges) {
	std::set<u16> kanji;
	std::set<u16> hangul1, hangul2, hangul3;
	for (int i = 0; i < sizeof(kanjiFilter) / sizeof(kanjiFilter[0]); i += 2) {
		// Kanji filtering.
		if ((kanjiFilter[i + 1] & USE_KANJI) > 0) {
			kanji.insert(kanjiFilter[i]);
		}
	}

	if (!File::Exists(Path("assets/lang/zh_CN.ini"))) {
		printf("you're running the atlas gen from the wrong dir");
		return false;
	}

	LearnFile("assets/lang/zh_CN.ini", "Chinese", kanji, 0x3400, 0xFFFF);
	LearnFile("assets/lang/zh_TW.ini", "Chinese", kanji, 0x3400, 0xFFFF);
	LearnFile("assets/langregion.ini", "Chinese", kanji, 0x3400, 0xFFFF);
	LearnFile("assets/lang/ko_KR.ini", "Korean", hangul1, 0x1100, 0x11FF);
	LearnFile("assets/lang/ko_KR.ini", "Korean", hangul2, 0x3130, 0x318F);
	LearnFile("assets/lang/ko_KR.ini", "Korean", hangul3, 0xAC00, 0xD7A3);
	LearnFile("assets/langregion.ini", "Korean", hangul1, 0x1100, 0x11FF);
	LearnFile("assets/langregion.ini", "Korean", hangul2, 0x3130, 0x318F);
	LearnFile("assets/langregion.ini", "Korean", hangul3, 0xAC00, 0xD7A3);
	// The end point of a range is now inclusive!

	for (size_t i = 0; i < strlen(locales); i++) {
		switch (locales[i]) {
		case 'U':  // Basic Latin (US ASCII)
			ranges.push_back(range(0x20, 0x7E));  // 00 - 1F are C0 Controls, 7F is DEL
			break;
		case 'W':  // Latin-1 Supplement
			ranges.push_back(range(0xA0, 0xFF));  // 80 - 9F are C1 Controls
			break;
		case 'E':  // Latin Extended-A (various European languages, Slavic, Hungarian, Romanian, Turkish, etc.)
			ranges.push_back(range(0x100, 0x17F));
			break;
		case 'e':  // Latin Extended-B (additions for European languages, some Romanized African and Asian languages)
			ranges.push_back(range(0x180, 0x24F));
			break;
		case 'G':  // Greek and Coptic
			ranges.push_back(range(0x0370, 0x03FF));
			break;
		case 'R':  // Cyrillic (Russian, Bulgarian, etc.)
			ranges.push_back(range(0x0400, 0x04FF));
			break;
		case 'H':  // Hebrew
			ranges.push_back(range(0x0590, 0x05FF));
			break;

		case 'S':  // Select symbols
			ranges.push_back(range(0x2007, 0x2007));  // Figure Space (digit-wide)
			ranges.push_back(range(0x2020, 0x2021));  // Dagger and Double Dagger
			ranges.push_back(range(0x20AC, 0x20AC));  // Euro sign
			ranges.push_back(range(0x2116, 0x2116));  // "No." symbol
			ranges.push_back(range(0x2120, 0x2122));  // "SM", "TEL" and "TM" symbols
			ranges.push_back(range(0x2139, 0x2139));  // "i" symbol
			ranges.push_back(range(0x2300, 0x2300));  // Diameter sign
			ranges.push_back(range(0x2302, 0x2302));  // House sign
			// ranges.push_back(range(0x2314, 0x2314));  // Place of Interest sign
			// ranges.push_back(range(0x2328, 0x2328));  // Keyboard sign
			// ranges.push_back(range(0x232B, 0x232B));  // Backspace symbol
			// ranges.push_back(range(0x23CE, 0x23CF));  // Return and Eject symbols
			// ranges.push_back(range(0x23E9, 0x23EF));  // UI and Media Control symbols 1
			// ranges.push_back(range(0x23F4, 0x23FA));  // UI and Media Control symbols 2
			ranges.push_back(range(0x2610, 0x2612));  // Ballot boxes
			ranges.push_back(range(0x26A0, 0x26A1));  // Warning and High Voltage signs
			ranges.push_back(range(0x32CF, 0x32CF));  // "LTD" symbol
			ranges.push_back(range(0x33C7, 0x33C7));  // "Co." symbol
			ranges.push_back(range(0x33CD, 0x33CD));  // "K.K." symbol
			ranges.push_back(range(0xFFFD, 0xFFFD));  // "?" Replacement character
			break;

		case 'k':  // Katakana
			ranges.push_back(range(0x30A0, 0x30FF));
			ranges.push_back(range(0x31F0, 0x31FF));
			ranges.push_back(range(0xFF00, 0xFFEF));  // Halfwidth ascii
			break;
		case 'h':  // Hiragana
			ranges.push_back(range(0x3041, 0x3097));
			ranges.push_back(range(0x3099, 0x309F));
			break;
		case 'J':  // Shift JIS (for Japanese fonts)
			ranges.push_back(range(0x2010, 0x2312)); // General Punctuation, Letterlike Symbols, Arrows, 
			// Mathematical Operators, Miscellaneous Technical
			ranges.push_back(range(0x2500, 0x254B)); // Box drawing
			ranges.push_back(range(0x25A0, 0x266F)); // Geometric Shapes, Miscellaneous Symbols
			break;
		case 'c':  // All Kanji, filtered though!
			ranges.push_back(range(0x3000, 0x303F));  // Ideographic symbols
			ranges.push_back(range(0x4E00, 0x9FFF, kanji));
			// ranges.push_back(range(0xFB00, 0xFAFF, kanji));
			break;
		case 'T':  // Thai
			ranges.push_back(range(0x0E00, 0x0E5B));
			break;
		case 'K':  // Korean (hangul)
			ranges.push_back(range(0xAC00, 0xD7A3, hangul3));
			break;
		case 'V':  // Vietnamese (need 'e' too)
			ranges.push_back(range(0x1EA0, 0x1EF9));
			break;
		}
	}

	ranges.push_back(range(0xFFFD, 0xFFFD));
	std::sort(ranges.begin(), ranges.end());

	return true;
}

void LoadAndResolve(std::vector<ImageDesc> &images, std::map<std::string, FontReferenceList> &fontRefs, std::vector<FontDesc> &fonts, int image_width, std::vector<Data> &results, Image &dest) {
	Bucket bucket;

	for (auto &image : images) {
		if (!LoadImage(image.filename.c_str(), image.effect, &bucket)) {
			fprintf(stderr, "Failed to load image %s\n", image.filename.c_str());
		}
	}

	for (const auto &ref : fontRefs) {
		FontDesc fnt;
		fnt.first_char_id = (int)bucket.items.size();

		vector<CharRange> finalRanges;
		float metrics_height;
		RasterizeFonts(ref.second, finalRanges, &metrics_height, &bucket);
		printf("font rasterized.\n");

		fnt.ranges = finalRanges;
		fnt.name = ref.first;
		fnt.metrics_height = metrics_height;

		fonts.push_back(fnt);
	}

	// Script read, all subimages have been generated.

	// Place the subimages onto the main texture. Also writes to png.
	// Place things on the bitmap.
	printf("Resolving...\n");

	results = bucket.Resolve(image_width, dest);
}
