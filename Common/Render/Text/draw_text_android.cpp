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

TextDrawerAndroid::TextDrawerAndroid(Draw::DrawContext *draw) : TextDrawer(draw) {
	auto env = getEnv();
	const char *textRendererClassName = "org/ppsspp/ppsspp/TextRenderer";
	jclass localClass = findClass(textRendererClassName);
	cls_textRenderer = reinterpret_cast<jclass>(env->NewGlobalRef(localClass));
	if (cls_textRenderer) {
		method_measureText = env->GetStaticMethodID(cls_textRenderer, "measureText", "(Ljava/lang/String;D)I");
		method_renderText = env->GetStaticMethodID(cls_textRenderer, "renderText", "(Ljava/lang/String;D)[I");
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
	ClearFonts();
}

bool TextDrawerAndroid::IsReady() const {
	return cls_textRenderer != nullptr && method_measureText != nullptr && method_renderText != nullptr;
}

uint32_t TextDrawerAndroid::SetFont(const char *fontName, int size, int flags) {
	// We will only use the default font but just for consistency let's still involve
	// the font name.
	uint32_t fontHash = fontName ? hash::Adler32((const uint8_t *)fontName, strlen(fontName)) : 1337;
	fontHash ^= size;
	fontHash ^= flags << 10;

	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHash;
		return fontHash;
	}

	// Just chose a factor that looks good, don't know what unit size is in anyway.
	AndroidFontEntry entry;
	entry.size = (float)(size * 1.4f) / dpiScale_;
	fontMap_[fontHash] = entry;
	fontHash_ = fontHash;
	return fontHash;
}

void TextDrawerAndroid::SetFont(uint32_t fontHandle) {
	uint32_t fontHash = fontHandle;
	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHandle;
	} else {
		ERROR_LOG(Log::G3D, "Invalid font handle %08x", fontHandle);
	}
}

void TextDrawerAndroid::MeasureString(std::string_view str, float *w, float *h) {
	if (str.empty()) {
		*w = 0.0;
		*h = 0.0;
		return;
	}

	CacheKey key{ std::string(str), fontHash_ };
	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(key);
	if (iter != sizeCache_.end()) {
		entry = iter->second.get();
	} else {
		float scaledSize = 14;
		auto iter = fontMap_.find(fontHash_);
		if (iter != fontMap_.end()) {
			scaledSize = iter->second.size;
		} else {
			ERROR_LOG(Log::G3D, "Missing font");
		}
		std::string text(str);
		auto env = getEnv();
		// Unfortunate that we can't create a jstr from a std::string_view directly.
		jstring jstr = env->NewStringUTF(text.c_str());
		uint32_t size = env->CallStaticIntMethod(cls_textRenderer, method_measureText, jstr, scaledSize);
		env->DeleteLocalRef(jstr);

		entry = new TextMeasureEntry();
		entry->width = (size >> 16);
		entry->height = (size & 0xFFFF);
		sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
	}
	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_ * dpiScale_;
	*h = entry->height * fontScaleY_ * dpiScale_;
}

bool TextDrawerAndroid::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	double size = 0.0;
	auto iter = fontMap_.find(fontHash_);
	if (iter != fontMap_.end()) {
		size = iter->second.size;
	} else {
		ERROR_LOG(Log::G3D, "Missing font");
	}

	auto env = getEnv();
	jstring jstr = env->NewStringUTF(std::string(str).c_str());
	uint32_t textSize = env->CallStaticIntMethod(cls_textRenderer, method_measureText, jstr, size);
	int imageWidth = (short)(textSize >> 16);
	int imageHeight = (short)(textSize & 0xFFFF);
	if (imageWidth <= 0)
		imageWidth = 1;
	if (imageHeight <= 0)
		imageHeight = 1;

	jintArray imageData = (jintArray)env->CallStaticObjectMethod(cls_textRenderer, method_renderText, jstr, size);
	env->DeleteLocalRef(jstr);

	entry.texture = nullptr;
	entry.bmWidth = imageWidth;
	entry.width = imageWidth;
	entry.bmHeight = imageHeight;
	entry.height = imageHeight;
	entry.lastUsedFrame = frameCount_;

	jint *jimage = env->GetIntArrayElements(imageData, nullptr);
	_assert_(env->GetArrayLength(imageData) == imageWidth * imageHeight);
	if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint32_t v = jimage[imageWidth * y + x];
				v = 0xFFF0 | ((v >> 28) & 0xF);  // Grab the upper bits from the alpha channel, and put directly in the 16-bit alpha channel.
				bitmapData16[entry.bmWidth * y + x] = (uint16_t)v;
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
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
				bitmapData32[entry.bmWidth * y + x] = v;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}
	env->ReleaseIntArrayElements(imageData, jimage, 0);
	env->DeleteLocalRef(imageData);
	return true;
}

void TextDrawerAndroid::ClearFonts() {
	fontMap_.clear();  // size is precomputed using dpiScale_.
}

#endif
