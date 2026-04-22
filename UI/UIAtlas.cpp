#include <cmath>
#include <string>
#include <unordered_map>

#include "Common/Data/Format/IniFile.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/TimeUtil.h"
#include "Common/GPU/thin3d.h"
#include "Common/StringUtils.h"
#include "Common/UI/Context.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Render/AtlasGen.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/Common.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Log.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Core/Config.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Util/PathUtil.h"
#include "Core/System.h"

#include "UI/UIAtlas.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "ext/nanosvg/src/nanosvg.h"
#include "ext/nanosvg/src/nanosvgrast.h"

constexpr bool SAVE_DEBUG_IMAGES = false;
constexpr bool SAVE_DEBUG_ATLAS = false;

static Atlas ui_atlas;
static Atlas font_atlas;

const Atlas *GetFontAtlas() {
	return &font_atlas;
}

Atlas *GetUIAtlas() {
	return &ui_atlas;
}

struct ImageMeta {
	std::string_view id;
	bool addShadow = false;
};

// We add shadows to all line-art images that are used for buttons, to improve visibility.
// However, some images are dual-used as general UI elemnts and also as custom button images. This is a problem. (I_ROTATE_LEFT, I_ROTATE_RIGHT, I_THREE_DOTS).
// I've added shadows to most of those for now. See customKeyImages in GamepadEmu.h.
static const ImageMeta g_uiImageIDs[] = {
	{"I_SOLIDWHITE", false},
	{"I_CROSS", true},
	{"I_CIRCLE", true},
	{"I_SQUARE", true},
	{"I_TRIANGLE", true},
	{"I_SELECT", true},
	{"I_START", true},
	{"I_ARROW", false},
	{"I_ROUND", false},
	{"I_ROUND_LINE", true},
	{"I_RECT", false},
	{"I_RECT_LINE", true},
	{"I_STICK", false},
	{"I_STICK_BG", false},
	{"I_STICK_LINE", true},
	{"I_STICK_BG_LINE", true},
	{"I_SHOULDER", false},
	{"I_SHOULDER_LINE", true},
	{"I_DIR", false},
	{"I_DIR_LINE", true},
	{"I_SQUARE_SHAPE", false},
	{"I_SQUARE_SHAPE_LINE", true},
	{"I_CHECKEDBOX", false},
	{"I_UNCHECKEDBOX", false},
	{"I_BG", false},
	{"I_L", true},
	{"I_R", true},
	{"I_DROP_SHADOW", false},
	{"I_LINES", false},
	{"I_GRID", false},
	{"I_LOGO", false},
	{"I_ICON", false},
	{"I_ICON_GOLD", false},
	{"I_FOLDER", false},
	{"I_UP_DIRECTORY", false},
	{"I_GEAR", false},
	{"I_GEAR_SMALL", true},
	{"I_GEAR_STAR", false},
	{"I_1", true},
	{"I_2", true},
	{"I_3", true},
	{"I_4", true},
	{"I_5", true},
	{"I_6", true},
	{"I_PSP_DISPLAY", false},
	{"I_FLAG_JP", false},
	{"I_FLAG_US", false},
	{"I_FLAG_EU", false},
	{"I_FLAG_HK", false},
	{"I_FLAG_AS", false},
	{"I_FLAG_KO", false},
	{"I_FULLSCREEN", false},
	{"I_RESTORE", false},
	{"I_SDCARD", false},
	{"I_HOME", false},
	{"I_A", true},
	{"I_B", true},
	{"I_C", true},
	{"I_D", true},
	{"I_E", true},
	{"I_F", true},
	{"I_FOLDER_OPEN", false},
	{"I_WARNING", false},
	{"I_TRASHCAN", false},
	{"I_PLUS", false},
	{"I_MINUS", false},
	{"I_ROTATE_LEFT", true},
	{"I_ROTATE_RIGHT", true},
	{"I_ARROW_LEFT", true},
	{"I_ARROW_RIGHT", true},
	{"I_ARROW_UP", true},
	{"I_ARROW_DOWN", true},
	{"I_SLIDERS", false},
	{"I_THREE_DOTS", true},
	{"I_INFO", false},
	{"I_RETROACHIEVEMENTS_LOGO", false},
	{"I_ACHIEVEMENT", false},
	{"I_CHECKMARK", false},
	{"I_PLAY", false},
	{"I_PAUSE", false},
	{"I_STOP", false},
	{"I_PLAY_LINE", false},
	{"I_PAUSE_LINE", false},
	{"I_FAST_FORWARD", false},
	{"I_FAST_FORWARD_LINE", true},
	{"I_RECORD", false},
	{"I_SPEAKER", false},
	{"I_SPEAKER_MAX", false},
	{"I_SPEAKER_OFF", false},
	{"I_WINNER_CUP", false},
	{"I_EMPTY", false},
	{"I_PIN", false},
	{"I_UNPIN", false},
	{"I_FOLDER_PINNED", false},
	{"I_FILLED_CIRCLE_1", false},
	{"I_FILLED_CIRCLE_2", false},
	{"I_FILLED_CIRCLE_3", false},
	{"I_FILLED_CIRCLE_4", false},
	{"I_FILLED_CIRCLE_5", false},
	{"I_DISPLAY", false},
	{"I_NAVIGATE_BACK", false},
	{"I_NAVIGATE_FORWARD", false},
	{"I_FOLDER_UPLOAD", false},
	{"I_FILE", false},
	{"I_FILE_COPY", false},
	{"I_WEB_BROWSER", false},
	{"I_WIFI", false},
	{"I_LOGO_X", false},
	{"I_LOGO_DISCORD", false},
	{"I_LINK_OUT", false},
	{"I_SHARE", false},
	{"I_LOGO_PLAY_STORE", false},
	{"I_LOGO_APP_STORE", false},
	{"I_SEARCH", false},
	{"I_DEVMENU", false},
	{"I_CONTROLLER", false},
	{"I_DEBUGGER", false},
	{"I_TOOLS", false},
	{"I_PSP", false},
	{"I_HOMEBREW_STORE", false},
	{"I_CHAT", false},
	{"I_UMD", false},
	{"I_EXIT", false},
	{"I_CHEAT", false},
	{"I_HAMBURGER", true},
	{"I_DEVICE_ROTATION_LANDSCAPE_REV", false},
	{"I_DEVICE_ROTATION_AUTO", false},
	{"I_DEVICE_ROTATION_LANDSCAPE", false},
	{"I_DEVICE_ROTATION_PORTRAIT", false},
	{"I_DEVICE_ROTATION_LANDSCAPE_AUT", false},
	{"I_MOVE", false},
	{"I_RESIZE", false},
	{"I_LINK_OUT_QUESTION", false},
	{"I_PSX_ISO", false},
	{"I_PS2_ISO", false},
	{"I_PS3_ISO", false},
	{"I_UNKNOWN_ISO", false},
	{"I_UMD_VIDEO_ISO", false},
	{"I_APP", false},
	{"I_SHORTCUT", false},
	{"I_KEYBOARD", false},
	{"I_MOUSE", false},
	{"I_FILE_SAVE", false},
	{"I_RADIO_EMPTY", false},
	{"I_RADIO_SELECTED", false},
	{"I_EDIT_TEXT", false},
	{"I_SEND", false},
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

static int GetImageIndex(const ImageMeta *imageIDs, size_t imageCount, std::string_view id) {
	for (int i = 0; i < imageCount; i++) {
		if (equals(id, imageIDs[i].id)) {
			return i;
		}
	}
	return -1;
}

static bool IsImageID(const ImageMeta *imageIDs, size_t imageCount, std::string_view id) {
	return GetImageIndex(imageIDs, imageCount, id) != -1;
}

static bool RasterizeSVG(std::string_view filename, float dpiScale, int maxTextureSize, const ImageMeta *imageIDs, size_t imageCount, std::vector<Image> *images, bool premultiplyAlpha = true) {
	Instant svgStart = Instant::Now();

	// Load SVGs here, trying to fill in the images. The remaining images we fill from PNGs.
	// For now we only load one hardcoded SVG.
	int shapeCount = 0;
	{
		size_t sz;
		const uint8_t *file_data = g_VFS.ReadFile(filename, &sz);  // ReadFile null-terminates
		if (!file_data) {
			return false;
		}
		NSVGimage *image = nsvgParse((char *)file_data, "px", 96.0f);
		if (!image) {
			return false;
		}
		delete[] file_data;

		// There's a couple of approaches here, either we can pick apart the SVG and render each piece separately,
		// or we just rasterize the whole thing in one go and use the bounding boxes to pick out the sub-images.
		// We'll start with the latter, although the momentary memory requirements are higher.
		struct UsedShape {
			float minX = 1000000.0f;
			float maxX = -1000000.0f;
			float minY = 1000000.0f;
			float maxY = -1000000.0f;

			void Merge(NSVGshape *shape) {
				if (shape->bounds[0] < minX) minX = shape->bounds[0];
				if (shape->bounds[1] < minY) minY = shape->bounds[1];
				if (shape->bounds[2] > maxX) maxX = shape->bounds[2];
				if (shape->bounds[3] > maxY) maxY = shape->bounds[3];
			}
		};

		std::map<std::string, UsedShape> usedShapes;
		// Loop through the shapes to list them, and to hide them if irrelevant.
		NSVGshape *shape = image->shapes;
		while (shape) {
			if (!IsImageID(imageIDs, imageCount, shape->id)) {
				// Not an image we care about, hide it.
				DEBUG_LOG(Log::G3D, "Ignoring shape %s", shape->id);
				shape->flags &= ~NSVG_FLAGS_VISIBLE;
			} else {
				if (usedShapes.find(shape->id) != usedShapes.end()) {
					DEBUG_LOG(Log::G3D, "Duplicate shape ID in SVG, merging bboxes: %s", shape->id);
				} else {
					DEBUG_LOG(Log::G3D, "Found shape: %s (%0.2f %0.2f %0.2f %0.2f)", shape->id, shape->bounds[0], shape->bounds[1], shape->bounds[2], shape->bounds[3]);
				}
				usedShapes[shape->id].Merge(shape);
			}
			shape = shape->next;
		}

		NSVGrasterizer *rast = NULL;
		// Rasterize here, and add into image list.
		rast = nsvgCreateRasterizer();

		// If we can tell that the scale won't fit in a supported texture size, reduce it.
		// This is a conservative check because the SVG has some empty space around the sub-images.
		// TODO: Also, if we use multiple SVGs, this check isn't really right.
		float scale = dpiScale;
		int maxSide = (int)(std::max(image->width, image->height) * scale);
		if (maxTextureSize > 0 && maxSide > maxTextureSize) {
			float newScale = (float)maxTextureSize / (float)maxSide;
			INFO_LOG(Log::G3D, "Reducing SVG scale from %0.2f to %0.2f to fit in max texture size", scale, newScale);
			scale = newScale;
		}

		int svgWidth = image->width * scale;
		int svgHeight = image->height * scale;

		INFO_LOG(Log::G3D, "Rasterizing SVG: %d x %d at scale %0.2f", svgWidth, svgHeight, scale);

		char *svgImg = new char[svgWidth * svgHeight * 4];
		memset(svgImg, 0, svgWidth * svgHeight * 4);
		nsvgRasterize(rast, image, 0, 0, scale, (unsigned char *)svgImg, svgWidth, svgHeight, svgWidth * 4);

		// Now, loop through the shapes again and copy out the ones we care about.
		for (const auto &[shapeId, bounds] : usedShapes) {
			int index = GetImageIndex(imageIDs, imageCount, shapeId);
			_dbg_assert_(index != -1);
			if (index == -1) {
				continue;
			}

			Image &img = (*images)[index];
			if (!img.IsEmpty()) {
				WARN_LOG(Log::G3D, "%.*s: Skipping image '%.*s' (%d), already loaded from SVG", STR_VIEW(filename), STR_VIEW(imageIDs[index].id), index);
				continue;
			}

			int minX = std::max(0, (int)floorf(bounds.minX * scale));
			int minY = std::max(0, (int)floorf(bounds.minY * scale));
			int maxX = std::min(svgWidth, (int)ceilf(bounds.maxX * scale));
			int maxY = std::min(svgHeight, (int)ceilf(bounds.maxY * scale));
			int w = maxX - minX;
			int h = maxY - minY;
			if (w <= 0 || h <= 0) {
				ERROR_LOG(Log::G3D, "Invalid size for %s: %dx%d", shapeId.c_str(), w, h);
				continue;
			}
			img.resize(w, h);

			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					int sx = minX + x;
					int sy = minY + y;
					const u32 *src = (u32 *)svgImg + (sy * svgWidth + sx);
					u32 col = *src;
					img.set1(x, y, col);
				}
			}

			img.scale = scale;

			if (SAVE_DEBUG_IMAGES) {
				std::string name = std::string("../buttons_") + PNGNameFromID(shapeId);
				WARN_LOG(Log::G3D, "Writing debug image %s", name.c_str());
				pngSave(Path(name), img.data(), img.width(), img.height(), 4);
			}

			if (premultiplyAlpha) {
				img.ConvertToPremultipliedAlpha();
			}
		}

		shapeCount = (int)usedShapes.size();

		if (SAVE_DEBUG_ATLAS) {
			WARN_LOG(Log::G3D, "Writing debug image buttons_rasterized.png");
			pngSave(Path("../buttons_rasterized.png"), svgImg, svgWidth, svgHeight, 4);
		}
		delete[] svgImg;

		nsvgDeleteRasterizer(rast);
		nsvgDelete(image);
	}

	INFO_LOG(Log::G3D, " - Rasterized %d images in the svg image in %0.2f ms", shapeCount, svgStart.ElapsedMs());
	return true;
}

