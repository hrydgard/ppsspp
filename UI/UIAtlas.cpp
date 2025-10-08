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
#include "Common/Log.h"
#include "UI/UIAtlas.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "ext/nanosvg/src/nanosvg.h"
#include "ext/nanosvg/src/nanosvgrast.h"

constexpr bool SAVE_DEBUG_IMAGES = false;

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
	"I_ROUND",
	"I_ROUND_LINE",
	"I_RECT",
	"I_RECT_LINE",
	"I_STICK",
	"I_STICK_BG",
	"I_STICK_LINE",
	"I_STICK_BG_LINE",
	"I_SHOULDER",
	"I_SHOULDER_LINE",
	"I_DIR",
	"I_DIR_LINE",
	"I_SQUARE_SHAPE",
	"I_SQUARE_SHAPE_LINE",
	"I_CHECKEDBOX",
	"I_UNCHECKEDBOX",
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
	"I_PIN",
	"I_UNPIN",
	"I_FOLDER_PINNED",
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
		if (equals(id, imageIDs[i])) {
			return i;
		}
	}
	return -1;
}

static bool IsImageID(std::string_view id) {
	return GetImageIndex(id) != -1;
}

Draw::Texture *GenerateUIAtlas(Draw::DrawContext *draw, Atlas *atlas, float dpiScale) {
	Bucket bucket;

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

			const float scale = dpiScale;
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
					pngSave(Path(std::string("../buttons_") + PNGNameFromID(shapeId)), img.data(), img.width(), img.height(), 4);
				}
			}

			shapeCount = (int)usedShapes.size();

			if (SAVE_DEBUG_IMAGES) {
				pngSave(Path("../buttons_rasterized.png"), svgImg, svgWidth, svgHeight, 4);
			}
			delete[] svgImg;

			nsvgDeleteRasterizer(rast);
			nsvgDelete(image);
		}
	}

	INFO_LOG(Log::G3D, " - Rasterized %d images in the svg image in %0.2f ms\n", shapeCount, svgStart.ElapsedMs());

	Instant shadowStart = Instant::Now();

	for (int i = 0; i < (int)images.size(); i++) {
		// Here we could exclude some images from the drop shadow, if desired.
		if (!images[i].IsEmpty()) {
			AddDropShadow(images[i], 3, 0.66f);
		}
	}

	INFO_LOG(Log::G3D, " - Drop-shadowed images in %0.2f ms\n", shadowStart.ElapsedMs());

	Instant pngStart = Instant::Now();

	// TODO: This can be parallelized if needed.
	int pngsLoaded = 0;
	for (int i = 0; i < (int)images.size(); i++) {
		resultIds[i] = i;

		Image &img = images[i];

		if (!img.IsEmpty()) {
			// Was already loaded from SVG.
			DEBUG_LOG(Log::G3D, "Skipping image %s, already loaded from SVG", imageIDs[i].c_str());
			continue;
		}

		bool success = true;
		if (equals(imageIDs[i], "I_SOLIDWHITE")) {
			img.resize(16, 16);
			img.fill(0xFFFFFFFF);
		} else if (equals(imageIDs[i], "I_EMPTY")) {
			img.resize(16, 16);
			img.fill(0);
		} else {
			std::string name = "ui_images/";
			std::string pngName = PNGNameFromID(imageIDs[i]);
			name.append(pngName);
			bool success = img.LoadPNG(name.c_str());
			if (!success) {
				ERROR_LOG(Log::G3D, "Failed to load %s", name.c_str());
			} else {
				pngsLoaded++;
			}
		}
	}
	INFO_LOG(Log::G3D, " - Loaded %d png images in %.2f ms", pngsLoaded, pngStart.ElapsedMs());

	Instant addStart = Instant::Now();
	for (int i = 0; i < images.size(); i++) {
		bucket.AddImage(std::move(images[i]), i);
	}

	INFO_LOG(Log::G3D, " - Added %zu images to bucket in %.2f ms", bucket.data.size(), addStart.ElapsedMs());

	int image_width = 512;
	Image dest;

	Instant bucketStart = Instant::Now();
	std::vector<Data> results = bucket.Resolve(image_width, dest);
	INFO_LOG(Log::G3D, " - Bucketed %zu images in %.2f ms", results.size(), bucketStart.ElapsedMs());

	_dbg_assert_(!results.empty());
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
	if (SAVE_DEBUG_IMAGES) {
		dest.SavePNG("../ui_atlas_gen.png");
	}

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

	INFO_LOG(Log::G3D, "UI atlas generated in %.2f ms, size %dx%d with %zu images\n", svgStart.ElapsedMs(), desc.width, desc.height, genAtlasImages.size());
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

AtlasData AtlasProvider(Draw::DrawContext *draw, AtlasChoice atlas, float dpiScale) {
	// Clamp the dpiScale to sane values. Might increase the range later.
	dpiScale = std::clamp(dpiScale, 0.5f, 4.0f);

	switch (atlas) {
	case AtlasChoice::General:
	{
		// Generate the atlas from scratch.
		Draw::Texture *tex = GenerateUIAtlas(draw, &ui_atlas, dpiScale);
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
