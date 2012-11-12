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


// This code is part shamelessly "inspired" from JPSCP.


#include "sceMpeg.h"
#include "HLE.h"
#include "../HW/MediaEngine.h"


struct StreamInfo {
	int id;
	int type;
};

static bool useMediaEngine;

// MPEG statics.
static const int PSMF_MAGIC = 0x464D5350;
static const int PSMF_VERSION_0012 = 0x32313030;
static const int PSMF_VERSION_0013 = 0x33313030;
static const int PSMF_VERSION_0014 = 0x34313030;
static const int PSMF_VERSION_0015 = 0x35313030;
static const int PSMF_STREAM_VERSION_OFFSET = 0x4;
static const int PSMF_STREAM_OFFSET_OFFSET = 0x8;
static const int PSMF_STREAM_SIZE_OFFSET = 0xC;
static const int PSMF_FIRST_TIMESTAMP_OFFSET = 0x56;
static const int PSMF_LAST_TIMESTAMP_OFFSET = 0x5C;
// MPEG AVC elementary stream.
static const int MPEG_AVC_ES_SIZE = 2048;          // MPEG packet size.
// MPEG ATRAC elementary stream.
static const int MPEG_ATRAC_ES_SIZE = 2112;
static const int MPEG_ATRAC_ES_OUTPUT_SIZE = 8192;
// MPEG PCM elementary stream.
static const int MPEG_PCM_ES_SIZE = 320;
static const int MPEG_PCM_ES_OUTPUT_SIZE = 320;
// MPEG analysis results.
static const int MPEG_VERSION_0012 = 0;
static const int MPEG_VERSION_0013 = 1;
static const int MPEG_VERSION_0014 = 2;
static const int MPEG_VERSION_0015 = 3;
// MPEG streams.
static const int MPEG_AVC_STREAM = 0;
static const int MPEG_ATRAC_STREAM = 1;
static const int MPEG_PCM_STREAM = 2;
static const int MPEG_DATA_STREAM = 3;      // Arbitrary user defined type. Can represent audio or video.
static const int MPEG_AUDIO_STREAM = 15;
static const int MPEG_AU_MODE_DECODE = 0;
static const int MPEG_AU_MODE_SKIP = 1;
static const int MPEG_MEMSIZE = 0x10000;          // 64k.

static const int MPEG_AVC_DECODE_SUCCESS = 1;       // Internal value.
static const int MPEG_AVC_DECODE_ERROR_FATAL = -8;

static const int atracDecodeDelay = 3000;         // Microseconds
static const int avcDecodeDelay = 5400;           // Microseconds
static const int mpegDecodeErrorDelay = 100;      // Delay in Microseconds in case of decode error
static const int mpegTimestampPerSecond = 90000; // How many MPEG Timestamp units in a second.
static const int videoTimestampStep = 3003;      // Value based on pmfplayer (mpegTimestampPerSecond / 29.970 (fps)).
static const int audioTimestampStep = 4180;      // For audio play at 44100 Hz (2048 samples / 44100 * mpegTimestampPerSecond == 4180)
//static const int audioFirstTimestamp = 89249;    // The first MPEG audio AU has always this timestamp
static const int audioFirstTimestamp = 90000;    // The first MPEG audio AU has always this timestamp
static const s64 UNKNOWN_TIMESTAMP = -1;

// At least 2048 bytes of MPEG data is provided when analysing the MPEG header
static const int MPEG_HEADER_BUFFER_MINIMUM_SIZE = 2048;

// As native in PSP ram
struct SceMpegRingBuffer {
	// PSP info
  int packets;
  int packetsRead;
  int packetsWritten;
  int packetsFree; // pspsdk: unk2, noxa: iUnk0
  int packetSize; // 2048
  int data; // address, ring buffer
  u32 callback_addr; // see sceMpegRingbufferPut
  int callback_args;
  int dataUpperBound;
  int semaID; // unused?
  u32 mpeg; // pointer to mpeg struct, fixed up in sceMpegCreate
};

struct SceMpegAu {
	s64 pts;  // presentation time stamp
	s64 dts;  // decode time stamp
	u32 esBuffer;
	u32 esSize;
};