int DumpButtonsPNGsToSystem() {
	Path textureDir = GetSysDirectory(DIRECTORY_TEXTURES);
	std::string gameID;
	if (g_paramSFO.IsValid()) {
		gameID = g_paramSFO.GetDiscID();
		if (!gameID.empty()) {
			textureDir = textureDir / gameID;
		}
	}

	Path sourceButtons = textureDir / "buttons.svg";
	if (!File::Exists(sourceButtons)) {
		sourceButtons = Path("ui_images/buttons.svg");
	}

	std::vector<Image> images(ARRAY_SIZE(g_uiImageIDs));
	if (!RasterizeSVG(sourceButtons.c_str(), 1.0f, 8192, g_uiImageIDs, ARRAY_SIZE(g_uiImageIDs), &images, false)) {
		ERROR_LOG(Log::G3D, "Failed to rasterize buttons SVG for PNG dump: %s", sourceButtons.c_str());
		return 0;
	}

	Path dumpDir = textureDir / "new";
	File::CreateFullPath(dumpDir);

	// Also dump the source SVG to keep PNG dumps and vector source together.
	size_t svgSize = 0;
	std::string sourceButtonsPath = sourceButtons.ToString();
	const uint8_t *svgData = g_VFS.ReadFile(sourceButtonsPath, &svgSize);
	if (svgData && svgSize > 0) {
		Path svgOutPath = dumpDir / "buttons.svg";
		if (!File::WriteDataToFile(false, svgData, svgSize, svgOutPath)) {
			WARN_LOG(Log::G3D, "Failed to write buttons SVG dump: %s", svgOutPath.c_str());
		}
		delete[] svgData;
	}

	int dumped = 0;
	for (int i = 0; i < (int)images.size(); i++) {
		if (images[i].IsEmpty()) {
			continue;
		}

		Path outPath = dumpDir / ("buttons_" + PNGNameFromID(g_uiImageIDs[i].id));
		pngSave(outPath, images[i].data(), images[i].width(), images[i].height(), 4);
		dumped++;
	}

	INFO_LOG(Log::G3D, "Dumped %d buttons PNG files to %s", dumped, dumpDir.c_str());
	return dumped;
}

