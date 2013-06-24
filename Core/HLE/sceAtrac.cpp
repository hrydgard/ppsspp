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

// This is pretty much a stub implementation. Doesn't actually do anything, just tries to return values
// to keep games happy anyway. So, no ATRAC3 music until someone has reverse engineered Atrac3+.


#include "Core/HLE/HLE.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Common/ChunkFile.h"

#include "sceKernel.h"
#include "sceUtility.h"
#include "sceKernelMemory.h"
#include "sceAtrac.h"


#define ATRAC_ERROR_API_FAIL                 0x80630002
#define ATRAC_ERROR_NO_ATRACID               0x80630003
#define ATRAC_ERROR_INVALID_CODECTYPE        0x80630004
#define ATRAC_ERROR_BAD_ATRACID              0x80630005
#define ATRAC_ERROR_ALL_DATA_LOADED          0x80630009
#define ATRAC_ERROR_NO_DATA		             0x80630010
#define ATRAC_ERROR_SECOND_BUFFER_NEEDED	 0x80630012
#define ATRAC_ERROR_INCORRECT_READ_SIZE	     0x80630013
#define ATRAC_ERROR_ADD_DATA_IS_TOO_BIG      0x80630018
#define ATRAC_ERROR_UNSET_PARAM              0x80630021
#define ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED 0x80630022
#define ATRAC_ERROR_BUFFER_IS_EMPTY			 0x80630023
#define ATRAC_ERROR_ALL_DATA_DECODED         0x80630024

#define AT3_MAGIC			0x0270
#define AT3_PLUS_MAGIC		0xFFFE
#define PSP_MODE_AT_3_PLUS	0x00001000
#define PSP_MODE_AT_3		0x00001001

const int FMT_CHUNK_MAGIC	= 0x20746D66;
const int DATA_CHUNK_MAGIC	= 0x61746164;
const int SMPL_CHUNK_MAGIC	= 0x6C706D73;
const int FACT_CHUNK_MAGIC	= 0x74636166;

const int PSP_ATRAC_ALLDATA_IS_ON_MEMORY = -1;
const int PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY = -2;
const int PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY = -3;

const u32 ATRAC3_MAX_SAMPLES = 0x400;
const u32 ATRAC3PLUS_MAX_SAMPLES = 0x800;

#ifdef USE_FFMPEG

// Urgh! Why is this needed?
#ifdef ANDROID
#ifndef UINT64_C
#define UINT64_C(c) (c ## ULL)
#endif
#endif

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}
#endif // USE_FFMPEG

#include "Core/HW/atrac3plus.h"

struct InputBuffer {
	u32 addr;
	u32 size;
	u32 offset;
	u32 writableBytes;
	u32 neededBytes;
	u32 filesize;
	u32 fileoffset;
};

struct Atrac;
int __AtracSetContext(Atrac *atrac);
void _AtracGenarateContext(Atrac *atrac, SceAtracId *context);

struct AtracLoopInfo {
	int cuePointID;
	int type;
	int startSample;
	int endSample;
	int fraction;
	int playCount;
};

struct Atrac {
	Atrac() : atracID(-1), data_buf(0), decodePos(0), decodeEnd(0), atracChannels(2), atracOutputChannels(2),
		atracBitrate(64), atracBytesPerFrame(0), atracBufSize(0),
		currentSample(0), endSample(-1), firstSampleoffset(0), loopinfoNum(0), loopNum(0) {
		memset(&first, 0, sizeof(first));
		memset(&second, 0, sizeof(second));
#ifdef USE_FFMPEG
		pFormatCtx = 0;
		pAVIOCtx = 0;
		pCodecCtx = 0;
		pSwrCtx = 0;
		pFrame = 0;
#endif // USE_FFMPEG
		decoder_context = 0;
		atracContext = 0;
		sampleQueue.clear();
	}

	~Atrac() {
		CleanStuff();
	}

	void CleanStuff() {
#ifdef USE_FFMPEG
		ReleaseFFMPEGContext();
#endif // USE_FFMPEG

		if (data_buf)
			delete [] data_buf;
		data_buf = 0;

		Atrac3plus_Decoder::CloseContext(&decoder_context);
		sampleQueue.clear();

		if (atracContext.Valid())
			kernelMemory.Free(atracContext.ptr);
	}

	void DoState(PointerWrap &p) {
		p.Do(atracChannels);
		p.Do(atracOutputChannels);

		p.Do(atracID);
		p.Do(first);
		p.Do(atracBufSize);
		p.Do(codeType);

		p.Do(currentSample);
		p.Do(endSample);
		p.Do(firstSampleoffset);

		u32 has_data_buf = data_buf != NULL;
		p.Do(has_data_buf);
		if (has_data_buf) {
			if (p.mode == p.MODE_READ) {
				data_buf = new u8[first.filesize];
			}
			p.DoArray(data_buf, first.filesize);
		}
		if (p.mode == p.MODE_READ && data_buf != NULL) {
			__AtracSetContext(this);
		}
		p.Do(second);

		p.Do(decodePos);
		p.Do(decodeEnd);

		p.Do(atracBitrate);
		p.Do(atracBytesPerFrame);

		p.Do(loopinfo);
		p.Do(loopinfoNum);

		p.Do(loopStartSample);
		p.Do(loopEndSample);
		p.Do(loopNum);

		p.Do(atracContext);

		p.DoMarker("Atrac");
	}

