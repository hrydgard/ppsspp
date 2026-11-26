#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

// Internal structs, all details in the .cpp file (pimpl to avoid pulling in excessive headers here).
// Their names must differ from the DirectWrite drawer's, which is linked into the same binary.
struct TextDrawerWin32Context;
class TextDrawerWin32FontContext;

class TextDrawerWin32 : public TextDrawer {
public:
	TextDrawerWin32(Draw::DrawContext *draw);
	~TextDrawerWin32();


protected:
	void EnsureFont(const FontStyle &style) override;
	void MeasureStringInternal(const FontStyle &style, std::string_view str, float *w, float *h) override;
	bool DrawStringBitmap(const FontStyle &style, std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;
	bool SupportsColorEmoji() const override { return false; }
	void ClearFonts() override;

	TextDrawerWin32Context *ctx_;
	std::map<FontStyle, std::unique_ptr<TextDrawerWin32FontContext>> fontMap_;
};

#endif
