#include "base/logging.h"
#include "base/stringutil.h"
#include "thin3d/thin3d.h"
#include "util/hash/hash.h"
#include "util/text/wrap_text.h"
#include "util/text/utf8.h"
#include "gfx_es2/draw_text.h"

#ifdef USING_QT_UI
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QFontMetrics>
#include <QtOpenGL/QGLWidget>
#endif

class TextDrawerWordWrapper : public WordWrapper {
public:
	TextDrawerWordWrapper(TextDrawer *drawer, const char *str, float maxW) : WordWrapper(str, maxW), drawer_(drawer) {
	}

protected:
	float MeasureWidth(const char *str, size_t bytes) override;

	TextDrawer *drawer_;
};

float TextDrawerWordWrapper::MeasureWidth(const char *str, size_t bytes) {
	float w, h;
	drawer_->MeasureString(str, bytes, &w, &h);
	return w;
}

#if defined(_WIN32) && !defined(USING_QT_UI)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

enum {
	MAX_TEXT_WIDTH = 1024,
	MAX_TEXT_HEIGHT = 512
};

struct TextDrawerFontContext {
	HFONT hFont;
};

struct TextDrawerContext {
	HDC hDC;
	HBITMAP hbmBitmap;
	int *pBitmapBits;
};

TextDrawer::TextDrawer(Thin3DContext *thin3d) : thin3d_(thin3d), ctx_(NULL) {
	fontScaleX_ = 1.0f;
	fontScaleY_ = 1.0f;

	ctx_ = new TextDrawerContext();
	ctx_->hDC = CreateCompatibleDC(NULL);

	BITMAPINFO bmi;
	ZeroMemory( &bmi.bmiHeader,  sizeof(BITMAPINFOHEADER) );
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = MAX_TEXT_WIDTH;
	bmi.bmiHeader.biHeight      = -MAX_TEXT_HEIGHT;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biBitCount    = 32;

	ctx_->hbmBitmap = CreateDIBSection(ctx_->hDC, &bmi, DIB_RGB_COLORS, (VOID**)&ctx_->pBitmapBits, NULL, 0);
	SetMapMode(ctx_->hDC, MM_TEXT);

	SelectObject(ctx_->hDC, ctx_->hbmBitmap);
}

TextDrawer::~TextDrawer() {
	for (auto iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
		delete iter.second;
	}
	cache_.clear();

	for (auto iter : sizeCache_) {
		delete iter.second;
	}
	sizeCache_.clear();

	for (auto iter = fontMap_.begin(); iter != fontMap_.end(); ++iter) {
		DeleteObject(iter->second->hFont);
		delete iter->second;
	}
	fontMap_.clear();

	DeleteObject(ctx_->hbmBitmap);
	DeleteDC(ctx_->hDC);

	delete ctx_;
}

uint32_t TextDrawer::SetFont(const char *fontName, int size, int flags) {
	uint32_t fontHash = fontName ? hash::Fletcher((const uint8_t *)fontName, strlen(fontName)) : 0;
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

	float textScale = 1.0f;

	INT nHeight = -MulDiv( size, (INT)(GetDeviceCaps(ctx_->hDC, LOGPIXELSY) * textScale), 72 );
	int dwBold = FW_LIGHT; ///FW_BOLD
	font->hFont = CreateFont(nHeight, 0, 0, 0, dwBold, 0,
		FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
		VARIABLE_PITCH, fname.c_str());
	fontMap_[fontHash] = font;
	fontHash_ = fontHash;
	return fontHash;
}

void TextDrawer::SetFont(uint32_t fontHandle) {
	auto iter = fontMap_.find(fontHandle);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHandle;
	}
}

void TextDrawer::MeasureString(const char *str, float *w, float *h) {
	MeasureString(str, strlen(str), w, h);
}

