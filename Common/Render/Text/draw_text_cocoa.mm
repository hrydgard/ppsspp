#include "ppsspp_config.h"

#import "draw_text_cocoa.h"

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)

#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>

#if PPSSPP_PLATFORM(MAC)
#import <AppKit/AppKit.h>
#define ColorType NSColor
#else
#import <UIKit/UIKit.h>
#define ColorType UIColor
#endif

#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_cocoa.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"

enum {
	MAX_TEXT_WIDTH = 4096,
	MAX_TEXT_HEIGHT = 512
};

#define APPLE_FONT "Helvetica"

class TextDrawerFontContext {
public:
	~TextDrawerFontContext() {
		Destroy();
	}

	void Create() {
		// Create an attributed string with string and font information
		CGFloat fontSize = ceilf((height / dpiScale) * 1.25f);
		INFO_LOG(G3D, "Creating cocoa typeface '%s' size %d (effective size %0.1f)", APPLE_FONT, height, fontSize);
		// CTFontRef font = CTFontCreateWithName(CFSTR(APPLE_FONT), fontSize, nil);
		CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nil);
		attributes = [NSDictionary dictionaryWithObjectsAndKeys:
			(id)font, kCTFontAttributeName,
			kCFBooleanTrue, kCTForegroundColorFromContextAttributeName,  // Lets us specify the color later.
			nil];
	}
	void Destroy() {
		//CFRelease(font);
		font = {};
	}

	NSDictionary* attributes = nil;
	CTFontRef font = nil;
	std::string fname;
	int height;
	int bold;
	float dpiScale;
};

TextDrawerCocoa::TextDrawerCocoa(Draw::DrawContext *draw) : TextDrawer(draw) {
}

TextDrawerCocoa::~TextDrawerCocoa() {
	ClearCache();

	fontMap_.clear();
}

// TODO: Share with other backends.
uint32_t TextDrawerCocoa::SetFont(const char *fontName, int size, int flags) {
	uint32_t fontHash = fontName ? hash::Adler32((const uint8_t *)fontName, strlen(fontName)) : 0;
	fontHash ^= size;
	fontHash ^= flags << 10;

	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHash;
		return fontHash;
	}

	std::string fname;
	if (fontName)
		fname = fontName;
	else
		fname = APPLE_FONT;

	TextDrawerFontContext *font = new TextDrawerFontContext();
	font->bold = false;
	font->height = size;
	font->fname = fname;
	font->dpiScale = dpiScale_;
	font->Create();

	fontMap_[fontHash] = std::unique_ptr<TextDrawerFontContext>(font);
	fontHash_ = fontHash;
	return fontHash;
}

void TextDrawerCocoa::SetFont(uint32_t fontHandle) {
	auto iter = fontMap_.find(fontHandle);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHandle;
	}
}

void TextDrawerCocoa::ClearCache() {
	for (auto &iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
	}
	cache_.clear();
	sizeCache_.clear();
}

void TextDrawerCocoa::RecreateFonts() {
	for (auto &iter : fontMap_) {
		iter.second->dpiScale = dpiScale_;
		iter.second->Create();
	}
}

void TextDrawerCocoa::MeasureString(std::string_view str, float *w, float *h) {
	CacheKey key{ std::string(str), fontHash_ };
	
	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(key);
	if (iter != sizeCache_.end()) {
		entry = iter->second.get();
	} else {
		auto iter = fontMap_.find(fontHash_);
		NSDictionary *attributes = nil;
		if (iter != fontMap_.end()) {
			attributes = iter->second->attributes;
		}

		std::string toMeasure = ReplaceAll(std::string(str), "&&", "&");

		std::vector<std::string_view> lines;
		SplitString(toMeasure, '\n', lines);

		int extW = 0, extH = 0;
		for (auto &line : lines) {
			NSString *string = [[NSString alloc] initWithBytes:line.data() length:line.size() encoding: NSUTF8StringEncoding];
			NSAttributedString* as = [[NSAttributedString alloc] initWithString:string attributes:attributes];
			CTLineRef ctline = CTLineCreateWithAttributedString((CFAttributedStringRef)as);
			CGFloat ascent, descent, leading;
			double fWidth = CTLineGetTypographicBounds(ctline, &ascent, &descent, &leading);

			size_t width = (size_t)ceilf(fWidth);
			size_t height = (size_t)ceilf(ascent + descent);
	
			if (width > extW)
				extW = width;
			extH += height;
		}

		entry = new TextMeasureEntry();
		entry->width = extW;
		entry->height = extH;
		sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
	}

	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_ * dpiScale_;
	*h = entry->height * fontScaleY_ * dpiScale_;
}


