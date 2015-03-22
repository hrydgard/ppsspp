// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "native/ext/jpge/jpgd.h"

#include "Common/Common.h"
#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceJpeg.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"

//Uncomment if you want to dump JPEGs loaded through sceJpeg to a file
//#define JPEG_DEBUG
#ifdef JPEG_DEBUG
#include "ext/xxhash.h"
#endif

#include <algorithm>

static int mjpegWidth, mjpegHeight;

void __JpegInit() {
	mjpegWidth = 0;
	mjpegHeight = 0;
}

void __JpegDoState(PointerWrap &p) {
	auto s = p.Section("sceJpeg", 1);
	if (!s)
		return;

	p.Do(mjpegWidth);
	p.Do(mjpegHeight);
}

static u32 convertYCbCrToABGR (int y, int cb, int cr) {
	//see http://en.wikipedia.org/wiki/Yuv#Y.27UV444_to_RGB888_conversion for more information.
	cb = cb - 128;
	cr = cr - 128;
	int r = y + cr + (cr >> 2) + (cr >> 3) + (cr >> 5);
	int g = y - ((cb >> 2) + (cb >> 4) + (cb >> 5)) - ((cr >> 1) + (cr >> 3) + (cr >> 4) + (cr >> 5));
	int b = y + cb + (cb >> 1) + (cb >> 2) + (cb >> 6);

	// check rgb value.
	if (r > 0xFF) r = 0xFF; if(r < 0) r = 0;
	if (g > 0xFF) g = 0xFF; if(g < 0) g = 0;
	if (b > 0xFF) b = 0xFF; if(b < 0) b = 0;

	return 0xFF000000 | (b << 16) | (g << 8) | (r << 0);
}

static void __JpegCsc(u32 imageAddr, u32 yCbCrAddr, int widthHeight, int bufferWidth) {
	int height = widthHeight & 0xFFF;
	int width = (widthHeight >> 16) & 0xFFF;
	int lineWidth = std::min(width, bufferWidth);
	int skipEndOfLine = std::max(0, bufferWidth - lineWidth);
	u32 *imageBuffer = (u32*)Memory::GetPointer(imageAddr);
	int sizeY = width * height;
	int sizeCb = sizeY >> 2;
	u8 *Y = (u8*)Memory::GetPointer(yCbCrAddr);
	u8 *Cb = Y + sizeY;
	u8 *Cr = Cb + sizeCb;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 4) {
			u8 y0 =  Y[x + 0];
			u8 y1 =  Y[x + 1];
			u8 y2 =  Y[x + 2];
			u8 y3 =  Y[x + 3];
			u8 cb = *Cb++;
			u8 cr = *Cr++;

			// Convert to ABGR. This is not a fast way to do it.
			u32 abgr0 = convertYCbCrToABGR(y0, cb, cr);
			u32 abgr1 = convertYCbCrToABGR(y1, cb, cr);
			u32 abgr2 = convertYCbCrToABGR(y2, cb, cr);
			u32 abgr3 = convertYCbCrToABGR(y3, cb, cr);

			// Write ABGR
			imageBuffer[x + 0] = abgr0;
			imageBuffer[x + 1] = abgr1;
			imageBuffer[x + 2] = abgr2;
			imageBuffer[x + 3] = abgr3;
		}
		Y += width;
		imageBuffer += width;
		imageBuffer += skipEndOfLine;
	}
}
static int sceJpegMJpegCsc(u32 imageAddr, u32 yCbCrAddr, int widthHeight, int bufferWidth)
{
	__JpegCsc(imageAddr, yCbCrAddr, widthHeight, bufferWidth);
	
	DEBUG_LOG(ME, "sceJpegMJpegCsc(%i, %i, %i, %i)", imageAddr, yCbCrAddr, widthHeight, bufferWidth);
	return 0;
}

static int sceJpegDecodeMJpeg(u32 jpegAddr, int jpegSize, u32 imageAddr, int dhtMode)
{
	ERROR_LOG_REPORT(ME, "UNIMPL sceJpegDecodeMJpeg(%i, %i, %i, %i)", jpegAddr, jpegSize, imageAddr, dhtMode);
	return 0;
}

