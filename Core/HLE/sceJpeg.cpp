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


int sceJpegDecompressAllImage()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecompressAllImage");
	return 0;
}

int sceJpegMJpegCsc()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegMJpegCsc");
	return 0;
}

int sceJpegDecodeMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecodeMJpeg");
	return 0;
}

int sceJpegDecodeMJpegYCbCrSuccessively()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecodeMJpegYCbCrSuccessively");
	return 0;
}

int sceJpegDeleteMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDeleteMJpeg");
	return 0;
}

int sceJpegDecodeMJpegSuccessively()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecodeMJpegSuccessively");
	return 0;
}

int sceJpegCsc()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegCsc");
	return 0;
}

int sceJpegFinishMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegFinishMJpeg");
	return 0;
}

int sceJpegGetOutputInfo()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegGetOutputInfo");
	return 0;
}

int sceJpegDecodeMJpegYCbCr()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegDecodeMJpegYCbCr");
	return 0;
}

int sceJpeg_9B36444C()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpeg_9B36444C");
	return 0;
}

int sceJpegCreateMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegCreateMJpeg");
	return 0;
}

int sceJpegInitMJpeg()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceJpegInitMJpeg");
	return 0;
}


const HLEFunction sceJpeg[] =
{
	{0x0425B986, WrapI_V<sceJpegDecompressAllImage>, "sceJpegDecompressAllImage"},
	{0x04B5AE02, WrapI_V<sceJpegMJpegCsc>, "sceJpegMJpegCsc"},
	{0x04B93CEF, WrapI_V<sceJpegDecodeMJpeg>, "sceJpegDecodeMJpeg"},
	{0x227662D7, WrapI_V<sceJpegDecodeMJpegYCbCrSuccessively>, "sceJpegDecodeMJpegYCbCrSuccessively"},
	{0x48B602B7, WrapI_V<sceJpegDeleteMJpeg>, "sceJpegDeleteMJpeg"},
	{0x64B6F978, WrapI_V<sceJpegDecodeMJpegSuccessively>, "sceJpegDecodeMJpegSuccessively"},
	{0x67F0ED84, WrapI_V<sceJpegCsc>, "sceJpegCsc"},
	{0x7D2F3D7F, WrapI_V<sceJpegFinishMJpeg>, "sceJpegFinishMJpeg"},
	{0x8F2BB012, WrapI_V<sceJpegGetOutputInfo>, "sceJpegGetOutputInfo"},
	{0x91EED83C, WrapI_V<sceJpegDecodeMJpegYCbCr>, "sceJpegDecodeMJpegYCbCr"},
	{0x9B36444C, WrapI_V<sceJpeg_9B36444C>, "sceJpeg_9B36444C"},
	{0x9D47469C, WrapI_V<sceJpegCreateMJpeg>, "sceJpegCreateMJpeg"},
	{0xAC9E70E6, WrapI_V<sceJpegInitMJpeg>, "sceJpegInitMJpeg"},
	{0xa06a75c4, 0, "sceJpeg_A06A75C4"},
};

void Register_sceJpeg()
{
	RegisterModule("sceJpeg", ARRAY_SIZE(sceJpeg), sceJpeg);
}
