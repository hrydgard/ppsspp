#include "ppsspp_config.h"

#if defined(__linux__) || defined(__FreeBSD__) && !defined(__ANDROID__)
# include <dlfcn.h>
#elif defined(_WIN32)
// Not CommonWindows.h: its WIN32_LEAN_AND_MEAN leaves out what the Direct2D/DirectWrite headers in draw_text_uwp.h need.
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#endif

#include <algorithm>
#include <cstring>

#include "ext/xxhash.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"

#include "Common/Render/DropShadow.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_win.h"
#include "Common/Render/Text/draw_text_cocoa.h"
#include "Common/Render/Text/draw_text_android.h"
#include "Common/Render/Text/draw_text_sdl.h"
#include "Common/Render/Text/draw_text_uwp.h"
#include "Common/StringUtils.h"
#include "Common/OSVersion.h"

// TODO: Move this to somewhere more appropriate.
static bool IsRenderDocLoaded() {
#if PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(ANDROID)
	return false;
#elif defined(_WIN32)
	return (GetModuleHandleA("renderdoc.dll") != nullptr);
#elif defined(__linux__) || defined(__FreeBSD__)
	// Returns a pointer if loaded, otherwise null
	void* handle = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
	if (handle) {
		dlclose(handle);
		return true;
	}
	return false;
#else
	return false;
#endif
}

size_t TextCacheKeyHash::operator()(const TextCacheKey &key) const {
	uint32_t maxWidthBits, scaleBits;
	memcpy(&maxWidthBits, &key.maxWidth, sizeof(maxWidthBits));
	memcpy(&scaleBits, &key.scale, sizeof(scaleBits));
	uint64_t seed = ((uint64_t)key.style.sizePts << 48) | ((uint64_t)key.style.family << 40) | ((uint64_t)key.style.flags << 32) | (uint32_t)key.flags;
	seed ^= (((uint64_t)maxWidthBits << 32) | scaleBits) * 0x9E3779B97F4A7C15ULL;
	return (size_t)XXH3_64bits_withSeed(key.text.data(), key.text.size(), seed);
}

TextDrawer::TextDrawer(Draw::DrawContext *draw) : draw_(draw) {
	// These probably shouldn't be state.
	dpiScale_ = CalculateDPIScale();
}

float TextDrawerWordWrapper::MeasureWidth(std::string_view str) {
	float w, h;
	drawer_->MeasureString(str, &w, &h);
	return w;
}

const TextCacheKey &TextDrawer::LookupKey(std::string_view str, int flags, float maxWidth, float scale) {
	lookupKey_.text.assign(str.data(), str.size());
	lookupKey_.style = fontStyle_;
	lookupKey_.flags = flags;
	lookupKey_.maxWidth = maxWidth;
	lookupKey_.scale = scale;
	return lookupKey_;
}

const std::string &TextDrawer::WrapString(std::string_view str, float maxW, int flags) {
	// The wrapper measures in scaled units, so the result depends on the scale too.
	auto [iter, inserted] = wrapCache_.try_emplace(LookupKey(str, flags, maxW, fontScaleX_));
	TextWrapEntry &entry = iter->second;
	entry.lastUsedFrame = frameCount_;
	if (inserted) {
		TextDrawerWordWrapper wrapper(this, str, maxW, flags);
		entry.wrapped = wrapper.Wrapped();
	}
	return entry.wrapped;
}

void TextDrawer::SetFontScale(float xscale, float yscale) {
	fontScaleX_ = xscale;
	fontScaleY_ = yscale;
}

float TextDrawer::CalculateDPIScale() const {
	if (ignoreGlobalDpi_)
		return dpiScale_;
	return g_display.dpi_scale_y;
}

