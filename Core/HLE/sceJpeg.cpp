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

#include "Core/HLE/HLE.h"
#include "Core/Reporting.h"
#include "Common.h"
#include "native/ext/cityhash/city.h"
#include "native/ext/jpge/jpgd.h"

static int mjpegWidth, mjpegHeight;

void __JpegInit() {
	mjpegWidth = 0;
	mjpegHeight = 0;
}

void __JpegDoState(PointerWrap &p) {
	p.Do(mjpegWidth);
	p.Do(mjpegHeight);
	p.DoMarker("sceJpeg");
}

//Uncomment if you want to dump JPEGs loaded through sceJpeg to a file
//#define JPEG_DEBUG

int sceJpegDecompressAllImage()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecompressAllImage()");
	return 0;
}

int sceJpegMJpegCsc(u32 imageAddr, u32 yCbCrAddr, int widthHeight, int bufferWidth)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegMJpegCsc(%i, %i, %i, %i)", imageAddr, yCbCrAddr, widthHeight, bufferWidth);
	return 0;
}

int sceJpegDecodeMJpeg(u32 jpegAddr, int jpegSize, u32 imageAddr, int dhtMode)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecodeMJpeg(%i, %i, %i, %i)", jpegAddr, jpegSize, imageAddr, dhtMode);
	return 0;
}

int sceJpegDecodeMJpegYCbCrSuccessively(u32 jpegAddr, int jpegSize, u32 yCbCrAddr, int yCbCrSize, int dhtMode)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecodeMJpegYCbCrSuccessively(%i, %i, %i, %i, %i)", jpegAddr, jpegSize, yCbCrAddr, yCbCrSize, dhtMode);
	return 0;
}

int sceJpegDeleteMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDeleteMJpeg()");
	return 0;
}

int sceJpegDecodeMJpegSuccessively(u32 jpegAddr, int jpegSize, u32 imageAddr, int dhtMode)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecodeMJpegSuccessively(%i, %i, %i, %i)", jpegAddr, jpegSize, imageAddr, dhtMode);
	return 0;
}

int sceJpegCsc(u32 imageAddr, u32 yCbCrAddr, int widthHeight, int bufferWidth, int colourInfo)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegCsc(%i, %i, %i, %i, %i)", imageAddr, yCbCrAddr, widthHeight, bufferWidth, colourInfo);
	return 0;
}

int sceJpegFinishMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegFinishMJpeg()");
	return 0;
}

int getYCbCrBufferSize(int w, int h)
{
	// Return necessary buffer size for conversion: 12 bits per pixel
	return ((w * h) >> 1) * 3;
}

int sceJpegGetOutputInfo(u32 jpegAddr, int jpegSize, u32 colourInfoAddr, int dhtMode)
{
	ERROR_LOG_REPORT(HLE, "sceJpegGetOutputInfo(%i, %i, %i, %i)", jpegAddr, jpegSize, colourInfoAddr, dhtMode);

	// Buffer to store info about the color space in use.
	// - Bits 24 to 32 (Always empty): 0x00
	// - Bits 16 to 24 (Color mode): 0x00 (Unknown), 0x01 (Greyscale) or 0x02 (YCbCr) 
	// - Bits 8 to 16 (Vertical chroma subsampling value): 0x00, 0x01 or 0x02
	// - Bits 0 to 8 (Horizontal chroma subsampling value): 0x00, 0x01 or 0x02
	if (Memory::IsValidAddress(colourInfoAddr))
		Memory::Write_U32(0x00020202, colourInfoAddr);

	int w = 0, h = 0, actual_components = 0;

	if (!Memory::IsValidAddress(jpegAddr))
	{
		ERROR_LOG(HLE, "sceJpegGetOutputInfo: Bad JPEG address 0x%08x", jpegAddr);
		return 0xC000;
	}
	else // Memory address is good
	{
		// But data may not be...so check it
		u8 *buf = Memory::GetPointer(jpegAddr);
		unsigned char *jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &w, &h, &actual_components, 3);
		if (actual_components != 3)
		{
			// The assumption that the image was RGB was wrong...
			// Try again.
			int components = actual_components;
			jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &w, &h, &actual_components, components);
		}
		if (jpegBuf == NULL)
		{
			ERROR_LOG(HLE, "sceJpegGetOutputInfo: Bad JPEG data");
			return 0xC000;
		}
	}

