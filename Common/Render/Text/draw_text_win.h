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
	void MeasureStringRect(std::string_view str, const Bounds &bounds, float *w, float *h, int align = ALIGN_TOPLEFT) override;
	void DrawString(DrawBuffer &target, std::string_view str, float x, float y, uint32_t color, int align = ALIGN_TOPLEFT) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align = ALIGN_TOPLEFT) override;
	// Use for housekeeping like throwing out old strings.
	void OncePerFrame() override;

protected:
	void ClearCache() override;
	void RecreateFonts();  // On DPI change

	TextDrawerContext *ctx_;
	std::map<uint32_t, std::unique_ptr<TextDrawerFontContext>> fontMap_;

	uint32_t fontHash_;
};

#endif
