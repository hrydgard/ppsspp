// Sprite packing method borrowed from glorp engine and heavily modified.
// For license safety, just run this as a build tool, don't build it into your game/program.
// https://github.com/zorbathut/glorp

// data we need to provide:
// sx, sy
// dx, dy
// ox, oy
// wx

// line height
// dist-per-pixel

#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftbitmap.h>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>

#include "gfx/texture_atlas.h"

#include "image/png_load.h"
#include "image/zim_save.h"

#include "kanjifilter.h"
// extracted only JIS Kanji on the CJK Unified Ideographs of UCS2. Cannot reading BlockAllocator. (texture size over)
//#define USE_KANJI KANJI_STANDARD | KANJI_RARELY_USED | KANJI_LEVEL4
// daily-use character only. However, it is too enough this.
//#define USE_KANJI KANJI_STANDARD (texture size over)
// Shift-JIS filtering. (texture size over)
//#define USE_KANJI KANJI_SJIS_L1 | KANJI_SJIS_L2
// more conpact daily-use character. but, not enough this.
// if when you find the unintelligible sequence of characters,
// add kanjiFilter Array with KANJI_LEARNING_ORDER_ADDTIONAL.
#define USE_KANJI KANJI_LEARNING_ORDER_ALL

#include "util/text/utf8.h"

#define CHECK(x) if (!(x)) { printf("%i: CHECK failed on this line\n", __LINE__); exit(1); }

using namespace std;
static int global_id;
static bool highcolor = false;




typedef unsigned short u16;

struct CharRange : public AtlasCharRange {
	std::set<u16> filter;
};

enum Effect {
  FX_COPY = 0,
  FX_RED_TO_ALPHA_SOLID_WHITE = 1,   // for alpha fonts
  FX_RED_TO_INTENSITY_ALPHA_255 = 2,
  FX_PREMULTIPLY_ALPHA = 3,
  FX_PINK_TO_ALPHA = 4,   // for alpha fonts
  FX_INVALID = 5,
};

const char *effect_str[5] = {
  "copy", "r2a", "r2i", "pre", "p2a",
};

Effect GetEffect(const char *text) {
  for (int i = 0; i < 5; i++) {
    if (!strcmp(text, effect_str[i])) {
      return (Effect)i;
    }
  }
  return FX_INVALID;
}

struct FontReference {
	FontReference(string name, string file, vector<CharRange> ranges, int pixheight, float vertOffset)
		: name_(name), file_(file), ranges_(ranges), size_(pixheight), vertOffset_(vertOffset) {
	}

	string name_;
	string file_;
	vector<CharRange> ranges_;
	int size_;
	float vertOffset_;
};

typedef vector<FontReference> FontReferenceList;

