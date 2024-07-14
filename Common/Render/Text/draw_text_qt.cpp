#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_qt.h"

#include "Common/Log.h"

#if defined(USING_QT_UI)

#include <QtGui/QFont>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QFontMetrics>
#include <QtOpenGL/QGLWidget>

TextDrawerQt::TextDrawerQt(Draw::DrawContext *draw) : TextDrawer(draw) {}

TextDrawerQt::~TextDrawerQt() {
	ClearCache();
	ClearFonts();
}

uint32_t TextDrawerQt::SetFont(const char *fontName, int size, int flags) {
	uint32_t fontHash = fontName && strlen(fontName) ? hash::Adler32((const uint8_t *)fontName, strlen(fontName)) : 0;
	fontHash ^= size;
	fontHash ^= flags << 10;

	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHash;
		return fontHash;
	}

	QFont *font = fontName ? new QFont(fontName) : new QFont();
	font->setPixelSize((int)((size + 6) / dpiScale_));
	fontMap_[fontHash] = font;
	fontHash_ = fontHash;
	return fontHash;
}

void TextDrawerQt::SetFont(uint32_t fontHandle) {
	uint32_t fontHash = fontHandle;
	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHandle;
	} else {
		ERROR_LOG(Log::G3D, "Invalid font handle %08x", fontHandle);
	}
}

void TextDrawerQt::MeasureString(std::string_view str, float *w, float *h) {
	CacheKey key{ std::string(str), fontHash_ };

	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(key);
	if (iter != sizeCache_.end()) {
		entry = iter->second.get();
	} else {
		QFont* font = fontMap_.find(fontHash_)->second;
		QFontMetrics fm(*font);
		QSize size = fm.size(0, QString::fromUtf8(str.data(), str.length()));

		entry = new TextMeasureEntry();
		entry->width = size.width();
		entry->height = size.height();
		sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
	}

	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_ * dpiScale_;
	*h = entry->height * fontScaleY_ * dpiScale_;
}

bool TextDrawerQt::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	_dbg_assert_(!fullColor);

	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	QFont *font = fontMap_.find(fontHash_)->second;
	QFontMetrics fm(*font);
	QSize size = fm.size(0, QString::fromUtf8(str.data(), str.length()));
	QImage image((size.width() + 3) & ~3, (size.height() + 3) & ~3, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) {
		bitmapData.clear();
		return false;
	}
	image.fill(0);

	QPainter painter;
	painter.begin(&image);
	painter.setFont(*font);
	painter.setPen(0xFFFFFFFF);
	// TODO: Involve ALIGN_HCENTER (bounds etc.)
	painter.drawText(image.rect(), Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8(str.data(), str.length()));
	painter.end();

	entry.texture = nullptr;
	entry.bmWidth = entry.width = image.width();
	entry.bmHeight = entry.height = image.height();
	entry.lastUsedFrame = frameCount_;

	if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int x = 0; x < entry.bmWidth; x++) {
			for (int y = 0; y < entry.bmHeight; y++) {
				bitmapData16[entry.bmWidth * y + x] = 0xfff0 | (image.pixel(x, y) >> 28);
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int x = 0; x < entry.bmWidth; x++) {
			for (int y = 0; y < entry.bmHeight; y++) {
				bitmapData[entry.bmWidth * y + x] = image.pixel(x, y) >> 24;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}
	return true;
}

void TextDrawerQt::ClearFonts() {
	// Also wipe the font map.
	for (auto iter : fontMap_) {
		delete iter.second;
	}
	fontMap_.clear();
	fontHash_ = 0;
}

#endif