	void Analyze();
	u32 getDecodePosBySample(int sample) {
		int atracSamplesPerFrame = (codeType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
		return (u32)(firstSampleoffset + sample / atracSamplesPerFrame * atracBytesPerFrame );
	}

	int getRemainFrames() {
		// games would like to add atrac data when it wants.
		// Do not try to guess when it want to add data.
		// Just return current remainFrames.

		int remainFrame;
		if (first.fileoffset >= first.filesize || currentSample >= endSample)
			remainFrame = PSP_ATRAC_ALLDATA_IS_ON_MEMORY;
		else {
			// guess the remain frames.
			remainFrame = ((int)first.size - (int)decodePos) / atracBytesPerFrame;
			if (remainFrame < 0)
				remainFrame = 0;
		}
		return remainFrame;
	}

	int atracID;
	u8* data_buf;

	u32 decodePos;
	u32 decodeEnd;

	u16 atracChannels;
	u16 atracOutputChannels;
	u32 atracBitrate;
	u16 atracBytesPerFrame;
	u32 atracBufSize;

	int currentSample;
	int endSample;
	// Offset of the first sample in the input buffer
	int firstSampleoffset;

	std::vector<AtracLoopInfo> loopinfo;
	int loopinfoNum;

	int loopStartSample;
	int loopEndSample;
	int loopNum;

	u32 codeType;

	InputBuffer first;
	InputBuffer second;

	Atrac3plus_Decoder::BufferQueue sampleQueue;
	void* decoder_context;

	PSPPointer<SceAtracId> atracContext;

#ifdef USE_FFMPEG
	AVFormatContext *pFormatCtx;
	AVIOContext	    *pAVIOCtx;
	AVCodecContext  *pCodecCtx;
	SwrContext      *pSwrCtx;
	AVFrame         *pFrame;
	int audio_stream_index;
	void ReleaseFFMPEGContext() {
		if (pFrame)
			av_free(pFrame);
		if (pAVIOCtx && pAVIOCtx->buffer)
			av_free(pAVIOCtx->buffer);
		if (pAVIOCtx)
			av_free(pAVIOCtx);
		if (pSwrCtx)
			swr_free(&pSwrCtx);
		if (pCodecCtx)
			avcodec_close(pCodecCtx);
		if (pFormatCtx)
			avformat_close_input(&pFormatCtx);
		pFormatCtx = 0;
		pAVIOCtx = 0;
		pCodecCtx = 0;
		pSwrCtx = 0;
		pFrame = 0;
	}

	void SeekToSample(int sample) {
		s64 seek_pos = (s64)sample;
		av_seek_frame(pFormatCtx, audio_stream_index, seek_pos, 0);
	}
#endif // USE_FFMPEG

};

const int PSP_NUM_ATRAC_IDS = 6;
static bool atracInited = true;
static Atrac *atracIDs[PSP_NUM_ATRAC_IDS];
static int atracIDTypes[PSP_NUM_ATRAC_IDS];

void __AtracInit() {
	atracInited = true;
	memset(atracIDs, 0, sizeof(atracIDs));

	// Start with 2 of each in this order.
	atracIDTypes[0] = PSP_MODE_AT_3_PLUS;
	atracIDTypes[1] = PSP_MODE_AT_3_PLUS;
	atracIDTypes[2] = PSP_MODE_AT_3;
	atracIDTypes[3] = PSP_MODE_AT_3;
	atracIDTypes[4] = 0;
	atracIDTypes[5] = 0;

#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
#endif // USE_FFMPEG

	Atrac3plus_Decoder::Init();
}

void __AtracDoState(PointerWrap &p) {
	p.Do(atracInited);
	for (int i = 0; i < PSP_NUM_ATRAC_IDS; ++i) {
		bool valid = atracIDs[i] != NULL;
		p.Do(valid);
		if (valid) {
			p.Do(atracIDs[i]);
		} else {
			delete atracIDs[i];
			atracIDs[i] = NULL;
		}
	}
	p.DoArray(atracIDTypes, PSP_NUM_ATRAC_IDS);

	p.DoMarker("sceAtrac");
}

void __AtracShutdown() {
	for (size_t i = 0; i < ARRAY_SIZE(atracIDs); ++i) {
		delete atracIDs[i];
		atracIDs[i] = NULL;
	}

	Atrac3plus_Decoder::Shutdown();
}

Atrac *getAtrac(int atracID) {
	if (atracID < 0 || atracID >= PSP_NUM_ATRAC_IDS) {
		return NULL;
	}
	return atracIDs[atracID];
}

int createAtrac(Atrac *atrac, int codecType) {
	for (int i = 0; i < (int)ARRAY_SIZE(atracIDs); ++i) {
		if (atracIDTypes[i] == codecType && atracIDs[i] == 0) {
			atracIDs[i] = atrac;
			atrac->atracID = i;
			return i;
		}
	}

	return ATRAC_ERROR_NO_ATRACID;
}

int deleteAtrac(int atracID) {
	if (atracID >= 0 && atracID < PSP_NUM_ATRAC_IDS) {
		if (atracIDs[atracID] != NULL) {
			delete atracIDs[atracID];
			atracIDs[atracID] = NULL;

			return 0;
		}
	}

	return ATRAC_ERROR_BAD_ATRACID;
}

int getCodecType(int addr) {
	int at3magic = Memory::Read_U16(addr+20);
	if (at3magic == AT3_MAGIC) {
		return PSP_MODE_AT_3;
	} else if (at3magic == AT3_PLUS_MAGIC) {
		return PSP_MODE_AT_3_PLUS;
	}
	return 0;
}

void Atrac::Analyze()
{
	// reset some values
	currentSample = 0;
	endSample = -1;
	loopNum = 0;
	loopinfoNum = 0;
	loopinfo.clear();
	loopStartSample = -1;
	loopEndSample = -1;
	decodePos = 0;

	if (first.size < 0x100)	{
		ERROR_LOG(HLE, "Atrac buffer very small: %d", first.size);
		return;
	}

	if (!Memory::IsValidAddress(first.addr)) {
		WARN_LOG(HLE, "Atrac buffer at invalid address: %08x-%08x", first.addr, first.size);
		return;
	}

	// TODO: Validate stuff.

	// RIFF size excluding chunk header.
	first.filesize = Memory::Read_U32(first.addr + 4) + 8;

	u32 offset = 12;
	int atracSampleoffset = 0;

	this->decodeEnd = first.filesize;
	bool bfoundData = false;
	while ((first.filesize - offset) >= 8 && !bfoundData) {
		int chunkMagic = Memory::Read_U32(first.addr + offset);
		u32 chunkSize = Memory::Read_U32(first.addr + offset + 4);
		offset += 8;
		if (chunkSize > first.filesize - offset)
			break;
		switch (chunkMagic) {
		case FMT_CHUNK_MAGIC:
			{
				if (chunkSize >= 16) {
					int codeMagic = Memory::Read_U16(first.addr + offset);
					if (codeMagic == AT3_MAGIC)
						codeType = PSP_MODE_AT_3;
					else if (codeMagic == AT3_PLUS_MAGIC)
						codeType = PSP_MODE_AT_3_PLUS;
					else
						codeType = 0;
					atracChannels = Memory::Read_U16(first.addr + offset + 2);
					// int atracSamplerate = Memory::Read_U32(first.addr + offset + 4);    ;Should always be 44100Hz
					int avgBytesPerSec = Memory::Read_U32(first.addr + offset + 8);
					atracBitrate = avgBytesPerSec * 8;
					atracBytesPerFrame = Memory::Read_U16(first.addr + offset + 12);
				}
			}
			break;
		case FACT_CHUNK_MAGIC:
			{
				if (chunkSize >= 8) {
					endSample = Memory::Read_U32(first.addr + offset);
					atracSampleoffset = Memory::Read_U32(first.addr + offset + 4);
				}
			}
			break;
		case SMPL_CHUNK_MAGIC:
			{
				if (chunkSize < 36)
					break;
				int checkNumLoops = Memory::Read_U32(first.addr + offset + 28);
				if (chunkSize >= 36 + (u32)checkNumLoops * 24) {
					loopinfoNum = checkNumLoops;
					loopinfo.resize(loopinfoNum);
					u32 loopinfoAddr = first.addr + offset + 36;
					for (int i = 0; i < loopinfoNum; i++, loopinfoAddr += 24) {
						loopinfo[i].cuePointID = Memory::Read_U32(loopinfoAddr);
						loopinfo[i].type = Memory::Read_U32(loopinfoAddr + 4);
						loopinfo[i].startSample = Memory::Read_U32(loopinfoAddr + 8) - atracSampleoffset;
						loopinfo[i].endSample = Memory::Read_U32(loopinfoAddr + 12) - atracSampleoffset;
						loopinfo[i].fraction = Memory::Read_U32(loopinfoAddr + 16);
						loopinfo[i].playCount = Memory::Read_U32(loopinfoAddr + 20);

						if (loopinfo[i].endSample > endSample)
							loopinfo[i].endSample = endSample;
					}
				}
			}
			break;
		case DATA_CHUNK_MAGIC:
			{
				bfoundData = true;
				firstSampleoffset = offset;
			}
			break;
		}
		offset += chunkSize;
	}

	// set the loopStartSample and loopEndSample by loopinfo
	if (loopinfoNum > 0) {
		loopStartSample = loopinfo[0].startSample;
		loopEndSample = loopinfo[0].endSample;
	} else {
		loopStartSample = loopEndSample = -1;
	}

	// if there is no correct endsample, try to guess it
	if (endSample < 0) {
		int atracSamplesPerFrame = (codeType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
		endSample = (first.filesize / atracBytesPerFrame) * atracSamplesPerFrame;
	}
}

u32 sceAtracGetAtracID(int codecType) {
	if (codecType != PSP_MODE_AT_3 && codecType != PSP_MODE_AT_3_PLUS) {
		ERROR_LOG_REPORT(HLE, "sceAtracGetAtracID(%i): invalid codecType", codecType);
		return ATRAC_ERROR_INVALID_CODECTYPE;
	}

	Atrac *atrac = new Atrac();
	atrac->codeType = codecType;
	int atracID = createAtrac(atrac, codecType);
	if (atracID < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracGetAtracID(%i): no free ID", codecType);
		delete atrac;
		return atracID;
	}

	INFO_LOG(HLE, "%d=sceAtracGetAtracID(%i)", atracID, codecType);
	return atracID;
}

u32 _AtracAddStreamData(int atracID, u8 *buf, u32 bytesToAdd) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac)
		return 0;
	int addbytes = std::min(bytesToAdd, atrac->first.filesize - atrac->first.fileoffset);
	memcpy(atrac->data_buf + atrac->first.fileoffset, buf, addbytes);
	atrac->first.size += bytesToAdd;
	if (atrac->first.size > atrac->first.filesize)
		atrac->first.size = atrac->first.filesize;
	atrac->first.fileoffset = atrac->first.size;
	atrac->first.writableBytes = 0;
	if (atrac->atracContext.Valid()) {
		// refresh atracContext
		_AtracGenarateContext(atrac, atrac->atracContext);
	}
	return 0;
}

// PSP allow games to add stream data to a temp buf, the buf size is given by "atracBufSize "here.
// "first.offset" means how many bytes the temp buf has been written,
// and "first.writableBytes" means how many bytes the temp buf is allowed to write
// (We always have "first.offset + first.writableBytes = atracBufSize").
// We only reset the temp buf when games call sceAtracGetStreamDataInfo,
// because that function would tell games how to add the left stream data.
u32 sceAtracAddStreamData(int atracID, u32 bytesToAdd)
{
	DEBUG_LOG(HLE, "sceAtracAddStreamData(%i, %08x)", atracID, bytesToAdd);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return 0;
	} else {
		// TODO
		if (bytesToAdd > atrac->first.writableBytes)
			return ATRAC_ERROR_ADD_DATA_IS_TOO_BIG;

		if (atrac->data_buf && (bytesToAdd > 0)) {
			int addbytes = std::min(bytesToAdd, atrac->first.filesize - atrac->first.fileoffset);
			Memory::Memcpy(atrac->data_buf + atrac->first.fileoffset, atrac->first.addr + atrac->first.offset, addbytes);
		}
		atrac->first.size += bytesToAdd;
		if (atrac->first.size > atrac->first.filesize)
			atrac->first.size = atrac->first.filesize;
		atrac->first.fileoffset = atrac->first.size;
		atrac->first.writableBytes -= bytesToAdd;
		atrac->first.offset += bytesToAdd;
	}
	return 0;
}

