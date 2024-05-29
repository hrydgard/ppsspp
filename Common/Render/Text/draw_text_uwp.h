#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if PPSSPP_PLATFORM(UWP)

#include <d2d1_3.h>
#include <dwrite_3.h>

struct TextDrawerContext;
// Internal struct but all details in .cpp file (pimpl to avoid pulling in excessive headers here)
class TextDrawerFontContext;

class TextDrawerUWP : public TextDrawer {
public:
	TextDrawerUWP(Draw::DrawContext *draw);
	~TextDrawerUWP();

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

	// Direct2D drawing components.
	ID2D1Factory5*        m_d2dFactory;
	ID2D1Device4*         m_d2dDevice;
	ID2D1DeviceContext4*  m_d2dContext;
	ID2D1SolidColorBrush* m_d2dWhiteBrush;

	// DirectWrite drawing components.
	IDWriteFactory5*        m_dwriteFactory;
	IDWriteFontFile*        m_fontFile;
	IDWriteFontSet*         m_fontSet;
	IDWriteFontSetBuilder1* m_fontSetBuilder;
	IDWriteFontCollection1* m_fontCollection;

};

#endif