void InitRingbuffer(SceMpegRingBuffer *buf, int packets, int data, int size, int callback_addr, int callback_args) {
	buf->packets = packets;
  buf->packetsRead = 0;
  buf->packetsWritten = 0;
  buf->packetsFree = 0; // set later
  buf->packetSize = 2048;
  buf->data = data;
  buf->callback_addr = callback_addr;
  buf->callback_args = callback_args;
  buf->dataUpperBound = data + packets * 2048;
  buf->semaID = -1;
  buf->mpeg = 0;
}	


SceMpegRingBuffer mpegRingbuffer;
SceMpegAu mpegAtracAu;
SceMpegAu mpegAvcAu;

bool isCurrentMpegAnalyzed;
static int mpegHandle;

u32 mpegRingbufferAddr;
u32 mpegStreamAddr;

u32 mpegMagic;
u32 mpegVersion;
u32 mpegRawVersion;
u32 mpegOffset; 
u32 mpegStreamSize;
u32 mpegFirstTimestamp;
u32 mpegLastTimestamp;
u32 mpegFirstDate;
u32 mpegLastDate;
int avcDetailFrameWidth;
int avcDetailFrameHeight;
int avcDecodeResult;
int avcFrameStatus;
u32 defaultFrameWidth;
int videoFrameCount = 0;
int audioFrameCount = 0;
bool endOfAudioReached = false;
bool endOfVideoReached = false;


MediaEngine me;

u32 endianSwap32(u32 x) {
	return x;  // TODO
}

u32 convertTimestampToDate(u32 ts) {
	return ts;  // TODO
}

void AnalyzeMpeg(u32 buffer_addr) {
	mpegStreamAddr = buffer_addr;
	mpegMagic = Memory::Read_U32(buffer_addr);
	mpegRawVersion = Memory::Read_U32(buffer_addr + PSMF_STREAM_VERSION_OFFSET);
	switch (mpegRawVersion) {
	case PSMF_VERSION_0012:
		mpegVersion = MPEG_VERSION_0012;
		break;
	case PSMF_VERSION_0013:
		mpegVersion = MPEG_VERSION_0013;
		break;
	case PSMF_VERSION_0014:
		mpegVersion = MPEG_VERSION_0014;
		break;
	case PSMF_VERSION_0015:
		mpegVersion = MPEG_VERSION_0015;
		break;
	default:
		mpegVersion = -1;
		break;
	}
	mpegOffset = endianSwap32(Memory::Read_U32(buffer_addr + PSMF_STREAM_OFFSET_OFFSET));
	mpegStreamSize = endianSwap32(Memory::Read_U32(buffer_addr + PSMF_STREAM_SIZE_OFFSET));
	mpegFirstTimestamp = endianSwap32(Memory::Read_U32(buffer_addr + PSMF_FIRST_TIMESTAMP_OFFSET));
	mpegLastTimestamp = endianSwap32(Memory::Read_U32(buffer_addr + PSMF_LAST_TIMESTAMP_OFFSET));
	mpegFirstDate = convertTimestampToDate(mpegFirstTimestamp);
	mpegLastDate = convertTimestampToDate(mpegLastTimestamp);
	avcDetailFrameWidth = (Memory::Read_U8(buffer_addr + 142) * 0x10);
	avcDetailFrameHeight = (Memory::Read_U8(buffer_addr + 143) * 0x10);
	avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;
	avcFrameStatus = 0;
	if (!isCurrentMpegAnalyzed) {
		InitRingbuffer(&mpegRingbuffer, 0, 0, 0, 0, 0);
		// ????
		Memory::WriteStruct(mpegRingbufferAddr, &mpegRingbuffer);
	}

	mpegAtracAu.dts = UNKNOWN_TIMESTAMP;
	mpegAtracAu.pts = 0;
	mpegAvcAu.dts = 0;
	mpegAvcAu.pts = 0;
	videoFrameCount = 0;
	audioFrameCount = 0;
	endOfAudioReached = false;
	endOfVideoReached = false;

	if ((mpegStreamSize > 0) && !isCurrentMpegAnalyzed) {
		me.init(buffer_addr, mpegStreamSize, mpegOffset);
		//meChannel = new PacketChannel();
		//meChannel.write(buffer_addr, mpegOffset);
	}
	// When used with scePsmf, some applications attempt to use sceMpegQueryStreamOffset
	// and sceMpegQueryStreamSize, which forces a packet overwrite in the Media Engine and in
	// the MPEG ringbuffer.
	// Mark the current MPEG as analyzed to filter this, and restore it at sceMpegFinish.
	isCurrentMpegAnalyzed = true;

	INFO_LOG(ME, "Stream offset: %d, Stream size: 0x%X", mpegOffset, mpegStreamSize);
	INFO_LOG(ME, "First timestamp: %d, Last timestamp: %d", mpegFirstTimestamp, mpegLastTimestamp);
}


