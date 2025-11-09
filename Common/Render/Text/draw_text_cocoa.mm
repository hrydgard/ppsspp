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

#define APPLE_FONT "RobotoCondensed-Regular"

// for future OpenEmu support
#ifndef PPSSPP_FONT_BUNDLE
#define PPSSPP_FONT_BUNDLE [NSBundle mainBundle]
#endif

class TextDrawerFontContext {
public:
	TextDrawerFontContext(const FontStyle &_style, float _dpiScale) : style(_style), dpiScale(_dpiScale) {
		FontStyleFlags styleFlags = style.flags;
		std::string fontName = GetFontNameForFontStyle(style, &styleFlags);

		// Create an attributed string with string and font information
		CGFloat fontSize = ceilf((style.sizePts / dpiScale) * 1.25f);
		INFO_LOG(Log::G3D, "Creating cocoa typeface '%s' size %d (effective size %0.1f)", fontName.c_str(), style.sizePts, fontSize);

		CTFontSymbolicTraits traits = 0;
		if (styleFlags & FontStyleFlags::Bold)   traits |= kCTFontTraitBold;
		if (styleFlags & FontStyleFlags::Italic) traits |= kCTFontTraitItalic;

		CTFontRef base = CTFontCreateWithName(CFStringCreateWithCString(kCFAllocatorDefault, fontName.c_str(), kCFStringEncodingUTF8), fontSize, nil);
		CTFontRef font = CTFontCreateCopyWithSymbolicTraits(base, fontSize, NULL, traits, traits); // desired & mask
		if (!font) {
			// Skip the traits.
			font = base;
		} else {
			CFRelease(base);
		}

		_dbg_assert_(font != nil);
		// CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nil);
		attributes = @{
			(__bridge id)kCTFontAttributeName: (__bridge id)font,
			(__bridge id)kCTForegroundColorFromContextAttributeName: (__bridge id)kCFBooleanTrue,
		};
		CFRelease(font);
	}
	~TextDrawerFontContext() {
		Destroy();
	}
	void Destroy() {
	}

	NSDictionary* attributes = nil;
	std::string fname;

	FontStyle style;
	float dpiScale;
};

TextDrawerCocoa::TextDrawerCocoa(Draw::DrawContext *draw) : TextDrawer(draw) {
	// Register fonts with CoreText
	// We only need to do this once.
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
		std::vector<std::string> allFonts = GetAllFontFilenames();

		for (const auto &fontName : allFonts) {
			// Convert C++ string to NSString
			NSString *fontFileName = [NSString stringWithUTF8String:fontName.c_str()];

			// Get the font URL from the bundle
			NSURL *fontURL = [PPSSPP_FONT_BUNDLE URLForResource:fontFileName
													withExtension:@"ttf"
													subdirectory:@"assets"];
			if (!fontURL) {
				NSLog(@"Font URL not found for %@", fontFileName);
				continue;
			}

			// Optional: Print font descriptors for debugging
			CFArrayRef descs = CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)fontURL);
			if (descs) {
				CFIndex count = CFArrayGetCount(descs);
				NSLog(@"Found %ld font descriptor(s) for %@", count, fontFileName);

				for (CFIndex i = 0; i < count; ++i) {
					CTFontDescriptorRef desc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descs, i);
					CFTypeRef attr = CTFontDescriptorCopyAttribute(desc, kCTFontNameAttribute);
					if (attr && CFGetTypeID(attr) == CFStringGetTypeID()) {
						CFStringRef name = (CFStringRef)attr;
						NSLog(@"Descriptor #%ld: %@", i, name);
						CFRelease(name);
					} else {
						NSLog(@"Descriptor #%ld: Unknown or non-string attribute", i);
						if (attr) CFRelease(attr);
					}
				}

				CFRelease(descs);
			} else {
				NSLog(@"Failed to retrieve font descriptors for %@", fontFileName);
			}

			// Register the font
			CTFontManagerRegisterFontsForURL((__bridge CFURLRef)fontURL, kCTFontManagerScopeProcess, NULL);
		}
	});
}

TextDrawerCocoa::~TextDrawerCocoa() {
	ClearCache();
	ClearFonts();
}

// TODO: Share with other backends.
void TextDrawerCocoa::SetOrCreateFont(const FontStyle &style) {
	auto iter = fontMap_.find(style);
	if (iter != fontMap_.end()) {
		fontStyle_ = style;
		return;
	}

	TextDrawerFontContext *font = new TextDrawerFontContext(style, dpiScale_);

	fontMap_[style] = std::unique_ptr<TextDrawerFontContext>(font);
	fontStyle_ = style;
}

void TextDrawerCocoa::ClearFonts() {
	for (auto &iter : fontMap_) {
		iter.second->dpiScale = dpiScale_;
		iter.second->Destroy();
	}
	fontMap_.clear();
}

