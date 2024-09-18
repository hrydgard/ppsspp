#include "ppsspp_config.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_sdl.h"

#if defined(USE_SDL2_TTF)

#include "SDL2/SDL.h"
#include "SDL2/SDL_ttf.h"

static std::string getlocale() {
	// setlocale is not an intuitive function...
	char *curlocale = setlocale(LC_CTYPE, nullptr);
	std::string loc = curlocale ? std::string(curlocale) : "en_US";
	size_t ptPos = loc.find('.');
	// Remove any secondary specifier.
	if (ptPos != std::string::npos) {
		loc.resize(ptPos);
	}
	return loc;
}

TextDrawerSDL::TextDrawerSDL(Draw::DrawContext *draw): TextDrawer(draw) {
	if (TTF_Init() < 0) {
		ERROR_LOG(Log::G3D, "Unable to initialize SDL2_ttf");
	}

	dpiScale_ = CalculateDPIScale();

#if defined(USE_SDL2_TTF_FONTCONFIG)
	config = FcInitLoadConfigAndFonts();
#endif

	PrepareFallbackFonts(getlocale());
}

TextDrawerSDL::~TextDrawerSDL() {
	ClearCache();
	ClearFonts();

	TTF_Quit();

#if defined(USE_SDL2_TTF_FONTCONFIG)
	FcConfigDestroy(config);
	// Don't call this - it crashes, see https://github.com/openframeworks/openFrameworks/issues/5061.
	//FcFini();
#endif
}

// If a user complains about missing characters on SDL, re-visit this!
void TextDrawerSDL::PrepareFallbackFonts(std::string_view locale) {
#if defined(USE_SDL2_TTF_FONTCONFIG)
	FcObjectSet *os = FcObjectSetBuild (FC_FILE, FC_INDEX, (char *) 0);

	// To install the recommended Droid Sans fallback font in Ubuntu:
	// sudo apt install fonts-droid-fallback
	const char *hardcodedNames[] = {
		"Droid Sans Medium",
		"Droid Sans Fallback",
		"Source Han Sans Medium",
		"Noto Sans CJK Medium",
		"Noto Sans Hebrew Medium",
		"Noto Sans Lao Medium",
		"Noto Sans Thai Medium",
		"DejaVu Sans Condensed",
		"DejaVu Sans",
		"Meera Regular",
		"FreeSans",
		"Gargi",
		"KacstDigital",
		"KacstFarsi",
		"Khmer OS",
		"Paduak",
		"Paduak",
		"Jamrul",
	};

	std::vector<const char *> names;
	if (locale == "zh_CN") {
		names.push_back("Noto Sans CJK SC");
	} else if (locale == "zh_TW") {
		names.push_back("Noto Sans CJK TC");
		names.push_back("Noto Sans CJK HK");
	} else if (locale == "ja_JP") {
		names.push_back("Noto Sans CJK JP");
	} else if (locale == "ko_KR") {
		names.push_back("Noto Sans CJK KR");
	} else {
		// Let's just pick one.
		names.push_back("Noto Sans CJK JP");
	}

	// Then push all the hardcoded ones.
	for (int i = 0; i < ARRAY_SIZE(hardcodedNames); i++) {
		names.push_back(hardcodedNames[i]);
	}

	// First, add the region-specific Noto fonts according to the locale.

	for (int i = 0; i < names.size(); i++) {
		// printf("trying font name %s\n", names[i]);
		FcPattern *name = FcNameParse((const FcChar8 *)names[i]);
		FcFontSet *foundFonts = FcFontList(config, name, os);
		
		for (int j = 0; foundFonts && j < foundFonts->nfont; ++j) {
			FcPattern* font = foundFonts->fonts[j];
			FcChar8 *path;
			int fontIndex;

			if (FcPatternGetInteger(font, FC_INDEX, 0, &fontIndex) != FcResultMatch) {
				fontIndex = 0; // The 0th face is guaranteed to exist 
			}

			if (FcPatternGetString(font, FC_FILE, 0, &path) == FcResultMatch) {
				std::string path_str((const char*)path);
				// printf("fallback font: %s\n", path_str.c_str());
				fallbackFontPaths_.push_back(std::make_pair(path_str, fontIndex));
			}
		}

		if (foundFonts) {
			FcFontSetDestroy(foundFonts);
		}

		FcPatternDestroy(name);
	}

	if (os) {
		FcObjectSetDestroy(os);
	}
#elif PPSSPP_PLATFORM(MAC)
	const char *fontDirs[] = {
		"/System/Library/Fonts/",
		"/System/Library/Fonts/Supplemental/",
		"/Library/Fonts/"
	};

	const char *fallbackFonts[] = {
		"Hiragino Sans GB.ttc",
		"PingFang.ttc",
		"PingFang SC.ttc",
		"PingFang TC.ttc",
		"ヒラギノ角ゴシック W4.ttc",
		"AppleGothic.ttf",
		"Arial Unicode.ttf",
	};

	for (int i = 0; i < ARRAY_SIZE(fontDirs); i++) {
		for (int j = 0; j < ARRAY_SIZE(fallbackFonts); j++) {
			Path fontPath = Path(fontDirs[i]) / fallbackFonts[j];

			if (File::Exists(fontPath)) {
				TTF_Font *openedFont = TTF_OpenFont(fontPath.ToString().c_str(), 24);
				int64_t numFaces = TTF_FontFaces(openedFont);

				for (int k = 0; k < numFaces; k++) {
					TTF_Font *fontFace = TTF_OpenFontIndex(fontPath.ToString().c_str(), 24, k);
					std::string fontFaceName(TTF_FontFaceStyleName(fontFace));
					TTF_CloseFont(fontFace);

					if (strstr(fontFaceName.c_str(), "Medium") ||
						strstr(fontFaceName.c_str(), "Regular"))
					{
						fallbackFontPaths_.push_back(std::make_pair(fontPath.ToString(), k));
						break;
					}
				}

				TTF_CloseFont(openedFont);
			}
		}	
	}
#else
	// We don't have a fallback font for this platform.
	// Unsupported characters will be rendered as squares.
#endif
}

