#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if defined(USING_QT_UI)

class QFont;

class TextDrawerQt : public TextDrawer {
public:
	TextDrawerQt(Draw::DrawContext *draw);
	~TextDrawerQt();

	void SetOrCreateFont(const FontStyle &style) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	void MeasureStringInternal(std::string_view str, float *w, float *h) override;
	bool SupportsColorEmoji() const override { return false; }

	void ClearFonts() override;

	std::map<FontStyle, QFont *> fontMap_;
};

#endif
