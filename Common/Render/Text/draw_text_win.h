#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if defined(_WIN32) && !defined(USING_QT_UI) && !PPSSPP_PLATFORM(UWP)

struct TextDrawerContext;
// Internal struct but all details in .cpp file (pimpl to avoid pulling in excessive headers here)
class TextDrawerFontContext;

class TextDrawerWin32 : public TextDrawer {
public:
	TextDrawerWin32(Draw::DrawContext *draw);
	~TextDrawerWin32();
	
	void SetOrCreateFont(const FontStyle &style) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	void MeasureStringInternal(std::string_view str, float *w, float *h) override;
	bool SupportsColorEmoji() const override { return false; }
	void ClearFonts() override;

	TextDrawerContext *ctx_;
	std::map<FontStyle, std::unique_ptr<TextDrawerFontContext>> fontMap_;
};

#endif
