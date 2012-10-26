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
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>

#include "image/png_load.h"
#include "image/zim_save.h"

#define CHECK(x) if (!(x)) { printf("%i: CHECK failed on this line\n", __LINE__); exit(1); }

using namespace std;
static int global_id;
static bool highcolor = false;

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
  // distance to move the origin forward
  float wx;

  int effect;
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
    for(int i = 0; i < (int)items.size(); i++) {
      int idx = items[i].first.dat[0].size();
      int idy = items[i].first.dat.size();
      CHECK(idx <= image_width);
      bool found = false;
      for(int ty = 0; ty < 2047 && !found; ty++) {
        if(ty + idy + 1 > (int)dest.dat.size()) {
          masq.resize(image_width, ty + idy + 1);
          dest.resize(image_width, ty + idy + 1);
        }
        // Brute force packing.
        for(int tx = 0; tx < image_width - (int)items[i].first.dat[0].size() && !found; tx++) {
          bool valid = !(masq.dat[ty][tx] || masq.dat[ty + idy - 1][tx] || masq.dat[ty][tx + idx - 1] || masq.dat[ty + idy - 1][tx + idx - 1]);
          if (valid) {
            for(int ity = 0; ity < idy && valid; ity++)
              for(int itx = 0; itx < idx && valid; itx++)
                if(masq.dat[ty + ity][tx + itx]) {
                  valid = false;
                }
          }
          if (valid) {
            dest.copyfrom(items[i].first, tx, ty, items[i].second.effect);
            masq.set(tx, ty, tx + idx + 1, ty + idy + 1, 255);

            items[i].second.sx = tx;
            items[i].second.sy = ty;

            items[i].second.ex = tx + idx;
            items[i].second.ey = ty + idy;

            found = true;

            // printf("Placed %d at %dx%d-%dx%d\n", items[i].second.id, tx, ty, tx + idx, ty + idy);
          }
        }
      }
    }

    if ((int)dest.dat.size() > image_width) {
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

void RasterizeFont(const char *fontfile, int fontsize, float *metrics_height, Bucket *bucket) {
  FT_Library freetype;
  CHECK(FT_Init_FreeType(&freetype) == 0);

  FT_Face font;
  CHECK(FT_New_Face(freetype, fontfile, 0, &font) == 0);

  printf("%d glyphs, %08x flags, %d units, %d strikes\n", (int)font->num_glyphs, (int)font->face_flags, (int)font->units_per_EM, (int)font->num_fixed_sizes);

  CHECK(FT_Set_Pixel_Sizes(font, 0, fontsize * supersample) == 0);

  // Character range. TODO: Make definable. We might want unicode
  // Convert all characters to bitmaps.
  for(int kar = 32; kar < 128; kar++) {
    Image<unsigned int> img;
    if (0 != FT_Load_Char(font, kar, FT_LOAD_RENDER|FT_LOAD_MONOCHROME)) {
			img.resize(1,1);
			Data dat;

			dat.id = global_id++;

			dat.sx = 0;
			dat.sy = 0;
			dat.ex = 0;
			dat.ey = 0;
			dat.ox = 0;
			dat.oy = 0;
			dat.wx = 0;
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
    dat.wx = (float)font->glyph->metrics.horiAdvance / 64 / supersample;

    dat.effect = FX_RED_TO_ALPHA_SOLID_WHITE;
    bucket->AddItem(img, dat);
  }

  *metrics_height = font->size->metrics.height;
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
    printf("loaded image: %ix%i\n", (int)img.dat[0].size(), (int)img.dat.size());
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

  void ComputeHeight(const vector<Data> &results, float distmult) {
    ascend = 0;
    descend = 0;
    for(int i = first_char_id; i < last_char_id; i++) {
      ascend = max(ascend, -results[i].oy);
      descend = max(descend, results[i].ey - results[i].sy + results[i].oy);
    }

    height = metrics_height / 64.0 / supersample;
  }

  void OutputSelf(FILE *fil, float tw, float th, const vector<Data> &results) {
    fprintf(fil, "const AtlasFont font_%s = {\n", name.c_str());
    fprintf(fil, "  %ff, // padding\n", height - ascend - descend);
    fprintf(fil, "  %ff, // height\n", ascend + descend);
    fprintf(fil, "  %ff, // ascend\n", ascend);
    fprintf(fil, "  %ff, // distslope\n", distmult / 256.0);
    fprintf(fil, "  {\n");
    CHECK(last_char_id - first_char_id == 96);
    for(int i = first_char_id; i < last_char_id; i++) {
      fprintf(fil, "    {%ff, %ff, %ff, %ff, %1.4ff, %1.4ff, %1.4ff, %i, %i},  // %i\n",
              /*results[i].id, */
							results[i].sx / tw,
							results[i].sy / th,
							results[i].ex / tw,
							results[i].ey / th,
							results[i].ox,
							results[i].oy,
							results[i].wx,
              results[i].ex - results[i].sx, results[i].ey - results[i].sy, (i - first_char_id + 32));
    }
    fprintf(fil, "  },\n");
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
    fprintf(fil, "  {%ff, %ff, %ff, %ff, %d, %d},\n",
        results[i].sx / tw + toffx, 
				results[i].sy / th + toffy,
				results[i].ex / tw - toffx,
				results[i].ey / th - toffy,
        results[i].ex - results[i].sx,
				results[i].ey - results[i].sy);
  }

  void OutputHeader(FILE *fil, int index) {
		fprintf(fil, "#define %s %i\n", name.c_str(), index);
  }
};

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
    if (rest)
		{
			*rest = 0;
			rest++;
		}
    char *word = line;
    if (!strcmp(word, "font")) {
      // Font!
      char fontname[256];
      char fontfile[256];
      int pixheight;
      sscanf(rest, "%s %s %i", fontname, fontfile, &pixheight);
      printf("Font: %s (%s) in size %i\n", fontname, fontfile, pixheight);

      FontDesc fnt;
      fnt.first_char_id = (int)bucket.items.size();
      float metrics_height;
      RasterizeFont(fontfile, pixheight, &metrics_height, &bucket);
      fnt.name = fontname;
      fnt.last_char_id = (int)bucket.items.size();
      CHECK(fnt.last_char_id - fnt.first_char_id == 96);

      fnt.metrics_height = metrics_height;
      fonts.push_back(fnt);
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
  // Script read, all subimages have been generated.

  // Place the subimages onto the main texture. Also writes to png.
  Image<unsigned int> dest;
  // Place things on the bitmap.
  printf("Resolving...\n");

  vector<Data> results = bucket.Resolve(image_width, dest);
	if (highcolor) {
		printf("Writing .ZIM %ix%i RGBA8888...\n", dest.width(), dest.height());
		dest.SaveZIM(image_name.c_str(), ZIM_RGBA8888);
	} else {
		printf("Writing .ZIM %ix%i RGBA4444...\n", dest.width(), dest.height());
		dest.SaveZIM(image_name.c_str(), ZIM_RGBA4444);
	}
  // Also save PNG for debugging.
  printf("Writing .PNG %s\n", (image_name + ".png").c_str());
  dest.SavePNG((image_name + ".png").c_str());

  printf("Done. Outputting source files %s_atlas.cpp/h.\n", out_prefix.c_str());
  // Sort items by ID.
  sort(results.begin(), results.end());

  FILE *cpp_fil = fopen((out_prefix + "_atlas.cpp").c_str(), "wb");
  fprintf(cpp_fil, "// C++ generated by atlastool from %s (hrydgard@gmail.com)\n\n", argv[1]);
  fprintf(cpp_fil, "#include \"%s\"\n\n", (out_prefix + "_atlas.h").c_str());
  for (int i = 0; i < (int)fonts.size(); i++) {
    FontDesc &xfont = fonts[i];
    xfont.ComputeHeight(results, distmult);
    xfont.OutputSelf(cpp_fil, dest.width(), dest.height(), results);
  }

  if (fonts.size()) {
    fprintf(cpp_fil, "const AtlasFont *%s_fonts[%i] = {\n", atlas_name, (int)fonts.size());
    for (int i = 0; i < (int)fonts.size(); i++) {
      fonts[i].OutputIndex(cpp_fil);
    }
    fprintf(cpp_fil, "};\n");
  }

  if (images.size()) {
    fprintf(cpp_fil, "const AtlasImage %s_images[%i] = {\n", atlas_name, (int)images.size());
    for (int i = 0; i < (int)images.size(); i++) {
      images[i].OutputSelf(cpp_fil, dest.width(), dest.height(), results);
    }
    fprintf(cpp_fil, "};\n");
  }

  fprintf(cpp_fil, "const Atlas %s_atlas = {\n", atlas_name);
  fprintf(cpp_fil, "  \"%s\",\n", image_name.c_str());
  if (fonts.size()) {
    fprintf(cpp_fil, "  %s_fonts, %i,\n", atlas_name, (int)fonts.size());
  } else {
    fprintf(cpp_fil, "  0, 0,\n");
  }
  if (images.size()) {
    fprintf(cpp_fil, "  %s_images, %i,\n", atlas_name, (int)images.size());
  } else {
    fprintf(cpp_fil, "  0, 0,\n");
  }
  fprintf(cpp_fil, "};\n");
  // Should output a list pointing to all the fonts as well.
  fclose(cpp_fil);
  FILE *h_fil = fopen((out_prefix + "_atlas.h").c_str(), "wb");
  fprintf(h_fil, "// Header generated by atlastool from %s (hrydgard@gmail.com)\n\n", argv[1]);
  fprintf(h_fil, "#pragma once\n");
  fprintf(h_fil, "#include \"gfx/texture_atlas.h\"\n\n");
  if (fonts.size()) {
    fprintf(h_fil, "// FONTS_%s\n", atlas_name);
    for (int i = 0; i < (int)fonts.size(); i++) {
      fonts[i].OutputHeader(h_fil, i);
    }
    fprintf(h_fil, "\n\n");
  }
  if (images.size()) {
    fprintf(h_fil, "// IMAGES_%s\n", atlas_name);
    for (int i = 0; i < (int)images.size(); i++) {
      images[i].OutputHeader(h_fil, i);
    }
    fprintf(h_fil, "\n\n");
  }
  fprintf(h_fil, "extern const Atlas %s_atlas;\n", atlas_name);
  fprintf(h_fil, "extern const AtlasImage %s_images[%i];\n", atlas_name, (int)images.size());
  fclose(h_fil);
  // TODO: Turn into C++ arrays.
}