static std::string NormalizeButtonAliasKey(std::string_view key) {
	std::string out;
	out.reserve(key.size());
	if (key.size() > 2 && key[0] == 'I' && key[1] == '_') {
		key = key.substr(2);
	}
	for (char c : key) {
		out.push_back((char)tolower((unsigned char)c));
	}
	return out;
}

static std::unordered_map<std::string, std::string> LoadButtonAliases(const Path &textureDir) {
	std::unordered_map<std::string, std::string> aliases;
	IniFile ini;
	if (!ini.Load(textureDir / "textures.ini")) {
		return aliases;
	}

	const Section *buttons = ini.GetSection("buttons");
	if (!buttons) {
		return aliases;
	}

	for (const ParsedIniLine &line : buttons->Lines()) {
		if (line.Key().empty() || line.Value().empty()) {
			continue;
		}
		aliases[NormalizeButtonAliasKey(line.Key())] = std::string(line.Value());
	}
	return aliases;
}

static int LoadButtonsPNGOverrides(const Path &textureDir, const std::unordered_map<std::string, std::string> &aliases, const ImageMeta *imageIDs, size_t imageCount, std::vector<Image> *images) {
	auto bilinearSample = [](const Image &img, float x, float y) {
		const int w = img.width();
		const int h = img.height();

		x = std::clamp(x, 0.0f, (float)(w - 1));
		y = std::clamp(y, 0.0f, (float)(h - 1));

		const int x0 = (int)std::floor(x);
		const int y0 = (int)std::floor(y);
		const int x1 = std::min(x0 + 1, w - 1);
		const int y1 = std::min(y0 + 1, h - 1);

		const float tx = x - (float)x0;
		const float ty = y - (float)y0;

		const u32 c00 = img.get1(x0, y0);
		const u32 c10 = img.get1(x1, y0);
		const u32 c01 = img.get1(x0, y1);
		const u32 c11 = img.get1(x1, y1);

		auto interpChannel = [&](int shift) {
			const float v00 = (float)((c00 >> shift) & 0xFF);
			const float v10 = (float)((c10 >> shift) & 0xFF);
			const float v01 = (float)((c01 >> shift) & 0xFF);
			const float v11 = (float)((c11 >> shift) & 0xFF);
			const float a = v00 + (v10 - v00) * tx;
			const float b = v01 + (v11 - v01) * tx;
			return (u32)std::lround(a + (b - a) * ty);
		};

		const u32 r = interpChannel(0);
		const u32 g = interpChannel(8);
		const u32 b = interpChannel(16);
		const u32 a = interpChannel(24);
		return r | (g << 8) | (b << 16) | (a << 24);
	};

	int loaded = 0;
	for (int i = 0; i < (int)imageCount; i++) {
		std::string pngName = PNGNameFromID(imageIDs[i].id);
		std::string key = NormalizeButtonAliasKey(imageIDs[i].id);

		Path chosenPath;
		auto alias = aliases.find(key);
		if (alias != aliases.end()) {
			chosenPath = textureDir / alias->second;
		} else {
			chosenPath = textureDir / ("buttons_" + pngName);
		}

		if (!File::Exists(chosenPath)) {
			continue;
		}

		Image loadedImage;
		if (!loadedImage.LoadPNG(chosenPath.c_str())) {
			ERROR_LOG(Log::G3D, "Failed to load custom buttons PNG: %s", chosenPath.c_str());
			continue;
		}

		const Image &targetImage = (*images)[i];
		if (!targetImage.IsEmpty() && targetImage.width() > 0 && targetImage.height() > 0) {
			const int targetW = targetImage.width();
			const int targetH = targetImage.height();
			const int srcW = loadedImage.width();
			const int srcH = loadedImage.height();

			float fitScale = std::min((float)targetW / (float)srcW, (float)targetH / (float)srcH);
			fitScale = std::min(fitScale, 1.0f);

			const int fitW = std::max(1, (int)std::lround(srcW * fitScale));
			const int fitH = std::max(1, (int)std::lround(srcH * fitScale));
			const int offsetX = (targetW - fitW) / 2;
			const int offsetY = (targetH - fitH) / 2;

			Image fitted;
			fitted.resize(targetW, targetH);
			fitted.fill(0);
			fitted.scale = targetImage.scale;

			for (int y = 0; y < fitH; y++) {
				const float srcY = ((float)y + 0.5f) * (float)srcH / (float)fitH - 0.5f;
				for (int x = 0; x < fitW; x++) {
					const float srcX = ((float)x + 0.5f) * (float)srcW / (float)fitW - 0.5f;
					fitted.set1(offsetX + x, offsetY + y, bilinearSample(loadedImage, srcX, srcY));
				}
			}

			loadedImage = std::move(fitted);
		}

		loadedImage.ConvertToPremultipliedAlpha();
		(*images)[i] = std::move(loadedImage);
		loaded++;
	}

	return loaded;
}