u32 _AtracDecodeData(int atracID, u8* outbuf, u32 *SamplesNum, u32* finish, int *remains)
{
	Atrac *atrac = getAtrac(atracID);

	u32 ret = 0;
	if (atrac != NULL) {
		// We already passed the end - return an error (many games check for this.)
		if (atrac->currentSample >= atrac->endSample && atrac->loopNum == 0) {
			*SamplesNum = 0;
			*finish = 1;
			*remains = 0;
			ret = ATRAC_ERROR_ALL_DATA_DECODED;
		} else {
			// TODO: This isn't at all right, but at least it makes the music "last" some time.
			u32 numSamples = 0;
			u32 atracSamplesPerFrame = (atrac->codeType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
#ifdef USE_FFMPEG
			if (atrac->codeType == PSP_MODE_AT_3 && atrac->pCodecCtx) {
				int forceseekSample = atrac->currentSample * 2 > atrac->endSample ? 0 : atrac->endSample;
				atrac->SeekToSample(forceseekSample);
				atrac->SeekToSample(atrac->currentSample);
				AVPacket packet;
				int got_frame, avret;
				while (av_read_frame(atrac->pFormatCtx, &packet) >= 0) {
					if (packet.stream_index == atrac->audio_stream_index) {
						got_frame = 0;
						avret = avcodec_decode_audio4(atrac->pCodecCtx, atrac->pFrame, &got_frame, &packet);
						if (avret < 0) {
							ERROR_LOG(HLE, "avcodec_decode_audio4: Error decoding audio %d", avret);
							av_free_packet(&packet);
							// Avoid getting stuck in a loop (Virtua Tennis)
							*SamplesNum = 0;
							*finish = 1;
							*remains = 0;
							return ATRAC_ERROR_ALL_DATA_DECODED;
						}

						if (got_frame) {
							// got a frame
							int decoded = av_samples_get_buffer_size(NULL, atrac->pFrame->channels,
								atrac->pFrame->nb_samples, (AVSampleFormat)atrac->pFrame->format, 1);
							u8* out = outbuf;
							numSamples = atrac->pFrame->nb_samples;
							avret = swr_convert(atrac->pSwrCtx, &out, atrac->pFrame->nb_samples,
								(const u8**)atrac->pFrame->extended_data, atrac->pFrame->nb_samples);
							if (avret < 0) {
								ERROR_LOG(HLE, "swr_convert: Error while converting %d", avret);
							}

						}
						av_free_packet(&packet);
						if (got_frame)
							break;
					}
				}

			} else
#endif // USE_FFMPEG

			if (atrac->decoder_context) {
				static u8 buf[0x8000];
				if (atrac->sampleQueue.getQueueSize() < ATRAC3PLUS_MAX_SAMPLES * sizeof(s16) * atrac->atracChannels) {
					int decodebytes = 0;
					atrac->decodePos = atrac->getDecodePosBySample(atrac->currentSample);
					int inbytes = std::max((int)atrac->first.size - (int)atrac->decodePos, 0);
					inbytes = std::min(inbytes, (int)atrac->atracBytesPerFrame);
					if (inbytes > 0 && inbytes == atrac->atracBytesPerFrame) {
						Atrac3plus_Decoder::Decode(atrac->decoder_context, atrac->data_buf + atrac->decodePos, inbytes, &decodebytes, buf);
						atrac->sampleQueue.push(buf, decodebytes);
					}
				}
				s16* out = (s16*)outbuf;
				memset(out, 0, ATRAC3PLUS_MAX_SAMPLES * sizeof(s16) * atrac->atracOutputChannels);
				int gotsize = atrac->sampleQueue.pop_front(buf, ATRAC3PLUS_MAX_SAMPLES * sizeof(s16) * atrac->atracChannels);
				numSamples = gotsize / sizeof(s16) / atrac->atracChannels;
				s16* in = (s16*)buf;
				for (u32 i = 0; i < numSamples; i++) {
					s16 sampleL = *in++;
					s16 sampleR = sampleL;
					if (atrac->atracChannels == 2)
						sampleR = *in++;
					*out++ = sampleL;
					if (atrac->atracOutputChannels == 2)
						*out++ = sampleR;
				}
				numSamples = ATRAC3PLUS_MAX_SAMPLES;
			} else
			{
				numSamples = atrac->endSample - atrac->currentSample;
				if (atrac->currentSample >= atrac->endSample) {
					numSamples = 0;
				} else if (numSamples > atracSamplesPerFrame) {
					numSamples = atracSamplesPerFrame;
				}

				if (numSamples == 0 && (atrac->loopNum != 0)) {
					numSamples = atracSamplesPerFrame;
				}
				memset(outbuf, 0, numSamples * sizeof(s16) * atrac->atracOutputChannels);
			}

			*SamplesNum = numSamples;
			// update current sample and decodePos
			atrac->currentSample += numSamples;
			atrac->decodePos = atrac->getDecodePosBySample(atrac->currentSample);

			int finishFlag = 0;
			if (atrac->loopNum != 0 && (atrac->currentSample + atracSamplesPerFrame > atrac->loopEndSample ||
				(numSamples == 0 && atrac->first.size >= atrac->first.filesize))) {
				atrac->currentSample = atrac->loopStartSample;
				if (atrac->loopNum > 0)
					atrac->loopNum --;
			} else if (atrac->currentSample >= atrac->endSample ||
				(numSamples == 0 && atrac->first.size >= atrac->first.filesize))
				finishFlag = 1;

			*finish = finishFlag;
			*remains = atrac->getRemainFrames();
		}
		if (atrac->atracContext.Valid()) {
			// refresh atracContext
			_AtracGenarateContext(atrac, atrac->atracContext);
		}
	// TODO: Can probably remove this after we validate no wrong ids?
	} else {
		memset(outbuf, 0, 4);
		*SamplesNum = 1;
		*finish = 1;
		*remains = -1;
	}

	return ret;
}

u32 sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr)
{
	DEBUG_LOG(HLE, "sceAtracDecodeData(%i, %08x, %08x, %08x, %08x)", atracID, outAddr, numSamplesAddr, finishFlagAddr, remainAddr);
	u32 numSamples = 0;
	u32 finish = 0;
	int remains = 0;
	int ret = _AtracDecodeData(atracID, Memory::GetPointer(outAddr), &numSamples, &finish, &remains);
	Memory::Write_U32(numSamples, numSamplesAddr);
	Memory::Write_U32(finish, finishFlagAddr);
	Memory::Write_U32(remains, remainAddr);
	return ret;
}

u32 sceAtracEndEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracEndEntry()");
	return 0;
}

u32 sceAtracGetBufferInfoForReseting(int atracID, int sample, u32 bufferInfoAddr)
{
	INFO_LOG(HLE, "sceAtracGetBufferInfoForReseting(%i, %i, %08x)",atracID, sample, bufferInfoAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		// TODO: Write the right stuff instead.
		Memory::Memset(bufferInfoAddr, 0, 32);
		//return -1;
	} else {
		int Sampleoffset = atrac->getDecodePosBySample(sample);
		int minWritebytes = std::max(Sampleoffset - (int)atrac->first.size, 0);
		// reset the temp buf for adding more stream data
		atrac->first.writableBytes = std::min(atrac->first.filesize - atrac->first.size, atrac->atracBufSize);
		atrac->first.offset = 0;
		// minWritebytes should not be bigger than writeablebytes
		minWritebytes = std::min(minWritebytes, (int)atrac->first.writableBytes);

		Memory::Write_U32(atrac->first.addr, bufferInfoAddr);
		Memory::Write_U32(atrac->first.writableBytes, bufferInfoAddr + 4);
		Memory::Write_U32(minWritebytes, bufferInfoAddr + 8);
		Memory::Write_U32(atrac->first.fileoffset, bufferInfoAddr + 12);
		Memory::Write_U32(atrac->second.addr, bufferInfoAddr + 16);
		Memory::Write_U32(atrac->second.writableBytes, bufferInfoAddr + 20);
		Memory::Write_U32(atrac->second.neededBytes, bufferInfoAddr + 24);
		Memory::Write_U32(atrac->second.fileoffset, bufferInfoAddr + 28);
	}
	return 0;
}

u32 sceAtracGetBitrate(int atracID, u32 outBitrateAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetBitrate(%i, %08x)", atracID, outBitrateAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return -1;
	} else {
		atrac->atracBitrate = ( atrac->atracBytesPerFrame * 352800 ) / 1000;
		if (atrac->codeType == PSP_MODE_AT_3_PLUS)
			atrac->atracBitrate = ((atrac->atracBitrate >> 11) + 8) & 0xFFFFFFF0;
		else
			atrac->atracBitrate = (atrac->atracBitrate + 511) >> 10;
		if (Memory::IsValidAddress(outBitrateAddr))
			Memory::Write_U32(atrac->atracBitrate, outBitrateAddr);
	}
	return 0;
}