void TextDrawerCocoa::MeasureStringRect(std::string_view str, const Bounds &bounds, float *w, float *h, int align) {
	auto iter = fontMap_.find(fontHash_);
	NSDictionary *attributes = nil;
	if (iter != fontMap_.end()) {
		attributes = iter->second->attributes;
	}

	std::string toMeasure = std::string(str);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	if (wrap) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toMeasure, toMeasure.c_str(), rotated ? bounds.h : bounds.w, wrap);
	}

	std::vector<std::string_view> lines;
	SplitString(toMeasure, '\n', lines);
	int total_w = 0;
	int total_h = 0;
	CacheKey key{ "", fontHash_};
	for (size_t i = 0; i < lines.size(); i++) {
		key.text = lines[i];
		TextMeasureEntry *entry;
		auto iter = sizeCache_.find(key);
		if (iter != sizeCache_.end()) {
			entry = iter->second.get();
		} else {
			std::string line = lines[i].empty() ? " " : ReplaceAll(lines[i], "&&", "&");
			NSString *string = [[NSString alloc] initWithBytes:line.data() length:line.size() encoding: NSUTF8StringEncoding];
			NSAttributedString* as = [[NSAttributedString alloc] initWithString:string attributes:attributes];
			CTLineRef ctline = CTLineCreateWithAttributedString((CFAttributedStringRef)as);
			CGFloat ascent, descent, leading;
			double fWidth = CTLineGetTypographicBounds(ctline, &ascent, &descent, &leading);

			size_t width = (size_t)ceilf(fWidth);
			size_t height = (size_t)ceilf(ascent + descent);
	
			entry = new TextMeasureEntry();
			entry->width = width;
			entry->height = height;
			entry->leading = leading;
			sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
		}
		entry->lastUsedFrame = frameCount_;

		if (total_w < entry->width) {
			total_w = entry->width;
		}
		int h = i == lines.size() - 1 ? entry->height : (entry->height + entry->leading);
		total_h += h;
	}

	*w = total_w * fontScaleX_ * dpiScale_;
	*h = total_h * fontScaleY_ * dpiScale_;
}

