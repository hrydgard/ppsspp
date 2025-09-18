#include <string>
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/TimeUtil.h"
#include "Common/GPU/thin3d.h"
#include "Common/StringUtils.h"
#include "Common/UI/Context.h"
#include "Common/Render/AtlasGen.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/Common.h"
#include "Common/Log.h"
#include "UI/UIAtlas.h"

static Atlas ui_atlas;
static Atlas font_atlas;

const Atlas *GetFontAtlas() {
	return &font_atlas;
}

Atlas *GetUIAtlas() {
	return &ui_atlas;
}

static const std::string imageIDs[] = {
	"I_SOLIDWHITE",
	"I_CROSS",
	"I_CIRCLE",
	"I_SQUARE",
	"I_TRIANGLE",
	"I_SELECT",
	"I_START",
	"I_ARROW",
	"I_DIR",
	"I_ROUND",
	"I_RECT",
	"I_STICK",
	"I_STICK_BG",
	"I_SHOULDER",
	"I_DIR_LINE",
	"I_ROUND_LINE",
	"I_RECT_LINE",
	"I_SHOULDER_LINE",
	"I_STICK_LINE",
	"I_STICK_BG_LINE",
	"I_CHECKEDBOX",
	"I_BG",
	"I_L",
	"I_R",
	"I_DROP_SHADOW",
	"I_LINES",
	"I_GRID",
	"I_LOGO",
	"I_ICON",
	"I_ICON_GOLD",
	"I_FOLDER",
	"I_UP_DIRECTORY",
	"I_GEAR",
	"I_1",
	"I_2",
	"I_3",
	"I_4",
	"I_5",
	"I_6",
	"I_PSP_DISPLAY",
	"I_FLAG_JP",
	"I_FLAG_US",
	"I_FLAG_EU",
	"I_FLAG_HK",
	"I_FLAG_AS",
	"I_FLAG_KO",
	"I_FULLSCREEN",
	"I_RESTORE",
	"I_SDCARD",
	"I_HOME",
	"I_A",
	"I_B",
	"I_C",
	"I_D",
	"I_E",
	"I_F",
	"I_SQUARE_SHAPE",
	"I_SQUARE_SHAPE_LINE",
	"I_FOLDER_OPEN",
	"I_WARNING",
	"I_TRASHCAN",
	"I_PLUS",
	"I_ROTATE_LEFT",
	"I_ROTATE_RIGHT",
	"I_ARROW_LEFT",
	"I_ARROW_RIGHT",
	"I_ARROW_UP",
	"I_ARROW_DOWN",
	"I_SLIDERS",
	"I_THREE_DOTS",
	"I_INFO",
	"I_RETROACHIEVEMENTS_LOGO",
	"I_CHECKMARK",
	"I_PLAY",
	"I_STOP",
	"I_PAUSE",
	"I_FAST_FORWARD",
	"I_RECORD",
	"I_SPEAKER",
	"I_SPEAKER_MAX",
	"I_SPEAKER_OFF",
	"I_WINNER_CUP",
	"I_EMPTY",
};

static std::string PNGNameFromID(std::string_view id) {
	std::string output;
	output.reserve(id.size() + 3);
	for (int i = 2; i < id.size(); i++) {
		output.push_back((char)tolower(id[i]));
	}
	output.append(".png");
	return output;
}

