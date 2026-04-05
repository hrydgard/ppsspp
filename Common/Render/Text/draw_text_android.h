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
	static void SetActivity(jobject activity) { activity_ = activity; }
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	void MeasureStringInternal(std::string_view str, float *w, float *h) override;
	bool SupportsColorEmoji() const override { return true; }

	void ClearFonts() override;

private:
	// JNI functions
	static jobject activity_;
	jclass cls_textRenderer;
	jmethodID method_allocFont;
	jmethodID method_freeAllFonts;
	jmethodID method_measureText;
	jmethodID method_renderText;

	std::map<FontStyle, AndroidFontEntry> fontMap_;
	std::map<std::string, int> allocatedFonts_;
};

#endif