u32 sceAtracGetChannel(int atracID, u32 channelAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetChannel(%i, %08x)", atracID, channelAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return -1;
	} else {
		if (Memory::IsValidAddress(channelAddr))
			Memory::Write_U32(atrac->atracChannels, channelAddr);
	}
	return 0;
}

u32 sceAtracGetLoopStatus(int atracID, u32 loopNumAddr, u32 statusAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetLoopStatus(%i, %08x, %08x)", atracID, loopNumAddr, statusAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		if (Memory::IsValidAddress(loopNumAddr))
			Memory::Write_U32(atrac->loopNum, loopNumAddr);
		// return audio's loopinfo in at3 file
		if (Memory::IsValidAddress(statusAddr)) {
			if (atrac->loopinfoNum > 0)
				Memory::Write_U32(1, statusAddr);
			else
				Memory::Write_U32(0, statusAddr);
		}
	}
	return 0;
}

u32 sceAtracGetInternalErrorInfo(int atracID, u32 errorAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetInternalErrorInfo(%i, %08x)", atracID, errorAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		if (Memory::IsValidAddress(errorAddr))
			Memory::Write_U32(0, errorAddr);
	}
	return 0;
}

u32 sceAtracGetMaxSample(int atracID, u32 maxSamplesAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetMaxSample(%i, %08x)", atracID, maxSamplesAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		if (Memory::IsValidAddress(maxSamplesAddr)) {
			int atracSamplesPerFrame = (atrac->codeType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
			Memory::Write_U32(atracSamplesPerFrame, maxSamplesAddr);
		}
	}
	return 0;
}

u32  sceAtracGetNextDecodePosition(int atracID, u32 outposAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetNextDecodePosition(%i, %08x)", atracID, outposAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return -1;
	} else {
		if (atrac->currentSample >= atrac->endSample)
			return ATRAC_ERROR_ALL_DATA_DECODED;
		if (Memory::IsValidAddress(outposAddr))
			Memory::Write_U32(atrac->currentSample, outposAddr);
	}
	return 0;
}

u32 sceAtracGetNextSample(int atracID, u32 outNAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetNextSample(%i, %08x)", atracID, outNAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
		Memory::Write_U32(1, outNAddr);
	} else {
		if (atrac->currentSample >= atrac->endSample) {
			if (Memory::IsValidAddress(outNAddr))
				Memory::Write_U32(0, outNAddr);
		} else {
			u32 numSamples = atrac->endSample - atrac->currentSample;
			u32 atracSamplesPerFrame = (atrac->codeType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
			if (numSamples > atracSamplesPerFrame)
				numSamples = atracSamplesPerFrame;
			if (Memory::IsValidAddress(outNAddr))
				Memory::Write_U32(numSamples, outNAddr);
		}
	}
	return 0;
}

u32 sceAtracGetRemainFrame(int atracID, u32 remainAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetRemainFrame(%i, %08x)", atracID, remainAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		if (Memory::IsValidAddress(remainAddr))
			Memory::Write_U32(12, remainAddr);
	} else {
		if (Memory::IsValidAddress(remainAddr))
			Memory::Write_U32(atrac->getRemainFrames(), remainAddr);
	}
	return 0;
}

u32 sceAtracGetSecondBufferInfo(int atracID, u32 outposAddr, u32 outBytesAddr)
{
	ERROR_LOG(HLE, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)", atracID, outposAddr, outBytesAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(outposAddr) && atrac)
		Memory::Write_U32(atrac->second.fileoffset, outposAddr);
	if (Memory::IsValidAddress(outBytesAddr) && atrac)
		Memory::Write_U32(atrac->second.writableBytes, outBytesAddr);
	// TODO: Maybe don't write the above?
	return ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED;
}

u32 sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetSoundSample(%i, %08x, %08x, %08x)", atracID, outEndSampleAddr, outLoopStartSampleAddr, outLoopEndSampleAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		if (Memory::IsValidAddress(outEndSampleAddr))
			Memory::Write_U32(atrac->endSample, outEndSampleAddr); // outEndSample
		if (Memory::IsValidAddress(outLoopStartSampleAddr))
			Memory::Write_U32(atrac->loopStartSample, outLoopStartSampleAddr); // outLoopStartSample
		if (Memory::IsValidAddress(outLoopEndSampleAddr))
			Memory::Write_U32(atrac->loopEndSample, outLoopEndSampleAddr); // outLoopEndSample
	}
	return 0;
}

// Games call this function to get some info for add more stream data,
// such as where the data read from, where the data add to,
// and how many bytes are allowed to add.
u32 sceAtracGetStreamDataInfo(int atracID, u32 writeAddr, u32 writableBytesAddr, u32 readOffsetAddr)
{
	DEBUG_LOG(HLE, "sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x)", atracID, writeAddr, writableBytesAddr, readOffsetAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		// reset the temp buf for adding more stream data
		atrac->first.writableBytes = std::min(atrac->first.filesize - atrac->first.size, atrac->atracBufSize);
		atrac->first.offset = 0;

		if (Memory::IsValidAddress(writeAddr))
		Memory::Write_U32(atrac->first.addr, writeAddr);
		if (Memory::IsValidAddress(writableBytesAddr))
		Memory::Write_U32(atrac->first.writableBytes, writableBytesAddr);
		if (Memory::IsValidAddress(readOffsetAddr))
		Memory::Write_U32(atrac->first.fileoffset, readOffsetAddr);
	}
	return 0;
}

u32 sceAtracReleaseAtracID(int atracID)
{
	INFO_LOG(HLE, "sceAtracReleaseAtracID(%i)", atracID);
	return deleteAtrac(atracID);
}

u32 sceAtracResetPlayPosition(int atracID, int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf)
{
	INFO_LOG(HLE, "sceAtracResetPlayPosition(%i, %i, %i, %i)", atracID, sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		if (bytesWrittenFirstBuf > 0)
			sceAtracAddStreamData(atracID, bytesWrittenFirstBuf);
		atrac->currentSample = sample;
#ifdef USE_FFMPEG
		if (atrac->codeType == PSP_MODE_AT_3 && atrac->pCodecCtx) {
			atrac->SeekToSample(sample);
		} else
#endif // USE_FFMPEG
		{
			atrac->decodePos = atrac->getDecodePosBySample(sample);
		}
	}
	return 0;
}

#ifdef USE_FFMPEG
int _AtracReadbuffer(void *opaque, uint8_t *buf, int buf_size)
{
	Atrac *atrac = (Atrac *)opaque;
	if (atrac->decodePos > atrac->first.filesize)
		return -1;
	int size = std::min((int)atrac->atracBufSize, buf_size);
	size = std::max(std::min(((int)atrac->first.size - (int)atrac->decodePos), size), 0);
	if (size > 0)
		memcpy(buf, atrac->data_buf + atrac->decodePos, size);
	atrac->decodePos += size;
	return size;
}

int64_t _AtracSeekbuffer(void *opaque, int64_t offset, int whence)
{
	Atrac *atrac = (Atrac*)opaque;
	if (offset > atrac->first.filesize)
		return -1;

	switch (whence) {
	case SEEK_SET:
		atrac->decodePos = (u32)offset;
		break;
	case SEEK_CUR:
		atrac->decodePos += (u32)offset;
		break;
	case SEEK_END:
		atrac->decodePos = atrac->first.filesize - (u32)offset;
		break;
#ifdef USE_FFMPEG
	case AVSEEK_SIZE:
		return atrac->first.filesize;
#endif
	}
	return atrac->decodePos;
}

#endif // USE_FFMPEG

#ifdef USE_FFMPEG
int __AtracUpdateOutputMode(Atrac *atrac, int wanted_channels) {
	if (atrac->pSwrCtx && atrac->atracOutputChannels == wanted_channels)
		return 0;
	atrac->atracOutputChannels = wanted_channels;
	int64_t wanted_channel_layout = av_get_default_channel_layout(wanted_channels);
	int64_t dec_channel_layout = av_get_default_channel_layout(atrac->atracChannels);

	atrac->pSwrCtx =
		swr_alloc_set_opts
		(
			atrac->pSwrCtx,
			wanted_channel_layout,
			AV_SAMPLE_FMT_S16,
			atrac->pCodecCtx->sample_rate,
			dec_channel_layout,
			atrac->pCodecCtx->sample_fmt,
			atrac->pCodecCtx->sample_rate,
			0,
			NULL
		);
	if (!atrac->pSwrCtx) {
		ERROR_LOG(HLE, "swr_alloc_set_opts: Could not allocate resampler context");
		return -1;
	}
	if (swr_init(atrac->pSwrCtx) < 0) {
		ERROR_LOG(HLE, "swr_init: Failed to initialize the resampling context");
		return -1;
	}
	return 0;
}
#endif // USE_FFMPEG

