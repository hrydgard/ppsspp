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

TextDrawerSDL::TextDrawerSDL(Draw::DrawContext *draw): TextDrawer(draw) {
	if (TTF_Init() < 0) {
		ERROR_LOG(G3D, "Unable to initialize SDL2_ttf");
	}

	dpiScale_ = CalculateDPIScale();

#if defined(USE_SDL2_TTF_FONTCONFIG)
	config = FcInitLoadConfigAndFonts();
#endif

	PrepareFallbackFonts();
}

TextDrawerSDL::~TextDrawerSDL() {
	ClearCache();
	TTF_Quit();

#if defined(USE_SDL2_TTF_FONTCONFIG)
	FcConfigDestroy(config);
	FcFini();
#endif
}

// If a user complains about missing characters on SDL, re-visit this!
void TextDrawerSDL::PrepareFallbackFonts() {
#if defined(USE_SDL2_TTF_FONTCONFIG)
	FcObjectSet *os = FcObjectSetBuild (FC_FILE, FC_INDEX, (char *) 0);

	FcPattern *names[] = {
		FcNameParse((const FcChar8 *) "Source Han Sans Medium"),
		FcNameParse((const FcChar8 *) "Droid Sans Bold"),
		FcNameParse((const FcChar8 *) "DejaVu Sans Condensed"),
		FcNameParse((const FcChar8 *) "Noto Sans CJK Medium"),
		FcNameParse((const FcChar8 *) "Noto Sans Hebrew Medium"),
		FcNameParse((const FcChar8 *) "Noto Sans Lao Medium"),
		FcNameParse((const FcChar8 *) "Noto Sans Thai Medium")
	};

	for (int i = 0; i < ARRAY_SIZE(names); i++) {
		FcFontSet *foundFonts = FcFontList(config, names[i], os);
		
		for (int j = 0; foundFonts && j < foundFonts->nfont; ++j) {
			FcPattern* font = foundFonts->fonts[j];
			FcChar8 *path;
			int fontIndex;

			if (FcPatternGetInteger(font, FC_INDEX, 0, &fontIndex) != FcResultMatch) {
				fontIndex = 0; // The 0th face is guaranteed to exist 
			}

			if (FcPatternGetString(font, FC_FILE, 0, &path) == FcResultMatch) {
				std::string path_str((const char*)path);
				fallbackFontPaths_.push_back(std::make_pair(path_str, fontIndex));
			}
		}

		if (foundFonts) {
			FcFontSetDestroy(foundFonts);
		}

		FcPatternDestroy(names[i]);
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

// If this returns true, the first font in fallbackFonts_ can be used as a fallback.
bool TextDrawerSDL::FindFallbackFonts(uint32_t missingGlyph, int ptSize) {
	// If we encounter a missing glyph, try to use one of the fallback fonts.
	for (int i = 0; i < fallbackFonts_.size(); i++) {
		TTF_Font *fallbackFont = fallbackFonts_[i];
		if (TTF_GlyphIsProvided32(fallbackFont, missingGlyph)) {
			fallbackFonts_.erase(fallbackFonts_.begin() + i);
			fallbackFonts_.insert(fallbackFonts_.begin(), fallbackFont);
			return true;
		}
	}

	// If none of the loaded fonts can handle it, load more fonts.
	for (int i = 0; i < fallbackFontPaths_.size(); i++) {
		std::string& fontPath = fallbackFontPaths_[i].first;
		int faceIndex = fallbackFontPaths_[i].second;

		TTF_Font *font = TTF_OpenFontIndex(fontPath.c_str(), ptSize, faceIndex);

		if (TTF_GlyphIsProvided32(font, missingGlyph)) {
			fallbackFonts_.insert(fallbackFonts_.begin(), font);
			fallbackFontPaths_.erase(fallbackFontPaths_.begin() + i);
			return true;
		} else {
			TTF_CloseFont(font);
		}
	}

	return false;
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
		ERROR_LOG(G3D, "Invalid font handle %08x", fontHandle);
	}
}

void TextDrawerSDL::MeasureString(const char *str, size_t len, float *w, float *h) {
	CacheKey key{ std::string(str, len), fontHash_ };

	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(key);
	if (iter != sizeCache_.end()) {
		entry = iter->second.get();
	} else {
		TTF_Font *font = fontMap_.find(fontHash_)->second;
		int ptSize = TTF_FontHeight(font) / 1.35;

		uint32_t missingGlyph = CheckMissingGlyph(key.text);
		
		if (missingGlyph && FindFallbackFonts(missingGlyph, ptSize)) {
			font = fallbackFonts_[0];
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

void TextDrawerSDL::MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align) {
	std::string toMeasure = std::string(str, len);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	if (wrap) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toMeasure, toMeasure.c_str(), rotated ? bounds.h : bounds.w, wrap);
	}

	TTF_Font *font = fontMap_.find(fontHash_)->second;
	int ptSize = TTF_FontHeight(font) / 1.35;
	uint32_t missingGlyph = CheckMissingGlyph(toMeasure);

	if (missingGlyph && FindFallbackFonts(missingGlyph, ptSize)) {
		font = fallbackFonts_[0];
	}

	std::vector<std::string> lines;
	SplitString(toMeasure, '\n', lines);

	int total_w = 0;
	int total_h = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		CacheKey key{ lines[i], fontHash_ };

		TextMeasureEntry *entry;
		auto iter = sizeCache_.find(key);
		if (iter != sizeCache_.end()) {
			entry = iter->second.get();
		} else {
			int width = 0;
			int height = 0;
			TTF_SizeUTF8(font, lines[i].c_str(), &width, &height);
			entry = new TextMeasureEntry();
			entry->width = width;
			entry->height = height;
			sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
		}

		entry->lastUsedFrame = frameCount_;
		if (total_w < entry->width) {
			total_w = entry->width;
		}
		total_h += TTF_FontLineSkip(font);
	}

	*w = total_w * fontScaleX_ * dpiScale_;
	*h = total_h * fontScaleY_ * dpiScale_;
}