void __MpegInit(bool useMediaEngine_) {

}


void __MpegFinish() {

}

void sceMpegInit()
{
	WARN_LOG(HLE, "sceMpegInit()");

	RETURN(0);
}

u32 sceMpegCreate(u32 mpegAddr, u32 dataPtr, u32 size, u32 ringbufferAddr, u32 frameWidth, u32 mode, u32 ddrTop) 
{
	INFO_LOG(HLE, "sceMpegCreate(%i, %08x, %i, %08x, %i, %i, %i)",
		mpegAddr, dataPtr, size, ringbufferAddr, frameWidth, mode, ddrTop);
	if (size < MPEG_MEMSIZE) {
		return -1;
		//return ERROR_MPEG_NO_MEMORY;
	}

	SceMpegRingBuffer ringbuffer;
	Memory::ReadStruct(ringbufferAddr, &ringbuffer);
	if (ringbuffer.packetSize == 0) {
		ringbuffer.packetsFree = 0;
	} else {
		ringbuffer.packetsFree = (ringbuffer.dataUpperBound - ringbuffer.data) / ringbuffer.packetSize;
	}
	ringbuffer.mpeg = mpegAddr;
	Memory::WriteStruct(ringbufferAddr, &ringbuffer);

	// Generate, and write mpeg handle into mpeg data, for some reason
	mpegHandle = dataPtr + 0x30;
	Memory::Write_U32(mpegAddr, mpegHandle);

	Memory::Memcpy(mpegHandle, "LIBMPEG.001", 12);
	Memory::Write_U32(-1, mpegHandle + 12);
	Memory::Write_U32(ringbufferAddr, mpegHandle + 16);
	Memory::Write_U32(ringbuffer.dataUpperBound, mpegHandle + 20);

	mpegRingbufferAddr = ringbufferAddr;
	mpegRingbuffer = ringbuffer;
	videoFrameCount = 0;
	audioFrameCount = 0;
	defaultFrameWidth = frameWidth;

	return 0;
}

u32 sceMpegAvcDecode(u32 mpeg, u32 auAddr, u32 frameWidth, u32 bufferAddr, u32 initAddr) 
{
	WARN_LOG(HLE, "sceMpegAvcDecode(%08x, %08x, %i, %08x, %08x)", mpeg, auAddr, frameWidth, bufferAddr, initAddr);

	// TODO

	return 0;
}

u32 sceMpegAvcDecodeStop(u32 mpeg, u32 frameWidth, u32 bufferAddr, u32 statusAddr)
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeStop(%08x, %08x, %08x, statusAddr=%08x)",
		mpeg, frameWidth, bufferAddr, statusAddr);

	return 0;
}

void sceMpegAvcDecodeMode() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeMode(...)");
	RETURN(0);
}

void sceMpegAvcDecodeStopYCbCr() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeStopYCbCr(...)");
	RETURN(0);
}

void sceMpegAvcDecodeYCbCr() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeYCbCr(...)");
	RETURN(0);
}

void sceMpegAvcDecodeDetail() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeDetail(...)");
	RETURN(0);
}

u32 sceMpegAvcDecodeFlush(u32 mpeg) 
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcDecodeFlush(%08x)", mpeg);
	return 0;
}

void sceMpegInitAu()
{
	WARN_LOG(HLE, "HACK sceMpegInitAu(...)");
	RETURN(0);
}

void sceMpegFinish() 
{
	WARN_LOG(HLE, "sceMpegFinish(...)");
	__MpegFinish();
	RETURN(0);
}

void sceMpegDelete() 
{
	WARN_LOG(HLE, "HACK sceMpegDelete(...)");
	RETURN(0);
}