int __AtracSetContext(Atrac *atrac)
{
	if (atrac->codeType == PSP_MODE_AT_3_PLUS) {
		atrac->decoder_context = Atrac3plus_Decoder::OpenContext();
		return 0;
	}

#ifdef USE_FFMPEG
	u8* tempbuf = (u8*)av_malloc(atrac->atracBufSize);

	atrac->pFormatCtx = avformat_alloc_context();
	atrac->pAVIOCtx = avio_alloc_context(tempbuf, atrac->atracBufSize, 0, (void*)atrac, _AtracReadbuffer, NULL, _AtracSeekbuffer);
	atrac->pFormatCtx->pb = atrac->pAVIOCtx;

	int ret;
	// Load audio buffer
	if((ret = avformat_open_input((AVFormatContext**)&atrac->pFormatCtx, NULL, NULL, NULL)) != 0) {
		ERROR_LOG(HLE, "avformat_open_input: Cannot open input %d", ret);
		return -1;
	}

	if((ret = avformat_find_stream_info(atrac->pFormatCtx, NULL)) < 0) {
		ERROR_LOG(HLE, "avformat_find_stream_info: Cannot find stream information %d", ret);
		return -1;
	}

	AVCodec *pCodec;
	// select the audio stream
	ret = av_find_best_stream(atrac->pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &pCodec, 0);
	if (ret < 0) {
		ERROR_LOG(HLE, "av_find_best_stream: Cannot find a audio stream in the input file %d", ret);
		return -1;
	}
	atrac->audio_stream_index = ret;
	atrac->pCodecCtx = atrac->pFormatCtx->streams[atrac->audio_stream_index]->codec;

	// open codec
	if ((ret = avcodec_open2(atrac->pCodecCtx, pCodec, NULL)) < 0) {
		ERROR_LOG(HLE, "avcodec_open2: Cannot open audio decoder %d", ret);
		return -1;
	}

	if ((ret = __AtracUpdateOutputMode(atrac, atrac->atracOutputChannels)) < 0)
		return ret;

	// alloc audio frame
	atrac->pFrame = avcodec_alloc_frame();
	// reinit decodePos, because ffmpeg had changed it.
	atrac->decodePos = 0;
#endif

	return 0;
}

int _AtracSetData(Atrac *atrac, u32 buffer, u32 bufferSize)
{
	if (atrac->first.size > atrac->first.filesize)
		atrac->first.size = atrac->first.filesize;
	atrac->first.fileoffset = atrac->first.size;

	// got the size of temp buf, and calculate writableBytes and offset
	atrac->atracBufSize = bufferSize;
	atrac->first.writableBytes = (u32)std::max((int)bufferSize - (int)atrac->first.size, 0);
	atrac->first.offset = atrac->first.size;

	// some games may reuse an atracID for playing sound
	atrac->CleanStuff();


	if (atrac->codeType == PSP_MODE_AT_3) {
		WARN_LOG(HLE, "This is an atrac3 audio");

#ifdef USE_FFMPEG
		atrac->data_buf = new u8[atrac->first.filesize];
		Memory::Memcpy(atrac->data_buf, buffer, std::min(bufferSize, atrac->first.filesize));
		return __AtracSetContext(atrac);
#endif // USE_FFMPEG

	} else if (atrac->codeType == PSP_MODE_AT_3_PLUS) {
		if (atrac->atracChannels == 1) {
			WARN_LOG(HLE, "This is an atrac3+ mono audio");
		} else {
			WARN_LOG(HLE, "This is an atrac3+ stereo audio");
		}
		atrac->data_buf = new u8[atrac->first.filesize];
		Memory::Memcpy(atrac->data_buf, buffer, std::min(bufferSize, atrac->first.filesize));
		return __AtracSetContext(atrac);
	}


	return 0;
}

int _AtracSetData(int atracID, u32 buffer, u32 bufferSize, bool needReturnAtracID = false)
{
	Atrac *atrac = getAtrac(atracID);
	if (!atrac)
		return -1;
	int ret = _AtracSetData(atrac, buffer, bufferSize);
	if (needReturnAtracID && ret >= 0)
		ret = atracID;
	// not sure the real delay time
	return hleDelayResult(ret, "atrac set data", 100);
}

u32 sceAtracSetHalfwayBuffer(int atracID, u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	INFO_LOG(HLE, "sceAtracSetHalfwayBuffer(%i, %08x, %8x, %8x)", atracID, halfBuffer, readSize, halfBufferSize);
	if (readSize > halfBufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;

	Atrac *atrac = getAtrac(atracID);
	int ret = 0;
	if (atrac != NULL) {
		atrac->first.addr = halfBuffer;
		atrac->first.size = readSize;
		atrac->Analyze();
		atrac->atracOutputChannels = 2;
		ret = _AtracSetData(atracID, halfBuffer, halfBufferSize);
	}
	return ret;
}

u32 sceAtracSetSecondBuffer(int atracID, u32 secondBuffer, u32 secondBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetSecondBuffer(%i, %08x, %8x)", atracID, secondBuffer, secondBufferSize);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

u32 sceAtracSetData(int atracID, u32 buffer, u32 bufferSize)
{
	INFO_LOG(HLE, "sceAtracSetData(%i, %08x, %08x)", atracID, buffer, bufferSize);
	Atrac *atrac = getAtrac(atracID);
	int ret = 0;
	if (atrac != NULL) {
		atrac->first.addr = buffer;
		atrac->first.size = bufferSize;
		atrac->Analyze();
		atrac->atracOutputChannels = 2;
		ret = _AtracSetData(atracID, buffer, bufferSize);
	}
	return ret;
}

int sceAtracSetDataAndGetID(u32 buffer, u32 bufferSize)
{
	int codecType = getCodecType(buffer);

	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	atrac->Analyze();
	atrac->atracOutputChannels = 2;
	int atracID = createAtrac(atrac, codecType);
	if (atracID < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracSetDataAndGetID(%08x, %08x): no free ID", buffer, bufferSize);
		delete atrac;
		return atracID;
	}
	INFO_LOG(HLE, "%d=sceAtracSetDataAndGetID(%08x, %08x)", atracID, buffer, bufferSize);
	int ret = _AtracSetData(atracID, buffer, bufferSize, true);
	if (ret < 0)
		return ret;
	return atracID;
}

int sceAtracSetHalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	if (readSize > halfBufferSize) {
		ERROR_LOG(HLE, "sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x): incorrect read size", halfBuffer, readSize, halfBufferSize);
		return ATRAC_ERROR_INCORRECT_READ_SIZE;
	}
	int codecType = getCodecType(halfBuffer);

	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = readSize;
	atrac->Analyze();
	atrac->atracOutputChannels = 2;
	int atracID = createAtrac(atrac, codecType);
	if (atracID < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x): no free ID", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return atracID;
	}
	INFO_LOG(HLE, "%d=sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	int ret = _AtracSetData(atracID, halfBuffer, halfBufferSize, true);
	if (ret < 0)
		return ret;
	return atracID;
}

u32 sceAtracStartEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracStartEntry(.)");
	return 0;
}

u32 sceAtracSetLoopNum(int atracID, int loopNum)
{
	INFO_LOG(HLE, "sceAtracSetLoopNum(%i, %i)", atracID, loopNum);
	Atrac *atrac = getAtrac(atracID);
	if (atrac) {
		if (atrac->loopinfoNum == 0)
			return ATRAC_ERROR_UNSET_PARAM;
		atrac->loopNum = loopNum;
		if (loopNum != 0 && atrac->loopinfoNum == 0) {
			// Just loop the whole audio
			atrac->loopStartSample = 0;
			atrac->loopEndSample = atrac->endSample;
		}
	}
	return 0;
}

int sceAtracReinit(int at3Count, int at3plusCount)
{
	for (int i = 0; i < PSP_NUM_ATRAC_IDS; ++i) {
		if (atracIDs[i] != NULL) {
			ERROR_LOG_REPORT(HLE, "sceAtracReinit(%d, %d): cannot reinit while IDs in use", at3Count, at3plusCount);
			return SCE_KERNEL_ERROR_BUSY;
		}
	}

	memset(atracIDTypes, 0, sizeof(atracIDTypes));
	int next = 0;
	int space = PSP_NUM_ATRAC_IDS;

	// This seems to deinit things.  Mostly, it cause a reschedule on next deinit (but -1, -1 does not.)
	if (at3Count == 0 && at3plusCount == 0) {
		INFO_LOG(HLE, "sceAtracReinit(%d, %d): deinit", at3Count, at3plusCount);
		atracInited = false;
		return hleDelayResult(0, "atrac reinit", 200);
	}

	// First, ATRAC3+.  These IDs seem to cost double (probably memory.)
	// Intentionally signed.  9999 tries to allocate, -1 does not.
	for (int i = 0; i < at3plusCount; ++i) {
		space -= 2;
		if (space >= 0) {
			atracIDTypes[next++] = PSP_MODE_AT_3_PLUS;
		}
	}
	for (int i = 0; i < at3Count; ++i) {
		space -= 1;
		if (space >= 0) {
			atracIDTypes[next++] = PSP_MODE_AT_3;
		}
	}

	// If we ran out of space, we still initialize some, but return an error.
	int result = space >= 0 ? 0 : SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	if (atracInited || next == 0) {
		INFO_LOG(HLE, "sceAtracReinit(%d, %d)", at3Count, at3plusCount);
		atracInited = true;
		return result;
	} else {
		INFO_LOG(HLE, "sceAtracReinit(%d, %d): init", at3Count, at3plusCount);
		atracInited = true;
		return hleDelayResult(result, "atrac reinit", 400);
	}
}

