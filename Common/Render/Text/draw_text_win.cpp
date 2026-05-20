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
	explicit TextDrawerFontContext(const FontStyle &_style, float _dpiScale) : hFont(0), style(_style), dpiScale(_dpiScale) {
		FontStyleFlags styleFlags{};
		std::string fontName = GetFontNameForFontStyle(style, &styleFlags);
		if (fontName.empty()) {
			// Shouldn't happen.
			fontName = "Tahoma";
		}

		int weight = FW_NORMAL;
		if (styleFlags & FontStyleFlags::Bold) {
			weight = FW_BOLD;
		}
		if (styleFlags & FontStyleFlags::Light) {
			weight = FW_LIGHT;
		}

		bool italic = (styleFlags & FontStyleFlags::Italic);
		int height = style.sizePts;

		if (hFont) {
			Destroy();
		}
		// We apparently specify all font sizes in pts (1pt = 1.33px), so divide by only 72 for pixels.
		int nHeight = -MulDiv(height, (int)(96.0f * (1.0f / dpiScale)), 72);
		hFont = CreateFont(nHeight, 0, 0, 0, weight, italic,
			FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
			VARIABLE_PITCH, ConvertUTF8ToWString(fontName).c_str());
	}

	~TextDrawerFontContext() {
		Destroy();
	}

	void Destroy() {
		DeleteObject(hFont);
		hFont = 0;
	}

	HFONT hFont = 0;
	FontStyle style;
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

	// Load the font files (pass the all flags so we get all filenames);
	std::vector<std::string> fonts = GetAllFontFilenames();
	for (const auto &iter : fonts) {
		std::string fn = "assets/" + iter + ".ttf";
		int numFontsAdded = AddFontResourceEx(ConvertUTF8ToWString(fn).c_str(), FR_PRIVATE, NULL);
		if (numFontsAdded == 0) {
			ERROR_LOG(Log::G3D, "Failed to add font resource from %s", fn.c_str());
		}
	}

	BITMAPINFO bmi{};
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
	ClearFonts();

	// Unload the fonts.
	std::vector<std::string> fonts = GetAllFontFilenames();
	for (const auto &iter : fonts) {
		std::string fn = "assets/" + iter + ".ttf";
		RemoveFontResourceEx(ConvertUTF8ToWString(fn).c_str(), FR_PRIVATE, NULL);
	}

	DeleteObject(ctx_->hbmBitmap);
	DeleteDC(ctx_->hDC);
	delete ctx_;
}

void TextDrawerWin32::SetOrCreateFont(const FontStyle &style) {
	auto iter = fontMap_.find(style);
	if (iter != fontMap_.end()) {
		fontStyle_ = style;
		return;
	}

	fontMap_[style] = std::make_unique<TextDrawerFontContext>(style, dpiScale_);
	fontStyle_ = style;
}

void TextDrawerWin32::MeasureStringInternal(std::string_view str, float *w, float *h) {
	auto iter = fontMap_.find(fontStyle_);
	if (iter != fontMap_.end()) {
		SelectObject(ctx_->hDC, iter->second->hFont);
	} else {
		ERROR_LOG(Log::G3D, "Failed to measure string");
		return;
	}

	std::vector<std::string_view> lines;
	SplitString(str, '\n', lines);

	int extW = 0, extH = 0;
	for (auto &line : lines) {
		SIZE size;
		std::wstring wstr = ConvertUTF8ToWString(line);
		if (wstr.empty() && lines.size() > 1) {
			// Measure empty lines as if it was a space.
			wstr = L" ";
		}
		GetTextExtentPoint32(ctx_->hDC, wstr.c_str(), (int)wstr.size(), &size);
		if (size.cx > extW)
			extW = size.cx;
		extH += size.cy;
	}

	*w = extW;
	*h = extH;
}

bool TextDrawerWin32::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}
	std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(str, "\n", "\r\n"));

	auto iter = fontMap_.find(fontStyle_);
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
	DrawTextExW(ctx_->hDC, (LPWSTR)wstr.c_str(), (int)wstr.size(), &textRect, DT_NOPREFIX | DT_TOP | dtAlign | DT_CALCRECT, 0);
	SIZE size;
	size.cx = textRect.right;
	size.cy = textRect.bottom;

	if (size.cx > MAX_TEXT_WIDTH)
		size.cx = MAX_TEXT_WIDTH;
	if (size.cy > MAX_TEXT_HEIGHT)
		size.cy = MAX_TEXT_HEIGHT;

	if (size.cx == 0 || size.cy == 0) {
		// Don't draw zero-sized textures.
		WARN_LOG(Log::G3D, "Text '%.*s' caused a zero size image", (int)str.length(), str.data());
		return false;
	}

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
	DrawTextExW(ctx_->hDC, (LPWSTR)wstr.c_str(), (int)wstr.size(), &rc, DT_NOPREFIX | DT_TOP | dtAlign, 0);

	// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
	// because we need white. Well, we could using swizzle, but not all our backends support that.
	if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM || texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint32_t));
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff);
				bitmapData32[entry.bmWidth * y + x] = AlphaToPremul8888(bAlpha);
			}
		}
	} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff);
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(bAlpha);
			}
		}
	} else if (texFormat == Draw::DataFormat::A4R4G4B4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff);
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(bAlpha);
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
	return true;
}

void TextDrawerWin32::ClearFonts() {
	for (auto &iter : fontMap_) {
		iter.second->Destroy();
	}
	fontMap_.clear();
}

#endif