#ifdef JPEG_DEBUG
		char jpeg_fname[256];
		u8 *jpegBuf = Memory::GetPointer(jpegAddr);
		uint32 jpeg_cityhash = CityHash32((const char *)jpegBuf, jpegSize);
		sprintf(jpeg_fname, "Jpeg\\%X.jpg", jpeg_cityhash);
		FILE *wfp = fopen(jpeg_fname, "wb");
		fwrite(jpegBuf, 1, jpegSize, wfp);
		fclose(wfp);
#endif //JPEG_DEBUG

	return getYCbCrBufferSize(w, h);
}

int getWidthHeight(int width, int height)
{
	return (width << 16) | height;
}

int sceJpegDecodeMJpegYCbCr(u32 jpegAddr, int jpegSize, u32 yCbCrAddr, int yCbCrSize, int dhtMode)
{
	ERROR_LOG_REPORT(HLE, "sceJpegDecodeMJpegYCbCr(%i, %i, %i, %i, %i)", jpegAddr, jpegSize, yCbCrAddr, yCbCrSize, dhtMode);

	if (!Memory::IsValidAddress(jpegAddr))
	{
		return getWidthHeight(0, 0);
	}

	u8 *buf = Memory::GetPointer(jpegAddr);
	int width, height, actual_components;
	unsigned char *jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &width, &height, &actual_components, 3);
	if (actual_components != 3)
	{
		// The assumption that the image was RGB was wrong...
		// Try again.
		int components = actual_components;
		jpegBuf = jpgd::decompress_jpeg_image_from_memory(buf, jpegSize, &width, &height, &actual_components, components);
	}
	if (jpegBuf == NULL)
		return getWidthHeight(0, 0);
	
	// TODO: There's more...

	return getWidthHeight(width, height);
}

int sceJpeg_9B36444C()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpeg_9B36444C()");
	return 0;
}

int sceJpegCreateMJpeg(int width, int height)
{
	ERROR_LOG_REPORT(HLE, "sceJpegCreateMJpeg(%i, %i)", width, height);

	mjpegWidth = width;
	mjpegHeight = height;

	return 0;
}

int sceJpegInitMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegInitMJpeg()");
	return 0;
}

int sceJpeg_A06A75C4()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpeg_A06A75C4()");
	return 0;
}


const HLEFunction sceJpeg[] =
{
	{0x0425B986, WrapI_V<sceJpegDecompressAllImage>, "sceJpegDecompressAllImage"},
	{0x04B5AE02, WrapI_UUII<sceJpegMJpegCsc>, "sceJpegMJpegCsc"},
	{0x04B93CEF, WrapI_UIUI<sceJpegDecodeMJpeg>, "sceJpegDecodeMJpeg"},
	{0x227662D7, WrapI_UIUII<sceJpegDecodeMJpegYCbCrSuccessively>, "sceJpegDecodeMJpegYCbCrSuccessively"},
	{0x48B602B7, WrapI_V<sceJpegDeleteMJpeg>, "sceJpegDeleteMJpeg"},
	{0x64B6F978, WrapI_UIUI<sceJpegDecodeMJpegSuccessively>, "sceJpegDecodeMJpegSuccessively"},
	{0x67F0ED84, WrapI_UUIII<sceJpegCsc>, "sceJpegCsc"},
	{0x7D2F3D7F, WrapI_V<sceJpegFinishMJpeg>, "sceJpegFinishMJpeg"},
	{0x8F2BB012, WrapI_UIUI<sceJpegGetOutputInfo>, "sceJpegGetOutputInfo"},
	{0x91EED83C, WrapI_UIUII<sceJpegDecodeMJpegYCbCr>, "sceJpegDecodeMJpegYCbCr"},
	{0x9B36444C, WrapI_V<sceJpeg_9B36444C>, "sceJpeg_9B36444C"},
	{0x9D47469C, WrapI_II<sceJpegCreateMJpeg>, "sceJpegCreateMJpeg"},
	{0xAC9E70E6, WrapI_V<sceJpegInitMJpeg>, "sceJpegInitMJpeg"},
	{0xa06a75c4, WrapI_V<sceJpeg_A06A75C4>, "sceJpeg_A06A75C4"},
};

void Register_sceJpeg()
{
	RegisterModule("sceJpeg", ARRAY_SIZE(sceJpeg), sceJpeg);
}
