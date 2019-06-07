#include "base/display.h"
#include "base/logging.h"
#include "base/stringutil.h"
#include "thin3d/thin3d.h"
#include "util/hash/hash.h"
#include "util/text/wrap_text.h"
#include "util/text/utf8.h"
#include "gfx_es2/draw_text.h"
#include "gfx_es2/draw_text_uwp.h"


#if PPSSPP_PLATFORM(UWP)

#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_3.h>
#include <dwrite_3.h>

enum {
	MAX_TEXT_WIDTH = 4096,
	MAX_TEXT_HEIGHT = 512
};

class TextDrawerFontContext {
public:
	~TextDrawerFontContext() {
		Destroy();
	}

	void Create() {
		if (textFmt) {
			Destroy();
		}

		if (factory == nullptr) return;

		HRESULT hr = factory->CreateTextFormat(
			fname.c_str(),
			fontCollection,
			weight,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			MulDiv(height, (int)(96.0f * (1.0f / dpiScale)), 72),
			L"",
			&textFmt
		);
	}
	void Destroy() {
		textFmt->Release();
		textFmt = nullptr;
	}

	IDWriteFactory4* factory = nullptr;
	IDWriteFontCollection1* fontCollection = nullptr;
	IDWriteTextFormat* textFmt = nullptr;
	std::wstring fname;
	int height;
	DWRITE_FONT_WEIGHT weight;
	float dpiScale;
};

struct TextDrawerContext {
	ID2D1Bitmap1* bitmap;
	ID2D1Bitmap1* mirror_bmp;
};

TextDrawerUWP::TextDrawerUWP(Draw::DrawContext *draw) : TextDrawer(draw), ctx_(nullptr) {
	HRESULT hr;
	// It's fine to assume we are using D3D11 in UWP
	ID3D11Device* d3ddevice = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);

	IDXGIDevice* dxgiDevice;
	hr = d3ddevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
	if (FAILED(hr)) FLOG("ID3DDevice QueryInterface IDXGIDevice failed");
	
	// Initialize the Direct2D Factory.
	D2D1_FACTORY_OPTIONS options = {};
	D2D1CreateFactory(
		D2D1_FACTORY_TYPE_SINGLE_THREADED,
		__uuidof(ID2D1Factory5),
		&options,
		(void**)&m_d2dFactory
	);

	// Initialize the DirectWrite Factory.
	DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory5),
		(IUnknown**)&m_dwriteFactory
	);

	// Create D2D Device and DeviceContext.
	// TODO: We have one sitting right in DX::DeviceResource, there might be a way to use that instead.
	hr = m_d2dFactory->CreateDevice(dxgiDevice, &m_d2dDevice);
	if (FAILED(hr)) FLOG("D2D CreateDevice failed");
	hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
	if (FAILED(hr)) FLOG("D2D CreateDeviceContext failed");

	// Load the Roboto font
	hr = m_dwriteFactory->CreateFontFileReference(L"Content/Roboto-Condensed.ttf", nullptr, &m_fontFile);
	if (FAILED(hr)) ELOG("CreateFontFileReference failed");
	hr = m_dwriteFactory->CreateFontSetBuilder(&m_fontSetBuilder);
	if (FAILED(hr)) ELOG("CreateFontSetBuilder failed");
	hr = m_fontSetBuilder->AddFontFile(m_fontFile);
	hr = m_fontSetBuilder->CreateFontSet(&m_fontSet);
	hr = m_dwriteFactory->CreateFontCollectionFromFontSet(m_fontSet, &m_fontCollection);

	ctx_ = new TextDrawerContext();
	
	D2D1_BITMAP_PROPERTIES1 properties;
	properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	properties.dpiX = 96.0f;
	properties.dpiY = 96.0f;
	properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_TARGET;
	properties.colorContext = nullptr;

	// Create main drawing bitmap
	m_d2dContext->CreateBitmap(
		D2D1::SizeU(MAX_TEXT_WIDTH, MAX_TEXT_HEIGHT),
		nullptr,
		0,
		&properties,
		&ctx_->bitmap
	);
	m_d2dContext->SetTarget(ctx_->bitmap);
	
	// Create mirror bitmap for mapping
	properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;
	m_d2dContext->CreateBitmap(
		D2D1::SizeU(MAX_TEXT_WIDTH, MAX_TEXT_HEIGHT),
		nullptr,
		0,
		&properties,
		&ctx_->mirror_bmp
	);

	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &m_d2dWhiteBrush);

}

