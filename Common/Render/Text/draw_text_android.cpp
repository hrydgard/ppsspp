#include "ppsspp_config.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_android.h"

#include "android/jni/app-android.h"

#if PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__)

#include <jni.h>

jobject TextDrawerAndroid::activity_;

TextDrawerAndroid::TextDrawerAndroid(Draw::DrawContext *draw) : TextDrawer(draw) {
	auto env = getEnv();
	const char *textRendererClassName = "org/ppsspp/ppsspp/TextRenderer";
	jclass localClass = findClass(textRendererClassName);
	cls_textRenderer = reinterpret_cast<jclass>(env->NewGlobalRef(localClass));
	if (cls_textRenderer) {
		method_allocFont = env->GetStaticMethodID(cls_textRenderer, "allocFont", "(Landroid/content/Context;Ljava/lang/String;)I");
		method_freeAllFonts = env->GetStaticMethodID(cls_textRenderer, "freeAllFonts", "()V");
		method_measureText = env->GetStaticMethodID(cls_textRenderer, "measureText", "(Ljava/lang/String;ID)I");
		method_renderText = env->GetStaticMethodID(cls_textRenderer, "renderText", "(Ljava/lang/String;IDII)[I");
	} else {
		ERROR_LOG(Log::G3D, "Failed to find class: '%s'", textRendererClassName);
	}
	dpiScale_ = CalculateDPIScale();

	INFO_LOG(Log::G3D, "Initializing TextDrawerAndroid with DPI scale %f", dpiScale_);
}

TextDrawerAndroid::~TextDrawerAndroid() {
	// Not sure why we can't do this but it crashes. Likely some deeper threading issue.
	// At worst we leak one ref...
	// env_->DeleteGlobalRef(cls_textRenderer);
	ClearCache();
	fontMap_.clear();  // size is precomputed using dpiScale_.
}

bool TextDrawerAndroid::IsReady() const {
	return cls_textRenderer != nullptr && method_measureText != nullptr && method_renderText != nullptr;
}

void TextDrawerAndroid::SetOrCreateFont(const FontStyle &style) {
	// We will only use the default font but just for consistency let's still involve
	// the font name.
	auto iter = fontMap_.find(style);
	if (iter != fontMap_.end()) {
		fontStyle_ = style;
		return;
	}

	std::string filename = GetFilenameForFontStyle(style) + ".ttf";
	auto fontIter = allocatedFonts_.find(filename);
	if (fontIter == allocatedFonts_.end()) {
		auto env = getEnv();
		jstring jstr = env->NewStringUTF(filename.c_str());
		int fontId = env->CallStaticIntMethod(cls_textRenderer, method_allocFont, activity_, jstr);
		env->DeleteLocalRef(jstr);

		if (fontId >= 0) {
			allocatedFonts_[filename] = fontId;
		}
	}

	AndroidFontEntry entry{};
	entry.font = allocatedFonts_[filename];
	entry.size = 1.25f * style.sizePts / dpiScale_;   // Not sure why this formula works.

	// Just chose a factor that looks good, don't know what unit size is in anyway.
	fontMap_[style] = entry;
	fontStyle_ = style;
}

void TextDrawerAndroid::MeasureStringInternal(std::string_view str, float *w, float *h) {
	auto iter = fontMap_.find(fontStyle_);
	if (iter == fontMap_.end()) {
		ERROR_LOG(Log::G3D, "Missing font");
		*w = 1.0f;
		*h = 1.0f;
		return;
	}

	std::string text;
	ConvertUTF8ToJavaModifiedUTF8(&text, str);

	auto env = getEnv();
	jstring jstr = env->NewStringUTF(text.c_str());
	uint32_t size = env->CallStaticIntMethod(cls_textRenderer, method_measureText, jstr, iter->second.font, iter->second.size);
	env->DeleteLocalRef(jstr);

	*w = (float)(size >> 16);
	*h = (float)(size & 0xFFFF);
}

bool TextDrawerAndroid::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	auto iter = fontMap_.find(fontStyle_);
	if (iter == fontMap_.end()) {
		ERROR_LOG(Log::G3D, "Missing font");
		return false;
	}

	auto env = getEnv();

	std::string text;
	ConvertUTF8ToJavaModifiedUTF8(&text, str);
	jstring jstr = env->NewStringUTF(text.c_str());

	uint32_t textSize = env->CallStaticIntMethod(cls_textRenderer, method_measureText, jstr, iter->second.font, iter->second.size);
	int imageWidth = (short)(textSize >> 16);
	int imageHeight = (short)(textSize & 0xFFFF);
	if (imageWidth <= 0)
		imageWidth = 1;
	if (imageHeight <= 0)
		imageHeight = 1;
	// WARN_LOG(Log::G3D, "Text: '%.*s' (%02x)", (int)str.length(), str.data(), str[0]);

	// TODO: Handle bold/italic
	jintArray imageData = (jintArray)env->CallStaticObjectMethod(cls_textRenderer, method_renderText, jstr, iter->second.font, iter->second.size, imageWidth, imageHeight);
	env->DeleteLocalRef(jstr);

	entry.texture = nullptr;
	entry.bmWidth = imageWidth;
	entry.width = imageWidth;
	entry.bmHeight = imageHeight;
	entry.height = imageHeight;
	entry.lastUsedFrame = frameCount_;

	jint *jimage = env->GetIntArrayElements(imageData, nullptr);
	if (env->GetArrayLength(imageData) != imageWidth * imageHeight) {
		ERROR_LOG(Log::G3D, "TextRenderer returned bad image size");
		env->ReleaseIntArrayElements(imageData, jimage, JNI_ABORT);
		env->DeleteLocalRef(imageData);
		return false;
	}
	if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint32_t v = jimage[imageWidth * y + x];
				// Premultiplied alpha: Put alpha in all four channels.
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(v >> 24);
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		// We only get here if swizzle is possible, so we can duplicate the value to all four channels.
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint32_t v = jimage[imageWidth * y + x];
				bitmapData[entry.bmWidth * y + x] = (uint8_t)(v >> 24);
			}
		}
	} else if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint32_t));
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint32_t v = jimage[imageWidth * y + x];
				// Swap R and B, for some reason.
				v = (v & 0xFF00FF00) | ((v >> 16) & 0xFF) | ((v << 16) & 0xFF0000);
				bitmapData32[entry.bmWidth * y + x] = RGBAToPremul8888(v);
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}
	env->ReleaseIntArrayElements(imageData, jimage, JNI_ABORT);
	env->DeleteLocalRef(imageData);
	return true;
}

void TextDrawerAndroid::ClearFonts() {
	fontMap_.clear();  // size is precomputed using dpiScale_.
}

#endif