static int sceJpegDeleteMJpeg()
{
	WARN_LOG(ME, "sceJpegDeleteMJpeg()");
	return 0;
}

static int sceJpegDecodeMJpegSuccessively(u32 jpegAddr, int jpegSize, u32 imageAddr, int dhtMode)
{
	ERROR_LOG_REPORT(ME, "UNIMPL sceJpegDecodeMJpegSuccessively(%i, %i, %i, %i)", jpegAddr, jpegSize, imageAddr, dhtMode);
	return 0;
}

static int sceJpegCsc(u32 imageAddr, u32 yCbCrAddr, int widthHeight, int bufferWidth, int colourInfo)
{
	if (bufferWidth < 0 || widthHeight < 0){
		WARN_LOG(ME, "sceJpegCsc(%i, %i, %i, %i, %i)", imageAddr, yCbCrAddr, widthHeight, bufferWidth, colourInfo);
		return ERROR_JPEG_INVALID_VALUE;
	}

	__JpegCsc(imageAddr, yCbCrAddr, widthHeight, bufferWidth);
	
	DEBUG_LOG(ME, "sceJpegCsc(%i, %i, %i, %i, %i)", imageAddr, yCbCrAddr, widthHeight, bufferWidth, colourInfo);
	return 0;
}

static int sceJpegFinishMJpeg()
{
	WARN_LOG(ME, "sceJpegFinishMJpeg()");
	return 0;
}

static int getYCbCrBufferSize(int w, int h) {
	// Return necessary buffer size for conversion: 12 bits per pixel
	return ((w * h) >> 1) * 3;
}

static int __JpegGetOutputInfo(u32 jpegAddr, int jpegSize, u32 colourInfoAddr) {
	u8 *buf = Memory::GetPointer(jpegAddr);
	int width, height, actual_components;
	unsigned char *jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &width, &height, &actual_components, 3);

	if (actual_components != 3) {
	// The assumption that the image was RGB was wrong...
	// Try again.
		int components = actual_components;
		jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &width, &height, &actual_components, components);
	}

	if (jpegBuf == NULL) {
		ERROR_LOG(ME, "sceJpegGetOutputInfo: Bad JPEG data");
		return getYCbCrBufferSize(0, 0);
	}

	free(jpegBuf);
	
	// Buffer to store info about the color space in use.
	// - Bits 24 to 32 (Always empty): 0x00
	// - Bits 16 to 24 (Color mode): 0x00 (Unknown), 0x01 (Greyscale) or 0x02 (YCbCr) 
	// - Bits 8 to 16 (Vertical chroma subsampling value): 0x00, 0x01 or 0x02
	// - Bits 0 to 8 (Horizontal chroma subsampling value): 0x00, 0x01 or 0x02
	if (Memory::IsValidAddress(colourInfoAddr)) {
		Memory::Write_U32(0x00020202, colourInfoAddr);
	}

#ifdef JPEG_DEBUG
		char jpeg_fname[256];
		u8 *jpegBuf = Memory::GetPointer(jpegAddr);
		uint32 jpeg_xxhash = XXH32((const char *)jpegBuf, jpegSize, 0xC0108888);
		sprintf(jpeg_fname, "Jpeg\\%X.jpg", jpeg_xxhash);
		FILE *wfp = fopen(jpeg_fname, "wb");
		if (!wfp) {
			_wmkdir(L"Jpeg\\");
			wfp = fopen(jpeg_fname, "wb");
		}
		fwrite(jpegBuf, 1, jpegSize, wfp);
		fclose(wfp);
#endif //JPEG_DEBUG

	return getYCbCrBufferSize(width, height);
}

static int sceJpegGetOutputInfo(u32 jpegAddr, int jpegSize, u32 colourInfoAddr, int dhtMode)
{
	if (!Memory::IsValidAddress(jpegAddr)) {
		ERROR_LOG(ME, "sceJpegGetOutputInfo: Bad JPEG address 0x%08x", jpegAddr);
		return getYCbCrBufferSize(0, 0);
	}

	DEBUG_LOG(ME, "sceJpegGetOutputInfo(%i, %i, %i, %i)", jpegAddr, jpegSize, colourInfoAddr, dhtMode);
	return __JpegGetOutputInfo(jpegAddr, jpegSize, colourInfoAddr);
}

