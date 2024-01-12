#include "ppsspp_config.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_win.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"

#if defined(_WIN32) && !defined(USING_QT_UI) && !PPSSPP_PLATFORM(UWP)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

enum {
	MAX_TEXT_WIDTH = 4096,
	MAX_TEXT_HEIGHT = 512
};

class TextDrawerFontContext {
public:
	~TextDrawerFontContext() {
		Destroy();
	}

	void Create() {
		if (hFont) {
			Destroy();
		}
		// We apparently specify all font sizes in pts (1pt = 1.33px), so divide by only 72 for pixels.
		int nHeight = -MulDiv(height, (int)(96.0f * (1.0f / dpiScale)), 72);
		hFont = CreateFont(nHeight, 0, 0, 0, bold, 0,
			FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
			VARIABLE_PITCH, fname.c_str());
	}
	void Destroy() {
		DeleteObject(hFont);
		hFont = 0;
	}

	HFONT hFont;
	std::wstring fname;
	int height;
	int bold;
	float dpiScale;
};

struct TextDrawerContext {
	HDC hDC;
	HBITMAP hbmBitmap;
	int *pBitmapBits;
};

TextDrawerWin32::TextDrawerWin32(Draw::DrawContext *draw) : TextDrawer(draw), ctx_(nullptr) {
	ctx_ = new TextDrawerContext();
	ctx_->hDC = CreateCompatibleDC(NULL);

	BITMAPINFO bmi;
	ZeroMemory(&bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = MAX_TEXT_WIDTH;
	bmi.bmiHeader.biHeight = -MAX_TEXT_HEIGHT;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biBitCount = 32;

	ctx_->hbmBitmap = CreateDIBSection(ctx_->hDC, &bmi, DIB_RGB_COLORS, (VOID**)&ctx_->pBitmapBits, NULL, 0);
	_assert_(ctx_->hbmBitmap != nullptr);
	SetMapMode(ctx_->hDC, MM_TEXT);

	SelectObject(ctx_->hDC, ctx_->hbmBitmap);
}

TextDrawerWin32::~TextDrawerWin32() {
	ClearCache();

	fontMap_.clear();

	DeleteObject(ctx_->hbmBitmap);
	DeleteDC(ctx_->hDC);
	delete ctx_;
}

uint32_t TextDrawerWin32::SetFont(const char *fontName, int size, int flags) {
	uint32_t fontHash = fontName ? hash::Adler32((const uint8_t *)fontName, strlen(fontName)) : 0;
	fontHash ^= size;
	fontHash ^= flags << 10;

	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHash;
		return fontHash;
	}

	std::wstring fname;
	if (fontName)
		fname = ConvertUTF8ToWString(fontName);
	else
		fname = L"Tahoma";

	TextDrawerFontContext *font = new TextDrawerFontContext();
	font->bold = FW_LIGHT;
	font->height = size;
	font->fname = fname;
	font->dpiScale = dpiScale_;
	font->Create();

	fontMap_[fontHash] = std::unique_ptr<TextDrawerFontContext>(font);
	fontHash_ = fontHash;
	return fontHash;
}

void TextDrawerWin32::SetFont(uint32_t fontHandle) {
	auto iter = fontMap_.find(fontHandle);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHandle;
	}
}

void TextDrawerWin32::MeasureString(const char *str, size_t len, float *w, float *h) {
	CacheKey key{ std::string(str, len), fontHash_ };
	
	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(key);
	if (iter != sizeCache_.end()) {
		entry = iter->second.get();
	} else {
		auto iter = fontMap_.find(fontHash_);
		if (iter != fontMap_.end()) {
			SelectObject(ctx_->hDC, iter->second->hFont);
		}

		std::string toMeasure = ReplaceAll(std::string(str, len), "&&", "&");

		std::vector<std::string_view> lines;
		SplitString(toMeasure, '\n', lines);

		int extW = 0, extH = 0;
		for (auto &line : lines) {
			SIZE size;
			std::wstring wstr = ConvertUTF8ToWString(line);
			GetTextExtentPoint32(ctx_->hDC, wstr.c_str(), (int)wstr.size(), &size);

			if (size.cx > extW)
				extW = size.cx;
			extH += size.cy;
		}

		entry = new TextMeasureEntry();
		entry->width = extW;
		entry->height = extH;
		sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
	}

	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_ * dpiScale_;
	*h = entry->height * fontScaleY_ * dpiScale_;
}

void TextDrawerWin32::MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align) {
	auto iter = fontMap_.find(fontHash_);
	if (iter != fontMap_.end()) {
		SelectObject(ctx_->hDC, iter->second->hFont);
	}

	std::string toMeasure = std::string(str, len);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	if (wrap) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toMeasure, toMeasure.c_str(), rotated ? bounds.h : bounds.w, wrap);
	}

	TEXTMETRIC metrics{};
	GetTextMetrics(ctx_->hDC, &metrics);

	std::vector<std::string_view> lines;
	SplitString(toMeasure, '\n', lines);
	int total_w = 0;
	int total_h = 0;
	CacheKey key{ "", fontHash_};
	for (size_t i = 0; i < lines.size(); i++) {
		key.text = lines[i];
		TextMeasureEntry *entry;
		auto iter = sizeCache_.find(key);
		if (iter != sizeCache_.end()) {
			entry = iter->second.get();
		} else {
			SIZE size;
			std::wstring wstr = ConvertUTF8ToWString(lines[i].empty() ? " " : ReplaceAll(lines[i], "&&", "&"));
			GetTextExtentPoint32(ctx_->hDC, wstr.c_str(), (int)wstr.size(), &size);

			entry = new TextMeasureEntry();
			entry->width = size.cx;
			entry->height = size.cy;
			sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
		}
		entry->lastUsedFrame = frameCount_;

		if (total_w < entry->width) {
			total_w = entry->width;
		}
		int h = i == lines.size() - 1 ? entry->height : metrics.tmHeight + metrics.tmExternalLeading;
		total_h += h;
	}

	*w = total_w * fontScaleX_ * dpiScale_;
	*h = total_h * fontScaleY_ * dpiScale_;
}