TextDrawerUWP::~TextDrawerUWP() {
	ClearCache();

	fontMap_.clear();

	ctx_->bitmap->Release();
	ctx_->mirror_bmp->Release();

	m_d2dWhiteBrush->Release();
	m_d2dContext->Release();
	m_d2dDevice->Release();
	m_d2dFactory->Release();

	m_fontCollection->Release();
	m_fontSet->Release();
	m_fontFile->Release();
	m_fontSetBuilder->Release();
	m_dwriteFactory->Release();
	delete ctx_;
}

uint32_t TextDrawerUWP::SetFont(const char *fontName, int size, int flags) {
	uint32_t fontHash = fontName ? hash::Adler32((const uint8_t *)fontName, strlen(fontName)) : 0;
	fontHash ^= size;
	fontHash ^= flags << 10;

	auto iter = fontMap_.find(fontHash);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHash;
		return fontHash;
	}

	std::wstring fname;
	if (fontName)
		fname = ConvertUTF8ToWString(fontName);
	else
		fname = L"Tahoma";

	TextDrawerFontContext *font = new TextDrawerFontContext();
	font->weight = DWRITE_FONT_WEIGHT_LIGHT;
	font->height = size;
	font->fname = fname;
	font->dpiScale = dpiScale_;
	font->factory = m_dwriteFactory;
	font->fontCollection = m_fontCollection;
	font->Create();

	fontMap_[fontHash] = std::unique_ptr<TextDrawerFontContext>(font);
	fontHash_ = fontHash;
	return fontHash;
}

void TextDrawerUWP::SetFont(uint32_t fontHandle) {
	auto iter = fontMap_.find(fontHandle);
	if (iter != fontMap_.end()) {
		fontHash_ = fontHandle;
	}
}

void TextDrawerUWP::MeasureString(const char *str, size_t len, float *w, float *h) {
	CacheKey key{ std::string(str, len), fontHash_ };
	
	TextMeasureEntry *entry;
	auto iter = sizeCache_.find(key);
	if (iter != sizeCache_.end()) {
		entry = iter->second.get();
	} else {
		IDWriteTextFormat* format = nullptr;
		auto iter = fontMap_.find(fontHash_);
		if (iter != fontMap_.end()) {
			format = iter->second->textFmt;
		}
		if (!format) return;

		std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(ReplaceAll(std::string(str, len), "\n", "\r\n"), "&&", "&"));

		format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
		
		IDWriteTextLayout* layout;
		m_dwriteFactory->CreateTextLayout(
			(LPWSTR)wstr.c_str(),
			(int)wstr.size(),
			format,
			MAX_TEXT_WIDTH,
			MAX_TEXT_HEIGHT,
			&layout
		);

		DWRITE_TEXT_METRICS metrics;
		layout->GetMetrics(&metrics);
		layout->Release();

		entry = new TextMeasureEntry();
		entry->width = metrics.width + 1;
		entry->height = metrics.height + 1;
		sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
	}

	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_ * dpiScale_;
	*h = entry->height * fontScaleY_ * dpiScale_;
}

