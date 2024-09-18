#include "ppsspp_config.h"

#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"

#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_win.h"
#include "Common/Render/Text/draw_text_cocoa.h"
#include "Common/Render/Text/draw_text_uwp.h"
#include "Common/Render/Text/draw_text_qt.h"
#include "Common/Render/Text/draw_text_android.h"
#include "Common/Render/Text/draw_text_sdl.h"

TextDrawer::TextDrawer(Draw::DrawContext *draw) : draw_(draw) {
	// These probably shouldn't be state.
	dpiScale_ = CalculateDPIScale();
}
TextDrawer::~TextDrawer() {
}

float TextDrawerWordWrapper::MeasureWidth(std::string_view str) {
	float w, h;
	drawer_->MeasureString(str, &w, &h);
	return w;
}

void TextDrawer::WrapString(std::string &out, std::string_view str, float maxW, int flags) {
	TextDrawerWordWrapper wrapper(this, str, maxW, flags);
	out = wrapper.Wrapped();
}

void TextDrawer::SetFontScale(float xscale, float yscale) {
	fontScaleX_ = xscale;
	fontScaleY_ = yscale;
}

float TextDrawer::CalculateDPIScale() const {
	if (ignoreGlobalDpi_)
		return dpiScale_;
	float scale = g_display.dpi_scale_y;
	if (scale >= 1.0f) {
		scale = 1.0f;
	}
	return scale;
}

void TextDrawer::DrawString(DrawBuffer &target, std::string_view str, float x, float y, uint32_t color, int align) {
	using namespace Draw;
	if (str.empty()) {
		return;
	}

	CacheKey key{ std::string(str), fontHash_ };
	target.Flush(true);

	TextStringEntry *entry;

	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		entry = iter->second.get();
		entry->lastUsedFrame = frameCount_;
		if (!entry->texture) {
			return;
		}
	} else {
		DataFormat texFormat;
		// Pick between the supported formats, of which at least one is supported on each platform. Prefer R8 (but only if swizzle is supported)
		bool emoji = SupportsColorEmoji() && AnyEmojiInString(str.data(), str.length());
		if (emoji) {
			texFormat = Draw::DataFormat::R8G8B8A8_UNORM;
		} else if ((draw_->GetDataFormatSupport(Draw::DataFormat::R8_UNORM) & Draw::FMT_TEXTURE) != 0 && draw_->GetDeviceCaps().textureSwizzleSupported) {
			texFormat = Draw::DataFormat::R8_UNORM;
		} else if (draw_->GetDataFormatSupport(Draw::DataFormat::R4G4B4A4_UNORM_PACK16) & FMT_TEXTURE) {
			texFormat = Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
		} else if (draw_->GetDataFormatSupport(Draw::DataFormat::A4R4G4B4_UNORM_PACK16) & FMT_TEXTURE) {
			texFormat = Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
		} else if (draw_->GetDataFormatSupport(Draw::DataFormat::B4G4R4A4_UNORM_PACK16) & FMT_TEXTURE) {
			texFormat = Draw::DataFormat::B4G4R4A4_UNORM_PACK16;
		} else {
			texFormat = Draw::DataFormat::R8G8B8A8_UNORM;
		}

		entry = new TextStringEntry(frameCount_);

		// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
		// because we need white. Well, we could using swizzle, but not all our backends support that.
		TextureDesc desc{};
		std::vector<uint8_t> bitmapData;
		if (!DrawStringBitmap(bitmapData, *entry, texFormat, str, align, emoji)) {
			// Nothing drawn. Store that fact in the cache.
			cache_[key] = std::unique_ptr<TextStringEntry>(entry);
			return;
		}

		desc.initData.push_back(&bitmapData[0]);

		desc.type = TextureType::LINEAR2D;
		desc.format = texFormat;
		desc.width = entry->bmWidth;
		desc.height = entry->bmHeight;
		desc.depth = 1;
		desc.mipLevels = 1;
		desc.tag = "TextDrawer";
		desc.swizzle = texFormat == Draw::DataFormat::R8_UNORM ? Draw::TextureSwizzle::R8_AS_ALPHA : Draw::TextureSwizzle::DEFAULT,
		entry->texture = draw_->CreateTexture(desc);
		cache_[key] = std::unique_ptr<TextStringEntry>(entry);
	}

	_dbg_assert_(entry->texture);
	draw_->BindTexture(0, entry->texture);

	// Okay, the texture is bound, let's draw.
	float w = (float)entry->width * (fontScaleX_ * dpiScale_);
	float h = (float)entry->height * (fontScaleY_ * dpiScale_);
	float u = (float)entry->width / (float)entry->bmWidth;
	float v = (float)entry->height / (float)entry->bmHeight;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);

	target.DrawTexRect(x, y, x + w, y + h, 0.0f, 0.0f, u, v, color);
	target.Flush(true);
}