static bool GenerateUIAtlasImage(Atlas *atlas, float dpiScale, Image *dest, int maxTextureSize, const ImageMeta *imageIDs, size_t imageCount) {
	Bucket bucket;

#ifdef _DEBUG
	for (int i = 0; i < imageCount; i++) {
		_dbg_assert_(imageIDs[i].id.size() < 32);
	}
#endif

	Instant svgStart = Instant::Now();

	// Script fully read, now read images and rasterize the fonts.
	std::vector<Image> images(imageCount);

	if (!RasterizeSVG("ui_images/images.svg", dpiScale, maxTextureSize, imageIDs, imageCount, &images)) {
		return false;
	}
	if (g_Config.bReplaceTextures) {
		Path textureDir = GetSysDirectory(DIRECTORY_TEXTURES);
		std::string gameID;
		if (g_paramSFO.IsValid()) {
			gameID = g_paramSFO.GetDiscID();
			if (!gameID.empty()) {
				textureDir = textureDir / gameID;
			}
		}

		Path customButtons = textureDir / "buttons.svg";
		if (File::Exists(customButtons)) {
			INFO_LOG(Log::G3D, "Using texture-replacement buttons SVG: %s", customButtons.c_str());
			if (!RasterizeSVG(customButtons.c_str(), dpiScale, maxTextureSize, imageIDs, imageCount, &images)) {
				return false;
			}
		} else {
			if (!RasterizeSVG("ui_images/buttons.svg", dpiScale, maxTextureSize, imageIDs, imageCount, &images)) {
				return false;
			}
		}

		std::unordered_map<std::string, std::string> aliases = LoadButtonAliases(textureDir);
		int buttonsPngOverridden = LoadButtonsPNGOverrides(textureDir, aliases, imageIDs, imageCount, &images);
		if (buttonsPngOverridden > 0) {
			INFO_LOG(Log::G3D, "Loaded %d custom buttons PNG overrides", buttonsPngOverridden);
		}
	} else {
		if (!RasterizeSVG("ui_images/buttons.svg", dpiScale, maxTextureSize, imageIDs, imageCount, &images)) {
			return false;
		}
	}

	Instant shadowStart = Instant::Now();

	// We can trivially parallelize shadowing/extension of the images.
	ParallelRangeLoop(&g_threadManager, [&images, imageIDs](int start, int end) {
		for (int i = start; i < end; i++) {
			// Here we could exclude some images from the drop shadow, if desired.
			if (!images[i].IsEmpty()) {
				if (imageIDs[i].addShadow) {
					// DEBUG_LOG(Log::G3D, "Adding drop shadow to %.*s", STR_VIEW(imageIDs[i].id));
					AddDropShadow(images[i], 3, 0.66f);
				} else {
					// Make sure there are transparent pixels to filter from.
					Add1PxTransparentBorder(images[i]);
				}
			}
		}
	}, 0, (int)images.size(), 2, TaskPriority::HIGH);

	INFO_LOG(Log::G3D, " - Drop-shadowed images in %0.2f ms", shadowStart.ElapsedMs());

	Instant pngStart = Instant::Now();

	// TODO: This can be parallelized if needed.
	std::vector<int> resultIds(imageCount);
	int pngsLoaded = 0;
	for (int i = 0; i < (int)images.size(); i++) {
		resultIds[i] = i;

		Image &img = images[i];

		if (!img.IsEmpty()) {
			// Was already loaded from SVG.
			DEBUG_LOG(Log::G3D, "Skipping image '%.*s' (%d), already loaded from SVG", STR_VIEW(imageIDs[i].id), i);
			continue;
		}

		bool success = true;
		if (equals(imageIDs[i].id, "I_SOLIDWHITE")) {
			img.resize(16, 16);
			img.fill(0xFFFFFFFF);
		} else if (equals(imageIDs[i].id, "I_EMPTY")) {
			img.resize(16, 16);
			img.fill(0);
		} else {
			std::string name = "ui_images/";
			std::string pngName = PNGNameFromID(imageIDs[i].id);
			name.append(pngName);
			bool success = img.LoadPNG(name.c_str());
			if (!success) {
				ERROR_LOG(Log::G3D, "%.*s is missing. Not present in SVG files and no suitable PNG found (%s)", STR_VIEW(imageIDs[i].id), name.c_str());
			} else {
				pngsLoaded++;
				img.ConvertToPremultipliedAlpha();
			}
		}
	}
	INFO_LOG(Log::G3D, " - Loaded %d png images in %.2f ms", pngsLoaded, pngStart.ElapsedMs());

	Instant addStart = Instant::Now();
	int area = 0;
	for (int i = 0; i < images.size(); i++) {
		bucket.AddImage(std::move(images[i]), i);
		area += images[i].width() * images[i].height();
	}

	INFO_LOG(Log::G3D, " - Added %zu images to bucket in %.2f ms", bucket.data.size(), addStart.ElapsedMs());

	int imageWidth = RoundToNextPowerOf2((int)sqrtf(area));

	Instant bucketStart = Instant::Now();
	bucket.Pack2(imageWidth);
	INFO_LOG(Log::G3D, " - Packed in %.2f ms (image size: %dx%d)", bucketStart.ElapsedMs(), bucket.w, bucket.h);

	Instant resolveStart = Instant::Now();
	std::vector<Data> results = bucket.Resolve(dest);
	INFO_LOG(Log::G3D, " - Resolved %zu images in %.2f ms (final image size: %dx%d)", results.size(), resolveStart.ElapsedMs(), dest->width(), dest->height());

	_dbg_assert_(!results.empty());
	// Fill out the atlas structure.
	std::vector<AtlasImage> genAtlasImages;
	genAtlasImages.reserve(imageCount);
	for (int i = 0; i < imageCount; i++) {
		genAtlasImages.push_back(ToAtlasImage(resultIds[i], imageIDs[i].id, (float)dest->width(), (float)dest->height(), results));
	}

	atlas->Clear();
	atlas->images = new AtlasImage[genAtlasImages.size()];
	std::copy(genAtlasImages.begin(), genAtlasImages.end(), atlas->images);
	atlas->num_images = (int)genAtlasImages.size();

	// For debug, write out the atlas.
	if (SAVE_DEBUG_ATLAS) {
		WARN_LOG(Log::G3D, "Writing debug image ui_atlas_gen.png");
		dest->SavePNG("../ui_atlas_gen.png");
	}
	INFO_LOG(Log::G3D, "UI atlas generated in %.2f ms, size %dx%d with %zu images", svgStart.ElapsedMs(), dest->width(), dest->height(), genAtlasImages.size());
	return true;
}

