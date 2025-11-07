// Sprite packing method borrowed from glorp engine and heavily modified.
// For license safety, just run this as a build tool, don't build it into your game/program.
// https://github.com/zorbathut/glorp

// Horrible build instructions:
// * Download freetype, put in ppsspp/ext as freetype/
// * Open tools.sln
// * In Code Generation on freetype, change from Multithreaded DLL to Multithreaded.
// * Build
// * Move exe file to ext/native/tools/build

// data we need to provide:
// sx, sy
// dx, dy
// ox, oy
// wx

// line height
// dist-per-pixel

#include <cstdio>
#include <assert.h>
#include <cstring>
#include <set>
#include <vector>
#include <map>
#include <string>
#include "ft2build.h"
#include "freetype/ftbitmap.h"
#include "zstd.h"

#include "Common/Render/AtlasGen.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/Data/Format/ZIMSave.h"
#include "kanjifilter.h"


constexpr int supersample = 16;
constexpr int distmult = 64 * 3;  // this is "one pixel in the final version equals 64 difference". reduce this number to increase the "blur" radius, increase it to make things "sharper"
constexpr int maxsearch = (128 * supersample + distmult - 1) / distmult;

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

using namespace std;

struct ImageDesc {
	std::string name;
	std::string fileName;
	int result_index;
};

struct CharRange : public AtlasCharRange {
	std::set<u16> filter;
};

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


struct FontReference {
	FontReference(string name, string file, std::vector<CharRange> ranges, int pixheight, float vertOffset)
		: name_(name), file_(file), ranges_(ranges), size_(pixheight), vertOffset_(vertOffset) {
	}

	std::string name_;
	std::string file_;
	std::vector<CharRange> ranges_;
	int size_;
	float vertOffset_;
};

typedef std::vector<FontReference> FontReferenceList;

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

struct Closest {
	FT_Bitmap bmp;
	Closest(FT_Bitmap bmp) : bmp(bmp) {}
	float find_closest(int x, int y, char search) {
		int best = 1 << 30;
		for (int i = 1; i <= maxsearch; i++) {
			if (i * i >= best)
				break;
			for (int f = -i; f < i; f++) {
				int dist = i * i + f * f;
				if (dist >= best) continue;
				if (safe_access(x + i, y + f) == search || safe_access(x - f, y + i) == search || safe_access(x - i, y - f) == search || safe_access(x + f, y - i) == search)
					best = dist;
			}
		}
		return (float)sqrt((float)best);
	}
	char safe_access(int x, int y) {
		if (x < 0 || y < 0 || x >= (int)bmp.width || y >= (int)bmp.rows)
			return 0;
		return bmp.buffer[x + y * bmp.width];
	}
};