static int getWidthHeight(int width, int height) {
	return (width << 16) | height;
}

static u32 convertRGBToYCbCr(u32 rgb) {
	//see http://en.wikipedia.org/wiki/Yuv#Y.27UV444_to_RGB888_conversion for more information.
	u8  r = (rgb >> 16) & 0xFF;
	u8  g = (rgb >>  8) & 0xFF;
	u8  b = (rgb >>  0) & 0xFF;
	int  y = 0.299f * r + 0.587f * g + 0.114f * b + 0;
	int cb = -0.169f * r - 0.331f * g + 0.499f * b + 128.0f;
	int cr = 0.499f * r - 0.418f * g - 0.0813f * b + 128.0f;

	// check yCbCr value
	if ( y > 0xFF)  y = 0xFF; if ( y < 0)  y = 0;
	if (cb > 0xFF) cb = 0xFF; if (cb < 0) cb = 0;
	if (cr > 0xFF) cr = 0xFF; if (cr < 0) cr = 0;

	return (y << 16) | (cb << 8) | cr;
}

static int __JpegConvertRGBToYCbCr (const void *data, u32 bufferOutputAddr, int width, int height) {
	u24_be *imageBuffer = (u24_be*)data;
	int sizeY = width * height;
	int sizeCb = sizeY >> 2;
	u8 *Y = (u8*)Memory::GetPointer(bufferOutputAddr);
	u8 *Cb = Y + sizeY;
	u8 *Cr = Cb + sizeCb;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 4) {
			u32 abgr0 = imageBuffer[x + 0];
			u32 abgr1 = imageBuffer[x + 1];
			u32 abgr2 = imageBuffer[x + 2];
			u32 abgr3 = imageBuffer[x + 3];

			u32 yCbCr0 = convertRGBToYCbCr(abgr0);
			u32 yCbCr1 = convertRGBToYCbCr(abgr1);
			u32 yCbCr2 = convertRGBToYCbCr(abgr2);
			u32 yCbCr3 = convertRGBToYCbCr(abgr3);
			
			Y[x + 0] = (yCbCr0 >> 16) & 0xFF;
			Y[x + 1] = (yCbCr1 >> 16) & 0xFF;
			Y[x + 2] = (yCbCr2 >> 16) & 0xFF;
			Y[x + 3] = (yCbCr3 >> 16) & 0xFF;

			*Cb++ = (yCbCr0 >> 8) & 0xFF;
			*Cr++ = yCbCr0 & 0xFF;
		}
		imageBuffer += width;
		Y += width ;
	}
	return (width << 16) | height;
}

static int __JpegDecodeMJpegYCbCr(u32 jpegAddr, int jpegSize, u32 yCbCrAddr) {
	u8 *buf = Memory::GetPointer(jpegAddr);
	int width, height, actual_components;
	unsigned char *jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &width, &height, &actual_components, 3);

	if (actual_components != 3) {
		// The assumption that the image was RGB was wrong...
		// Try again.
		int components = actual_components;
		jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &width, &height, &actual_components, components);
	}

	if (jpegBuf == NULL) {
		return getWidthHeight(0, 0);
	}

	if (actual_components == 3) {
		__JpegConvertRGBToYCbCr(jpegBuf, yCbCrAddr, width, height);
	}

	free(jpegBuf);

	// TODO: There's more...

	return getWidthHeight(width, height);
}

static int sceJpegDecodeMJpegYCbCr(u32 jpegAddr, int jpegSize, u32 yCbCrAddr, int yCbCrSize, int dhtMode)
{
	if (!Memory::IsValidAddress(jpegAddr)) {
		ERROR_LOG(ME, "sceJpegDecodeMJpegYCbCr: Bad JPEG address 0x%08x", jpegAddr);
		return getWidthHeight(0, 0);
	}
	
	DEBUG_LOG(ME, "sceJpegDecodeMJpegYCbCr(%i, %i, %i, %i, %i)", jpegAddr, jpegSize, yCbCrAddr, yCbCrSize, dhtMode);
	return __JpegDecodeMJpegYCbCr(jpegAddr, jpegSize, yCbCrAddr);
}

