#include <string>
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
static const ImageMeta imageIDs[] = {
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
	{"I_STOP", false},
	{"I_PAUSE", false},
	{"I_FAST_FORWARD", false},
	{"I_FAST_FORWARD_LINE", false},
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
	{"I_HAMBURGER", false},
	{"I_DEVICE_ROTATION_LANDSCAPE_REV", false},
	{"I_DEVICE_ROTATION_AUTO", false},
	{"I_DEVICE_ROTATION_LANDSCAPE", false},
	{"I_DEVICE_ROTATION_PORTRAIT", false},
	{"I_MOVE", false},
	{"I_RESIZE", false},
	{"I_LINK_OUT_QUESTION", false},
	{"I_PSX_ISO", false},
	{"I_PS2_ISO", false},
	{"I_PS3_ISO", false},
	{"I_UNKNOWN_ISO", false},
	{"I_UMD_VIDEO_ISO", false},
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

static int GetImageIndex(std::string_view id) {
	for (int i = 0; i < ARRAY_SIZE(imageIDs); i++) {
		if (equals(id, imageIDs[i].id)) {
			return i;
		}
	}
	return -1;
}

static bool IsImageID(std::string_view id) {
	return GetImageIndex(id) != -1;
}

static bool GenerateUIAtlasImage(Atlas *atlas, float dpiScale, Image *dest, int maxTextureSize) {
	Bucket bucket;

#ifdef _DEBUG
	for (int i = 0; i < ARRAY_SIZE(imageIDs); i++) {
		_dbg_assert_(imageIDs[i].id.size() < 32);
	}
#endif

	// Script fully read, now read images and rasterize the fonts.
	std::vector<Image> images(ARRAY_SIZE(imageIDs));
	int resultIds[ARRAY_SIZE(imageIDs)]{};

	Instant svgStart = Instant::Now();

	// Load SVGs here, trying to fill in the images. The remaining images we fill from PNGs.
	// For now we only load one hardcoded SVG.
	int shapeCount = 0;
	{
		size_t sz;
		const uint8_t *file_data = g_VFS.ReadFile("ui_images/images.svg", &sz);  // ReadFile null-terminates
		if (file_data) {
			NSVGimage *image = nsvgParse((char *)file_data, "px", 96.0f);
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
			if (image) {
				// Loop through the shapes to list them, and to hide them if irrelevant.
				NSVGshape *shape = image->shapes;
				while (shape) {
					if (!IsImageID(shape->id)) {
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
			}

			NSVGrasterizer *rast = NULL;
			// Rasterize here, and add into image list.
			rast = nsvgCreateRasterizer();

			// If we can tell that the scale won't fit in a supported texture size, reduce it.
			// This is a conservative check because the SVG has some empty space around the sub-images.
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
				int index = GetImageIndex(shapeId);
				_dbg_assert_(index != -1);
				if (index == -1) {
					continue;
				}

				Image &img = images[index];
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

				img.ConvertToPremultipliedAlpha();
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
	}

	INFO_LOG(Log::G3D, " - Rasterized %d images in the svg image in %0.2f ms", shapeCount, svgStart.ElapsedMs());

	Instant shadowStart = Instant::Now();

	// We can trivially parallelize shadowing/extension of the images.
	ParallelRangeLoop(&g_threadManager, [&](int start, int end) {
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
	int pngsLoaded = 0;
	for (int i = 0; i < (int)images.size(); i++) {
		resultIds[i] = i;

		Image &img = images[i];

		if (!img.IsEmpty()) {
			// Was already loaded from SVG.
			DEBUG_LOG(Log::G3D, "Skipping image %.*s, already loaded from SVG", STR_VIEW(imageIDs[i].id));
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
				ERROR_LOG(Log::G3D, "Failed to load %s", name.c_str());
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
	genAtlasImages.reserve(ARRAY_SIZE(imageIDs));
	for (int i = 0; i < ARRAY_SIZE(imageIDs); i++) {
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
		if (!GenerateUIAtlasImage(atlas, dpiScale, &g_cachedUIAtlasImage, draw->GetDeviceCaps().maxTextureSize)) {
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