void TextDrawerUWP::MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align) {

	IDWriteTextFormat* format = nullptr;
	auto iter = fontMap_.find(fontHash_);
	if (iter != fontMap_.end()) {
		format = iter->second->textFmt;
	}
	if (!format) return;

	std::string toMeasure = std::string(str, len);
	if (align & FLAG_WRAP_TEXT) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toMeasure, toMeasure.c_str(), rotated ? bounds.h : bounds.w);
	}

	std::vector<std::string> lines;
	SplitString(toMeasure, '\n', lines);
	float total_w = 0.0f;
	float total_h = 0.0f;
	for (size_t i = 0; i < lines.size(); i++) {
		CacheKey key{ lines[i], fontHash_ };

		TextMeasureEntry *entry;
		auto iter = sizeCache_.find(key);
		if (iter != sizeCache_.end()) {
			entry = iter->second.get();
		} else {
			std::wstring wstr = ConvertUTF8ToWString(lines[i].length() == 0 ? " " : ReplaceAll(lines[i], "&&", "&"));

			if (align & ALIGN_HCENTER)
				format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			else if (align & ALIGN_LEFT)
				format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			else if (align & ALIGN_RIGHT)
				format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

			IDWriteTextLayout* layout;
			m_dwriteFactory->CreateTextLayout(
				(LPWSTR)wstr.c_str(),
				(int)wstr.size(),
				format,
				MAX_TEXT_WIDTH,
				MAX_TEXT_HEIGHT,
				&layout
			);

			DWRITE_TEXT_METRICS metrics;
			layout->GetMetrics(&metrics);
			layout->Release();

			entry = new TextMeasureEntry();
			entry->width = metrics.width + 1;
			entry->height = metrics.height + 1;
			sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
		}
		entry->lastUsedFrame = frameCount_;

		if (total_w < entry->width * fontScaleX_) {
			total_w = entry->width * fontScaleX_;
		}
		total_h += entry->height * fontScaleY_;
	}
	*w = total_w * dpiScale_;
	*h = total_h * dpiScale_;
}