static Image g_cachedUIAtlasImage;
static float g_cachedDpiScale = 0.0f;

// The caller must cache the Atlas.
Draw::Texture *GenerateUIAtlas(Draw::DrawContext *draw, Atlas *atlas, float dpiScale, bool invalidate) {
	if (g_cachedUIAtlasImage.IsEmpty() || dpiScale != g_cachedDpiScale || invalidate) {
		INFO_LOG(Log::G3D, "Regenerating atlas (empty: %s). Dpi scale (changed: %s): %0.2f (invalidate=%d)",
			g_cachedUIAtlasImage.IsEmpty() ? "true" : "false", dpiScale != g_cachedDpiScale ? "true" : "false", dpiScale, invalidate);

		g_cachedUIAtlasImage.clear();
		if (!GenerateUIAtlasImage(atlas, dpiScale, &g_cachedUIAtlasImage, draw->GetDeviceCaps().maxTextureSize, g_uiImageIDs, ARRAY_SIZE(g_uiImageIDs))) {
			ERROR_LOG(Log::G3D, "Failed to generate UI atlas!");
			return nullptr;
		}
	}

	g_cachedDpiScale = dpiScale;

	// Create the texture.
	Draw::TextureDesc desc{};
	desc.width = g_cachedUIAtlasImage.width();
	desc.height = g_cachedUIAtlasImage.height();
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	desc.type = Draw::TextureType::LINEAR2D;
	desc.initData.push_back((const u8 *)g_cachedUIAtlasImage.data());
	desc.tag = "UIAtlas";
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

AtlasData AtlasProvider(Draw::DrawContext *draw, AtlasChoice atlas, float dpiScale, bool invalidate) {
	// Clamp the dpiScale to sane values. Might increase the range later.
	dpiScale = std::clamp(dpiScale, 0.5f, 4.0f);

	switch (atlas) {
	case AtlasChoice::General:
	{
		// Generate the atlas from scratch.
		Draw::Texture *tex = GenerateUIAtlas(draw, &ui_atlas, dpiScale, invalidate);
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
