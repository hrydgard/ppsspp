#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if defined(_WIN32) && !defined(USING_QT_UI) && !PPSSPP_PLATFORM(UWP)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct TextDrawerContext;
// Internal struct but all details in .cpp file (pimpl to avoid pulling in excessive headers here)
class TextDrawerFontContext;

class TextDrawerWin32 : public TextDrawer {
public:
	TextDrawerWin32(Draw::DrawContext *draw);
	~TextDrawerWin32();

	uint32_t SetFont(const char *fontName, int size, int flags) override;
	void SetFont(uint32_t fontHandle) override;  // Shortcut once you've set the font once.
	void MeasureString(std::string_view str, float *w, float *h) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	bool SupportsColorEmoji() const override { return false; }
	void ClearFonts() override;

	TextDrawerContext *ctx_;
	std::map<uint32_t, std::unique_ptr<TextDrawerFontContext>> fontMap_;
};

#endif