void TextDrawerUWP::DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align) {
	using namespace Draw;
	if (!strlen(str))
		return;

	CacheKey key{ std::string(str), fontHash_ };

	target.Flush(true);

	TextStringEntry *entry;

	auto iter = cache_.find(key);
	if (iter != cache_.end()) {
		entry = iter->second.get();
		entry->lastUsedFrame = frameCount_;
	}
	else {
		// Render the string to our bitmap and save to a GL texture.
		std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(ReplaceAll(str, "\n", "\r\n"), "&&", "&"));
		SIZE size;

		IDWriteTextFormat* format = nullptr;
		auto iter = fontMap_.find(fontHash_);
		if (iter != fontMap_.end()) {
			format = iter->second->textFmt;
		}
		if (!format) return;

		if (align & ALIGN_HCENTER)
			format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		else if (align & ALIGN_LEFT)
			format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
		else if (align & ALIGN_RIGHT)
			format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
		
		IDWriteTextLayout* layout;
		m_dwriteFactory->CreateTextLayout(
			(LPWSTR)wstr.c_str(),
			(int)wstr.size(),
			format,
			MAX_TEXT_WIDTH,
			MAX_TEXT_HEIGHT,
			&layout
		);

		DWRITE_TEXT_METRICS metrics;
		layout->GetMetrics(&metrics);

		// Set the max size the same as current size, avoiding alignment issues
		layout->SetMaxHeight(metrics.height);
		layout->SetMaxWidth(metrics.width);

		size.cx = metrics.width + 1;
		size.cy = metrics.height + 1;

		// Prevent zero-sized textures, which can occur. Not worth to avoid
		// creating the texture altogether in this case. One example is a string
		// containing only '\r\n', see issue #10764.
		if (size.cx == 0)
			size.cx = 1;
		if (size.cy == 0)
			size.cy = 1;

		entry = new TextStringEntry();
		entry->width = size.cx;
		entry->height = size.cy;
		entry->bmWidth = (size.cx + 3) & ~3;
		entry->bmHeight = (size.cy + 3) & ~3;
		entry->lastUsedFrame = frameCount_;

		m_d2dContext->BeginDraw();
		m_d2dContext->Clear();
		m_d2dContext->DrawTextLayout(D2D1::Point2F(0.0f, 0.0f), layout, m_d2dWhiteBrush);
		m_d2dContext->EndDraw();

		layout->Release();

		D2D1_POINT_2U dstP = D2D1::Point2U(0, 0);
		D2D1_RECT_U srcR = D2D1::RectU(0, 0, entry->bmWidth, entry->bmHeight);
		D2D1_MAPPED_RECT map;
		ctx_->mirror_bmp->CopyFromBitmap(&dstP, ctx_->bitmap, &srcR);
		ctx_->mirror_bmp->Map(D2D1_MAP_OPTIONS_READ, &map);

		DataFormat texFormat;
		// For our purposes these are equivalent, so just choose the supported one. D3D can emulate them.
		if (draw_->GetDataFormatSupport(Draw::DataFormat::A4R4G4B4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
		else if (draw_->GetDataFormatSupport(Draw::DataFormat::B4G4R4A4_UNORM_PACK16) & FMT_TEXTURE)
			texFormat = Draw::DataFormat::B4G4R4A4_UNORM_PACK16;
		else
			texFormat = Draw::DataFormat::R8G8B8A8_UNORM;

		// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
		// because we need white. Well, we could using swizzle, but not all our backends support that.
		TextureDesc desc{};
		uint32_t *bitmapData32 = nullptr;
		uint16_t *bitmapData16 = nullptr;
		if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM || texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
			bitmapData32 = new uint32_t[entry->bmWidth * entry->bmHeight];
			for (int y = 0; y < entry->bmHeight; y++) {
				for (int x = 0; x < entry->bmWidth; x++) {
					uint8_t bAlpha = (uint8_t)(map.bits[map.pitch * y + x * 4] & 0xff);
					bitmapData32[entry->bmWidth * y + x] = (bAlpha << 24) | 0x00ffffff;
				}
			}
			desc.initData.push_back((uint8_t *)bitmapData32);
		} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16) {
			bitmapData16 = new uint16_t[entry->bmWidth * entry->bmHeight];
			for (int y = 0; y < entry->bmHeight; y++) {
				for (int x = 0; x < entry->bmWidth; x++) {
					uint8_t bAlpha = (uint8_t)((map.bits[map.pitch * y + x * 4] & 0xff) >> 4);
					bitmapData16[entry->bmWidth * y + x] = (bAlpha) | 0xfff0;
				}
			}
			desc.initData.push_back((uint8_t *)bitmapData16);
		} else if (texFormat == Draw::DataFormat::A4R4G4B4_UNORM_PACK16) {
			bitmapData16 = new uint16_t[entry->bmWidth * entry->bmHeight];
			for (int y = 0; y < entry->bmHeight; y++) {
				for (int x = 0; x < entry->bmWidth; x++) {
					uint8_t bAlpha = (uint8_t)((map.bits[map.pitch * y + x * 4] & 0xff) >> 4);
					bitmapData16[entry->bmWidth * y + x] = (bAlpha << 12) | 0x0fff;
				}
			}
			desc.initData.push_back((uint8_t *)bitmapData16);
		}

		ctx_->mirror_bmp->Unmap();

		desc.type = TextureType::LINEAR2D;
		desc.format = texFormat;
		desc.width = entry->bmWidth;
		desc.height = entry->bmHeight;
		desc.depth = 1;
		desc.mipLevels = 1;
		desc.tag = "TextDrawer";
		entry->texture = draw_->CreateTexture(desc);
		if (bitmapData16)
			delete[] bitmapData16;
		if (bitmapData32)
			delete[] bitmapData32;
		cache_[key] = std::unique_ptr<TextStringEntry>(entry);
	}

	draw_->BindTexture(0, entry->texture);

	// Okay, the texture is bound, let's draw.
	float w = entry->width * fontScaleX_ * dpiScale_;
	float h = entry->height * fontScaleY_ * dpiScale_;
	float u = entry->width / (float)entry->bmWidth;
	float v = entry->height / (float)entry->bmHeight;
	DrawBuffer::DoAlign(align, &x, &y, &w, &h);
	target.DrawTexRect(x, y, x + w, y + h, 0.0f, 0.0f, u, v, color);
	target.Flush(true);
	
}

void TextDrawerUWP::RecreateFonts() {
	for (auto& iter : fontMap_) {
		iter.second->dpiScale = dpiScale_;
		iter.second->Create();
	}
}

void TextDrawerUWP::ClearCache() {
	for (auto &iter : cache_) {
		if (iter.second->texture)
			iter.second->texture->Release();
	}
	cache_.clear();
	sizeCache_.clear();
}

void TextDrawerUWP::DrawStringRect(DrawBuffer &target, const char *str, const Bounds &bounds, uint32_t color, int align) {
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

	std::string toDraw = str;
	if (align & FLAG_WRAP_TEXT) {
		bool rotated = (align & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) != 0;
		WrapString(toDraw, str, rotated ? bounds.h : bounds.w);
	}

	DrawString(target, toDraw.c_str(), x, y, color, align);
}

void TextDrawerUWP::OncePerFrame() {
	frameCount_++;
	// If DPI changed (small-mode, future proper monitor DPI support), drop everything.
	float newDpiScale = CalculateDPIScale();
	if (newDpiScale != dpiScale_) {
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