uint32_t TextDrawerSDL::CheckMissingGlyph(const std::string& text) {
	TTF_Font *font = fontMap_.find(fontHash_)->second;
	UTF8 utf8Decoded(text.c_str());

	uint32_t missingGlyph = 0;
	for (int i = 0; i < text.length(); ) {
		uint32_t glyph = utf8Decoded.next();
		i = utf8Decoded.byteIndex();

		if (!TTF_GlyphIsProvided32(font, glyph)) {
			missingGlyph = glyph;
			break;
		}
	}

	return missingGlyph;
}

// If this returns >= 0, the nth font in fallbackFonts_ can be used as a fallback.
int TextDrawerSDL::FindFallbackFonts(uint32_t missingGlyph, int ptSize) {
	auto iter = glyphFallbackFontIndex_.find(missingGlyph);

	if (iter != glyphFallbackFontIndex_.end()) {
		return iter->second;
	}

	// If we encounter a missing glyph, try to use one of already loaded fallback fonts.
	for (int i = 0; i < fallbackFonts_.size(); i++) {
		TTF_Font *fallbackFont = fallbackFonts_[i];
		if (TTF_GlyphIsProvided32(fallbackFont, missingGlyph)) {
			glyphFallbackFontIndex_[missingGlyph] = i;
			return i;
		}
	}

	// If none of the loaded fonts can handle it, load more fonts.
	// TODO: Don't retry already tried fonts.
	for (int i = 0; i < fallbackFontPaths_.size(); i++) {
		std::string& fontPath = fallbackFontPaths_[i].first;
		int faceIndex = fallbackFontPaths_[i].second;

		TTF_Font *font = TTF_OpenFontIndex(fontPath.c_str(), ptSize, faceIndex);

		if (TTF_GlyphIsProvided32(font, missingGlyph)) {
			fallbackFonts_.push_back(font);
			return fallbackFonts_.size() - 1;
		} else {
			TTF_CloseFont(font);
		}
	}

	// Not found at all? Let's remember that for this glyph.
	glyphFallbackFontIndex_[missingGlyph] = -1;
	return -1;
}

uint32_t TextDrawerSDL::SetFont(const char *fontName, int size, int flags) {
	uint32_t fontHash = fontName && strlen(fontName) ? hash::Adler32((const uint8_t *)fontName, strlen(fontName)) : 0;
	fontHash ^= size;
	fontHash ^= flags << 10;

	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHash;
		return fontHash;
	}

	const char *useFont = fontName ? fontName : "Roboto-Condensed.ttf";
	const int ptSize = (int)((size + 6) / dpiScale_);

	TTF_Font *font = TTF_OpenFont(useFont, ptSize);

	if (!font) {
		File::FileInfo fileInfo;
   		g_VFS.GetFileInfo("Roboto-Condensed.ttf", &fileInfo);
		font = TTF_OpenFont(fileInfo.fullName.c_str(), ptSize);
	}

	fontMap_[fontHash] = font;
	fontHash_ = fontHash;
	return fontHash;
}