void TextDrawer::MeasureString(const char *str, size_t len, float *w, float *h) {
	uint32_t stringHash = hash::Fletcher((const uint8_t *)str, len);
	uint32_t entryHash = stringHash ^ fontHash_;

	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(entryHash);
	if (iter != sizeCache_.end()) {
		entry = iter->second;
	} else {
		auto iter = fontMap_.find(fontHash_);
		if (iter != fontMap_.end()) {
			SelectObject(ctx_->hDC, iter->second->hFont);
		}

		SIZE size;
		std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(std::string(str, len), "\n", "\r\n"));
		GetTextExtentPoint32(ctx_->hDC, wstr.c_str(), (int)wstr.size(), &size);

		entry = new TextMeasureEntry();
		entry->width = size.cx;
		entry->height = size.cy;
		sizeCache_[entryHash] = entry;
	}

	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_;
	*h = entry->height * fontScaleY_;
}

void TextDrawer::MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align) {
	auto iter = fontMap_.find(fontHash_);
	if (iter != fontMap_.end()) {
		SelectObject(ctx_->hDC, iter->second->hFont);
	}

	std::string toMeasure = std::string(str, len);
	if (align & FLAG_WRAP_TEXT) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toMeasure, toMeasure.c_str(), rotated ? bounds.h : bounds.w);
	}

	std::vector<std::string> lines;
	SplitString(toMeasure, '\n', lines);
	float total_w = 0.0f;
	float total_h = 0.0f;
	for (size_t i = 0; i < lines.size(); i++) {
		uint32_t stringHash = hash::Fletcher((const uint8_t *)&lines[i][0], lines[i].length());
		uint32_t entryHash = stringHash ^ fontHash_;

		TextMeasureEntry *entry;
		auto iter = sizeCache_.find(entryHash);
		if (iter != sizeCache_.end()) {
			entry = iter->second;
		} else {
			SIZE size;
			std::wstring wstr = ConvertUTF8ToWString(lines[i].length() == 0 ? " " : lines[i]);
			GetTextExtentPoint32(ctx_->hDC, wstr.c_str(), (int)wstr.size(), &size);

			entry = new TextMeasureEntry();
			entry->width = size.cx;
			entry->height = size.cy;
			sizeCache_[entryHash] = entry;
		}
		entry->lastUsedFrame = frameCount_;

		if (total_w < entry->width * fontScaleX_) {
			total_w = entry->width * fontScaleX_;
		}
		total_h += entry->height * fontScaleY_;
	}
	*w = total_w;
	*h = total_h;
}

void TextDrawer::DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align) {
	if (!strlen(str))
		return;

	uint32_t stringHash = hash::Fletcher((const uint8_t *)str, strlen(str));
	uint32_t entryHash = stringHash ^ fontHash_ ^ (align << 24);

	target.Flush(true);

	TextStringEntry *entry;

	auto iter = cache_.find(entryHash);
	if (iter != cache_.end()) {
		entry = iter->second;
		entry->lastUsedFrame = frameCount_;
	} else {
		// Render the string to our bitmap and save to a GL texture.
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

		RECT textRect = {0};
		DrawTextExW(ctx_->hDC, (LPWSTR)wstr.c_str(), (int)wstr.size(), &textRect, DT_HIDEPREFIX|DT_TOP|dtAlign|DT_CALCRECT, 0);
		size.cx = textRect.right;
		size.cy = textRect.bottom;

		// GetTextExtentPoint32(ctx_->hDC, wstr.c_str(), (int)wstr.size(), &size);
		RECT rc = {0};
		rc.right = size.cx + 4;
		rc.bottom = size.cy + 4;
		FillRect(ctx_->hDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
		//ExtTextOut(ctx_->hDC, 0, 0, ETO_OPAQUE | ETO_CLIPPED, NULL, wstr.c_str(), (int)wstr.size(), NULL);
		DrawTextExW(ctx_->hDC, (LPWSTR)wstr.c_str(), (int)wstr.size(), &rc, DT_HIDEPREFIX|DT_TOP|dtAlign, 0);

		if (size.cx > MAX_TEXT_WIDTH)
			size.cx = MAX_TEXT_WIDTH;
		if (size.cy > MAX_TEXT_HEIGHT)
			size.cy = MAX_TEXT_HEIGHT;

		entry = new TextStringEntry();
		entry->width = size.cx;
		entry->height = size.cy;
		entry->bmWidth = (size.cx + 3) & ~3;
		entry->bmHeight = (size.cy + 3) & ~3;
		entry->lastUsedFrame = frameCount_;
		entry->texture = thin3d_->CreateTexture(LINEAR2D, RGBA4444, entry->bmWidth, entry->bmHeight, 1, 1);

		// Convert the bitmap to a gl-compatible array of pixels.
		uint16_t *bitmapData = new uint16_t[entry->bmWidth * entry->bmHeight];
		for (int y = 0; y < entry->bmHeight; y++) {
			for (int x = 0; x < entry->bmWidth; x++) {
				BYTE bAlpha = (BYTE)((ctx_->pBitmapBits[MAX_TEXT_WIDTH * y + x] & 0xff) >> 4);
				bitmapData[entry->bmWidth * y + x] = (bAlpha) | 0xfff0;
			}
		}
		entry->texture->SetImageData(0, 0, 0, entry->bmWidth, entry->bmHeight, 1, 0, entry->bmWidth * 2, (const uint8_t *)bitmapData);
		entry->texture->Finalize(0);
		delete [] bitmapData;

		cache_[entryHash] = entry;
	}

	thin3d_->SetTexture(0, entry->texture);

	// Okay, the texture is bound, let's draw.
	float w = entry->bmWidth * fontScaleX_;
	float h = entry->bmHeight * fontScaleY_;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);
	target.DrawTexRect(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, color);
	target.Flush(true);
}

