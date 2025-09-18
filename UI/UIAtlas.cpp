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

static const ImageDesc imageDescs[] = {
	{"I_SOLIDWHITE", "solidwhite.png"},
	{"I_CROSS", "cross.png"},
	{"I_CIRCLE", "circle.png"},
	{"I_SQUARE", "square.png"},
	{"I_TRIANGLE", "triangle.png"},
	{"I_SELECT", "select.png"},
	{"I_START", "start.png"},
	{"I_ARROW", "arrow.png"},
	{"I_DIR", "dir.png"},
	{"I_ROUND", "round.png"},
	{"I_RECT", "rect.png"},
	{"I_STICK", "stick.png"},
	{"I_STICK_BG", "stick_bg.png"},
	{"I_SHOULDER", "shoulder.png"},
	{"I_DIR_LINE", "dir_line.png"},
	{"I_ROUND_LINE", "round_line.png"},
	{"I_RECT_LINE", "rect_line.png"},
	{"I_SHOULDER_LINE", "shoulder_line.png"},
	{"I_STICK_LINE", "stick_line.png"},
	{"I_STICK_BG_LINE", "stick_bg_line.png"},
	{"I_CHECKEDBOX", "checkedbox.png"},
	{"I_BG", "bg.png"},
	{"I_L", "l.png"},
	{"I_R", "r.png"},
	{"I_DROP_SHADOW", "drop_shadow.png"},
	{"I_LINES", "lines.png"},
	{"I_GRID", "grid.png"},
	{"I_LOGO", "logo.png"},
	{"I_ICON", "icon.png"},
	{"I_ICON_GOLD", "icon_gold.png"},
	{"I_FOLDER", "folder.png"},
	{"I_UP_DIRECTORY", "up_directory.png"},
	{"I_GEAR", "gear.png"},
	{"I_1", "1.png"},
	{"I_2", "2.png"},
	{"I_3", "3.png"},
	{"I_4", "4.png"},
	{"I_5", "5.png"},
	{"I_6", "6.png"},
	{"I_PSP_DISPLAY", "psp_display.png"},
	{"I_FLAG_JP", "flag_jp.png"},
	{"I_FLAG_US", "flag_us.png"},
	{"I_FLAG_EU", "flag_eu.png"},
	{"I_FLAG_HK", "flag_hk.png"},
	{"I_FLAG_AS", "flag_as.png"},
	{"I_FLAG_KO", "flag_ko.png"},
	{"I_FULLSCREEN", "fullscreen.png"},
	{"I_RESTORE", "restore.png"},
	{"I_SDCARD", "sdcard.png"},
	{"I_HOME", "home.png"},
	{"I_A", "a.png"},
	{"I_B", "b.png"},
	{"I_C", "c.png"},
	{"I_D", "d.png"},
	{"I_E", "e.png"},
	{"I_F", "f.png"},
	{"I_SQUARE_SHAPE", "square_shape.png"},
	{"I_SQUARE_SHAPE_LINE", "square_shape_line.png"},
	{"I_FOLDER_OPEN", "folder_open.png"},
	{"I_WARNING", "warning.png"},
	{"I_TRASHCAN", "trashcan.png"},
	{"I_PLUS", "plus.png"},
	{"I_ROTATE_LEFT", "rotate_left.png"},
	{"I_ROTATE_RIGHT", "rotate_right.png"},
	{"I_ARROW_LEFT", "arrow_left.png"},
	{"I_ARROW_RIGHT", "arrow_right.png"},
	{"I_ARROW_UP", "arrow_up.png"},
	{"I_ARROW_DOWN", "arrow_down.png"},
	{"I_SLIDERS", "sliders.png"},
	{"I_THREE_DOTS", "three_dots.png"},
	{"I_INFO", "info.png"},
	{"I_RETROACHIEVEMENTS_LOGO", "retroachievements_logo.png"},
	{"I_CHECKMARK", "checkmark.png"},
	{"I_PLAY", "play.png"},
	{"I_STOP", "stop.png"},
	{"I_PAUSE", "pause.png"},
	{"I_FAST_FORWARD", "fast_forward.png"},
	{"I_RECORD", "record.png"},
	{"I_SPEAKER", "speaker.png"},
	{"I_SPEAKER_MAX", "speaker_max.png"},
	{"I_SPEAKER_OFF", "speaker_off.png"},
	{"I_WINNER_CUP", "winner_cup.png"},
	{"I_EMPTY", "empty.png"},
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
	Image images[ARRAY_SIZE(imageDescs)];
	int resultIds[ARRAY_SIZE(imageDescs)];

	Instant start = Instant::Now();

	// TODO: This can be parallelized if needed.
	for (int i = 0; i < ARRAY_SIZE(imageDescs); i++) {
		resultIds[i] = i;

		Image &img = images[i];

		bool success = true;
		if (equals(imageDescs[i].fileName, "solidwhite.png")) {
			img.resize(16, 16);
			img.fill(0xFFFFFFFF);
		} else {
			std::string name = "ui_images/";
			name.append(imageDescs[i].fileName);
			std::string pngName = PNGNameFromID(imageDescs[i].name);
			_dbg_assert_(equals(imageDescs[i].fileName, pngName));
			bool success = img.LoadPNG(name.c_str());
			if (!success) {
				ERROR_LOG(Log::G3D, "Failed to load %s\n", name.c_str());
			}
		}
	}
	INFO_LOG(Log::G3D, " - Loaded %zu images in %.2f ms\n", ARRAY_SIZE(images), start.ElapsedMs());

	Instant addStart = Instant::Now();
	for (int i = 0; i < ARRAY_SIZE(imageDescs); i++) {
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
	genAtlasImages.reserve(ARRAY_SIZE(imageDescs));
	for (int i = 0; i < ARRAY_SIZE(imageDescs); i++) {
		genAtlasImages.push_back(ToAtlasImage(resultIds[i], imageDescs[i].name, (float)dest.width(), (float)dest.height(), results));
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