void TextDrawer::DrawString(DrawBuffer &target, std::string_view str, float x, float y, uint32_t color, int align) {
	using namespace Draw;
	if (str.empty()) {
		return;
	}

	const bool dropShadow = (align & FLAG_DROP_SHADOW) != 0;
	target.Flush(true);

	TextStringEntry *entry;

	auto [iter, inserted] = cache_.try_emplace(LookupKey(str, align & FLAG_DROP_SHADOW));
	if (!inserted) {
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

		iter->second = std::make_unique<TextStringEntry>(frameCount_);
		entry = iter->second.get();

		// The shadow is built on the CPU from the text's coverage (or its colors, for emoji), and needs a color format.
		const DataFormat bitmapFormat = dropShadow ? (emoji ? DataFormat::R8G8B8A8_UNORM : DataFormat::R8_UNORM) : texFormat;

		// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
		// because we need white. Well, we could using swizzle, but not all our backends support that.
		TextureDesc desc{};
		std::vector<uint8_t> bitmapData;
		if (!DrawStringBitmap(bitmapData, *entry, bitmapFormat, str, align, emoji)) {
			// Nothing drawn. The entry without a texture records that fact.
			return;
		}
		if (dropShadow) {
			AddDropShadow(bitmapData, *entry, bitmapFormat);
			texFormat = DataFormat::R8G8B8A8_UNORM;
		}

		desc.initData.push_back(&bitmapData[0]);

		desc.type = TextureType::LINEAR2D;
		desc.format = texFormat;
		desc.width = entry->bmWidth;
		desc.height = entry->bmHeight;
		desc.depth = 1;
		desc.mipLevels = 1;
		desc.tag = "TextDrawer";
		desc.swizzle = texFormat == Draw::DataFormat::R8_UNORM ? Draw::TextureSwizzle::R8_AS_PREMUL_ALPHA : Draw::TextureSwizzle::DEFAULT,
		entry->texture = draw_->CreateTexture(desc);
	}

	_dbg_assert_(entry->texture);
	draw_->BindTexture(0, entry->texture);

	// Okay, the texture is bound, let's draw.
	float w = (float)entry->width * (fontScaleX_ * dpiScale_);
	float h = (float)entry->height * (fontScaleY_ * dpiScale_);
	float u = (float)(entry->width + entry->shadowPad * 2) / (float)entry->bmWidth;
	float v = (float)(entry->height + entry->shadowPad * 2) / (float)entry->bmHeight;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);

	// Align by the text, then grow the quad to include the shadow padding.
	const float padX = entry->shadowPad * (fontScaleX_ * dpiScale_);
	const float padY = entry->shadowPad * (fontScaleY_ * dpiScale_);
	target.DrawTexRect(x - padX, y - padY, x + w + padX, y + h + padY, 0.0f, 0.0f, u, v, color);
	target.Flush(true);
}

void TextDrawer::MeasureString(std::string_view str, float *w, float *h) {
	if (str.empty()) {
		*w = 0.0f;
		*h = 0.0f;
		return;
	}

	// Clamp the size to something sane.
	if (str.size() > MAX_TEXT_LENGTH) {
		str = str.substr(0, MAX_TEXT_LENGTH);
	}

	auto [iter, inserted] = sizeCache_.try_emplace(LookupKey(str));
	TextMeasureEntry &entry = iter->second;
	if (inserted) {
		float extW, extH;
		MeasureStringInternal(str, &extW, &extH);
		entry.width = extW;
		entry.height = extH;
		// Hm, use the old calculation?
		// int h = i == lines.size() - 1 ? entry->height : metrics.tmHeight + metrics.tmExternalLeading;
	}

	entry.lastUsedFrame = frameCount_;
	*w = entry.width * fontScaleX_ * dpiScale_;
	*h = entry.height * fontScaleY_ * dpiScale_;
}