int sceAtracGetOutputChannel(int atracID, u32 outputChanPtr)
{
	DEBUG_LOG(HLE, "sceAtracGetOutputChannel(%i, %08x)", atracID, outputChanPtr);
	Atrac *atrac = getAtrac(atracID);
	if (Memory::IsValidAddress(outputChanPtr))
		Memory::Write_U32(atrac ? atrac->atracOutputChannels : 2, outputChanPtr);
	return 0;
}

int sceAtracIsSecondBufferNeeded(int atracID)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracIsSecondBufferNeeded(%i)", atracID);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

int sceAtracSetMOutHalfwayBuffer(int atracID, u32 MOutHalfBuffer, u32 readSize, u32 MOutHalfBufferSize)
{
	INFO_LOG(HLE, "sceAtracSetMOutHalfwayBuffer(%i, %08x, %08x, %08x)", atracID, MOutHalfBuffer, readSize, MOutHalfBufferSize);
	if (readSize > MOutHalfBufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;

	Atrac *atrac = getAtrac(atracID);
	int ret = 0;
	if (atrac != NULL) {
		atrac->first.addr = MOutHalfBuffer;
		atrac->first.size = readSize;
		atrac->Analyze();
		atrac->atracOutputChannels = 1;
		ret = _AtracSetData(atracID, MOutHalfBuffer, MOutHalfBufferSize);
	}
	return ret;
}

u32 sceAtracSetMOutData(int atracID, u32 buffer, u32 bufferSize)
{
	INFO_LOG(HLE, "sceAtracSetMOutData(%i, %08x, %08x)", atracID, buffer, bufferSize);
	Atrac *atrac = getAtrac(atracID);
	int ret = 0;
	if (atrac != NULL) {
		atrac->first.addr = buffer;
		atrac->first.size = bufferSize;
		atrac->Analyze();
		atrac->atracOutputChannels = 1;
		ret = _AtracSetData(atracID, buffer, bufferSize);
	}
	return ret;
}

int sceAtracSetMOutDataAndGetID(u32 buffer, u32 bufferSize)
{
	int codecType = getCodecType(buffer);

	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	atrac->Analyze();
	atrac->atracOutputChannels = 1;
	int atracID = createAtrac(atrac, codecType);
	if (atracID < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracSetMOutDataAndGetID(%08x, %08x): no free ID", buffer, bufferSize);
		delete atrac;
		return atracID;
	}
	INFO_LOG(HLE, "%d=sceAtracSetMOutDataAndGetID(%08x, %08x)", atracID, buffer, bufferSize);
	int ret = _AtracSetData(atracID, buffer, bufferSize, true);
	if (ret < 0)
		return ret;
	return atracID;
}

int sceAtracSetMOutHalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	if (readSize > halfBufferSize) {
		ERROR_LOG(HLE, "sceAtracSetMOutDataAndGetID(%08x, %08x, %08x): incorrect read size", halfBuffer, readSize, halfBufferSize);
		return ATRAC_ERROR_INCORRECT_READ_SIZE;
	}
	int codecType = getCodecType(halfBuffer);

	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = readSize;
	atrac->Analyze();
	atrac->atracOutputChannels = 1;
	int atracID = createAtrac(atrac, codecType);
	if (atracID < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracSetMOutDataAndGetID(%08x, %08x, %08x): no free ID", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return atracID;
	}
	INFO_LOG(HLE, "sceAtracSetMOutHalfwayBufferAndGetID(%08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	int ret = _AtracSetData(atracID, halfBuffer, halfBufferSize, true);
	if (ret < 0)
		return ret;
	return atracID;
}

int sceAtracSetAA3DataAndGetID(u32 buffer, int bufferSize, int fileSize, u32 metadataSizeAddr)
{
	int codecType = getCodecType(buffer);

	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	atrac->Analyze();
	int atracID = createAtrac(atrac, codecType);
	if (atracID < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracSetAA3DataAndGetID(%08x, %i, %i, %08x): no free ID",  buffer, bufferSize, fileSize, metadataSizeAddr);
		delete atrac;
		return atracID;
	}
	ERROR_LOG(HLE, "UNIMPL %d=sceAtracSetAA3DataAndGetID(%08x, %i, %i, %08x)", atracID, buffer, bufferSize, fileSize, metadataSizeAddr);
	return atracID;
}

int _AtracGetIDByContext(u32 contextAddr) {
	int atracID = (int)Memory::Read_U32(contextAddr + 0xfc);
#ifdef USE_FFMPEG
	Atrac *atrac = getAtrac(atracID);
	if (atrac)
		__AtracUpdateOutputMode(atrac, 1);
#endif // USE_FFMPEG
	return atracID;
}

void _AtracGenarateContext(Atrac *atrac, SceAtracId *context) {
	context->info.buffer = atrac->first.addr;
	context->info.bufferByte = atrac->atracBufSize;
	context->info.secondBuffer = atrac->second.addr;
	context->info.secondBufferByte = atrac->second.size;
	context->info.codec = atrac->codeType;
	context->info.loopNum = atrac->loopNum;
	context->info.loopStart = atrac->loopStartSample > 0 ? atrac->loopStartSample : 0;
	context->info.loopEnd = atrac->loopEndSample > 0 ? atrac->loopEndSample : 0;
	if (atrac->first.size >= atrac->first.filesize) {
		// state 2, all data loaded
		context->info.state = 2;
	} else if (atrac->loopinfoNum == 0) {
		// state 3, lack some data, no loop info
		context->info.state = 3;
	} else {
		// state 6, lack some data, has loop info
		context->info.state = 6;
	}
	context->info.samplesPerChan = (atrac->codeType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
	context->info.sampleSize = atrac->atracBytesPerFrame;
	context->info.numChan = atrac->atracChannels;
	context->info.dataOff = atrac->firstSampleoffset;
	context->info.endSample = atrac->endSample;
	context->info.dataEnd = atrac->first.filesize;
	context->info.curOff = atrac->first.size;
	context->info.decodePos = atrac->getDecodePosBySample(atrac->currentSample);
	context->info.streamDataByte = atrac->first.size - atrac->firstSampleoffset;

	u8* buf = (u8*)context;
	*(u32*)(buf + 0xfc) = atrac->atracID;
}

int _sceAtracGetContextAddress(int atracID)
{
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(HLE, "_sceAtracGetContextAddress(%i): bad atrac id", atracID);
		return 0;
	}
	if (!atrac->atracContext.Valid()) {
		// allocate a new atracContext
		u32 contextsize = 256;
		atrac->atracContext = kernelMemory.Alloc(contextsize, false, "Atrac Context");
		if (atrac->atracContext.Valid())
			Memory::Memset(atrac->atracContext.ptr, 0, 256);

		WARN_LOG(HLE, "%08x=_sceAtracGetContextAddress(%i): allocated new context", atrac->atracContext.ptr, atracID);
	}
	else
		WARN_LOG(HLE, "%08x=_sceAtracGetContextAddress(%i)", atrac->atracContext.ptr, atracID);
	if (atrac->atracContext.Valid())
		_AtracGenarateContext(atrac, atrac->atracContext);
	if (atrac->currentSample >= atrac->endSample && atrac->loopNum == 0) {
		// This is a hack method to release those already finished atrac3 voice.
		// It should be removed after the real issue solved.
		deleteAtrac(atracID);
		return 0;
	}
	return atrac->atracContext.ptr;
}

static u8 at3Header[] ={0x52,0x49,0x46,0x46,0x3b,0xbe,0x00,0x00,0x57,0x41,0x56,0x45,0x66,0x6d,0x74,0x20,0x20,0x00,0x00,0x00,0x70,0x02,0x02,0x00,0x44,0xac,0x00,0x00,0x4d,0x20,0x00,0x00,0xc0,0x00,0x00,0x00,0x0e,0x00,0x01,0x00,0x00,0x10,0x00,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x64,0x61,0x74,0x61,0xc0,0xbd,0x00,0x00};
static const u16 at3HeaderMap[][4] = {
    { 0x00C0, 0x1, 0x8,  0x00 },
    { 0x0098, 0x1, 0x8,  0x00 },
    { 0x0180, 0x2, 0x10, 0x00 },
    { 0x0130, 0x2, 0x10, 0x00 },
    { 0x00C0, 0x2, 0x10, 0x01 }
};

static const int at3HeaderMapSize = sizeof(at3HeaderMap)/(sizeof(u16) * 4);

bool initAT3Decoder(Atrac *atrac, u32 dataSize = 0xffb4a8)
{
	for (int i = 0; i < at3HeaderMapSize; i ++) {
		if (at3HeaderMap[i][0] == atrac->atracBytesPerFrame && at3HeaderMap[i][1] == atrac->atracChannels) {
			*(u32*)(at3Header + 0x04) = dataSize + sizeof(at3Header) - 8;
			*(u16*)(at3Header + 0x16) = atrac->atracChannels;
			*(u16*)(at3Header + 0x20) = atrac->atracBytesPerFrame;
			atrac->atracBitrate = ( atrac->atracBytesPerFrame * 352800 ) / 1000;
			atrac->atracBitrate = (atrac->atracBitrate + 511) >> 10;
			*(u32*)(at3Header + 0x1c) = atrac->atracBitrate * 1000 / 8;
			at3Header[0x29] = (u8)at3HeaderMap[i][2];
			at3Header[0x2c] = (u8)at3HeaderMap[i][3];
			at3Header[0x2e] = (u8)at3HeaderMap[i][3];
			*(u32*)(at3Header + sizeof(at3Header) - 4) = dataSize;
			return true;
		}
	}
	return false;
}

static u8 at3plusHeader[] = {0x52,0x49,0x46,0x46,0x00,0xb5,0xff,0x00,0x57,0x41,0x56,0x45,0x66,0x6d,0x74,0x20,0x34,0x00,0x00,0x00,0xfe,0xff,0x02,0x00,0x44,0xac,0x00,0x00,0xa0,0x1f,0x00,0x00,0xe8,0x02,0x00,0x00,0x22,0x00,0x00,0x08,0x03,0x00,0x00,0x00,0xbf,0xaa,0x23,0xe9,0x58,0xcb,0x71,0x44,0xa1,0x19,0xff,0xfa,0x01,0xe4,0xce,0x62,0x01,0x00,0x28,0x5c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x66,0x61,0x63,0x74,0x08,0x00,0x00,0x00,0xff,0xff,0xff,0x00,0x00,0x08,0x00,0x00,0x64,0x61,0x74,0x61,0xa8,0xb4,0xff,0x00};
static const u16 at3plusHeaderMap[][3] = {
    { 0x00C0, 0x1, 0x0 },
    { 0x1724, 0x0, 0x0 },
    { 0x0180, 0x1, 0x0 },
    { 0x2224, 0x0, 0x0 },
    { 0x0178, 0x1, 0x0 },
    { 0x2E24, 0x0, 0x0 },

    { 0x0230, 0x1, 0x0 },
    { 0x4524, 0x0, 0x0 },
    { 0x02E8, 0x1, 0x0 },
    { 0x5C24, 0x0, 0x0 },

    { 0x0118, 0x2, 0x0 },
    { 0x2228, 0x0, 0x0 },
    { 0x0178, 0x2, 0x0 },
    { 0x2E28, 0x0, 0x0 },

    { 0x0230, 0x2, 0x0 },
    { 0x4528, 0x0, 0x0 },
    { 0x02E8, 0x2, 0x0 },
    { 0x5C28, 0x0, 0x0 },

    { 0x03A8, 0x2, 0x0 },
    { 0x7428, 0x0, 0x0 },
    { 0x0460, 0x2, 0x0 },
    { 0x8B28, 0x0, 0x0 },

    { 0x05D0, 0x2, 0x0 },
    { 0xB928, 0x0, 0x0 },
    { 0x0748, 0x2, 0x0 },
    { 0xE828, 0x0, 0x0 },

    { 0x0800, 0x2, 0x0 },
    { 0xFF28, 0x0, 0x0 }
};

static const int at3plusHeaderMapSize = sizeof(at3plusHeaderMap)/(sizeof(u16) * 3);

bool initAT3plusDecoder(Atrac *atrac, u32 dataSize = 0xffb4a8)
{
	for (int i = 0; i < at3plusHeaderMapSize; i += 2) {
		if (at3plusHeaderMap[i][0] == atrac->atracBytesPerFrame && at3plusHeaderMap[i][1] == atrac->atracChannels) {
			*(u32*)(at3plusHeader + 0x04) = dataSize + sizeof(at3plusHeader) - 8;
			*(u16*)(at3plusHeader + 0x16) = atrac->atracChannels;
			*(u16*)(at3plusHeader + 0x20) = atrac->atracBytesPerFrame;
			atrac->atracBitrate = ( atrac->atracBytesPerFrame * 352800 ) / 1000;
			atrac->atracBitrate = ((atrac->atracBitrate >> 11) + 8) & 0xFFFFFFF0;
			*(u32*)(at3plusHeader + 0x1c) = atrac->atracBitrate * 1000 / 8;
			*(u16*)(at3plusHeader + 0x3e) = at3plusHeaderMap[i + 1][0];
			*(u32*)(at3plusHeader + sizeof(at3plusHeader) - 4) = dataSize;
			return true;
		}
	}
	return false;
}

int sceAtracLowLevelInitDecoder(int atracID, u32 paramsAddr)
{
	INFO_LOG(HLE, "sceAtracLowLevelInitDecoder(%i, %08x)", atracID, paramsAddr);
	Atrac *atrac = getAtrac(atracID);
	if (atrac && Memory::IsValidAddress(paramsAddr)) {
		atrac->atracChannels = Memory::Read_U32(paramsAddr);
		atrac->atracOutputChannels = Memory::Read_U32(paramsAddr + 4);
		atrac->atracBufSize = Memory::Read_U32(paramsAddr + 8);
		atrac->atracBytesPerFrame = atrac->atracBufSize;
		atrac->first.writableBytes = atrac->atracBytesPerFrame;
		atrac->CleanStuff();
		INFO_LOG(HLE, "Channels: %i outputChannels: %i bytesperFrame: %x", 
			atrac->atracChannels, atrac->atracOutputChannels, atrac->atracBytesPerFrame);
#ifdef USE_FFMPEG
		if (atrac->codeType == PSP_MODE_AT_3) {
			WARN_LOG(HLE, "This is an atrac3 audio (low level)");
			int headersize = sizeof(at3Header);
			initAT3Decoder(atrac);
			atrac->firstSampleoffset = headersize;
			atrac->first.size = headersize;
			atrac->first.filesize = headersize + atrac->atracBytesPerFrame;
			atrac->data_buf = new u8[atrac->first.filesize];
			memcpy(atrac->data_buf, at3Header, headersize);
			atrac->currentSample = 0;
			__AtracSetContext(atrac);
			return 0;
		}
#endif // USE_FFMPEG

		if (atrac->codeType == PSP_MODE_AT_3_PLUS){
			atrac->data_buf = new u8[atrac->atracBytesPerFrame];
			__AtracSetContext(atrac);
			return 0;
		}
	}
	return 0;
}

int sceAtracLowLevelDecode(int atracID, u32 sourceAddr, u32 sourceBytesConsumedAddr, u32 samplesAddr, u32 sampleBytesAddr)
{
	DEBUG_LOG(HLE, "UNIMPL sceAtracLowLevelDecode(%i, %08x, %08x, %08x, %08x)", atracID, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);
	Atrac *atrac = getAtrac(atracID);

#ifdef USE_FFMPEG
	if (Memory::IsValidAddress(sourceAddr) && Memory::IsValidAddress(sourceBytesConsumedAddr) &&
		Memory::IsValidAddress(samplesAddr) && Memory::IsValidAddress(sampleBytesAddr) && atrac && atrac->pCodecCtx) {
			u32 sourcebytes = atrac->first.writableBytes;
			if (sourcebytes > 0) {
				Memory::Memcpy(atrac->data_buf + atrac->first.size, sourceAddr, sourcebytes);
				atrac->first.size += sourcebytes;
			}
			int numSamples = 0;
			int forceseekSample = 0x200000;
			atrac->SeekToSample(forceseekSample);
			atrac->SeekToSample(atrac->currentSample);
			AVPacket packet;
			int got_frame, avret;
			while (av_read_frame(atrac->pFormatCtx, &packet) >= 0) {
				if (packet.stream_index == atrac->audio_stream_index) {
					got_frame = 0;
					avret = avcodec_decode_audio4(atrac->pCodecCtx, atrac->pFrame, &got_frame, &packet);
					if (avret < 0) {
						ERROR_LOG(HLE, "atracID: %i, avcodec_decode_audio4: Error decoding audio %d", atracID, avret);
						av_free_packet(&packet);
						break;
					}

					if (got_frame) {
						// got a frame
						int decoded = av_samples_get_buffer_size(NULL, atrac->pFrame->channels,
							atrac->pFrame->nb_samples, (AVSampleFormat)atrac->pFrame->format, 1);
						u8* out = Memory::GetPointer(samplesAddr);
						numSamples = atrac->pFrame->nb_samples;
						avret = swr_convert(atrac->pSwrCtx, &out, atrac->pFrame->nb_samples,
							(const u8**)atrac->pFrame->extended_data, atrac->pFrame->nb_samples);
						if (avret < 0) {
							ERROR_LOG(HLE, "swr_convert: Error while converting %d", avret);
						}
					}
					av_free_packet(&packet);
					if (got_frame)
						break;
				}
			}
			atrac->currentSample += numSamples;
			numSamples = ATRAC3_MAX_SAMPLES;
			Memory::Write_U32(numSamples * sizeof(s16) * atrac->atracOutputChannels, sampleBytesAddr);
			atrac->SeekToSample(atrac->currentSample);
			if (atrac->decodePos >= atrac->first.size) {
				atrac->first.writableBytes = atrac->atracBytesPerFrame;
				atrac->first.size = atrac->firstSampleoffset;
				atrac->currentSample = 0;
			}
			else
				atrac->first.writableBytes = 0;
			Memory::Write_U32(atrac->first.writableBytes, sourceBytesConsumedAddr);
			return 0;
	}
#endif // USE_FFMPEG

	if (Memory::IsValidAddress(sourceAddr) && Memory::IsValidAddress(sourceBytesConsumedAddr) &&
		Memory::IsValidAddress(samplesAddr) && Memory::IsValidAddress(sampleBytesAddr) && atrac && atrac->decoder_context) {
			u32 sourcebytes = atrac->first.writableBytes;
			static u8 buf[0x8000];
			if (sourcebytes > 0) {
				int decodebytes = 0;
				Atrac3plus_Decoder::Decode(atrac->decoder_context, Memory::GetPointer(sourceAddr), sourcebytes, &decodebytes, buf);
				atrac->sampleQueue.push(buf, decodebytes);
			}
			s16* out = (s16*)Memory::GetPointer(samplesAddr);
			memset(out, 0, ATRAC3PLUS_MAX_SAMPLES * sizeof(s16) * atrac->atracOutputChannels);
			int gotsize = atrac->sampleQueue.pop_front(buf, ATRAC3PLUS_MAX_SAMPLES * sizeof(s16) * atrac->atracChannels);
			int numSamples = gotsize / sizeof(s16) / atrac->atracChannels;
			s16* in = (s16*)buf;
			for (int i = 0; i < numSamples; i++) {
				s16 sampleL = *in++;
				s16 sampleR = sampleL;
				if (atrac->atracChannels == 2)
					sampleR = *in++;
				*out++ = sampleL;
				if (atrac->atracOutputChannels == 2)
					*out++ = sampleR;
			}
			numSamples = ATRAC3PLUS_MAX_SAMPLES;
			Memory::Write_U32(numSamples * sizeof(s16) * atrac->atracOutputChannels, sampleBytesAddr);
			int space = atrac->sampleQueue.getQueueSize();
			if (space < ATRAC3PLUS_MAX_SAMPLES * sizeof(s16) * atrac->atracChannels)
				atrac->first.writableBytes = atrac->atracBytesPerFrame;
			else
				atrac->first.writableBytes = 0;
			Memory::Write_U32(atrac->first.writableBytes, sourceBytesConsumedAddr);
			return 0;
	}

	return 0;
}

int sceAtracSetAA3HalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	if (readSize > halfBufferSize) {
		ERROR_LOG(HLE, "sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x): invalid read size", halfBuffer, readSize, halfBufferSize);
		return ATRAC_ERROR_INCORRECT_READ_SIZE;
	}

	if (readSize < 0 || halfBufferSize < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x): invalid buffer size", halfBuffer, readSize, halfBufferSize);
		return -1;
	}

	int codecType = getCodecType(halfBuffer);
	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = halfBufferSize;
	atrac->Analyze();
	int atracID = createAtrac(atrac, codecType);
	if (atracID < 0) {
		ERROR_LOG_REPORT(HLE, "sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x): no free ID", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return atracID;
	}
	ERROR_LOG(HLE, "UNIMPL %d=sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	return createAtrac(atrac, codecType);
}

const HLEFunction sceAtrac3plus[] =
{
	{0x7db31251,WrapU_IU<sceAtracAddStreamData>,"sceAtracAddStreamData"},
	{0x6a8c3cd5,WrapU_IUUUU<sceAtracDecodeData>,"sceAtracDecodeData"},
	{0xd5c28cc0,WrapU_V<sceAtracEndEntry>,"sceAtracEndEntry"},
	{0x780f88d1,WrapU_I<sceAtracGetAtracID>,"sceAtracGetAtracID"},
	{0xca3ca3d2,WrapU_IIU<sceAtracGetBufferInfoForReseting>,"sceAtracGetBufferInfoForReseting"},
	{0xa554a158,WrapU_IU<sceAtracGetBitrate>,"sceAtracGetBitrate"},
	{0x31668baa,WrapU_IU<sceAtracGetChannel>,"sceAtracGetChannel"},
	{0xfaa4f89b,WrapU_IUU<sceAtracGetLoopStatus>,"sceAtracGetLoopStatus"},
	{0xe88f759b,WrapU_IU<sceAtracGetInternalErrorInfo>,"sceAtracGetInternalErrorInfo"},
	{0xd6a5f2f7,WrapU_IU<sceAtracGetMaxSample>,"sceAtracGetMaxSample"},
	{0xe23e3a35,WrapU_IU<sceAtracGetNextDecodePosition>,"sceAtracGetNextDecodePosition"},
	{0x36faabfb,WrapU_IU<sceAtracGetNextSample>,"sceAtracGetNextSample"},
	{0x9ae849a7,WrapU_IU<sceAtracGetRemainFrame>,"sceAtracGetRemainFrame"},
	{0x83e85ea0,WrapU_IUU<sceAtracGetSecondBufferInfo>,"sceAtracGetSecondBufferInfo"},
	{0xa2bba8be,WrapU_IUUU<sceAtracGetSoundSample>,"sceAtracGetSoundSample"},
	{0x5d268707,WrapU_IUUU<sceAtracGetStreamDataInfo>,"sceAtracGetStreamDataInfo"},
	{0x61eb33f5,WrapU_I<sceAtracReleaseAtracID>,"sceAtracReleaseAtracID"},
	{0x644e5607,WrapU_IIII<sceAtracResetPlayPosition>,"sceAtracResetPlayPosition"},
	{0x3f6e26b5,WrapU_IUUU<sceAtracSetHalfwayBuffer>,"sceAtracSetHalfwayBuffer"},
	{0x83bf7afd,WrapU_IUU<sceAtracSetSecondBuffer>,"sceAtracSetSecondBuffer"},
	{0x0E2A73AB,WrapU_IUU<sceAtracSetData>,"sceAtracSetData"}, //?
	{0x7a20e7af,WrapI_UU<sceAtracSetDataAndGetID>,"sceAtracSetDataAndGetID"},
	{0xd1f59fdb,WrapU_V<sceAtracStartEntry>,"sceAtracStartEntry"},
	{0x868120b5,WrapU_II<sceAtracSetLoopNum>,"sceAtracSetLoopNum"},
	{0x132f1eca,WrapI_II<sceAtracReinit>,"sceAtracReinit"},
	{0xeca32a99,WrapI_I<sceAtracIsSecondBufferNeeded>,"sceAtracIsSecondBufferNeeded"},
	{0x0fae370e,WrapI_UUU<sceAtracSetHalfwayBufferAndGetID>,"sceAtracSetHalfwayBufferAndGetID"},
	{0x2DD3E298,WrapU_IIU<sceAtracGetBufferInfoForReseting>,"sceAtracGetBufferInfoForResetting"},
	{0x5CF9D852,WrapI_IUUU<sceAtracSetMOutHalfwayBuffer>,"sceAtracSetMOutHalfwayBuffer"},
	{0xF6837A1A,WrapU_IUU<sceAtracSetMOutData>,"sceAtracSetMOutData"},
	{0x472E3825,WrapI_UU<sceAtracSetMOutDataAndGetID>,"sceAtracSetMOutDataAndGetID"},
	{0x9CD7DE03,WrapI_UUU<sceAtracSetMOutHalfwayBufferAndGetID>,"sceAtracSetMOutHalfwayBufferAndGetID"},
	{0xB3B5D042,WrapI_IU<sceAtracGetOutputChannel>,"sceAtracGetOutputChannel"},
	{0x5622B7C1,WrapI_UIIU<sceAtracSetAA3DataAndGetID>,"sceAtracSetAA3DataAndGetID"},
	{0x5DD66588,WrapI_UUU<sceAtracSetAA3HalfwayBufferAndGetID>,"sceAtracSetAA3HalfwayBufferAndGetID"},
	{0x231FC6B7,WrapI_I<_sceAtracGetContextAddress>,"_sceAtracGetContextAddress"},
	{0x1575D64B,WrapI_IU<sceAtracLowLevelInitDecoder>,"sceAtracLowLevelInitDecoder"},
	{0x0C116E1B,WrapI_IUUUU<sceAtracLowLevelDecode>,"sceAtracLowLevelDecode"},
};


void Register_sceAtrac3plus()
{
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
