#include "ppsspp_config.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_uwp.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"

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

		if (!factory) {
			return;
		}

		HRESULT hr = factory->CreateTextFormat(
			fname.c_str(),
			fontCollection,
			weight,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			(float)MulDiv(height, (int)(96.0f * (1.0f / dpiScale)), 72),
			L"",
			&textFmt
		);
		if (FAILED(hr)) {
			ERROR_LOG(Log::G3D, "Failed creating font %s", fname.c_str());
		}
	}
	void Destroy() {
		if (textFmt) {
			textFmt->Release();
		}
		textFmt = nullptr;
	}

	IDWriteFactory4 *factory = nullptr;
	IDWriteFontCollection1 *fontCollection = nullptr;
	IDWriteTextFormat *textFmt = nullptr;
	std::wstring fname;
	int height;
	DWRITE_FONT_WEIGHT weight;
	float dpiScale;
};

struct TextDrawerContext {
	ID2D1Bitmap1 *bitmap;
	ID2D1Bitmap1 *mirror_bmp;
};

TextDrawerUWP::TextDrawerUWP(Draw::DrawContext *draw) : TextDrawer(draw), ctx_(nullptr) {
	HRESULT hr;
	// It's fine to assume we are using D3D11 in UWP
	ID3D11Device* d3ddevice = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);

	IDXGIDevice* dxgiDevice;
	hr = d3ddevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
	if (FAILED(hr)) _assert_msg_(false, "ID3DDevice QueryInterface IDXGIDevice failed");
	
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
	if (FAILED(hr)) _assert_msg_(false, "D2D CreateDevice failed");
	hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
	if (FAILED(hr)) _assert_msg_(false, "D2D CreateDeviceContext failed");

	// Load the Roboto font
	hr = m_dwriteFactory->CreateFontFileReference(L"Content/Roboto-Condensed.ttf", nullptr, &m_fontFile);
	if (FAILED(hr)) ERROR_LOG(Log::System, "CreateFontFileReference failed");
	hr = m_dwriteFactory->CreateFontSetBuilder(&m_fontSetBuilder);
	if (FAILED(hr)) ERROR_LOG(Log::System, "CreateFontSetBuilder failed");
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
	ClearFonts();

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

void TextDrawerUWP::MeasureString(std::string_view str, float *w, float *h) {
	CacheKey key{ std::string(str), fontHash_ };
	
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

		std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(std::string(str), "\n", "\r\n"));

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
		entry->width = (int)(metrics.width + 1.0f);
		entry->height = (int)(metrics.height + 1.0f);
		sizeCache_[key] = std::unique_ptr<TextMeasureEntry>(entry);
	}

	entry->lastUsedFrame = frameCount_;
	*w = entry->width * fontScaleX_ * dpiScale_;
	*h = entry->height * fontScaleY_ * dpiScale_;
}

bool TextDrawerUWP::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(str, "\n", "\r\n"));
	SIZE size;

	IDWriteTextFormat *format = nullptr;
	auto iter = fontMap_.find(fontHash_);
	if (iter != fontMap_.end()) {
		format = iter->second->textFmt;
	}
	if (!format) {
		bitmapData.clear();
		return false;
	}

	if (align & ALIGN_HCENTER)
		format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	else if (align & ALIGN_LEFT)
		format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	else if (align & ALIGN_RIGHT)
		format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

	IDWriteTextLayout *layout;
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

	size.cx = (int)metrics.width + 1;
	size.cy = (int)metrics.height + 1;

	if (size.cx > MAX_TEXT_WIDTH)
		size.cx = MAX_TEXT_WIDTH;
	if (size.cy > MAX_TEXT_HEIGHT)
		size.cy = MAX_TEXT_HEIGHT;

	// Prevent zero-sized textures, which can occur. Not worth to avoid
	// creating the texture altogether in this case. One example is a string
	// containing only '\r\n', see issue #10764.
	if (size.cx == 0)
		size.cx = 1;
	if (size.cy == 0)
		size.cy = 1;

	entry.texture = nullptr;
	entry.width = size.cx;
	entry.height = size.cy;
	entry.bmWidth = (size.cx + 3) & ~3;
	entry.bmHeight = (size.cy + 3) & ~3;
	entry.lastUsedFrame = frameCount_;

	m_d2dContext->BeginDraw();
	m_d2dContext->Clear();
	m_d2dContext->DrawTextLayout(D2D1::Point2F(0.0f, 0.0f), layout, m_d2dWhiteBrush, texFormat == Draw::DataFormat::R8G8B8A8_UNORM ? D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT : D2D1_DRAW_TEXT_OPTIONS_NONE);
	m_d2dContext->EndDraw();

	layout->Release();

	D2D1_POINT_2U dstP = D2D1::Point2U(0, 0);
	D2D1_RECT_U srcR = D2D1::RectU(0, 0, entry.bmWidth, entry.bmHeight);
	D2D1_MAPPED_RECT map;
	ctx_->mirror_bmp->CopyFromBitmap(&dstP, ctx_->bitmap, &srcR);
	ctx_->mirror_bmp->Map(D2D1_MAP_OPTIONS_READ, &map);

	// Convert the bitmap to a Thin3D compatible array of 16-bit pixels. Can't use a single channel format
	// because we need white. Well, we could using swizzle, but not all our backends support that.
	if (texFormat == Draw::DataFormat::R8G8B8A8_UNORM || texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint32_t));
		bool swap = texFormat == Draw::DataFormat::R8G8B8A8_UNORM;
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			uint32_t *bmpLine = (uint32_t *)&map.bits[map.pitch * y];
			for (int x = 0; x < entry.bmWidth; x++) {
				uint32_t v = bmpLine[x];
				if (fullColor) {
					if (swap)
						v = (v & 0xFF00FF00) | ((v >> 16) & 0xFF) | ((v << 16) & 0xFF0000);
					bitmapData32[entry.bmWidth * y + x] = v;
				} else {
					bitmapData32[entry.bmWidth * y + x] = (v << 24) | 0xFFFFFF;
				}
			}
		}
	} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)((map.bits[map.pitch * y + x * 4] & 0xff) >> 4);
				bitmapData16[entry.bmWidth * y + x] = (bAlpha) | 0xfff0;
			}
		}
	} else if (texFormat == Draw::DataFormat::A4R4G4B4_UNORM_PACK16) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)((map.bits[map.pitch * y + x * 4] & 0xff) >> 4);
				bitmapData16[entry.bmWidth * y + x] = (bAlpha << 12) | 0x0fff;
			}
		}
	} else if (texFormat == Draw::DataFormat::R8_UNORM) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight);
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(map.bits[map.pitch * y + x * 4] & 0xff);
				bitmapData[entry.bmWidth * y + x] = bAlpha;
			}
		}
	} else {
		_assert_msg_(false, "Bad TextDrawer format");
	}

	ctx_->mirror_bmp->Unmap();
	return true;
}

void TextDrawerUWP::ClearFonts() {
	for (auto &iter : fontMap_) {
		iter.second->dpiScale = dpiScale_;
		iter.second->Destroy();
	}
	fontMap_.clear();
}

#endif