void TextDrawer::MeasureStringRect(std::string_view str, float maxWidth, float *w, float *h, int align) {
	const int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);

	// Clamp the size to something sane.
	if (str.size() > MAX_TEXT_LENGTH) {
		str = str.substr(0, MAX_TEXT_LENGTH);
	}

	float plainW, plainH;
	MeasureString(str, &plainW, &plainH);

	if (wrap && plainW > maxWidth) {
		MeasureString(WrapString(str, maxWidth, wrap), w, h);
	} else {
		*w = plainW;
		*h = plainH;
	}
}

void TextDrawer::DrawStringRect(DrawBuffer &target, std::string_view str, const Bounds &bounds, uint32_t color, int align) {
	if (bounds.w < 0.0f || bounds.h < 0.0f) {
		return;
	}
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

	// Clamp the size to something sane.
	if (str.size() > MAX_TEXT_LENGTH) {
		str = str.substr(0, MAX_TEXT_LENGTH);
	}

	const int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	DrawString(target, wrap ? std::string_view(WrapString(str, bounds.w, wrap)) : str, x, y, color, align);
}

bool TextDrawer::DrawStringBitmapRect(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, const Bounds &bounds, int align, bool fullColor) {
	// Clamp the size to something sane.
	if (str.size() > MAX_TEXT_LENGTH) {
		str = str.substr(0, MAX_TEXT_LENGTH);
	}

	const int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	return DrawStringBitmap(bitmapData, entry, texFormat, wrap ? std::string_view(WrapString(str, bounds.w, wrap)) : str, align, fullColor);
}

void TextDrawer::AddDropShadow(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat srcFormat) const {
	// The bitmap has 1/dpiScale_ pixels per UI unit, so at higher DPI the blur spans more pixels.
	const DropShadowParams params = UIDropShadowParams(1.0f / dpiScale_);
	const int pad = params.Padding();

	const int srcW = entry.bmWidth;
	const int srcH = entry.bmHeight;
	std::vector<u32> src;
	if (srcFormat == Draw::DataFormat::R8G8B8A8_UNORM) {
		src.resize(srcW * srcH);
		memcpy(src.data(), bitmapData.data(), src.size() * sizeof(u32));
	} else {
		src.reserve(srcW * srcH);
		for (uint8_t a : bitmapData) {
			src.push_back(AlphaToPremul8888(a));
		}
	}

	const int bmWidth = (srcW + pad * 2 + 3) & ~3;
	const int bmHeight = (srcH + pad * 2 + 3) & ~3;
	bitmapData.assign(bmWidth * bmHeight * sizeof(u32), 0);
	CompositeDropShadow(src.data(), srcW, srcH, (u32 *)bitmapData.data(), bmWidth, params);

	entry.bmWidth = bmWidth;
	entry.bmHeight = bmHeight;
	entry.shadowPad = pad;
}

void TextDrawer::ClearCache() {
	for (auto &iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
	}
	cache_.clear();
	sizeCache_.clear();
	wrapCache_.clear();
	fontStyle_ = {};
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
	if (frameCount_ % 63 == 0) {
		for (auto iter = cache_.begin(); iter != cache_.end();) {
			if (frameCount_ - iter->second->lastUsedFrame > 100) {
				if (iter->second->texture)
					iter->second->texture->Release();
				iter = cache_.erase(iter);
			} else {
				iter++;
			}
		}

		for (auto iter = sizeCache_.begin(); iter != sizeCache_.end(); ) {
			if (frameCount_ - iter->second.lastUsedFrame > 100) {
				iter = sizeCache_.erase(iter);
			} else {
				iter++;
			}
		}

		for (auto iter = wrapCache_.begin(); iter != wrapCache_.end(); ) {
			if (frameCount_ - iter->second.lastUsedFrame > 100) {
				iter = wrapCache_.erase(iter);
			} else {
				iter++;
			}
		}
	}
}

size_t TextDrawer::GetCacheDataSize() const {
	size_t sz = 0;
	for (const auto &iter : cache_) {
		if (iter.second->texture) {
			sz += iter.second->texture->DataSize();
		}
	}
	return sz;
}