void RasterizeFonts(const FontReferenceList &fontRefs, vector<CharRange> &ranges, float *metrics_height, Bucket *bucket, int &global_id) {
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

		if (FT_Set_Pixel_Sizes(font, 0, fontRefs[i].size_ * supersample) != 0) {
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

			if (!foundMatch || filtered || 0 != FT_Load_Char(font, kar, FT_LOAD_RENDER | FT_LOAD_MONOCHROME)) {
				Image img;
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
				dat.redToWhiteAlpha = true;
				bucket->AddItem(std::move(img), dat);
				continue;
			}
			Image img;

			// printf("%dx%d %p\n", font->glyph->bitmap.width, font->glyph->bitmap.rows, font->glyph->bitmap.buffer);
			const int bord = (128 + distmult - 1) / distmult + 1;
			if (font->glyph->bitmap.buffer) {
				FT_Bitmap tempbitmap;
				FT_Bitmap_New(&tempbitmap);
				FT_Bitmap_Convert(freetype, &font->glyph->bitmap, &tempbitmap, 1);
				Closest closest(tempbitmap);

				// No resampling, just sets the size of the image.
				img.resize((tempbitmap.width + supersample - 1) / supersample + bord * 2, (tempbitmap.rows + supersample - 1) / supersample + bord * 2);
				int lmx = (int)img.width();
				int lmy = (int)img.height();

				// AA by finding distance to character. Probably a fairly decent approximation but why not do it right?
				for (int y = 0; y < lmy; y++) {
					int cty = (y - bord) * supersample + supersample / 2;
					for (int x = 0; x < lmx; x++) {
						int ctx = (x - bord) * supersample + supersample / 2;
						float dist;
						if (closest.safe_access(ctx, cty)) {
							dist = closest.find_closest(ctx, cty, 0);
						} else {
							dist = -closest.find_closest(ctx, cty, 1);
						}
						dist = dist / supersample * distmult + 127.5f;
						dist = floorf(dist + 0.5f);
						if (dist < 0.0f) dist = 0.0f;
						if (dist > 255.0f) dist = 255.0f;

						// Only set the red channel. We process when adding the image.
						img.set1(x, y, (u8)dist);
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
			dat.ex = (int)img.width();
			dat.ey = (int)img.height();
			dat.w = dat.ex;
			dat.h = dat.ey;
			dat.ox = (float)font->glyph->metrics.horiBearingX / 64 / supersample - bord;
			dat.oy = -(float)font->glyph->metrics.horiBearingY / 64 / supersample - bord;
			dat.voffset = vertOffset;
			dat.wx = (float)font->glyph->metrics.horiAdvance / 64 / supersample;
			dat.charNum = kar;

			dat.redToWhiteAlpha = true;
			bucket->AddItem(std::move(img), dat);
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

// Use the result array, and recorded data, to generate C++ tables for everything.
struct FontDesc {
	string name;

	int first_char_id = -1;

	float ascend = 0.0f;
	float descend = 0.0f;
	float height = 0.0f;

	float metrics_height = 0.0f;

	std::vector<CharRange> ranges;

	void ComputeHeight(const vector<Data> &results, float distmult) {
		ascend = 0;
		descend = 0;
		for (size_t r = 0; r < ranges.size(); r++) {
			for (int i = ranges[r].start; i < ranges[r].end; i++) {
				int idx = i - ranges[r].start + ranges[r].result_index;
				ascend = max(ascend, -results[idx].oy);
				descend = max(descend, results[idx].ey - results[idx].sy + results[idx].oy);
			}
		}

		height = metrics_height / 64.0f / supersample;
	}

	AtlasFontHeader GetHeader() const {
		int numChars = 0;
		for (size_t r = 0; r < ranges.size(); r++) {
			numChars += ranges[r].end - ranges[r].start;
		}
		AtlasFontHeader header{};
		header.padding = height - ascend - descend;
		header.height = ascend + descend;
		header.ascend = ascend;
		header.distslope = distmult / 256.0;
		truncate_cpy(header.name, name);
		header.numChars = numChars;
		header.numRanges = (int)ranges.size();
		return header;
	}

	vector<AtlasCharRange> GetRanges() const {
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

	vector<AtlasChar> GetChars(float tw, float th, const vector<Data> &results) const {
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
};


void LearnFile(const char *filename, const char *desc, std::set<u16> &chars, uint32_t lowerLimit, uint32_t upperLimit) {
	FILE *f = fopen(filename, "rb");
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

void GetLocales(const char *locales, std::vector<CharRange> &ranges)
{
	std::set<u16> kanji;
	std::set<u16> hangul1, hangul2, hangul3;
	for (int i = 0; i < sizeof(kanjiFilter) / sizeof(kanjiFilter[0]); i += 2)
	{
		// Kanji filtering.
		if ((kanjiFilter[i + 1] & USE_KANJI) > 0) {
			kanji.insert(kanjiFilter[i]);
		}
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
}

static bool WriteCompressed(const void *src, size_t sz, size_t num, FILE *fp) {
	size_t src_size = sz * num;
	size_t compressed_size = ZSTD_compressBound(src_size);
	uint8_t *compressed = new uint8_t[compressed_size];
	compressed_size = ZSTD_compress(compressed, compressed_size, src, src_size, 22);
	if (ZSTD_isError(compressed_size)) {
		delete[] compressed;
		return false;
	}

	uint32_t write_size = (uint32_t)compressed_size;
	if (fwrite(&write_size, sizeof(uint32_t), 1, fp) != 1) {
		delete[] compressed;
		return false;
	}
	if (fwrite(compressed, 1, compressed_size, fp) != compressed_size) {
		delete[] compressed;
		return false;
	}

	delete[] compressed;
	return true;
}

//argv[1], argv[2], argv[3]
int GenerateFromScript(const char *script_file, const char *atlas_name, bool highcolor) {
	map<string, FontReferenceList> fontRefs;
	vector<FontDesc> fonts;
	vector<ImageDesc> images;

	int global_id = 0;

	std::string image_name = string(atlas_name) + "_atlas.zim";
	std::string meta_name = string(atlas_name) + "_atlas.meta";

	const std::string out_prefix = atlas_name;

	char line[512]{};
	FILE *script = fopen(script_file, "r");
	if (!fgets(line, 512, script)) {
		printf("Error fgets-ing\n");
	}
	int image_width = 0;
	sscanf(line, "%i", &image_width);
	printf("Texture width: %i\n", image_width);
	while (!feof(script)) {
		if (!fgets(line, 511, script)) break;
		if (!strlen(line)) break;
		if (line[0] == '#') continue;
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
			sscanf(rest, "%255s %255s %255s %i %f", fontname, fontfile, locales, &pixheight, &vertOffset);
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
			sscanf(rest, "%255s %255s %255s", imagename, imagefile, effectname);
			printf("Image %s\n", imagefile);
			ImageDesc desc;
			desc.fileName = imagefile;
			desc.name = imagename;
			desc.result_index = 0;
			images.push_back(desc);
		} else {
			fprintf(stderr, "Warning: Failed to parse line starting with %s\n", line);
		}
	}
	fclose(script);

	Bucket bucket;

	// Script fully read, now read images and rasterize the fonts.
	for (auto &image : images) {
		image.result_index = (int)bucket.data.size();

		Image img;
		bool success = img.LoadPNG(image.fileName.c_str());
		if (!success) {
			fprintf(stderr, "Failed to load image %s\n", image.fileName.c_str());
			continue;
		}
		bucket.AddImage(std::move(img), global_id);
		global_id++;
	}

	for (auto it = fontRefs.begin(), end = fontRefs.end(); it != end; ++it) {
		FontDesc fnt;
		fnt.first_char_id = (int)bucket.data.size();

		vector<CharRange> finalRanges;
		float metrics_height;
		RasterizeFonts(it->second, finalRanges, &metrics_height, &bucket, global_id);
		printf("font rasterized.\n");

		fnt.ranges = finalRanges;
		fnt.name = it->first;
		fnt.metrics_height = metrics_height;

		fonts.push_back(fnt);
	}

	// Script read, all subimages have been generated.

	// Place the subimages onto the main texture. Also writes to png.
	Image dest;
	// Place things on the bitmap.
	printf("Resolving...\n");

	std::vector<Data> results = bucket.Resolve(image_width, &dest);
	if (highcolor) {
		printf("Writing .ZIM %ix%i RGBA8888...\n", dest.width(), dest.height());
		dest.SaveZIM(image_name.c_str(), ZIM_RGBA8888 | ZIM_ZSTD_COMPRESSED);
	} else {
		printf("Writing .ZIM %ix%i RGBA4444...\n", dest.width(), dest.height());
		dest.SaveZIM(image_name.c_str(), ZIM_RGBA4444 | ZIM_ZSTD_COMPRESSED);
	}

	// Also save PNG for debugging.
	printf("Writing .PNG %s\n", (image_name + ".png").c_str());
	dest.SavePNG((image_name + ".png").c_str());

	printf("Done. Outputting meta file %s_atlas.meta.\n", out_prefix.c_str());

	// Save all the metadata into a packed file.
	{
		FILE *meta = fopen(meta_name.c_str(), "wb");
		AtlasHeader header{};
		header.magic = ATLAS_MAGIC;
		header.version = 1;
		header.numFonts = (int)fonts.size();
		header.numImages = (int)images.size();
		fwrite(&header, 1, sizeof(header), meta);
		// For each image
		AtlasImage *atlas_images = new AtlasImage[images.size()];
		for (int i = 0; i < (int)images.size(); i++) {
			atlas_images[i] = ToAtlasImage(images[i].result_index, images[i].name, (float)dest.width(), (float)dest.height(), results);
		}
		WriteCompressed(atlas_images, sizeof(AtlasImage), images.size(), meta);
		// For each font
		for (int i = 0; i < (int)fonts.size(); i++) {
			auto &font = fonts[i];
			font.ComputeHeight(results, distmult);
			AtlasFontHeader font_header = font.GetHeader();
			fwrite(&font_header, 1, sizeof(font_header), meta);
			auto ranges = font.GetRanges();
			WriteCompressed(ranges.data(), sizeof(AtlasCharRange), ranges.size(), meta);
			auto chars = font.GetChars((float)dest.width(), (float)dest.height(), results);
			WriteCompressed(chars.data(), sizeof(AtlasChar), chars.size(), meta);
		}
		fclose(meta);
	}
	return 0;
}

int main(int argc, char **argv) {
	// initProgram(&argc, const_cast<const char ***>(&argv));
	// /usr/share/fonts/truetype/msttcorefonts/Arial_Black.ttf
	// /usr/share/fonts/truetype/ubuntu-font-family/Ubuntu-R.ttf
	if (argc < 3) {
		printf("Not enough arguments.\nSee buildatlas.sh for example.\n");
		return 1;
	}
	assert(argc >= 3);

	bool highcolor = false;

	if (argc > 3) {
		if (!strcmp(argv[3], "8888")) {
			highcolor = true;
			printf("RGBA8888 enabled!\n");
		}
	}
	printf("Reading script %s\n", argv[1]);

	return GenerateFromScript(argv[1], argv[2], highcolor);
}