void TextDrawerCocoa::MeasureStringInternal(std::string_view str, float *w, float *h) {
	// INFO_LOG(Log::System, "Measuring %.*s", (int)str.length(), str.data());
	auto iter = fontMap_.find(fontStyle_);
	NSDictionary *attributes = nil;
	if (iter != fontMap_.end()) {
		attributes = iter->second->attributes;
	}

	std::vector<std::string_view> lines;
	SplitString(str, '\n', lines);

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

	*w = extW;
	*h = extH;
}

bool TextDrawerCocoa::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	auto iter = fontMap_.find(fontStyle_);
	if (iter == fontMap_.end()) {
		return false;
	}

	// INFO_LOG(Log::System, "Rasterizing %.*s", (int)str.length(), str.data());

	NSString* string = [[NSString alloc] initWithBytes:str.data() length:str.length() encoding: NSUTF8StringEncoding];

	NSDictionary* attributes = iter->second->attributes;
	NSAttributedString* as = [[NSAttributedString alloc] initWithString:string attributes:attributes];

	// Figure out how big an image we need.
	// We re-use MeasureString here.
	float w, h;
	MeasureString(str, &w, &h);
	// Reverse the DPI scale that MeasureString baked in.
	w /= (dpiScale_ * fontScaleX_);
	h /= (dpiScale_ * fontScaleY_);

	int width = (int)ceilf(w);
	int height = (int)ceilf(h);
	if (width <= 0 || height <= 0) {
		WARN_LOG(Log::G3D, "Text '%.*s' caused a zero size image", (int)str.length(), str.data());
		return false;
	}

	// Round width and height upwards to the closest multiple of 4.
	int bmWidth = (width + 1 + 3) & ~3;
	int bmHeight = (height + 1 + 3) & ~3;

	uint32_t *bitmap = new uint32_t[bmWidth * bmHeight];
	memset(bitmap, 0, bmWidth * bmHeight * 4);

	// Create the context and fill it with white background
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGBitmapInfo bitmapInfo = kCGImageAlphaPremultipliedLast;
	CGContextRef ctx = CGBitmapContextCreate(bitmap, bmWidth, bmHeight, 8, bmWidth*4, space, bitmapInfo);
	CGColorSpaceRelease(space);
	// CGContextSetRGBStrokeColor(ctx, 1.0, 1.0, 1.0, 1.0); // white background
	CGContextSetStrokeColorWithColor(ctx, [ColorType whiteColor].CGColor);
	CGContextSetFillColorWithColor(ctx, [ColorType whiteColor].CGColor);

	std::vector<std::string_view> lines;
	SplitString(str, '\n', lines);

	float lineY = 0.0;
	for (std::string_view line : lines) {
		NSString *string = [[NSString alloc] initWithBytes:line.data() length:line.size() encoding: NSUTF8StringEncoding];
		NSAttributedString* as = [[NSAttributedString alloc] initWithString:string attributes:attributes];
		CTLineRef ctline = CTLineCreateWithAttributedString((CFAttributedStringRef)as);
		CGFloat ascent, descent, leading;
		double fWidth = CTLineGetTypographicBounds(ctline, &ascent, &descent, &leading);

		// Draw the text
		CGFloat x = 0.0;
		CGFloat y = bmHeight - lineY - ascent;  // from bottom???
		CGContextSetTextPosition(ctx, x, y);
		CTLineDraw(ctline, ctx);
		CFRelease(ctline);

		lineY += ascent + descent + leading;
	}

	entry.texture = nullptr;
	entry.width = width;
	entry.height = height;
	entry.bmWidth = bmWidth;
	entry.bmHeight = bmHeight;
	entry.lastUsedFrame = frameCount_;

	// data now contains the bytes in RGBA, presumably.
	// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
	// because we need white. Well, we could using swizzle, but not all our backends support that.
	if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM || texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint32_t));
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint32_t color = bitmap[bmWidth * y + x];
				if (fullColor) {
					bitmapData32[entry.bmWidth * y + x] = RGBAToPremul8888(color);
				} else {
					// Don't know why we'd end up here, but let's support it.
					bitmapData32[entry.bmWidth * y + x] = AlphaToPremul8888(color);
				}
			}
		}
	} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(bitmap[bmWidth * y + x] & 0xff);
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(bAlpha);
			}
		}
	} else if (texFormat == Draw::DataFormat::A4R4G4B4_UNORM_PACK16) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(bitmap[bmWidth * y + x] & 0xff);
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(bAlpha);
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = bitmap[bmWidth * y + x] & 0xff;
				bitmapData[entry.bmWidth * y + x] = bAlpha;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}
	
	delete [] bitmap;
	return true;
}

#endif
