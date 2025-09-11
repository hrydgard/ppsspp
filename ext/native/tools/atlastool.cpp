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

#include "Common/Render/AtlasGen.h"
#include <zstd.h>

#include <cstdio>
#include <assert.h>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>

#include "Common/Data/Format/ZIMSave.h"


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

static const char * const effect_str[5] = {
	"copy", "r2a", "r2i", "pre", "p2a",
};

static Effect GetEffect(const char *text) {
	for (int i = 0; i < 5; i++) {
		if (!strcmp(text, effect_str[i])) {
			return (Effect)i;
		}
	}
	return Effect::FX_INVALID;
}

//argv[1], argv[2], argv[3]
int GenerateFromScript(const char *script_file, const char *atlas_name, bool highcolor) {
	std::vector<FontDesc> fonts;
	std::vector<ImageDesc> images;

	std::map<std::string, FontReferenceList> fontRefs;

	std::string image_name = std::string(atlas_name) + "_atlas.zim";
	std::string meta_name = std::string(atlas_name) + "_atlas.meta";

	const std::string out_prefix = atlas_name;

	char line[1024]{};
	FILE *script = fopen(script_file, "r");
	if (!fgets(line, 512, script)) {
		printf("Error fgets-ing\n");
		return -1;
	}
	int image_width = 0;
	if (1 != sscanf(line, "%i", &image_width)) {
		printf("missing image width (first line)");
		return -1;
	}
	printf("Texture width: %i\n", image_width);
	while (!feof(script)) {
		if (!fgets(line, sizeof(line), script)) break;
		if (!strlen(line)) break;
		if (line[0] == '#') continue;
		char *rest = strchr(line, ' ');
		if (rest) {
			*rest = 0;
			rest++;
		} else {
			printf("Bad line in file: '%s'", line);
			return -1;
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
			if (!GetLocales(locales, ranges)) {
				return -1;
			}
			printf("locales fetched.\n");

			FontReference fnt(fontname, fontfile, ranges, pixheight, vertOffset);
			fontRefs[fontname].push_back(fnt);
		} else if (!strcmp(word, "image")) {
			char imagename[256];
			char imagefile[256];
			char effectname[256];
			if (3 != sscanf(rest, "%255s %255s %255s", imagename, imagefile, effectname)) {
				printf("Bad line in file: '%s %s'", line, rest);
				return -1;
			}
			Effect effect = GetEffect(effectname);
			printf("Image %s with effect %s (%i)\n", imagefile, effectname, (int)effect);
			ImageDesc desc{};
			desc.filename = imagefile;
			desc.name = imagename;
			desc.effect = effect;
			desc.result_index = 0;
			images.push_back(desc);
		} else {
			fprintf(stderr, "Warning: Failed to parse line starting with %s\n", line);
		}
	}
	fclose(script);

	// Script fully read, now load images and rasterize the fonts.
	std::vector<Data> results;
	Image dest;
	LoadAndResolve(images, fontRefs, fonts, image_width, results, dest);

	std::vector<AtlasImage> atlas_images;
	atlas_images.reserve(images.size());

	for (int i = 0; i < images.size(); i++) {
		atlas_images[i] = images[i].ToAtlasImage((float)dest.width(), (float)dest.height(), results);
	}

	for (int i = 0; i < fonts.size(); i++) {
		fonts[i].ComputeHeight(results, AtlasDistMult);
	}

	// Here the data is ready.

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

	printf("Done. Outputting source and meta files %s_atlas.cpp/h/meta.\n", out_prefix.c_str());
	// Sort items by ID.
	std::sort(results.begin(), results.end());

	// Save all the metadata.
	{
		FILE *meta = fopen(meta_name.c_str(), "wb");
		AtlasHeader header{};
		header.magic = ATLAS_MAGIC;
		header.version = 1;
		header.numFonts = (int)fonts.size();
		header.numImages = (int)images.size();
		fwrite(&header, 1, sizeof(header), meta);
		WriteCompressed(atlas_images.data(), sizeof(AtlasImage), images.size(), meta);
		// For each font
		for (int i = 0; i < (int)fonts.size(); i++) {
			const auto &font = fonts[i];
			AtlasFontHeader font_header = font.GetHeader();
			fwrite(&font_header, 1, sizeof(font_header), meta);
			auto ranges = font.GetRanges();
			WriteCompressed(ranges.data(), sizeof(AtlasCharRange), ranges.size(), meta);
			auto chars = font.GetChars((float)dest.width(), (float)dest.height(), results);
			WriteCompressed(chars.data(), sizeof(AtlasChar), chars.size(), meta);
		}
		fclose(meta);
	}

	FILE *cpp_file = fopen((out_prefix + "_atlas.cpp").c_str(), "wb");
	fprintf(cpp_file, "// C++ generated by atlastool from %s (hrydgard@gmail.com)\n\n", script_file);
	fprintf(cpp_file, "#include \"%s\"\n\n", (out_prefix + "_atlas.h").c_str());
	for (int i = 0; i < (int)fonts.size(); i++) {
		FontDesc &xfont = fonts[i];
		xfont.OutputSelf(cpp_file, (float)dest.width(), (float)dest.height(), results);
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
			images[i].OutputSelf(cpp_file, (float)dest.width(), (float)dest.height(), results);
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
	fprintf(h_file, "// Header generated by atlastool from %s (hrydgard@gmail.com)\n\n", script_file);
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
	return 0;
}

int main(int argc, char **argv) {
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
		} else {
			printf("Bad third argument\n");
		}
	}
	printf("Reading script %s\n", argv[1]);

	return GenerateFromScript(argv[1], argv[2], highcolor);
}