void TextDrawerCocoa::DrawString(DrawBuffer &target, std::string_view str, float x, float y, uint32_t color, int align) {
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
	} else {
		DataFormat texFormat;
		// For our purposes these are equivalent, so just choose the supported one. D3D can emulate them.
		if (draw_->GetDataFormatSupport(Draw::DataFormat::A4R4G4B4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
		else if (draw_->GetDataFormatSupport(Draw::DataFormat::R4G4B4A4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
		else if (draw_->GetDataFormatSupport(Draw::DataFormat::B4G4R4A4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::B4G4R4A4_UNORM_PACK16;
		else
			texFormat = Draw::DataFormat::R8G8B8A8_UNORM;

		entry = new TextStringEntry();

		bool emoji = AnyEmojiInString(key.text.c_str(), key.text.size());
		if (emoji)
			texFormat = Draw::DataFormat::R8G8B8A8_UNORM;

		// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
		// because we need white. Well, we could using swizzle, but not all our backends support that.
		TextureDesc desc{};
		std::vector<uint8_t> bitmapData;
		if (!DrawStringBitmap(bitmapData, *entry, texFormat, str, align)) {
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
		entry->texture = draw_->CreateTexture(desc);
		cache_[key] = std::unique_ptr<TextStringEntry>(entry);
	}

	if (entry->texture) {
		draw_->BindTexture(0, entry->texture);
	}

	// Okay, the texture is bound, let's draw.
	float w = entry->width * fontScaleX_ * dpiScale_;
	float h = entry->height * fontScaleY_ * dpiScale_;
	float u = entry->width / (float)entry->bmWidth;
	float v = entry->height / (float)entry->bmHeight;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);
	if (entry->texture) {
		target.DrawTexRect(x, y, x + w, y + h, 0.0f, 0.0f, u, v, color);
		target.Flush(true);
	}
}

bool TextDrawerCocoa::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	std::string printable = ReplaceAll(str, "&&", "&");
	NSString* string = [[NSString alloc] initWithBytes:printable.data() length:printable.length() encoding: NSUTF8StringEncoding];

	auto iter = fontMap_.find(fontHash_);
	if (iter == fontMap_.end()) {
		return false;
	}
	NSDictionary* attributes = iter->second->attributes;
	NSAttributedString* as = [[NSAttributedString alloc] initWithString:string attributes:attributes];

	// Figure out how big an image we need
	CTLineRef line = CTLineCreateWithAttributedString((CFAttributedStringRef)as);
	CGFloat ascent, descent, leading;
	double fWidth = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

	// On iOS 4.0 and Mac OS X v10.6 you can pass null for data
	size_t width = (size_t)ceilf(fWidth) + 1;
	size_t height = (size_t)ceilf(ascent + descent) + 1;

	// Round width and height upwards to the closest multiple of 4.
	width = (width + 3) & ~3;
	height = (height + 3) & ~3;

	if (!width || !height) {
		WARN_LOG(G3D, "Text '%.*s' caused a zero size image", (int)str.length(), str.data());
		return false;
	}

	uint32_t *bitmap = new uint32_t[width * height];
	memset(bitmap, 0, width * height * 4);

	// Create the context and fill it with white background
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGBitmapInfo bitmapInfo = kCGImageAlphaPremultipliedLast;
	CGContextRef ctx = CGBitmapContextCreate(bitmap, width, height, 8, width*4, space, bitmapInfo);
	CGColorSpaceRelease(space);
	// CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 0.0); // white background
	// CGContextFillRect(ctx, CGRectMake(0.0, 0.0, width, height));
	// CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0); // white background
	// CGContextSetRGBStrokeColor(ctx, 1.0, 1.0, 1.0, 1.0); // white background
	CGContextSetStrokeColorWithColor(ctx, [ColorType whiteColor].CGColor);
	CGContextSetFillColorWithColor(ctx, [ColorType whiteColor].CGColor);

	// Draw the text 
	CGFloat x = 0.0;
	CGFloat y = descent;
	CGContextSetTextPosition(ctx, x, y);
	CTLineDraw(line, ctx);
	CFRelease(line);

	entry.texture = nullptr;
	entry.width = width;
	entry.height = height;
	entry.bmWidth = width;
	entry.bmHeight = height;
	entry.lastUsedFrame = frameCount_;

	// data now contains the bytes in RGBA, presumably.
	// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
	// because we need white. Well, we could using swizzle, but not all our backends support that.
	if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM || texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint32_t));
		// If we chose this format, emoji are involved. Pass straight through.
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint32_t color = bitmap[width * y + x];
				bitmapData32[entry.bmWidth * y + x] = color;
			}
		}
	} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)((bitmap[width * y + x] & 0xff) >> 4);
				bitmapData16[entry.bmWidth * y + x] = (bAlpha) | 0xfff0;
			}
		}
	} else if (texFormat == Draw::DataFormat::A4R4G4B4_UNORM_PACK16) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)((bitmap[width * y + x] & 0xff) >> 4);
				bitmapData16[entry.bmWidth * y + x] = (bAlpha << 12) | 0x0fff;
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = bitmap[width * y + x] & 0xff;
				bitmapData[entry.bmWidth * y + x] = bAlpha;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}
	
	delete [] bitmap;
	return true;
}

void TextDrawerCocoa::OncePerFrame() {
	frameCount_++;
	// If DPI changed (small-mode, future proper monitor DPI support), drop everything.
	float newDpiScale = CalculateDPIScale();
	if (newDpiScale != dpiScale_) {
		INFO_LOG(G3D, "TextDrawerCocoa: DPI scale: %0.1f", newDpiScale);
		dpiScale_ = newDpiScale;
		ClearCache();
		RecreateFonts();
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

#endif