template<class T>
struct Image {
  vector<vector<T> > dat;
  void resize(int x, int y) {
    dat.resize(y);
    for(int i = 0; i < y; i++)
      dat[i].resize(x);
  }
  int width() const {
    return (int)dat[0].size();
  }
  int height() const {
    return (int)dat.size();
  }
  void copyfrom(const Image &img, int ox, int oy, int effect) {
    CHECK(img.dat[0].size() + ox <= dat[0].size());
    CHECK(img.dat.size() + oy <= dat.size());
    for (int y = 0; y < (int)img.dat.size(); y++) {
      for (int x = 0; x < (int)img.dat[y].size(); x++) {
        switch (effect) {
        case FX_COPY:
          dat[y + oy][ox + x] = img.dat[y][x];
          break;
        case FX_RED_TO_ALPHA_SOLID_WHITE:
          dat[y + oy][ox + x] = 0x00FFFFFF | (img.dat[y][x] << 24);
          break;
        case FX_RED_TO_INTENSITY_ALPHA_255:
          dat[y + oy][ox + x] = 0xFF000000 | img.dat[y][x] | (img.dat[y][x] << 8) | (img.dat[y][x] << 16);
          break;
        case FX_PREMULTIPLY_ALPHA:
        {
          unsigned int color = img.dat[y][x];
          unsigned int a = color >> 24;
          unsigned int r = (color & 0xFF) * a >> 8, g = (color & 0xFF00) * a>> 8, b = (color & 0xFF0000) * a >> 8;
          color = (color & 0xFF000000) | (r & 0xFF) | (g & 0xFF00) | (b & 0xFF0000);
          // Simulate 4444
          color = color & 0xF0F0F0F0;
          color |= color >> 4;
          dat[y + oy][ox + x] = color;
          break;
        }
        case FX_PINK_TO_ALPHA:
          dat[y + oy][ox + x] = ((img.dat[y][x]&0xFFFFFF) == 0xFF00FF) ? 0x00FFFFFF : (img.dat[y][x] | 0xFF000000);
          break;
        default:
          dat[y + oy][ox + x] = 0xFFFF00FF;
          break;
        }
      }
    }
  }
  void set(int sx, int sy, int ex, int ey, unsigned char fil) {
    for(int y = sy; y < ey; y++)
      fill(dat[y].begin() + sx, dat[y].begin() + ex, fil);
  }
  bool LoadPNG(const char *png_name) {
    unsigned char *img_data;
    int w, h;
    if (1 != pngLoad(png_name, &w, &h, &img_data, false)) {
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
  void SavePNG(const char *png_name) {
    // Save PNG
    FILE *fil = fopen(png_name, "wb");
    png_structp  png_ptr;
    png_infop  info_ptr;
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    CHECK(png_ptr);
    info_ptr = png_create_info_struct(png_ptr);
    CHECK(info_ptr);
    png_init_io(png_ptr, fil);
    //png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
    png_set_IHDR(png_ptr, info_ptr, dat[0].size(), dat.size(), 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);
    for(int y = 0; y < (int)dat.size(); y++) {
      png_write_row(png_ptr, (png_byte*)&dat[y][0]);
    }
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
  }
  void SaveZIM(const char *zim_name, int zim_format) {
    uint8 *image_data = new uint8[width() * height() * 4];
    for (int y = 0; y < height(); y++) {
      memcpy(image_data + y * width() * 4, &dat[y][0], width() * 4);
    }
    ::SaveZIM(zim_name, width(), height(), width() * 4, zim_format | ZIM_DITHER, image_data);
  }
};

template<class S, class T>
bool operator<(const Image<S> &lhs, const Image<T> &rhs) {
  return lhs.dat.size() * lhs.dat[0].size() > rhs.dat.size() * rhs.dat[0].size();
}

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

bool operator<(const Data &lhs, const Data &rhs) {
  return lhs.id < rhs.id; // should be unique
}

string out_prefix;

int NextPowerOf2(int x) {
  int powof2 = 1;
  // Double powof2 until >= val
  while (powof2 < x) powof2 <<= 1;
  return powof2;
}

struct Bucket {
	vector<pair<Image<unsigned int>, Data> > items;
	void AddItem(const Image<unsigned int> &img, const Data &dat) {
		items.push_back(make_pair(img, dat));
	}
	vector<Data> Resolve(int image_width, Image<unsigned int> &dest) {
		// Place all the little images - whatever they are.
		// Uses greedy fill algorithm. Slow but works surprisingly well, CPUs are fast.
		Image<unsigned char> masq;
		masq.resize(image_width, 1);
		dest.resize(image_width, 1);
		sort(items.begin(), items.end());
		for (int i = 0; i < (int)items.size(); i++) {
			if ((i + 1) % 200 == 0) {
				printf("Resolving (%i / %i)\n", i, (int)items.size());
			}
			int idx = items[i].first.dat[0].size();
			int idy = items[i].first.dat.size();
			if (idx > 1 && idy > 1) {
				CHECK(idx <= image_width);
				for (int ty = 0; ty < 2047; ty++) {
					if(ty + idy + 1 > (int)dest.dat.size()) {
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
							for(int ity = 0; ity < idy && valid; ity++) {
								for(int itx = 0; itx < idx && valid; itx++) {
									if(masq.dat[ty + ity][tx + itx]) {
										goto skip;
									}
								}
							}
							dest.copyfrom(items[i].first, tx, ty, items[i].second.effect);
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
    dest.resize(image_width, NextPowerOf2(dest.dat.size()));

    // Output the glyph data.
    vector<Data> dats;
    for(int i = 0; i < (int)items.size(); i++)
      dats.push_back(items[i].second);
    return dats;
  }
};

const int supersample = 16;
const int distmult = 64 * 3;  // this is "one pixel in the final version equals 64 difference". reduce this number to increase the "blur" radius, increase it to make things "sharper"
const int maxsearch = (128 * supersample + distmult - 1) / distmult;

struct Closest {
  FT_Bitmap bmp;
  Closest(FT_Bitmap bmp) : bmp(bmp) { }
  float find_closest(int x, int y, char search) {
    int best = 1 << 30;
    for(int i = 1; i <= maxsearch; i++) {
      if(i * i >= best)
        break;
      for(int f = -i; f < i; f++) {
        int dist = i * i + f * f;
        if(dist >= best) continue;
        if(safe_access(x + i, y + f) == search || safe_access(x - f, y + i) == search || safe_access(x - i, y - f) == search || safe_access(x + f, y - i) == search)
          best = dist;
      }
    }
    return sqrt((float)best);
  }
  char safe_access(int x, int y) {
    if(x < 0 || y < 0 || x >= bmp.width || y >= bmp.rows)
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

void RasterizeFonts(const FontReferenceList fontRefs, vector<CharRange> &ranges, float *metrics_height, Bucket *bucket) {
	FT_Library freetype;
	CHECK(FT_Init_FreeType(&freetype) == 0);

	vector<FT_Face> fonts;
	fonts.resize(fontRefs.size());

	// The ranges may overlap, so build a list of fonts per range.
	map<int, FT_Face_List> fontsByRange;
	// TODO: Better way than average?
	float totalHeight = 0.0f;

	for (size_t i = 0, n = fontRefs.size(); i < n; ++i) {
		FT_Face &font = fonts[i];
		CHECK(FT_New_Face(freetype, fontRefs[i].file_.c_str(), 0, &font) == 0);
		printf("TTF info: %d glyphs, %08x flags, %d units, %d strikes\n", (int)font->num_glyphs, (int)font->face_flags, (int)font->units_per_EM, (int)font->num_fixed_sizes);

		CHECK(FT_Set_Pixel_Sizes(font, 0, fontRefs[i].size_ * supersample) == 0);

		ranges = merge(ranges, fontRefs[i].ranges_);
		for (size_t r = 0, rn = fontRefs[i].ranges_.size(); r < rn; ++r) {
			const CharRange &range = fontRefs[i].ranges_[r];
			fontsByRange[range.start].push_back(fonts[i]);
		}

		totalHeight += font->size->metrics.height;
	}
	*metrics_height = totalHeight / (float) fontRefs.size();

	// Convert all characters to bitmaps.
	for (size_t r = 0, rn = ranges.size(); r < rn; r++) {
		FT_Face_List &tryFonts = fontsByRange[ranges[r].start];
		ranges[r].start_index = global_id;
		for(int kar = ranges[r].start; kar < ranges[r].end; kar++) {
			bool filtered = false;
			if (ranges[r].filter.size()) {
				if (ranges[r].filter.find((u16)kar) == ranges[r].filter.end())
					filtered = true;
			}

			FT_Face font;
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
			if (!foundMatch)
				fprintf(stderr, "WARNING: No font contains character %x.\n", kar);

			Image<unsigned int> img;
			if (filtered || 0 != FT_Load_Char(font, kar, FT_LOAD_RENDER|FT_LOAD_MONOCHROME)) {
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
				dat.charNum = kar;
				dat.effect = FX_RED_TO_ALPHA_SOLID_WHITE;
				bucket->AddItem(img, dat);
				continue;
			}

			// printf("%dx%d %p\n", font->glyph->bitmap.width, font->glyph->bitmap.rows, font->glyph->bitmap.buffer);
			const int bord = (128 + distmult - 1) / distmult + 1;
			if(font->glyph->bitmap.buffer) {
				FT_Bitmap tempbitmap;
				FT_Bitmap_New(&tempbitmap);
				FT_Bitmap_Convert(freetype, &font->glyph->bitmap, &tempbitmap, 1);
				Closest closest(tempbitmap);

				// No resampling, just sets the size of the image.
				img.resize((tempbitmap.width + supersample - 1) / supersample + bord * 2, (tempbitmap.rows + supersample - 1) / supersample + bord * 2);
				int lmx = img.dat[0].size();
				int lmy = img.dat.size();

				// AA by finding distance to character. Probably a fairly decent approximation but why not do it right?
				for(int y = 0; y < lmy; y++) {
					int cty = (y - bord) * supersample + supersample / 2;
					for(int x = 0; x < lmx; x++) {
						int ctx = (x - bord) * supersample + supersample / 2;
						float dist;
						if(closest.safe_access(ctx, cty)) {
							dist = closest.find_closest(ctx, cty, 0);
						} else {
							dist = -closest.find_closest(ctx, cty, 1);
						}
						dist = dist / supersample * distmult + 127.5;
						dist = floor(dist + 0.5);
						if(dist < 0) dist = 0;
						if(dist > 255) dist = 255;

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
			dat.ex = img.dat[0].size();
			dat.ey = img.dat.size();
			dat.ox = (float)font->glyph->metrics.horiBearingX / 64 / supersample - bord;
			dat.oy = -(float)font->glyph->metrics.horiBearingY / 64 / supersample - bord;
			dat.voffset = vertOffset;
			dat.wx = (float)font->glyph->metrics.horiAdvance / 64 / supersample;
			dat.charNum = kar;

			dat.effect = FX_RED_TO_ALPHA_SOLID_WHITE;
			bucket->AddItem(img, dat);
		}
	}

	for (size_t i = 0, n = fonts.size(); i < n; ++i) {
		FT_Done_Face(fonts[i]);
	}
	FT_Done_FreeType(freetype);
}


bool LoadImage(const char *imagefile, Effect effect, Bucket *bucket) {
  Image<unsigned int> img;

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

  Data dat;
  memset(&dat, 0, sizeof(dat));
  dat.id = global_id++;
  dat.sx = 0;
  dat.sy = 0;
  dat.ex = img.dat[0].size();
  dat.ey = img.dat.size();
  dat.effect = effect;
  bucket->AddItem(img, dat);
  return true;
}

// Use the result array, and recorded data, to generate C++ tables for everything.
struct FontDesc {
  string name;

  int first_char_id;
  int last_char_id;

  float ascend;
  float descend;
  float height;

  float metrics_height;

	std::vector<CharRange> ranges;

	FontDesc()
	{
	}

  void ComputeHeight(const vector<Data> &results, float distmult) {
    ascend = 0;
    descend = 0;
		for (size_t r = 0; r < ranges.size(); r++) {
			for(int i = ranges[r].start; i < ranges[r].end; i++) {
				int idx = i - ranges[r].start + ranges[r].start_index;
				ascend = max(ascend, -results[idx].oy);
				descend = max(descend, results[idx].ey - results[idx].sy + results[idx].oy);
			}
		}

    height = metrics_height / 64.0 / supersample;
  }

  void OutputSelf(FILE *fil, float tw, float th, const vector<Data> &results) {
		// Dump results as chardata.
		fprintf(fil, "const AtlasChar font_%s_chardata[] = {\n", name.c_str());
		for (size_t r = 0; r < ranges.size(); r++) {
			fprintf(fil, "// RANGE: 0x%x - 0x%x, start 0x%x\n", ranges[r].start, ranges[r].end, ranges[r].start_index);
			for (int i = ranges[r].start; i < ranges[r].end; i++) {
				int idx = i - ranges[r].start + ranges[r].start_index;
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
		}
		fprintf(fil, "};\n");

		fprintf(fil, "const AtlasCharRange font_%s_ranges[] = {\n", name.c_str());
		// Write range information.
		int start_index = 0;
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
    fprintf(fil, "  %ff, // distslope\n", distmult / 256.0);
    fprintf(fil, "  font_%s_chardata,\n", name.c_str());
		fprintf(fil, "  font_%s_ranges,\n", name.c_str());
		fprintf(fil, "  %i,\n", (int)ranges.size());
		fprintf(fil, "  \"%s\", // name\n", name.c_str());
    fprintf(fil, "};\n");
  }

  void OutputIndex(FILE *fil) {
    fprintf(fil, "  &font_%s,\n", name.c_str());
  }

  void OutputHeader(FILE *fil, int index) {
		fprintf(fil, "#define %s %i\n", name.c_str(), index);
  }
};

struct ImageDesc {
  string name;
  Effect effect;
  int result_index;

  void OutputSelf(FILE *fil, float tw, float th, const vector<Data> &results) {
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

  void OutputHeader(FILE *fil, int index) {
		fprintf(fil, "#define %s %i\n", name.c_str(), index);
  }
};


CharRange range(int start, int end, const std::set<u16> &filter) {
	CharRange r;
	r.start = start;
	r.end = end + 1;
	r.start_index = 0;
	r.filter = filter;
	return r;
}

CharRange range(int start, int end) {
	CharRange r;
	r.start = start;
	r.end = end + 1;
	r.start_index = 0;
	return r;
}

inline bool operator <(const CharRange &a, const CharRange &b) {
	// These ranges should never overlap so this should be enough.
	return a.start < b.start;
}


void LearnFile(const char *filename, const char *desc, std::set<u16> &chars, uint32_t lowerLimit, uint32_t upperLimit) {
	FILE *f = fopen(filename, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		size_t sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		char *data = new char[sz+1];
		fread(data, 1, sz, f);
		fclose(f);
		data[sz]=0;

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
		delete [] data;
		printf("%i %s characters learned.\n", learnCount, desc);
	}

}

void GetLocales(const char *locales, std::vector<CharRange> &ranges)
{
	std::set<u16> kanji;
	std::set<u16> hangul1, hangul2, hangul3;
	for (int i = 0; i < sizeof(kanjiFilter)/sizeof(kanjiFilter[0]); i+=2)
	{
		// Kanji filtering.
		if ((kanjiFilter[i+1] & USE_KANJI) > 0) {
			kanji.insert(kanjiFilter[i]);
		}
	}

	// Also, load chinese.txt if available.
	LearnFile("chinese.txt", "Chinese", kanji, 0x3400, 0xFFFF);
	LearnFile("korean.txt", "Korean", hangul1, 0x1100, 0x11FF);
	LearnFile("korean.txt", "Korean", hangul2, 0x3130, 0x318F);
	LearnFile("korean.txt", "Korean", hangul3, 0xAC00, 0xD7A3);

	// The end point of a range is now inclusive!

	for (size_t i = 0; i < strlen(locales); i++) {
		switch (locales[i]) {
		case 'U':  // US ASCII
			ranges.push_back(range(32, 127));
			break;
		case 'W':  // Latin-1 extras 1
			ranges.push_back(range(0x80, 0x80));  // euro sign
			ranges.push_back(range(0xA2, 0xFF));  // 80 - A0 appears to contain nothing interesting
			ranges.push_back(range(0x2121, 0x2122));  // TEL symbol and trademark symbol 
			break;
		case 'E':  // Latin-1 Extended A (needed for Hungarian etc)
			ranges.push_back(range(0x100, 0x17F));
			break;
		case 'e':  // Latin-1 Extended B (for some African and latinized asian languages?)
			ranges.push_back(range(0x180, 0x250));
			break;
		case 'k':  // Katakana
			ranges.push_back(range(0x30A0, 0x30FF));
			ranges.push_back(range(0x31F0, 0x31FF));
			ranges.push_back(range(0xFF00, 0xFFEF));  // half-width ascii
			break;
		case 'h':  // Hiragana
			ranges.push_back(range(0x3041, 0x3097));
			ranges.push_back(range(0x3099, 0x309F));
			break;
		case 's':  // ShiftJIS symbols
			ranges.push_back(range(0x2010, 0x2312)); // General Punctuation, Letterlike Symbols, Arrows, 
			                                         // Mathematical Operators, Miscellaneous Technical
			ranges.push_back(range(0x2500, 0x254B)); // Box drawing
			ranges.push_back(range(0x25A0, 0x266F)); //  Geometric Shapes,  Miscellaneous Symbols
			ranges.push_back(range(0x3231, 0x3231)); // Co,.Ltd. symbol
			ranges.push_back(range(0x2116, 0x2116)); // "No." symbol
			ranges.push_back(range(0x33CD, 0x33CD)); // "K.K." symbol
		case 'H':  // Hebrew
			ranges.push_back(range(0x0590, 0x05FF));
			break;
		case 'G':  // Greek
			ranges.push_back(range(0x0370, 0x03FF));
			break;
		case 'R':  // Russian
			ranges.push_back(range(0x0400, 0x04FF));
			break;
		case 'c':  // All Kanji, filtered though!
			ranges.push_back(range(0x3000, 0x303f));  // Ideographic symbols
			ranges.push_back(range(0x4E00, 0x9FFF, kanji));
			// ranges.push_back(range(0xFB00, 0xFAFF, kanji));
			break;
		case 'T':  // Thai
			ranges.push_back(range(0x0E00, 0x0E5B));
			break;
		case 'K':  // Korean (hangul)
			ranges.push_back(range(0xAC00, 0xD7A3, hangul3));
			break;
		}
	}
	
	ranges.push_back(range(0xFFFD, 0xFFFD));
	std::sort(ranges.begin(), ranges.end());
}

int main(int argc, char **argv) {
  // initProgram(&argc, const_cast<const char ***>(&argv));
  // /usr/share/fonts/truetype/msttcorefonts/Arial_Black.ttf
  // /usr/share/fonts/truetype/ubuntu-font-family/Ubuntu-R.ttf

  CHECK(argc >= 3);
	if (argc > 3)
	{
		highcolor = true;
		printf("RGBA8888 enabled!\n");
	}
  printf("Reading script %s\n", argv[1]);
  const char *atlas_name = argv[2];
  string image_name = string(atlas_name) + "_atlas.zim";
  out_prefix = argv[2];

  map<string, FontReferenceList> fontRefs;
  vector<FontDesc> fonts;
  vector<ImageDesc> images;

  Bucket bucket;

  char line[512];
  FILE *script = fopen(argv[1], "r");
  if (!fgets(line, 512, script)) {
    printf("Error fgets-ing\n");
  }
  int image_width;
  sscanf(line, "%i", &image_width);
  printf("Texture width: %i\n", image_width);
	while (!feof(script)) {
		if (!fgets(line, 511, script)) break;
		if (!strlen(line)) break;
		char *rest = strchr(line, ' ');
		if (rest) {
			*rest = 0;
			rest++;
		}
		char *word = line;
		if (!strcmp(word, "font")) {
			// Font!
			char fontname[256];
			char fontfile[256];
			char locales[256];
			int pixheight;
			float vertOffset = 0;
			sscanf(rest, "%s %s %s %i %f", fontname, fontfile, locales, &pixheight, &vertOffset);
			printf("Font: %s (%s) in size %i. Locales: %s\n", fontname, fontfile, pixheight, locales);

			std::vector<CharRange> ranges;
			GetLocales(locales, ranges);
			printf("locales fetched.\n");

			FontReference fnt(fontname, fontfile, ranges, pixheight, vertOffset);
			fontRefs[fontname].push_back(fnt);
    } else if (!strcmp(word, "image")) {
      char imagename[256];
      char imagefile[256];
      char effectname[256];
      sscanf(rest, "%s %s %s", imagename, imagefile, effectname);
      Effect effect = GetEffect(effectname);
      printf("Image %s with effect %s (%i)\n", imagefile, effectname, (int)effect);
      ImageDesc desc;
      desc.name = imagename;
      desc.effect = effect;
      desc.result_index = (int)bucket.items.size();
      images.push_back(desc);
      if (!LoadImage(imagefile, effect, &bucket)) {
        fprintf(stderr, "Failed to load image %s\n", imagefile);
      }
    } else {
			fprintf(stderr, "Warning: Failed to parse line starting with %s\n", line);
		}
	}
	fclose(script);

	// Script fully read, now rasterize the fonts.
	for (auto it = fontRefs.begin(), end = fontRefs.end(); it != end; ++it) {
		FontDesc fnt;
		fnt.first_char_id = (int)bucket.items.size();

		vector<CharRange> finalRanges;
		float metrics_height;
		RasterizeFonts(it->second, finalRanges, &metrics_height, &bucket);
		printf("font rasterized.\n");

		fnt.ranges = finalRanges;
		fnt.name = it->first;
		fnt.metrics_height = metrics_height;

		fonts.push_back(fnt);
	}

  // Script read, all subimages have been generated.

  // Place the subimages onto the main texture. Also writes to png.
  Image<unsigned int> dest;
  // Place things on the bitmap.
  printf("Resolving...\n");

  vector<Data> results = bucket.Resolve(image_width, dest);
	if (highcolor) {
		printf("Writing .ZIM %ix%i RGBA8888...\n", dest.width(), dest.height());
		dest.SaveZIM(image_name.c_str(), ZIM_RGBA8888 | ZIM_ZLIB_COMPRESSED);
	} else {
		printf("Writing .ZIM %ix%i RGBA4444...\n", dest.width(), dest.height());
		dest.SaveZIM(image_name.c_str(), ZIM_RGBA4444 | ZIM_ZLIB_COMPRESSED);
	}
  // Also save PNG for debugging.
  printf("Writing .PNG %s\n", (image_name + ".png").c_str());
  dest.SavePNG((image_name + ".png").c_str());

  printf("Done. Outputting source files %s_atlas.cpp/h.\n", out_prefix.c_str());
  // Sort items by ID.
  sort(results.begin(), results.end());

  FILE *cpp_file = fopen((out_prefix + "_atlas.cpp").c_str(), "wb");
  fprintf(cpp_file, "// C++ generated by atlastool from %s (hrydgard@gmail.com)\n\n", argv[1]);
  fprintf(cpp_file, "#include \"%s\"\n\n", (out_prefix + "_atlas.h").c_str());
  for (int i = 0; i < (int)fonts.size(); i++) {
    FontDesc &xfont = fonts[i];
    xfont.ComputeHeight(results, distmult);
    xfont.OutputSelf(cpp_file, dest.width(), dest.height(), results);
  }

  if (fonts.size()) {
    fprintf(cpp_file, "const AtlasFont *%s_fonts[%i] = {\n", atlas_name, (int)fonts.size());
    for (int i = 0; i < (int)fonts.size(); i++) {
      fonts[i].OutputIndex(cpp_file);
    }
    fprintf(cpp_file, "};\n");
  }

  if (images.size()) {
    fprintf(cpp_file, "const AtlasImage %s_images[%i] = {\n", atlas_name, (int)images.size());
    for (int i = 0; i < (int)images.size(); i++) {
      images[i].OutputSelf(cpp_file, dest.width(), dest.height(), results);
    }
    fprintf(cpp_file, "};\n");
  }

  fprintf(cpp_file, "const Atlas %s_atlas = {\n", atlas_name);
  fprintf(cpp_file, "  \"%s\",\n", image_name.c_str());
  if (fonts.size()) {
    fprintf(cpp_file, "  %s_fonts, %i,\n", atlas_name, (int)fonts.size());
  } else {
    fprintf(cpp_file, "  0, 0,\n");
  }
  if (images.size()) {
    fprintf(cpp_file, "  %s_images, %i,\n", atlas_name, (int)images.size());
  } else {
    fprintf(cpp_file, "  0, 0,\n");
  }
  fprintf(cpp_file, "};\n");
  // Should output a list pointing to all the fonts as well.
  fclose(cpp_file);

  FILE *h_file = fopen((out_prefix + "_atlas.h").c_str(), "wb");
  fprintf(h_file, "// Header generated by atlastool from %s (hrydgard@gmail.com)\n\n", argv[1]);
  fprintf(h_file, "#pragma once\n");
  fprintf(h_file, "#include \"gfx/texture_atlas.h\"\n\n");
  if (fonts.size()) {
    fprintf(h_file, "// FONTS_%s\n", atlas_name);
    for (int i = 0; i < (int)fonts.size(); i++) {
      fonts[i].OutputHeader(h_file, i);
    }
    fprintf(h_file, "\n\n");
  }
  if (images.size()) {
    fprintf(h_file, "// IMAGES_%s\n", atlas_name);
    for (int i = 0; i < (int)images.size(); i++) {
      images[i].OutputHeader(h_file, i);
    }
    fprintf(h_file, "\n\n");
  }
  fprintf(h_file, "extern const Atlas %s_atlas;\n", atlas_name);
  fprintf(h_file, "extern const AtlasImage %s_images[%i];\n", atlas_name, (int)images.size());
  fclose(h_file);
  // TODO: Turn into C++ arrays.
}
