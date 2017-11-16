#pragma once

#include "ppsspp_config.h"

#include <map>
#include "gfx_es2/draw_text.h"

#if defined(USING_QT_UI)

class TextDrawerQt : public TextDrawer {
public:
	TextDrawerQt(Draw::DrawContext *draw);
	~TextDrawerQt();

	uint32_t SetFont(const char *fontName, int size, int flags) override;
	void SetFont(uint32_t fontHandle) override;  // Shortcut once you've set the font once.
	void MeasureString(const char *str, size_t len, float *w, float *h) override;
	void MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align = ALIGN_TOPLEFT) override;
	void DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align = ALIGN_TOPLEFT) override;
	void DrawStringRect(DrawBuffer &target, const char *str, const Bounds &bounds, uint32_t color, int align) override;
	// Use for housekeeping like throwing out old strings.
	void OncePerFrame() override;

protected:
	void ClearCache() override;

	uint32_t fontHash_;
	std::map<uint32_t, QFont *> fontMap_;

	// The key is the CityHash of the string xor the fontHash_.
	std::map<uint32_t, std::unique_ptr<TextStringEntry>> cache_;
	std::map<uint32_t, std::unique_ptr<TextMeasureEntry>> sizeCache_;
};

#endif