Draw::Texture *GenerateUIAtlas(Draw::DrawContext *draw, Atlas *atlas) {
	Bucket bucket;

	// Script fully read, now read images and rasterize the fonts.
	Image images[ARRAY_SIZE(imageIDs)];
	int resultIds[ARRAY_SIZE(images)];

	Instant start = Instant::Now();

	// TODO: Load SVGs here, trying to fill in the images. The remaining images we fill from PNGs.

	// TODO: This can be parallelized if needed.
	for (int i = 0; i < ARRAY_SIZE(imageIDs); i++) {
		resultIds[i] = i;

		Image &img = images[i];

		if (!img.IsEmpty()) {
			// Was already loaded from SVG.
			continue;
		}

		bool success = true;
		if (equals(imageIDs[i], "I_SOLIDWHITE")) {
			img.resize(16, 16);
			img.fill(0xFFFFFFFF);
		} else {
			std::string name = "ui_images/";
			std::string pngName = PNGNameFromID(imageIDs[i]);
			name.append(pngName);
			bool success = img.LoadPNG(name.c_str());
			if (!success) {
				ERROR_LOG(Log::G3D, "Failed to load %s\n", name.c_str());
			}
		}
	}
	INFO_LOG(Log::G3D, " - Loaded %zu images in %.2f ms\n", ARRAY_SIZE(images), start.ElapsedMs());

	Instant addStart = Instant::Now();
	for (int i = 0; i < ARRAY_SIZE(images); i++) {
		bucket.AddImage(std::move(images[i]), i);
	}

	INFO_LOG(Log::G3D, " - Added %zu images in %.2f ms\n", bucket.data.size(), addStart.ElapsedMs());

	int image_width = 512;
	Image dest;

	Instant bucketStart = Instant::Now();
	std::vector<Data> results = bucket.Resolve(image_width, dest);
	INFO_LOG(Log::G3D, " - Bucketed %zu images in %.2f ms\n", results.size(), bucketStart.ElapsedMs());

	// Fill out the atlas structure.
	std::vector<AtlasImage> genAtlasImages;
	genAtlasImages.reserve(ARRAY_SIZE(imageIDs));
	for (int i = 0; i < ARRAY_SIZE(imageIDs); i++) {
		genAtlasImages.push_back(ToAtlasImage(resultIds[i], imageIDs[i], (float)dest.width(), (float)dest.height(), results));
	}

	atlas->Clear();
	atlas->images = new AtlasImage[genAtlasImages.size()];
	std::copy(genAtlasImages.begin(), genAtlasImages.end(), atlas->images);
	atlas->num_images = (int)genAtlasImages.size();

	// For debug, write out the atlas.
	// dest.SavePNG("../gen.png");

	// Then, create the texture too.
	Draw::TextureDesc desc{};
	desc.width = image_width;
	desc.height = dest.height();
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	desc.type = Draw::TextureType::LINEAR2D;
	desc.initData.push_back((const u8 *)dest.data());
	desc.tag = "UIAtlas";

	INFO_LOG(Log::G3D, "UI atlas generated in %.2f ms, size %dx%d with %zu images\n", start.ElapsedMs(), desc.width, desc.height, genAtlasImages.size());
	return draw->CreateTexture(desc);
}

static void LoadAtlasMetadata(Atlas &metadata, const char *filename) {
	size_t atlas_data_size = 0;
	const uint8_t *atlas_data = g_VFS.ReadFile(filename, &atlas_data_size);
	bool load_success = atlas_data != nullptr && metadata.LoadMeta(atlas_data, atlas_data_size);
	if (!load_success) {
		ERROR_LOG(Log::G3D, "Failed to load %s - graphics may be broken", filename);
		// Stumble along with broken visuals instead of dying...
	}
	delete[] atlas_data;
}

AtlasData AtlasProvider(Draw::DrawContext *draw, AtlasChoice atlas) {
	switch (atlas) {
	case AtlasChoice::General:
	{
		// Generate the atlas from scratch.
		Draw::Texture *tex = GenerateUIAtlas(draw, &ui_atlas);
		return {&ui_atlas, tex};
	}
	case AtlasChoice::Font:
	{
		Draw::Texture *fontTexture = nullptr;
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
		// Load the smaller ascii font only, like on Android. For debug ui etc.
		// NOTE: We better be sure here that the correct metadata is loaded..
		LoadAtlasMetadata(font_atlas, "asciifont_atlas.meta");
		fontTexture = CreateTextureFromFile(draw, "asciifont_atlas.zim", ImageFileType::ZIM, false);
		if (!fontTexture) {
			WARN_LOG(Log::System, "Failed to load font_atlas.zim or asciifont_atlas.zim");
		}
#else
		// Load the full font texture.
		LoadAtlasMetadata(font_atlas, "font_atlas.meta");
		fontTexture = CreateTextureFromFile(draw, "font_atlas.zim", ImageFileType::ZIM, false);
#endif
		return {
			&font_atlas,
			fontTexture,
		};
	}
	default:
		return {};
	};
}