TextDrawer *TextDrawer::Create(Draw::DrawContext *draw) {
	TextDrawer *drawer = nullptr;
#if defined(__LIBRETRO__)
	// No text drawer
#elif PPSSPP_PLATFORM(UWP)
	drawer = new TextDrawerUWP(draw);
#elif defined(_WIN32)
	// If RenderDoc is active, we can't use the UWP drawer since it uses D3D11 to initialize D2D,
	// and RenderDoc doesn't permit multiple graphics contexts.
	// Also, this seems to cause failures under Windows 7, so let's gate on Win 10 to be safe.
	if (IsRenderDocLoaded() || !IsWin10OrHigher()) {
		drawer = new TextDrawerWin32(draw);
	} else {
		drawer = new TextDrawerUWP(draw);
	}
#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	drawer = new TextDrawerCocoa(draw);
#elif PPSSPP_PLATFORM(ANDROID)
	drawer = new TextDrawerAndroid(draw);
#elif USE_SDL3_TTF
	drawer = new TextDrawerSDL(draw);
#endif
	if (drawer && !drawer->IsReady()) {
		delete drawer;
		drawer = nullptr;
	}
	return drawer;
}

struct FontDesc {
	FontFamily family;
	FontStyleFlags flags;
	std::string_view fontName;
	std::string_view filename;
};

// Append ".ttf" to get the actual filenames.
static const FontDesc g_fontDescs[] = {
	{FontFamily::SansSerif, FontStyleFlags::Default, "Roboto Condensed", "Roboto_Condensed-Regular"},
	{FontFamily::SansSerif, FontStyleFlags::Bold, "Roboto Condensed", "Roboto_Condensed-Bold"},
	{FontFamily::SansSerif, FontStyleFlags::Italic, "Roboto Condensed", "Roboto_Condensed-Italic"},
	{FontFamily::SansSerif, FontStyleFlags::Light, "Roboto Condensed", "Roboto_Condensed-Light"},
	{FontFamily::Fixed, FontStyleFlags::Default, "Inconsolata", "Inconsolata-Regular"},
};

std::map<FontFamily, std::string> g_fontOverrides;

void SetFontNameOverride(FontFamily family, std::string_view overrideFont) {
	g_fontOverrides[family] = std::string(overrideFont);
}

std::vector<std::string> GetAllFontFilenames() {
	std::vector<std::string> vec;
	for (const auto &desc : g_fontDescs) {
		vec.emplace_back(desc.filename);
	}
	// uniquify idiom
	std::sort(vec.begin(), vec.end());
	vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
	return vec;
}

std::string GetFilenameForFontStyle(const FontStyle &font) {
	for (const auto &desc : g_fontDescs) {
		if (desc.flags == font.flags && desc.family == font.family) {
			return std::string(desc.filename);
		}
	}

	// If nothing matched, just return the first font of the requested family.
	for (const auto &desc : g_fontDescs) {
		if (desc.family == font.family) {
			return std::string(desc.filename);
		}
	}

	_dbg_assert_(false);
	return "";
}

std::string GetFontNameForFontStyle(const FontStyle &font, FontStyleFlags *outFlags) {
	auto override = g_fontOverrides.find(font.family);
	if (override != g_fontOverrides.end()) {
		*outFlags = font.flags;
		return override->second;
	}

	for (const auto &desc : g_fontDescs) {
		if (desc.flags == font.flags && desc.family == font.family) {
			*outFlags = font.flags;
			return std::string(desc.fontName);
		}
	}
	// If nothing matched, just return the first font of the requested family.
	for (const auto &desc : g_fontDescs) {
		if (desc.family == font.family) {
			// Return the flags of the font we found.
			// TODO: Some platforms can actually synthesize traits (bold, italic) if missing.
			*outFlags = desc.flags;
			return std::string(desc.fontName);
		}
	}

	return "";
}
