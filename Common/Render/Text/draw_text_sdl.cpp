#include "ppsspp_config.h"

#include <string>
#include <regex>

#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_sdl.h"

#if defined(USE_SDL2_TTF)

#include "SDL2/SDL.h"
#include "SDL2/SDL_ttf.h"

const uint32_t MAX_WIDTH = 4096;

TextDrawerSDL::TextDrawerSDL(Draw::DrawContext *draw): TextDrawer(draw) {
	if (TTF_Init() < 0) {
		ERROR_LOG(G3D, "Unable to initialize SDL2_ttf");
	}

	dpiScale_ = CalculateDPIScale();
}

TextDrawerSDL::~TextDrawerSDL() {
	ClearCache();
	TTF_Quit();
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

		int width = 0;
		int height = 0;
		TTF_SizeUTF8(font, str, &width, &height);

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

	TTF_Font* font = fontMap_.find(fontHash_)->second;

	int width = 0;
	int height = 0;
	TTF_SizeUTF8(font, str, &width, &height);

	*w = width * fontScaleX_ * dpiScale_;
	*h = height * fontScaleY_ * dpiScale_;
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

	std::string processed_str(str);
	processed_str = std::regex_replace(processed_str, std::regex("&&"), "&");

	TTF_Font *font = fontMap_.find(fontHash_)->second;

	if (align & ALIGN_HCENTER)
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_CENTER);
	else if (align & ALIGN_LEFT)
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_LEFT);
	else if (align & ALIGN_RIGHT)
		TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_RIGHT);

	SDL_Color fgColor = { 0xFF, 0xFF, 0xFF, 0xFF };
	SDL_Surface *text = TTF_RenderUTF8_Blended(font, processed_str.c_str(), fgColor);
	SDL_LockSurface(text);

	entry.texture = nullptr;
	entry.bmWidth = entry.width = text->pitch / sizeof(uint32_t);
	entry.bmHeight = entry.height = text->h;
	entry.lastUsedFrame = frameCount_;

	uint32_t *imageData = (uint32_t *) text->pixels;

	if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];

		for (int x = 0; x < entry.bmWidth; x++) {
			for (int y = 0; y < entry.bmHeight; y++) {
				uint64_t index = entry.bmWidth * y + x;
				bitmapData16[entry.bmWidth * y + x] = 0xfff0 | (imageData[index] >> 28);
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int x = 0; x < entry.bmWidth; x++) {
			for (int y = 0; y < entry.bmHeight; y++) {
				uint64_t index = text->pitch / sizeof(uint32_t) * y + x;
				bitmapData[entry.bmWidth * y + x] = imageData[index] >> 24;
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
	fontMap_.clear();
	fontHash_ = 0;
}

#endif
