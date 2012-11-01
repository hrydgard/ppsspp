// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "sceMpeg.h"
#include "HLE.h"

void sceMpegInit()
{
	DEBUG_LOG(HLE, "HACK sceMpegInit(...)");
	RETURN(0);
}
void sceMpegCreate()
{
	DEBUG_LOG(HLE, "HACK sceMpegCreate(...)");
	RETURN(0);
}
void sceMpegInitAu()
{
	DEBUG_LOG(HLE, "HACK sceMpegInitAu(...)");
	RETURN(0);
}

void sceMpegQueryMemSize()
{
	DEBUG_LOG(HLE, "HACK sceMpegQueryMemSize(...)");
	RETURN(0x10000);	// 64K
}

void sceMpegRingbufferQueryMemSize()
{
	int packets = PARAM(0);
	DEBUG_LOG(HLE, "HACK sceMpegRingbufferQueryMemSize(...)");
	RETURN(packets * (104 + 2048));
}

void sceMpegRingbufferConstruct()
{
	DEBUG_LOG(HLE, "HACK sceMpegRingbufferConstruct(...)");
	RETURN(0);
}

const HLEFunction sceMpeg[] =
{
	{0xe1ce83a7,0,"sceMpegGetAtracAu"},
	{0xfe246728,0,"sceMpegGetAvcAu"},
	{0xd8c5f121,sceMpegCreate,"sceMpegCreate"},
	{0xf8dcb679,0,"sceMpegQueryAtracEsSize"},
	{0xc132e22f,sceMpegQueryMemSize,"sceMpegQueryMemSize"},
	{0x21ff80e4,0,"sceMpegQueryStreamOffset"},
	{0x611e9e11,0,"sceMpegQueryStreamSize"},
	{0x42560f23,0,"sceMpegRegistStream"},
	{0x591a4aa2,0,"sceMpegUnRegistStream"},
	{0x707b7629,0,"sceMpegFlushAllStream"},
	{0xa780cf7e,0,"sceMpegMallocAvcEsBuf"},
	{0xceb870b1,0,"sceMpegFreeAvcEsBuf"},
	{0x167afd9e,sceMpegInitAu,"sceMpegInitAu"},
	{0x682a619b,sceMpegInit,"sceMpegInit"},
	{0x800c44df,0,"sceMpegAtracDecode"},
	{0x740fccd1,0,"sceMpegAvcDecodeStop"},
	{0x0e3c2e9d,0,"sceMpegAvcDecode"},
	{0xd7a29f46,sceMpegRingbufferQueryMemSize,"sceMpegRingbufferQueryMemSize"},
	{0x37295ed8,sceMpegRingbufferConstruct,"sceMpegRingbufferConstruct"},
	{0x13407f13,0,"sceMpegRingbufferDestruct"},
	{0xb240a59e,0,"sceMpegRingbufferPut"},
	{0xb5f6dc87,0,"sceMpegRingbufferAvailableSize"},
	{0x606a4649,0,"sceMpegDelete"},
	{0x874624d6,0,"sceMpegFinish"},
	{0x4571cc64,0,"sceMpegAvcDecodeFlush"},
	{0x0f6c18d7,0,"sceMpegAvcDecodeDetail"},
	{0x211a057c,0,"sceMpegAvcQueryYCbCrSize"},
	{0x67179b1b,0,"sceMpegAvcInitYCbCr"},
	{0xf0eb1125,0,"sceMpegAvcDecodeYCbCr"},
	{0xf2930c9c,0,"sceMpegAvcDecodeStopYCbCr"},
	{0x31bd0272,0,"sceMpegAvcCsc"},
	{0xa11c7026,0,"sceMpegAvcDecodeMode"},
};

const HLEFunction sceMp3[] =
{
	{0x07EC321A,0,"sceMp3ReserveMp3Handle"},
	{0x0DB149F4,0,"sceMp3NotifyAddStreamData"},
	{0x2A368661,0,"sceMp3ResetPlayPosition"},
	{0x354D27EA,0,"sceMp3GetSumDecodedSample"},
	{0x35750070,0,"sceMp3InitResource"},
	{0x3C2FA058,0,"sceMp3TermResource"},
	{0x3CEF484F,0,"sceMp3SetLoopNum"},
	{0x44E07129,0,"sceMp3Init"},
	{0x732B042A,0,"sceMp3EndEntry"},
	{0x7F696782,0,"sceMp3GetMp3ChannelNum"},
	{0x87677E40,0,"sceMp3GetBitRate"},
	{0x87C263D1,0,"sceMp3GetMaxOutputSample"},
	{0x8AB81558,0,"sceMp3StartEntry"},
	{0x8F450998,0,"sceMp3GetSamplingRate"},
	{0xA703FE0F,0,"sceMp3GetInfoToAddStreamData"},
	{0xD021C0FB,0,"sceMp3Decode"},
	{0xD0A56296,0,"sceMp3CheckStreamDataNeeded"},
	{0xD8F54A51,0,"sceMp3GetLoopNum"},
	{0xF5478233,0,"sceMp3ReleaseMp3Handle"},
};

void Register_sceMpeg()
{
	RegisterModule("sceMpeg", ARRAY_SIZE(sceMpeg), sceMpeg);
}

void Register_sceMp3()
{
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