static int sceJpegDecodeMJpegYCbCrSuccessively(u32 jpegAddr, int jpegSize, u32 yCbCrAddr, int yCbCrSize, int dhtMode)
{
	if (!Memory::IsValidAddress(jpegAddr)) {
		ERROR_LOG(ME, "sceJpegDecodeMJpegYCbCrSuccessively: Bad JPEG address 0x%08x", jpegAddr);
		return getWidthHeight(0, 0);
	}
	
	DEBUG_LOG(ME, "sceJpegDecodeMJpegYCbCrSuccessively(%i, %i, %i, %i, %i)", jpegAddr, jpegSize, yCbCrAddr, yCbCrSize, dhtMode);
	// Do as same way as sceJpegDecodeMJpegYCbCr() but with smaller block size
	return __JpegDecodeMJpegYCbCr(jpegAddr, jpegSize, yCbCrAddr);
}

static int sceJpeg_9B36444C()
{
	ERROR_LOG_REPORT(ME, "UNIMPL sceJpeg_9B36444C()");
	return 0;
}

static int sceJpegCreateMJpeg(int width, int height)
{
	mjpegWidth = width;
	mjpegHeight = height;

	INFO_LOG(ME, "sceJpegCreateMJpeg(%i, %i)", width, height);
	return 0;
}

static int sceJpegInitMJpeg()
{
	WARN_LOG(ME, "sceJpegInitMJpeg()");
	return 0;
}

static int sceJpegMJpegCscWithColorOption()
{
	ERROR_LOG_REPORT(ME, "UNIMPL sceJpegMJpegCscWithColorOption()");
	return 0;
}

static int sceJpegDecompressAllImage()
{
	ERROR_LOG_REPORT(ME, "UNIMPL sceJpegDecompressAllImage()");
	return 0;
}

const HLEFunction sceJpeg[] =
{
	{0X0425B986, &WrapI_V<sceJpegDecompressAllImage>,               "sceJpegDecompressAllImage",           'i', ""     },
	{0X04B5AE02, &WrapI_UUII<sceJpegMJpegCsc>,                      "sceJpegMJpegCsc",                     'i', "xxii" },
	{0X04B93CEF, &WrapI_UIUI<sceJpegDecodeMJpeg>,                   "sceJpegDecodeMJpeg",                  'i', "xixi" },
	{0X227662D7, &WrapI_UIUII<sceJpegDecodeMJpegYCbCrSuccessively>, "sceJpegDecodeMJpegYCbCrSuccessively", 'i', "xixii"},
	{0X48B602B7, &WrapI_V<sceJpegDeleteMJpeg>,                      "sceJpegDeleteMJpeg",                  'i', ""     },
	{0X64B6F978, &WrapI_UIUI<sceJpegDecodeMJpegSuccessively>,       "sceJpegDecodeMJpegSuccessively",      'i', "xixi" },
	{0X67F0ED84, &WrapI_UUIII<sceJpegCsc>,                          "sceJpegCsc",                          'i', "xxiii"},
	{0X7D2F3D7F, &WrapI_V<sceJpegFinishMJpeg>,                      "sceJpegFinishMJpeg",                  'i', ""     },
	{0X8F2BB012, &WrapI_UIUI<sceJpegGetOutputInfo>,                 "sceJpegGetOutputInfo",                'i', "xixi" },
	{0X91EED83C, &WrapI_UIUII<sceJpegDecodeMJpegYCbCr>,             "sceJpegDecodeMJpegYCbCr",             'i', "xixii"},
	{0X9B36444C, &WrapI_V<sceJpeg_9B36444C>,                        "sceJpeg_9B36444C",                    'i', ""     },
	{0X9D47469C, &WrapI_II<sceJpegCreateMJpeg>,                     "sceJpegCreateMJpeg",                  'i', "ii"   },
	{0XAC9E70E6, &WrapI_V<sceJpegInitMJpeg>,                        "sceJpegInitMJpeg",                    'i', ""     },
	{0XA06A75C4, &WrapI_V<sceJpegMJpegCscWithColorOption>,          "sceJpegMJpegCscWithColorOption",      'i', ""     },
};

void Register_sceJpeg()
{
	RegisterModule("sceJpeg", ARRAY_SIZE(sceJpeg), sceJpeg);
}