void TextDrawerSDL::SetFont(uint32_t fontHandle) {
	uint32_t fontHash = fontHandle;
	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHandle;
	} else {
		ERROR_LOG(Log::G3D, "Invalid font handle %08x", fontHandle);
	}
}

void TextDrawerSDL::MeasureString(std::string_view str, float *w, float *h) {
	CacheKey key{ std::string(str), fontHash_ };

	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(key);
	if (iter != sizeCache_.end()) {
		entry = iter->second.get();
	} else {
		TTF_Font *font = fontMap_.find(fontHash_)->second;
		int ptSize = TTF_FontHeight(font) / 1.35;

		uint32_t missingGlyph = CheckMissingGlyph(key.text);
		
		if (missingGlyph) {
			int fallbackFont = FindFallbackFonts(missingGlyph, ptSize);
			if (fallbackFont >= 0) {
				font = fallbackFonts_[fallbackFont];
			}
		}

		int width = 0;
		int height = 0;
		TTF_SizeUTF8(font, key.text.c_str(), &width, &height);

		entry = new TextMeasureEntry();
		entry->width = width;
		entry->height = height;
		sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
	}

	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_ * dpiScale_;
	*h = entry->height * fontScaleY_ * dpiScale_;
}

bool TextDrawerSDL::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	_dbg_assert_(!fullColor)

	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	std::string processedStr(str);

	// If a string includes only newlines, SDL2_ttf will refuse to render it
	// thinking it is empty. Add a space to avoid that. 
	bool isAllNewline = processedStr.find_first_not_of('\n') == std::string::npos;

	if (isAllNewline) {
		processedStr.push_back(' ');
	}

	TTF_Font *font = fontMap_.find(fontHash_)->second;
	int ptSize = TTF_FontHeight(font) / 1.35;

	uint32_t missingGlyph = CheckMissingGlyph(processedStr);

	if (missingGlyph) {
		int fallbackFont = FindFallbackFonts(missingGlyph, ptSize);
		if (fallbackFont >= 0) {
			font = fallbackFonts_[fallbackFont];
		}
	}

#if SDL_TTF_VERSION_ATLEAST(2, 20, 0)
	if (align & ALIGN_HCENTER)
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_CENTER);
	else if (align & ALIGN_RIGHT)
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_RIGHT);
	else
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_LEFT);
#endif

	SDL_Color fgColor = { 0xFF, 0xFF, 0xFF, 0xFF };
	SDL_Surface *text = TTF_RenderUTF8_Blended_Wrapped(font, processedStr.c_str(), fgColor, 0);
	SDL_LockSurface(text);

	entry.texture = nullptr;
	// Each row of pixel needs to be aligned to 8 bytes (= 2 pixels), or else
	// graphics corruption occurs. Made it 16-byte aligned just to be sure.
	entry.bmWidth = entry.width = (text->w + 3) & ~3;
	entry.bmHeight = entry.height = text->h;
	entry.lastUsedFrame = frameCount_;

	uint32_t *imageData = (uint32_t *)text->pixels;
	uint32_t pitch = text->pitch / sizeof(uint32_t);

	if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];

		for (int x = 0; x < entry.bmWidth; x++) {
			for (int y = 0; y < entry.bmHeight; y++) {
				uint64_t index = entry.bmWidth * y + x;
				bitmapData16[index] = 0xfff0 | (imageData[pitch * y + x] >> 28);
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int x = 0; x < entry.bmWidth; x++) {
			for (int y = 0; y < entry.bmHeight; y++) {
				uint64_t index = entry.bmWidth * y + x;
				bitmapData[index] = imageData[pitch * y + x] >> 24;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}

	SDL_UnlockSurface(text);
	SDL_FreeSurface(text);
	return true;
}

void TextDrawerSDL::ClearFonts() {
	for (auto iter : fontMap_) {
		TTF_CloseFont(iter.second);
	}
	for (auto iter : fallbackFonts_) {
		TTF_CloseFont(iter);
	}
	fontMap_.clear();
	fallbackFonts_.clear();
}

#endif
