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

#include "sceMpeg.h"
#include "HLE.h"

void sceMpegInit()
{
	WARN_LOG(HLE, "HACK sceMpegInit(...)");
	RETURN(0);
}
void sceMpegCreate()
{
	WARN_LOG(HLE, "HACK sceMpegCreate(...)");
	RETURN(0);
}
void sceMpegInitAu()
{
	WARN_LOG(HLE, "HACK sceMpegInitAu(...)");
	RETURN(0);
}

void sceMpegQueryMemSize()
{
	WARN_LOG(HLE, "HACK sceMpegQueryMemSize(...)");
	RETURN(0x10000);	// 64K
}

void sceMpegRingbufferQueryMemSize()
{
	int packets = PARAM(0);
	WARN_LOG(HLE, "HACK sceMpegRingbufferQueryMemSize(...)");
	RETURN(packets * (104 + 2048));
}

void sceMpegRingbufferConstruct()
{
	WARN_LOG(HLE, "HACK sceMpegRingbufferConstruct(...)");
	RETURN(0);
}

void sceMpegRegistStream() 
{
	WARN_LOG(HLE, "HACK sceMpegRegistStream(...)");
	RETURN(0);
}

void sceMpegUnRegistStream() 
{
	WARN_LOG(HLE, "HACK sceMpegRegistStream(...)");
	RETURN(0);
}

void sceMpegGetAtracAu() 
{
	WARN_LOG(HLE, "HACK sceMpegGetAtracAu(...)");
	RETURN(0);
}

void sceMpegQueryPcmEsSize() 
{
	WARN_LOG(HLE, "HACK sceMpegQueryPcmEsSize(...)");
	RETURN(0);
}

void sceMpegQueryAtracEsSize() 
{
	WARN_LOG(HLE, "HACK sceMpegQueryAtracEsSize(...)");
	RETURN(0);
}

void sceMpegChangeGetAuMode() 
{
	WARN_LOG(HLE, "HACK sceMpegChangeGetAuMode(...)");
	RETURN(0);
}

void sceMpegQueryStreamOffset() 
{
	WARN_LOG(HLE, "HACK sceMpegQueryStreamOffset(...)");
	RETURN(0);
}

void sceMpegGetPcmAu() 
{
	WARN_LOG(HLE, "HACK sceMpegGetPcmAu(...)");
	RETURN(0);
}

void sceMpegRingbufferQueryPackNum() 
{
	WARN_LOG(HLE, "HACK sceMpegRingbufferQueryPackNum(...)");
	RETURN(0);
}

void sceMpegFlushAllStream() 
{
	WARN_LOG(HLE, "HACK sceMpegFlushAllStream(...)");
	RETURN(0);
}

void sceMpegMallocAvcEsBuf() 
{
	WARN_LOG(HLE, "HACK sceMpegMallocAvcEsBuf(...)");
	RETURN(0);
}

void sceMpegAvcCopyYCbCr() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcCopyYCbCr(...)");
	RETURN(0);
}

void sceMpegFreeAvcEsBuf() 
{
	WARN_LOG(HLE, "HACK sceMpegFreeAvcEsBuf(...)");
	RETURN(0);
}

void sceMpegAtracDecode() 
{
	WARN_LOG(HLE, "HACK sceMpegAtracDecode(...)");
	RETURN(0);
}

void sceMpegAvcDecodeStop() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeStop(...)");
	RETURN(0);
}

void sceMpegAvcDecodeMode() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeMode(...)");
	RETURN(0);
}

void sceMpegAvcDecode() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecode(...)");
	RETURN(0);
}

void sceMpegAvcCsc() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcCsc(...)");
	RETURN(0);
}

void sceMpegAvcDecodeStopYCbCr() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeStopYCbCr(...)");
	RETURN(0);
}

void sceMpegRingbufferDestruct() 
{
	WARN_LOG(HLE, "HACK sceMpegRingbufferDestruct(...)");
	RETURN(0);
}

void sceMpegAvcDecodeYCbCr() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeYCbCr(...)");
	RETURN(0);
}

void sceMpegRingbufferPut() 
{
	WARN_LOG(HLE, "HACK sceMpegRingbufferPut(...)");
	RETURN(0);
}

void sceMpegAvcInitYCbCr() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcInitYCbCr(...)");
	RETURN(0);
}

void sceMpegAvcQueryYCbCrSize() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcQueryYCbCrSize(...)");
	RETURN(0);
}

void sceMpegRingbufferAvailableSize() 
{
	WARN_LOG(HLE, "HACK sceMpegRingbufferAvailableSize(...)");
	RETURN(0);
}

void sceMpegAvcDecodeDetail() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeDetail(...)");
	RETURN(0);
}