#else

TextDrawer::TextDrawer(Thin3DContext *thin3d) : thin3d_(thin3d), ctx_(NULL) {
	fontScaleX_ = 1.0f;
	fontScaleY_ = 1.0f;
}

TextDrawer::~TextDrawer() {
	for (auto iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
		delete iter.second;
	}
	cache_.clear();

	for (auto iter : sizeCache_) {
		delete iter.second;
	}
	sizeCache_.clear();
}

uint32_t TextDrawer::SetFont(const char *fontName, int size, int flags) {
#ifdef USING_QT_UI
	// We will only use the default font
	uint32_t fontHash = 0; //hash::Fletcher((const uint8_t *)fontName, strlen(fontName));
	fontHash ^= size;
	fontHash ^= flags << 10;

	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHash;
		return fontHash;
	}

	QFont* font = new QFont();
	font->setPixelSize(size + 6);
	fontMap_[fontHash] = font;
	fontHash_ = fontHash;
	return fontHash;
#else
	ELOG("System fonts not supported on this platform");
	return 0;
#endif
}

void TextDrawer::SetFont(uint32_t fontHandle) {

}

void TextDrawer::MeasureString(const char *str, float *w, float *h) {
	MeasureString(str, strlen(str), w, h);
}

void TextDrawer::MeasureString(const char *str, size_t len, float *w, float *h) {
#ifdef USING_QT_UI
	QFont* font = fontMap_.find(fontHash_)->second;
	QFontMetrics fm(*font);
	QSize size = fm.size(0, QString::fromUtf8(str, (int)len));
	*w = (float)size.width() * fontScaleX_;
	*h = (float)size.height() * fontScaleY_;
#else
	*w = 0;
	*h = 0;
#endif
}

void TextDrawer::MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align) {
	std::string toMeasure = std::string(str, len);
	if (align & FLAG_WRAP_TEXT) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toMeasure, toMeasure.c_str(), rotated ? bounds.h : bounds.w);
	}

#ifdef USING_QT_UI
	QFont* font = fontMap_.find(fontHash_)->second;
	QFontMetrics fm(*font);
	QSize size = fm.size(0, QString::fromUtf8(toMeasure.c_str(), (int)toMeasure.size()));
	*w = (float)size.width() * fontScaleX_;
	*h = (float)size.height() * fontScaleY_;
#else
	*w = 0;
	*h = 0;
#endif
}

