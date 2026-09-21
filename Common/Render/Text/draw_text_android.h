#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <jni.h>

struct AndroidFontEntry {
	int font;
	float size;
};

class TextDrawerAndroid : public TextDrawer {
public:
	TextDrawerAndroid(Draw::DrawContext *draw);
	~TextDrawerAndroid();

	bool IsReady() const override;
	void SetOrCreateFont(const FontStyle &style) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	void MeasureStringInternal(std::string_view str, float *w, float *h) override;
	bool SupportsColorEmoji() const override { return true; }

	void ClearFonts() override;

private:
	// JNI functions
	jclass cls_textRenderer = nullptr;
	jmethodID method_allocFont = nullptr;
	jmethodID method_freeAllFonts = nullptr;
	jmethodID method_measureText = nullptr;
	jmethodID method_renderText = nullptr;

	std::map<FontStyle, AndroidFontEntry> fontMap_;
	std::map<std::string, int> allocatedFonts_;
};

#endif