void TextDrawer::MeasureStringRect(std::string_view str, const Bounds &bounds, float *w, float *h, int align) {
	std::string toMeasure = std::string(str);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	if (wrap) {
		WrapString(toMeasure, toMeasure.c_str(), bounds.w, wrap);
	}
	MeasureString(toMeasure, w, h);
}

void TextDrawer::DrawStringRect(DrawBuffer &target, std::string_view str, const Bounds &bounds, uint32_t color, int align) {
	float x = bounds.x;
	float y = bounds.y;
	if (align & ALIGN_HCENTER) {
		x = bounds.centerX();
	} else if (align & ALIGN_RIGHT) {
		x = bounds.x2();
	}
	if (align & ALIGN_VCENTER) {
		y = bounds.centerY();
	} else if (align & ALIGN_BOTTOM) {
		y = bounds.y2();
	}

	std::string toDraw(str);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	if (wrap) {
		WrapString(toDraw, str, bounds.w, wrap);
	}

	DrawString(target, toDraw.c_str(), x, y, color, align);
}

bool TextDrawer::DrawStringBitmapRect(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, const Bounds &bounds, int align, bool fullColor) {
	std::string toDraw(str);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	if (wrap) {
		WrapString(toDraw, str, bounds.w, wrap);
	}
	return DrawStringBitmap(bitmapData, entry, texFormat, toDraw.c_str(), align, fullColor);
}

void TextDrawer::ClearCache() {
	for (auto &iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
	}
	cache_.clear();
	sizeCache_.clear();
	fontHash_ = 0;
}

void TextDrawer::OncePerFrame() {
	frameCount_++;
	// If DPI changed (small-mode, future proper monitor DPI support), drop everything.
	float newDpiScale = CalculateDPIScale();
	if (newDpiScale != dpiScale_) {
		INFO_LOG(Log::G3D, "DPI Scale changed (%f to %f) - wiping font cache (%d items)", dpiScale_, newDpiScale, (int)cache_.size());
		dpiScale_ = newDpiScale;
		ClearCache();
		ClearFonts();
	}

	// Drop old strings. Use a prime number to reduce clashing with other rhythms
	if (frameCount_ % 23 == 0) {
		for (auto iter = cache_.begin(); iter != cache_.end();) {
			if (frameCount_ - iter->second->lastUsedFrame > 100) {
				if (iter->second->texture)
					iter->second->texture->Release();
				cache_.erase(iter++);
			} else {
				iter++;
			}
		}

		for (auto iter = sizeCache_.begin(); iter != sizeCache_.end(); ) {
			if (frameCount_ - iter->second->lastUsedFrame > 100) {
				sizeCache_.erase(iter++);
			} else {
				iter++;
			}
		}
	}
}

TextDrawer *TextDrawer::Create(Draw::DrawContext *draw) {
	TextDrawer *drawer = nullptr;
#if defined(__LIBRETRO__)
	// No text drawer
#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	drawer = new TextDrawerWin32(draw);
#elif PPSSPP_PLATFORM(UWP)
	drawer = new TextDrawerUWP(draw);
#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	drawer = new TextDrawerCocoa(draw);
#elif defined(USING_QT_UI)
	drawer = new TextDrawerQt(draw);
#elif PPSSPP_PLATFORM(ANDROID)
	drawer = new TextDrawerAndroid(draw);
#elif USE_SDL2_TTF
	drawer = new TextDrawerSDL(draw);
#endif
	if (drawer && !drawer->IsReady()) {
		delete drawer;
		drawer = nullptr;
	}
	return drawer;
}
