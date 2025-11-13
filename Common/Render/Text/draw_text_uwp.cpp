#include "ppsspp_config.h"

#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Hash/Hash.h"
#include "Common/Data/Text/WrapText.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/Text/draw_text_uwp.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/File/Path.h"

#if PPSSPP_PLATFORM(UWP)
#include <string>

#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_3.h>
#include <dwrite_3.h>

using namespace Microsoft::WRL;

enum {
	MAX_TEXT_WIDTH = 4096,
	MAX_TEXT_HEIGHT = 512
};

class TextDrawerFontContext {
public:
	TextDrawerFontContext(const FontStyle &_style, float _dpiScale, IDWriteFactory4 *factory, IDWriteFontCollection1 *fontCollection) : style(_style), dpiScale(_dpiScale) {
		DWRITE_FONT_STYLE fontStyle = DWRITE_FONT_STYLE_NORMAL;
		DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
		int height = style.sizePts;
		FontStyleFlags styleFlags = FontStyleFlags::Default;
		std::string fontName = GetFontNameForFontStyle(style, &styleFlags);
		if (styleFlags & FontStyleFlags::Bold) {
			weight = DWRITE_FONT_WEIGHT_BOLD;
		}
		if (styleFlags & FontStyleFlags::Italic) {
			fontStyle = DWRITE_FONT_STYLE_ITALIC;
		}

		HRESULT hr = factory->CreateTextFormat(
			ConvertUTF8ToWString(fontName).c_str(),
			fontCollection,
			weight,
			fontStyle,
			DWRITE_FONT_STRETCH_NORMAL,
			(float)MulDiv(height, (int)(96.0f * (1.0f / dpiScale)), 72),
			L"en-us",
			&textFmt
		);
		if (FAILED(hr)) {
			ERROR_LOG(Log::G3D, "Failed creating text format for font %s", fontName.c_str());
		}
	}

	~TextDrawerFontContext() {
		if (textFmt) {
			textFmt->Release();
		}
		textFmt = nullptr;
	}

	IDWriteTextFormat *textFmt = nullptr;
	FontStyle style;
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

	hr = m_dwriteFactory->CreateFontSetBuilder(&m_fontSetBuilder);
	if (FAILED(hr)) ERROR_LOG(Log::System, "CreateFontSetBuilder failed");

	hr = m_dwriteFactory->CreateInMemoryFontFileLoader(&m_inMemoryLoader);
	if (FAILED(hr)) {
		_assert_msg_(false, "D2D CreateInMemoryFontFileLoader failed");
	}
	hr = m_dwriteFactory->RegisterFontFileLoader(m_inMemoryLoader);
	if (FAILED(hr)) {
		_assert_msg_(false, "D2D RegisterFontFileLoader failed");
	}
	// Load our fonts.
	const std::vector<std::string> fontFilenames = GetAllFontFilenames();
	for (const auto &fname : fontFilenames) {
		size_t size;
		uint8_t *data = g_VFS.ReadFile((fname + ".ttf").c_str(), &size);

		ComPtr<IDWriteFontFile> fontFile;
		hr = m_inMemoryLoader->CreateInMemoryFontFileReference(
			m_dwriteFactory,
			data,
			static_cast<UINT32>(size),
			nullptr,       // optional last write time
			&fontFile
		);

		m_fontSetBuilder->AddFontFile(fontFile.Get());

		delete[] data;
		m_fontFiles.push_back(fontFile);
	}

	hr = m_fontSetBuilder->CreateFontSet(&m_fontSet);
	hr = m_dwriteFactory->CreateFontCollectionFromFontSet(m_fontSet, &m_fontCollection);

	UINT32 familyCount = m_fontCollection->GetFontFamilyCount();
	for (UINT32 i = 0; i < familyCount; ++i) {
		IDWriteFontFamily *family;
		m_fontCollection->GetFontFamily(i, &family);

		IDWriteLocalizedStrings *names;
		family->GetFamilyNames(&names);

		UINT32 length = 0;
		names->GetStringLength(0, &length);

		std::wstring name(length + 1, L'\0');
		names->GetString(0, &name[0], length + 1);

		std::string nname = ConvertWStringToUTF8(name);

		NOTICE_LOG(Log::G3D, "Font family: %s", nname.c_str());

		names->Release();
		family->Release();
	}

	ctx_ = new TextDrawerContext();
	
	D2D1_BITMAP_PROPERTIES1 properties{};
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

	m_dwriteFactory->UnregisterFontFileLoader(m_inMemoryLoader);
	m_fontCollection->Release();
	m_fontSet->Release();
	for (auto file : m_fontFiles) {
		file->Release();
	}
	m_fontFiles.clear();
	m_fontSetBuilder->Release();
	m_dwriteFactory->Release();
	m_inMemoryLoader->Release();

	m_d2dWhiteBrush->Release();
	m_d2dContext->Release();
	m_d2dDevice->Release();
	m_d2dFactory->Release();
	delete ctx_;
}

void TextDrawerUWP::SetOrCreateFont(const FontStyle &style) {
	auto iter = fontMap_.find(style);
	if (iter != fontMap_.end()) {
		fontStyle_ = style;
		return;
	}

	fontMap_[style] = std::make_unique<TextDrawerFontContext>(style, dpiScale_, m_dwriteFactory, m_fontCollection);
	fontStyle_ = style;
}

void TextDrawerUWP::MeasureStringInternal(std::string_view str, float *w, float *h) {
	IDWriteTextFormat* format = nullptr;
	auto iter = fontMap_.find(fontStyle_);
	if (iter != fontMap_.end()) {
		format = iter->second->textFmt;
	}
	if (!format) return;

	std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(str, "\n", "\r\n"));

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

	*w = (int)(metrics.width + 1.0f);
	*h = (int)(metrics.height + 1.0f);
}

bool TextDrawerUWP::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	if (str.empty()) {
		bitmapData.clear();
		return false;
	}

	std::wstring wstr = ConvertUTF8ToWString(ReplaceAll(str, "\n", "\r\n"));
	SIZE size;

	IDWriteTextFormat *format = nullptr;
	auto iter = fontMap_.find(fontStyle_);
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
					bitmapData32[entry.bmWidth * y + x] = RGBAToPremul8888(v);
				} else {
					bitmapData32[entry.bmWidth * y + x] = AlphaToPremul8888(v >> 24);
				}
			}
		}
	} else if (texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(map.bits[map.pitch * y + x * 4] & 0xff);
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(bAlpha);
			}
		}
	} else if (texFormat == Draw::DataFormat::A4R4G4B4_UNORM_PACK16) {
		_dbg_assert_(!fullColor);
		bitmapData.resize(entry.bmWidth * entry.bmHeight * sizeof(uint16_t));
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		for (int y = 0; y < entry.bmHeight; y++) {
			for (int x = 0; x < entry.bmWidth; x++) {
				uint8_t bAlpha = (uint8_t)(map.bits[map.pitch * y + x * 4] & 0xff);
				bitmapData16[entry.bmWidth * y + x] = AlphaToPremul4444(bAlpha);
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
	}
	fontMap_.clear();
}

#endif