void TextDrawerSDL::DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align) {
	using namespace Draw;
	if (!strlen(str))
		return;

	CacheKey key{ std::string(str), fontHash_ };
	target.Flush(true);

	TextStringEntry *entry;

	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		entry = iter->second.get();
		entry->lastUsedFrame = frameCount_;
	} else {
		DataFormat texFormat = Draw::DataFormat::R4G4B4A4_UNORM_PACK16;

		entry = new TextStringEntry();

		TextureDesc desc{};
		std::vector<uint8_t> bitmapData;
		DrawStringBitmap(bitmapData, *entry, texFormat, str, align);
		desc.initData.push_back(&bitmapData[0]);

		desc.type = TextureType::LINEAR2D;
		desc.format = texFormat;
		desc.width = entry->bmWidth;
		desc.height = entry->bmHeight;
		desc.depth = 1;
		desc.mipLevels = 1;
		desc.tag = "TextDrawer";
		entry->texture = draw_->CreateTexture(desc);
		cache_[key] = std::unique_ptr<TextStringEntry>(entry);
	}

	if (entry->texture) {
		draw_->BindTexture(0, entry->texture);
	}

	float w = entry->bmWidth * fontScaleX_ * dpiScale_;
	float h = entry->bmHeight * fontScaleY_ * dpiScale_;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);
	if (entry->texture) {
		target.DrawTexRect(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, color);
		target.Flush(true);
	}
}

void TextDrawerSDL::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, const char *str, int align) {
	if (!strlen(str)) {
		bitmapData.clear();
		return;
	}

	// Replace "&&" with "&"
	std::string processedStr = ReplaceAll(str, "&&", "&");

	// If a string includes only newlines, SDL2_ttf will refuse to render it
	// thinking it is empty. Add a space to avoid that. 
	bool isAllNewline = processedStr.find_first_not_of('\n') == std::string::npos;

	if (isAllNewline) {
		processedStr.push_back(' ');
	}

	TTF_Font *font = fontMap_.find(fontHash_)->second;
	int ptSize = TTF_FontHeight(font) / 1.35;

	uint32_t missingGlyph = CheckMissingGlyph(processedStr);

	if (missingGlyph && FindFallbackFonts(missingGlyph, ptSize)) {
		font = fallbackFonts_[0];
	}

	if (align & ALIGN_HCENTER)
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_CENTER);
	else if (align & ALIGN_RIGHT)
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_RIGHT);
	else
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_LEFT);

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
}

void TextDrawerSDL::OncePerFrame() {
	// Reset everything if DPI changes
	float newDpiScale = CalculateDPIScale();
	if (newDpiScale != dpiScale_) {
		dpiScale_ = newDpiScale;
		ClearCache();
	}

	// Drop old strings. Use a prime number to reduce clashing with other rhythms
	if (frameCount_ % 23 == 0) {
		for (auto iter = cache_.begin(); iter != cache_.end();) {
			if (frameCount_ - iter->second->lastUsedFrame > 100) {
				if (iter->second->texture)
					iter->second->texture->Release();
				cache_.erase(iter++);
			} else {
				iter++;
			}
		}

		for (auto iter = sizeCache_.begin(); iter != sizeCache_.end(); ) {
			if (frameCount_ - iter->second->lastUsedFrame > 100) {
				sizeCache_.erase(iter++);
			} else {
				iter++;
			}
		}
	}
}

void TextDrawerSDL::ClearCache() {
	for (auto &iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
	}
	cache_.clear();
	sizeCache_.clear();

	for (auto iter : fontMap_) {
		TTF_CloseFont(iter.second);
	}
	for (auto iter : fallbackFonts_) {
		TTF_CloseFont(iter);
	}
	fontMap_.clear();
	fallbackFonts_.clear();
	fontHash_ = 0;
}

#endif