void TextDrawerWin32::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, const char *str, int align) {
	if (!strlen(str)) {
		bitmapData.clear();
		return;
	}

	std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(str, "\n", "\r\n"));
	SIZE size;

	auto iter = fontMap_.find(fontHash_);
	if (iter != fontMap_.end()) {
		SelectObject(ctx_->hDC, iter->second->hFont);
	}
	// Set text properties
	SetTextColor(ctx_->hDC, 0xFFFFFF);
	SetBkColor(ctx_->hDC, 0);
	SetTextAlign(ctx_->hDC, TA_TOP);

	// This matters for multi-line text - DT_CENTER is horizontal only.
	UINT dtAlign = (align & ALIGN_HCENTER) == 0 ? DT_LEFT : DT_CENTER;

	RECT textRect = { 0 };
	DrawTextExW(ctx_->hDC, (LPWSTR)wstr.c_str(), (int)wstr.size(), &textRect, DT_HIDEPREFIX | DT_TOP | dtAlign | DT_CALCRECT, 0);
	size.cx = textRect.right;
	size.cy = textRect.bottom;

	if (size.cx > MAX_TEXT_WIDTH)
		size.cx = MAX_TEXT_WIDTH;
	if (size.cy > MAX_TEXT_HEIGHT)
		size.cy = MAX_TEXT_HEIGHT;
	// Prevent zero-sized textures, which can occur. Not worth to avoid
	// creating the texture altogether in this case. One example is a string
	// containing only '\r\n', see issue #10764.
	if (size.cx == 0)
		size.cx = 1;
	if (size.cy == 0)
		size.cy = 1;

	entry.texture = nullptr;
	entry.width = size.cx;
	entry.height = size.cy;
	entry.bmWidth = (size.cx + 3) & ~3;
	entry.bmHeight = (size.cy + 3) & ~3;
	entry.lastUsedFrame = frameCount_;

	RECT rc = { 0 };
	rc.right = entry.bmWidth;
	rc.bottom = entry.bmHeight;
	FillRect(ctx_->hDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
	DrawTextExW(ctx_->hDC, (LPWSTR)wstr.c_str(), (int)wstr.size(), &rc, DT_HIDEPREFIX | DT_TOP | dtAlign, 0);

	// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
	// because we need white. Well, we could using swizzle, but not all our backends support that.
	if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM || texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint32_t));
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff);
				bitmapData32[entry.bmWidth * y + x] = (bAlpha << 24) | 0x00ffffff;
			}
		}
	} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)((ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff) >> 4);
				bitmapData16[entry.bmWidth * y + x] = (bAlpha) | 0xfff0;
			}
		}
	} else if (texFormat == Draw::DataFormat::A4R4G4B4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)((ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff) >> 4);
				bitmapData16[entry.bmWidth * y + x] = (bAlpha << 12) | 0x0fff;
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff;
				bitmapData[entry.bmWidth * y + x] = bAlpha;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}
}

void TextDrawerWin32::DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align) {
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
		DataFormat texFormat;
		// For our purposes these are equivalent, so just choose the supported one. D3D can emulate them.
		if (draw_->GetDataFormatSupport(Draw::DataFormat::A4R4G4B4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
		else if (draw_->GetDataFormatSupport(Draw::DataFormat::R4G4B4A4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
		else if (draw_->GetDataFormatSupport(Draw::DataFormat::B4G4R4A4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::B4G4R4A4_UNORM_PACK16;
		else
			texFormat = Draw::DataFormat::R8G8B8A8_UNORM;

		entry = new TextStringEntry();

		// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
		// because we need white. Well, we could using swizzle, but not all our backends support that.
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

	// Okay, the texture is bound, let's draw.
	float w = entry->width * fontScaleX_ * dpiScale_;
	float h = entry->height * fontScaleY_ * dpiScale_;
	float u = entry->width / (float)entry->bmWidth;
	float v = entry->height / (float)entry->bmHeight;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);
	if (entry->texture) {
		target.DrawTexRect(x, y, x + w, y + h, 0.0f, 0.0f, u, v, color);
		target.Flush(true);
	}
}

void TextDrawerWin32::RecreateFonts() {
	for (auto &iter : fontMap_) {
		iter.second->dpiScale = dpiScale_;
		iter.second->Create();
	}
}

void TextDrawerWin32::ClearCache() {
	for (auto &iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
	}
	cache_.clear();
	sizeCache_.clear();
}

void TextDrawerWin32::OncePerFrame() {
	frameCount_++;
	// If DPI changed (small-mode, future proper monitor DPI support), drop everything.
	float newDpiScale = CalculateDPIScale();
	if (newDpiScale != dpiScale_) {
		dpiScale_ = newDpiScale;
		ClearCache();
		RecreateFonts();
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

#endif