void sceMpegQueryMemSize()
{
	DEBUG_LOG(HLE, "sceMpegQueryMemSize()");
	RETURN(0x10000);	// 64K
}

u32 sceMpegRingbufferQueryMemSize(int packets)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferQueryMemSize(%i)", packets);
	return packets * (104 + 2048);
}

u32 sceMpegRingbufferConstruct(u32 ringbufferAddr, u32 numPackets, u32 data, u32 size, u32 callbackAddr, u32 callbackArg)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferConstruct(%08x, %i, %08x, %i, %08x, %i)",
		ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
	SceMpegRingBuffer ring;
	InitRingbuffer(&ring, numPackets, data, size, callbackAddr, callbackArg);
	Memory::WriteStruct(ringbufferAddr, &ring);
	return 0;
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

void sceMpegAvcCsc() 
{
	WARN_LOG(HLE, "HACK sceMpegAvcCsc(...)");
	RETURN(0);
}

void sceMpegRingbufferDestruct() 
{
	WARN_LOG(HLE, "HACK sceMpegRingbufferDestruct(...)");
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

void sceMpegGetAvcAu() 
{
	WARN_LOG(HLE, "HACK sceMpegDelete(...)");
	RETURN(0);
}

void sceMpegQueryStreamSize() 
{
	WARN_LOG(HLE, "HACK sceMpegDelete(...)");
	RETURN(0);
}

const HLEFunction sceMpeg[] =
{
	{0xe1ce83a7,sceMpegGetAtracAu,"sceMpegGetAtracAu"},
	{0xfe246728,sceMpegGetAvcAu,"sceMpegGetAvcAu"},
	{0xd8c5f121,&WrapU_UUUUUUU<sceMpegCreate>,"sceMpegCreate"},
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
	{0x606a4649,sceMpegDelete,"sceMpegDelete"},
	{0x874624d6,sceMpegFinish,"sceMpegFinish"},
	{0x800c44df,sceMpegAtracDecode,"sceMpegAtracDecode"},
	{0x0e3c2e9d,&WrapU_UUUUU<sceMpegAvcDecode>,"sceMpegAvcDecode"},
	{0x740fccd1,&WrapU_UUUU<sceMpegAvcDecodeStop>,"sceMpegAvcDecodeStop"},
	{0x4571cc64,&WrapU_U<sceMpegAvcDecodeFlush>,"sceMpegAvcDecodeFlush"},
	{0x0f6c18d7,sceMpegAvcDecodeDetail,"sceMpegAvcDecodeDetail"},
	{0xf0eb1125,sceMpegAvcDecodeYCbCr,"sceMpegAvcDecodeYCbCr"},
	{0xf2930c9c,sceMpegAvcDecodeStopYCbCr,"sceMpegAvcDecodeStopYCbCr"},
	{0xa11c7026,sceMpegAvcDecodeMode,"sceMpegAvcDecodeMode"},
	{0x37295ed8,WrapU_UUUUUU<sceMpegRingbufferConstruct>,"sceMpegRingbufferConstruct"},
	{0x13407f13,sceMpegRingbufferDestruct,"sceMpegRingbufferDestruct"},
	{0xb240a59e,sceMpegRingbufferPut,"sceMpegRingbufferPut"},
	{0xb5f6dc87,sceMpegRingbufferAvailableSize,"sceMpegRingbufferAvailableSize"},
	{0xd7a29f46,WrapU_I<sceMpegRingbufferQueryMemSize>,"sceMpegRingbufferQueryMemSize"},
	{0x769BEBB6,sceMpegRingbufferQueryPackNum,"sceMpegRingbufferQueryPackNum"},
	{0x31bd0272,sceMpegAvcCsc,"sceMpegAvcCsc"},
	{0x211a057c,sceMpegAvcQueryYCbCrSize,"sceMpegAvcQueryYCbCrSize"},
	{0x67179b1b,sceMpegAvcInitYCbCr,"sceMpegAvcInitYCbCr"},
	{0x0558B075,sceMpegAvcCopyYCbCr,"sceMpegAvcCopyYCbCr"},
	{0x9DCFB7EA,sceMpegChangeGetAuMode,"sceMpegChangeGetAuMode"},
	{0x8C1E027D,sceMpegGetPcmAu,"sceMpegGetPcmAu"},
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