void TextDrawer::DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align) {
	if (!strlen(str))
		return;

#ifdef USING_QT_UI
	uint32_t stringHash = hash::Fletcher((const uint8_t *)str, strlen(str));
	uint32_t entryHash = stringHash ^ fontHash_ ^ (align << 24);
	
	target.Flush(true);

	TextStringEntry *entry;

	auto iter = cache_.find(entryHash);
	if (iter != cache_.end()) {
		entry = iter->second;
		entry->lastUsedFrame = frameCount_;
		thin3d_->SetTexture(0, entry->texture);
	} else {
		QFont *font = fontMap_.find(fontHash_)->second;
		QFontMetrics fm(*font);
		QSize size = fm.size(0, QString::fromUtf8(str));
		QImage image((size.width() + 3) & ~ 3, (size.height() + 3) & ~ 3, QImage::Format_ARGB32_Premultiplied);
		if (image.isNull()) {
			return;
		}
		image.fill(0);

		QPainter painter;
		painter.begin(&image);
		painter.setFont(*font);
		painter.setPen(color);
		// TODO: Involve ALIGN_HCENTER (bounds etc.)
		painter.drawText(image.rect(), Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8(str).replace("&&", "&"));
		painter.end();

		entry = new TextStringEntry();
		entry->bmWidth = entry->width = image.width();
		entry->bmHeight = entry->height = image.height();
		entry->lastUsedFrame = frameCount_;
		entry->texture = thin3d_->CreateTexture(LINEAR2D, RGBA4444, entry->bmWidth, entry->bmHeight, 1, 0);

		uint16_t *bitmapData = new uint16_t[entry->bmWidth * entry->bmHeight];
		for (int x = 0; x < entry->bmWidth; x++) {
			for (int y = 0; y < entry->bmHeight; y++) {
				bitmapData[entry->bmWidth * y + x] = 0xfff0 | image.pixel(x, y) >> 28;
			}
		}
		entry->texture->SetImageData(0, 0, 0, entry->bmWidth, entry->bmHeight, 1, 0, entry->bmWidth * 2, (const uint8_t *)bitmapData);
		entry->texture->Finalize(0);

		delete [] bitmapData;

		cache_[entryHash] = entry;
	}
	float w = entry->bmWidth * fontScaleX_;
	float h = entry->bmHeight * fontScaleY_;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);
	target.DrawTexRect(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, color);
	target.Flush(true);
#endif
}

#endif

void TextDrawer::WrapString(std::string &out, const char *str, float maxW) {
	TextDrawerWordWrapper wrapper(this, str, maxW);
	out = wrapper.Wrapped();
}

void TextDrawer::SetFontScale(float xscale, float yscale) {
	fontScaleX_ = xscale;
	fontScaleY_ = yscale;
}

void TextDrawer::DrawStringRect(DrawBuffer &target, const char *str, const Bounds &bounds, uint32_t color, int align) {
	float x = bounds.x;
	float y = bounds.y;
	if (align & ALIGN_HCENTER) {
		x = bounds.centerX();
	} else if (align & ALIGN_RIGHT) {
		x = bounds.x2();
	}
	if (align & ALIGN_VCENTER) {
		y = bounds.centerY();
	} else if (align & ALIGN_BOTTOM) {
		y = bounds.y2();
	}

	std::string toDraw = str;
	if (align & FLAG_WRAP_TEXT) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toDraw, str, rotated ? bounds.h : bounds.w);
	}

	DrawString(target, toDraw.c_str(), x, y, color, align);
}

void TextDrawer::OncePerFrame() {
	frameCount_++;
	// Use a prime number to reduce clashing with other rhythms
	if (frameCount_ % 23 == 0) {
		for (auto iter = cache_.begin(); iter != cache_.end();) {
			if (frameCount_ - iter->second->lastUsedFrame > 100) {
				if (iter->second->texture)
					iter->second->texture->Release();
				delete iter->second;
				cache_.erase(iter++);
			} else {
				iter++;
			}
		}

		for (auto iter = sizeCache_.begin(); iter != sizeCache_.end(); ) {
			if (frameCount_ - iter->second->lastUsedFrame > 100) {
				delete iter->second;
				sizeCache_.erase(iter++);
			} else {
				iter++;
			}
		}
	}
}
