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

	uint32_t SetFont(const char *fontName, int size, int flags) override;
	void SetFont(uint32_t fontHandle) override;  // Shortcut once you've set the font once.
	void MeasureString(std::string_view str, float *w, float *h) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	bool SupportsColorEmoji() const override { return false; }

	void ClearFonts() override;

	std::map<uint32_t, QFont *> fontMap_;
};

#endif