void sceMpegAvcDecodeFlush() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeFlush(...)");
	RETURN(0);
}

void sceMpegFinish() 
{
	WARN_LOG(HLE, "HACK sceMpegFinish(...)");
	RETURN(0);
}

void sceMpegDelete() 
{
	WARN_LOG(HLE, "HACK sceMpegDelete(...)");
	RETURN(0);
}

void sceMpegGetAvcAu() 
{
	WARN_LOG(HLE, "HACK sceMpegGetAvcAu(...)");
	RETURN(0);
}

void sceMpegQueryStreamSize() 
{
	WARN_LOG(HLE, "HACK sceMpegQueryStreamSize(...)");
	RETURN(0);
}

const HLEFunction sceMpeg[] =
{
	{0xe1ce83a7,sceMpegGetAtracAu,"sceMpegGetAtracAu"},
	{0xfe246728,sceMpegGetAvcAu,"sceMpegGetAvcAu"},
	{0xd8c5f121,sceMpegCreate,"sceMpegCreate"},
	{0xf8dcb679,sceMpegQueryAtracEsSize,"sceMpegQueryAtracEsSize"},
	{0xc132e22f,sceMpegQueryMemSize,"sceMpegQueryMemSize"},
	{0x21ff80e4,sceMpegQueryStreamOffset,"sceMpegQueryStreamOffset"},
	{0x611e9e11,sceMpegQueryStreamSize,"sceMpegQueryStreamSize"},
	{0x42560f23,sceMpegRegistStream,"sceMpegRegistStream"},
	{0x591a4aa2,sceMpegUnRegistStream,"sceMpegUnRegistStream"},
	{0x707b7629,sceMpegFlushAllStream,"sceMpegFlushAllStream"},
	{0xa780cf7e,sceMpegMallocAvcEsBuf,"sceMpegMallocAvcEsBuf"},
	{0xceb870b1,sceMpegFreeAvcEsBuf,"sceMpegFreeAvcEsBuf"},
	{0x167afd9e,sceMpegInitAu,"sceMpegInitAu"},
	{0x682a619b,sceMpegInit,"sceMpegInit"},
	{0x800c44df,sceMpegAtracDecode,"sceMpegAtracDecode"},
	{0x740fccd1,sceMpegAvcDecodeStop,"sceMpegAvcDecodeStop"},
	{0x0e3c2e9d,sceMpegAvcDecode,"sceMpegAvcDecode"},
	{0xd7a29f46,sceMpegRingbufferQueryMemSize,"sceMpegRingbufferQueryMemSize"},
	{0x37295ed8,sceMpegRingbufferConstruct,"sceMpegRingbufferConstruct"},
	{0x13407f13,sceMpegRingbufferDestruct,"sceMpegRingbufferDestruct"},
	{0xb240a59e,sceMpegRingbufferPut,"sceMpegRingbufferPut"},
	{0xb5f6dc87,sceMpegRingbufferAvailableSize,"sceMpegRingbufferAvailableSize"},
	{0x606a4649,sceMpegDelete,"sceMpegDelete"},
	{0x874624d6,sceMpegFinish,"sceMpegFinish"},
	{0x4571cc64,sceMpegAvcDecodeFlush,"sceMpegAvcDecodeFlush"},
	{0x0f6c18d7,sceMpegAvcDecodeDetail,"sceMpegAvcDecodeDetail"},
	{0x211a057c,sceMpegAvcQueryYCbCrSize,"sceMpegAvcQueryYCbCrSize"},
	{0x67179b1b,sceMpegAvcInitYCbCr,"sceMpegAvcInitYCbCr"},
	{0xf0eb1125,sceMpegAvcDecodeYCbCr,"sceMpegAvcDecodeYCbCr"},
	{0xf2930c9c,sceMpegAvcDecodeStopYCbCr,"sceMpegAvcDecodeStopYCbCr"},
	{0x31bd0272,sceMpegAvcCsc,"sceMpegAvcCsc"},
	{0xa11c7026,sceMpegAvcDecodeMode,"sceMpegAvcDecodeMode"},
	{0x0558B075,sceMpegAvcCopyYCbCr,"sceMpegAvcCopyYCbCr"},
	{0x769BEBB6,sceMpegRingbufferQueryPackNum,"sceMpegRingbufferQueryPackNum"},
	{0x8C1E027D,sceMpegGetPcmAu,"sceMpegGetPcmAu"},
	{0x9DCFB7EA,sceMpegChangeGetAuMode,"sceMpegChangeGetAuMode"},
	{0xC02CF6B5,sceMpegQueryPcmEsSize,"sceMpegQueryPcmEsSize"},
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
