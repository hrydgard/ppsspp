#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if PPSSPP_PLATFORM(UWP)

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wrl/client.h>

struct TextDrawerContext;
// Internal struct but all details in .cpp file (pimpl to avoid pulling in excessive headers here)
class TextDrawerFontContext;

class TextDrawerUWP : public TextDrawer {
public:
	TextDrawerUWP(Draw::DrawContext *draw);
	~TextDrawerUWP();

	void SetOrCreateFont(const FontStyle &style) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	void MeasureStringInternal(std::string_view str, float *w, float *h) override;
	bool SupportsColorEmoji() const override { return true; }
	void ClearFonts() override;

	TextDrawerContext *ctx_;
	std::map<FontStyle, std::unique_ptr<TextDrawerFontContext>> fontMap_;

	// Direct2D drawing components.
	ID2D1Factory5*        m_d2dFactory = nullptr;
	ID2D1Device4*         m_d2dDevice = nullptr;
	ID2D1DeviceContext4*  m_d2dContext = nullptr;
	ID2D1SolidColorBrush* m_d2dWhiteBrush = nullptr;

	// DirectWrite drawing components.
	IDWriteFactory5*        m_dwriteFactory = nullptr;
	IDWriteFontSet*         m_fontSet = nullptr;
	IDWriteFontSetBuilder1* m_fontSetBuilder = nullptr;
	IDWriteFontCollection1* m_fontCollection = nullptr;
	IDWriteInMemoryFontFileLoader *m_inMemoryLoader = nullptr;
	std::vector<Microsoft::WRL::ComPtr<IDWriteFontFile>> m_fontFiles;
};

#endif
