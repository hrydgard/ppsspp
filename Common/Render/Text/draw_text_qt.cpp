#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_qt.h"

#include "Common/Log.h"

#if defined(USING_QT_UI)

#include <QtGui/QFont>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QFontMetrics>
#include <QFontDatabase>
#include <QtOpenGL/QGLWidget>

TextDrawerQt::TextDrawerQt(Draw::DrawContext *draw) : TextDrawer(draw) {
	// Add all fonts we ship with.

	std::vector<std::string> filenames = GetAllFontFilenames();

	for (const auto &iter : filenames) {
		std::string fn = "assets/" + iter + ".ttf";
		size_t fontSize = 0;
		uint8_t *fontData = g_VFS.ReadFile(fn.c_str(), &fontSize);
		if (fontData) {
			int fontID = QFontDatabase::addApplicationFontFromData(QByteArray((const char *)fontData, fontSize));
			delete[] fontData;
			QStringList fontsFound = QFontDatabase::applicationFontFamilies(fontID);
			if (fontsFound.size() == 0) {
				WARN_LOG(Log::G3D, "Failed to load font from %s", fn.c_str());
			} else {
				for (const auto &f : fontsFound) {
					WARN_LOG(Log::G3D, "Loaded font %s from %s", f.toUtf8().constData(), fn.c_str());
				}
			}
		} else {
			ERROR_LOG(Log::G3D, "Failed to load font file %s", fn.c_str());
		}
	}
}

TextDrawerQt::~TextDrawerQt() {
	ClearCache();
	ClearFonts();
}

void TextDrawerQt::SetOrCreateFont(const FontStyle &style) {
	auto iter = fontMap_.find(style);
	if (iter != fontMap_.end()) {
		fontStyle_ = style;
		return;
	}

	FontStyleFlags styleFlags;
	std::string fontName = GetFontNameForFontStyle(style, &styleFlags);

	QFont *font = new QFont(fontName.c_str());
	font->setPixelSize((int)((style.sizePts + 6) / dpiScale_));
	font->setBold(styleFlags & FontStyleFlags::Bold);
	if (styleFlags & FontStyleFlags::Light) {
		font->setWeight(QFont::Light);
	}
	font->setItalic(styleFlags & FontStyleFlags::Italic);
	font->setUnderline(styleFlags & FontStyleFlags::Underline);
	font->setStrikeOut(styleFlags & FontStyleFlags::Strikethrough);

	fontMap_[style] = font;
	fontStyle_ = style;
}

void TextDrawerQt::MeasureStringInternal(std::string_view str, float *w, float *h) {
	QFont* font = fontMap_.find(fontStyle_)->second;
	QFontMetrics fm(*font);
	QSize size = fm.size(0, QString::fromUtf8(str.data(), str.length()));

	*w = size.width();
	*h = size.height();
}

bool TextDrawerQt::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	_dbg_assert_(!fullColor);

	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	QFont *font = fontMap_.find(fontStyle_)->second;
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
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(image.pixel(x, y) >> 24);
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
	fontStyle_ = {};
}

#endif
