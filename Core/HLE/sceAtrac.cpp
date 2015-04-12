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

#include <algorithm>

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"
#include "Common/ChunkFile.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceAtrac.h"

// Notes about sceAtrac buffer management
//
// sceAtrac decodes from a buffer the game fills, where this buffer is one of:
//   * Not yet initialized (state NO DATA = 1)
//   * The entire size of the audio data, and filled with audio data (state ALL DATA LOADED = 2)
//   * The entire size, but only partially filled so far (state HALFWAY BUFFER = 3)
//   * Smaller than the audio, sliding without any loop (state STREAMED WITHOUT LOOP = 4)
//   * Smaller than the audio, sliding with a loop at the end (state STREAMED WITH LOOP AT END = 5)
//   * Smaller with a second buffer to help with a loop in the middle (state STREAMED WITH SECOND BUF = 6)
//   * Not managed, decoding using "low level" manual looping etc. (LOW LEVEL = 8)
//   * Not managed, reserved externally - possibly by sceSas - through low level (RESERVED = 16)

#define ATRAC_ERROR_API_FAIL                 0x80630002
#define ATRAC_ERROR_NO_ATRACID               0x80630003
#define ATRAC_ERROR_INVALID_CODECTYPE        0x80630004
#define ATRAC_ERROR_BAD_ATRACID              0x80630005
#define ATRAC_ERROR_UNKNOWN_FORMAT           0x80630006
#define ATRAC_ERROR_WRONG_CODECTYPE          0x80630007
#define ATRAC_ERROR_ALL_DATA_LOADED          0x80630009
#define ATRAC_ERROR_NO_DATA                  0x80630010
#define ATRAC_ERROR_SIZE_TOO_SMALL           0x80630011
#define ATRAC_ERROR_SECOND_BUFFER_NEEDED     0x80630012
#define ATRAC_ERROR_INCORRECT_READ_SIZE      0x80630013
#define ATRAC_ERROR_BAD_SAMPLE               0x80630015
#define ATRAC_ERROR_ADD_DATA_IS_TOO_BIG      0x80630018
#define ATRAC_ERROR_NOT_MONO                 0x80630019
#define ATRAC_ERROR_NO_LOOP_INFORMATION      0x80630021
#define ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED 0x80630022
#define ATRAC_ERROR_BUFFER_IS_EMPTY          0x80630023
#define ATRAC_ERROR_ALL_DATA_DECODED         0x80630024
#define ATRAC_ERROR_AA3_INVALID_DATA         0x80631003
#define ATRAC_ERROR_AA3_SIZE_TOO_SMALL       0x80631004

#define AT3_MAGIC           0x0270
#define AT3_PLUS_MAGIC      0xFFFE
#define PSP_MODE_AT_3_PLUS  0x00001000
#define PSP_MODE_AT_3       0x00001001

const int RIFF_CHUNK_MAGIC = 0x46464952;
const int RIFF_WAVE_MAGIC = 0x45564157;
const int FMT_CHUNK_MAGIC  = 0x20746D66;
const int DATA_CHUNK_MAGIC = 0x61746164;
const int SMPL_CHUNK_MAGIC = 0x6C706D73;
const int FACT_CHUNK_MAGIC = 0x74636166;

const int PSP_ATRAC_ALLDATA_IS_ON_MEMORY = -1;
const int PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY = -2;
const int PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY = -3;

const u32 ATRAC3_MAX_SAMPLES = 0x400;
const u32 ATRAC3PLUS_MAX_SAMPLES = 0x800;

static const int atracDecodeDelay = 2300;

#ifdef USE_FFMPEG

extern "C" {
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/samplefmt.h"
}

#endif // USE_FFMPEG

enum AtracDecodeResult {
	ATDECODE_FAILED = -1,
	ATDECODE_FEEDME = 0,
	ATDECODE_GOTFRAME = 1,
};

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
	Atrac() : atracID(-1), data_buf(0), decodePos(0), decodeEnd(0), bufferPos(0),
		atracChannels(0),atracOutputChannels(2),
		atracBitrate(64), atracBytesPerFrame(0), atracBufSize(0),
		currentSample(0), endSample(0), firstSampleoffset(0), dataOff(0),
		loopinfoNum(0), loopStartSample(-1), loopEndSample(-1), loopNum(0),
		failedDecode(false), resetBuffer(false), codecType(0) {
		memset(&first, 0, sizeof(first));
		memset(&second, 0, sizeof(second));
#ifdef USE_FFMPEG
		pFormatCtx = nullptr;
		pAVIOCtx = nullptr;
		pCodecCtx = nullptr;
		pSwrCtx = nullptr;
		pFrame = nullptr;
		packet = nullptr;
		audio_stream_index = 0;
#endif // USE_FFMPEG
		atracContext = 0;
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

		if (atracContext.IsValid())
			kernelMemory.Free(atracContext.ptr);
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("Atrac", 1, 4);
		if (!s)
			return;

		p.Do(atracChannels);
		p.Do(atracOutputChannels);

		p.Do(atracID);
		p.Do(first);
		p.Do(atracBufSize);
		p.Do(codecType);

		p.Do(currentSample);
		p.Do(endSample);
		p.Do(firstSampleoffset);
		if (s >= 3) {
			p.Do(dataOff);
		} else {
			dataOff = firstSampleoffset;
		}

		u32 has_data_buf = data_buf != NULL;
		p.Do(has_data_buf);
		if (has_data_buf) {
			if (p.mode == p.MODE_READ) {
				if (data_buf)
					delete [] data_buf;
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
		if (s >= 4) {
			p.Do(bufferPos);
		} else {
			bufferPos = decodePos;
		}

		p.Do(atracBitrate);
		p.Do(atracBytesPerFrame);

		p.Do(loopinfo);
		p.Do(loopinfoNum);

		p.Do(loopStartSample);
		p.Do(loopEndSample);
		p.Do(loopNum);

		p.Do(atracContext);
		
		if (s >= 2)
			p.Do(resetBuffer);
	}

	int Analyze();
	int AnalyzeAA3();

	u32 getDecodePosBySample(int sample) const {
		int atracSamplesPerFrame = (codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
		return (u32)(firstSampleoffset + sample / atracSamplesPerFrame * atracBytesPerFrame );
	}

	int getRemainFrames() const {
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
	u32 bufferPos;

	u16 atracChannels;
	u16 atracOutputChannels;
	u32 atracBitrate;
	u16 atracBytesPerFrame;
	u32 atracBufSize;

	int currentSample;
	int endSample;
	int firstSampleoffset;
	// Offset of the first sample in the input buffer
	int dataOff;

	std::vector<AtracLoopInfo> loopinfo;
	int loopinfoNum;

	int loopStartSample;
	int loopEndSample;
	int loopNum;

	bool failedDecode;
	bool resetBuffer;

	u32 codecType;

	InputBuffer first;
	InputBuffer second;

	PSPPointer<SceAtracId> atracContext;

#ifdef USE_FFMPEG
	AVFormatContext *pFormatCtx;
	AVIOContext	    *pAVIOCtx;
	AVCodecContext  *pCodecCtx;
	SwrContext      *pSwrCtx;
	AVFrame         *pFrame;
	AVPacket        *packet;
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
		if (packet)
			av_free_packet(packet);
		delete packet;
		pFormatCtx = nullptr;
		pAVIOCtx = nullptr;
		pCodecCtx = nullptr;
		pSwrCtx = nullptr;
		pFrame = nullptr;
		packet = nullptr;
	}

	void ForceSeekToSample(int sample) {
		av_seek_frame(pFormatCtx, audio_stream_index, sample + 0x200000, 0);
		av_seek_frame(pFormatCtx, audio_stream_index, sample, 0);
		avcodec_flush_buffers(pCodecCtx);

		// Discard any pending packet data.
		packet->size = 0;

		currentSample = sample;
	}

	void SeekToSample(int sample) {
		const u32 atracSamplesPerFrame = (codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);

		// Discard any pending packet data.
		packet->size = 0;

		// Some kind of header size?
		const u32 firstOffsetExtra = codecType == PSP_CODEC_AT3PLUS ? 368 : 69;
		// It seems like the PSP aligns the sample position to 0x800...?
		const u32 offsetSamples = firstSampleoffset + firstOffsetExtra;
		const u32 unalignedSamples = (offsetSamples + sample) % atracSamplesPerFrame;
		int seekFrame = sample + offsetSamples - unalignedSamples;

		if (sample != currentSample) {
			// "Seeking" by reading frames seems to work much better.
			av_seek_frame(pFormatCtx, audio_stream_index, 0, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(pCodecCtx);

			for (int i = 0; i < seekFrame; i += atracSamplesPerFrame) {
				while (FillPacket() && DecodePacket() == ATDECODE_FEEDME) {
					continue;
				}
			}
		} else {
			// For some reason, if we skip seeking, we get the wrong amount of data.
			// (even without flushing the packet...)
			av_seek_frame(pFormatCtx, audio_stream_index, seekFrame, 0);
			avcodec_flush_buffers(pCodecCtx);
		}
		currentSample = sample;
	}

	bool FillPacket() {
		if (packet->size > 0) {
			return true;
		}
		do {
			// This is double-free safe, so we just call it before each read and at the end.
			av_free_packet(packet);
			if (av_read_frame(pFormatCtx, packet) < 0) {
				return false;
			}
			// We keep reading until we get the right stream index.
		} while (packet->stream_index != audio_stream_index);

		return true;
	}

	AtracDecodeResult DecodePacket() {
		AVPacket tempPacket;
		AVPacket *decodePacket = packet;
		if (packet->size < (int)atracBytesPerFrame) {
			// Whoops, we have a packet that is smaller than a frame.  Let's meld a new one.
			u32 initialSize = packet->size;
			int needed = atracBytesPerFrame - initialSize;
			av_init_packet(&tempPacket);
			av_copy_packet(&tempPacket, packet);
			av_grow_packet(&tempPacket, needed);

			// Okay, we're "out of data", let's get more.
			packet->size = 0;

			if (FillPacket()) {
				int to_copy = packet->size >= needed ? needed : packet->size;
				memcpy(tempPacket.data + initialSize, packet->data, to_copy);
				packet->size -= to_copy;
				packet->data += to_copy;
				tempPacket.size = initialSize + to_copy;
			} else {
				tempPacket.size = initialSize;
			}
			decodePacket = &tempPacket;
		}

		int got_frame = 0;
		int bytes_read = avcodec_decode_audio4(pCodecCtx, pFrame, &got_frame, decodePacket);
		if (packet != decodePacket) {
			av_free_packet(&tempPacket);
		}
		if (bytes_read == AVERROR_PATCHWELCOME) {
			ERROR_LOG(ME, "Unsupported feature in ATRAC audio.");
			// Let's try the next packet.
			if (packet == decodePacket) {
				packet->size = 0;
			}
			// TODO: Or actually, should we return a blank frame and pretend it worked?
			return ATDECODE_FEEDME;
		} else if (bytes_read < 0) {
			ERROR_LOG_REPORT(ME, "avcodec_decode_audio4: Error decoding audio %d / %08x", bytes_read, bytes_read);
			failedDecode = true;
			return ATDECODE_FAILED;
		}

		if (packet == decodePacket) {
			packet->size -= bytes_read;
			packet->data += bytes_read;
		}
		return got_frame ? ATDECODE_GOTFRAME : ATDECODE_FEEDME;
	}
#endif // USE_FFMPEG

private:
	void AnalyzeReset();
};

struct AtracSingleResetBufferInfo {
	u32 writePosPtr;
	u32 writableBytes;
	u32 minWriteBytes;
	u32 filePos;
};

struct AtracResetBufferInfo {
	AtracSingleResetBufferInfo first;
	AtracSingleResetBufferInfo second;
};

const int PSP_NUM_ATRAC_IDS = 6;
static bool atracInited = true;
static Atrac *atracIDs[PSP_NUM_ATRAC_IDS];
static u32 atracIDTypes[PSP_NUM_ATRAC_IDS];

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
}

void __AtracDoState(PointerWrap &p) {
	auto s = p.Section("sceAtrac", 1);
	if (!s)
		return;

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
}

void __AtracShutdown() {
	for (size_t i = 0; i < ARRAY_SIZE(atracIDs); ++i) {
		delete atracIDs[i];
		atracIDs[i] = NULL;
	}
}

static Atrac *getAtrac(int atracID) {
	if (atracID < 0 || atracID >= PSP_NUM_ATRAC_IDS) {
		return NULL;
	}
	return atracIDs[atracID];
}

static int createAtrac(Atrac *atrac) {
	for (int i = 0; i < (int)ARRAY_SIZE(atracIDs); ++i) {
		if (atracIDTypes[i] == atrac->codecType && atracIDs[i] == 0) {
			atracIDs[i] = atrac;
			atrac->atracID = i;
			return i;
		}
	}

	return ATRAC_ERROR_NO_ATRACID;
}

static int deleteAtrac(int atracID) {
	if (atracID >= 0 && atracID < PSP_NUM_ATRAC_IDS) {
		if (atracIDs[atracID] != NULL) {
			delete atracIDs[atracID];
			atracIDs[atracID] = NULL;

			return 0;
		}
	}

	return ATRAC_ERROR_BAD_ATRACID;
}

void Atrac::AnalyzeReset() {
	// Reset some values.
	codecType = 0;
	currentSample = 0;
	endSample = -1;
	loopNum = 0;
	loopinfoNum = 0;
	loopinfo.clear();
	loopStartSample = -1;
	loopEndSample = -1;
	decodePos = 0;
	bufferPos = 0;
	atracChannels = 2;
}

struct RIFFFmtChunk {
	u16_le fmtTag;
	u16_le channels;
	u32_le samplerate;
	u32_le avgBytesPerSec;
	u16_le blockAlign;
};

int Atrac::Analyze() {
	AnalyzeReset();

	// 72 is about the size of the minimum required data to even be valid.
	if (first.size < 72) {
		ERROR_LOG_REPORT(ME, "Atrac buffer very small: %d", first.size);
		return ATRAC_ERROR_SIZE_TOO_SMALL;
	}

	if (!Memory::IsValidAddress(first.addr)) {
		WARN_LOG_REPORT(ME, "Atrac buffer at invalid address: %08x-%08x", first.addr, first.size);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	// TODO: Validate stuff.

	if (Memory::Read_U32(first.addr) != RIFF_CHUNK_MAGIC) {
		ERROR_LOG_REPORT(ME, "Atrac buffer invalid RIFF header: %08x", first.addr);
		return ATRAC_ERROR_UNKNOWN_FORMAT;
	}

	u32 offset = 8;
	int loopFirstSampleOffset = 0;
	firstSampleoffset = 0;

	while (Memory::Read_U32(first.addr + offset) != RIFF_WAVE_MAGIC) {
		// Get the size preceding the magic.
		int chunk = Memory::Read_U32(first.addr + offset - 4);
		// Round the chunk size up to the nearest 2.
		offset += chunk + (chunk & 1);
		if (offset + 12 > first.size) {
			ERROR_LOG_REPORT(ME, "Atrac buffer too small without WAVE chunk: %d at %d", first.size, offset);
			return ATRAC_ERROR_SIZE_TOO_SMALL;
		}
		if (Memory::Read_U32(first.addr + offset) != RIFF_CHUNK_MAGIC) {
			ERROR_LOG_REPORT(ME, "RIFF chunk did not contain WAVE");
			return ATRAC_ERROR_UNKNOWN_FORMAT;
		}
		offset += 8;
	}
	offset += 4;

	if (offset != 12) {
		WARN_LOG_REPORT(ME, "RIFF chunk at offset: %d", offset);
	}

	// RIFF size excluding chunk header.
	first.filesize = Memory::Read_U32(first.addr + offset - 8) + 8;

	this->decodeEnd = first.filesize;
	bool bfoundData = false;
	while (first.filesize >= offset + 8 && !bfoundData) {
		int chunkMagic = Memory::Read_U32(first.addr + offset);
		u32 chunkSize = Memory::Read_U32(first.addr + offset + 4);
		offset += 8;
		if (chunkSize > first.filesize - offset)
			break;
		switch (chunkMagic) {
		case FMT_CHUNK_MAGIC:
			{
				if (codecType != 0) {
					ERROR_LOG_REPORT(ME, "Atrac buffer with multiple fmt definitions");
					return ATRAC_ERROR_UNKNOWN_FORMAT;
				}

				auto at3fmt = PSPPointer<const RIFFFmtChunk>::Create(first.addr + offset);
				if (chunkSize < 32 || (at3fmt->fmtTag == AT3_PLUS_MAGIC && chunkSize < 52)) {
					ERROR_LOG_REPORT(ME, "Atrac buffer with too small fmt definition %d", chunkSize);
					return ATRAC_ERROR_UNKNOWN_FORMAT;
				}

				if (at3fmt->fmtTag == AT3_MAGIC)
					codecType = PSP_MODE_AT_3;
				else if (at3fmt->fmtTag == AT3_PLUS_MAGIC)
					codecType = PSP_MODE_AT_3_PLUS;
				else {
					ERROR_LOG_REPORT(ME, "Atrac buffer with invalid fmt magic: %04x", at3fmt->fmtTag);
					return ATRAC_ERROR_UNKNOWN_FORMAT;
				}
				atracChannels = at3fmt->channels;
				if (atracChannels != 1 && atracChannels != 2) {
					ERROR_LOG_REPORT(ME, "Atrac buffer with invalid channel count: %d", atracChannels);
					return ATRAC_ERROR_UNKNOWN_FORMAT;
				}
				if (at3fmt->samplerate != 44100) {
					ERROR_LOG_REPORT(ME, "Atrac buffer with unsupported sample rate: %d", at3fmt->samplerate);
					return ATRAC_ERROR_UNKNOWN_FORMAT;
				}
				atracBitrate = at3fmt->avgBytesPerSec * 8;
				atracBytesPerFrame = at3fmt->blockAlign;
				if (atracBytesPerFrame == 0) {
					ERROR_LOG_REPORT(ME, "Atrac buffer with invalid bytes per frame: %d", atracBytesPerFrame);
					return ATRAC_ERROR_UNKNOWN_FORMAT;
				}

				// TODO: There are some format specific bytes here which seem to have fixed values?
			}
			break;
		case FACT_CHUNK_MAGIC:
			{
				endSample = Memory::Read_U32(first.addr + offset);
				firstSampleoffset = Memory::Read_U32(first.addr + offset + 4);
				if (chunkSize >= 12) {
					// Seems like this indicates it's got a separate offset for loops.
					loopFirstSampleOffset = Memory::Read_U32(first.addr + offset + 8);
				} else if (chunkSize >= 8) {
					loopFirstSampleOffset = firstSampleoffset;
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
						loopinfo[i].startSample = Memory::Read_U32(loopinfoAddr + 8) - loopFirstSampleOffset;
						loopinfo[i].endSample = Memory::Read_U32(loopinfoAddr + 12) - loopFirstSampleOffset;
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
				dataOff = offset;
			}
			break;
		}
		offset += chunkSize;
	}

	if (codecType == 0) {
		WARN_LOG_REPORT(ME, "Atrac buffer with unexpected or no magic bytes");
		return ATRAC_ERROR_UNKNOWN_FORMAT;
	}

	// set the loopStartSample and loopEndSample by loopinfo
	if (loopinfoNum > 0) {
		loopStartSample = loopinfo[0].startSample;
		loopEndSample = loopinfo[0].endSample;
	} else {
		loopStartSample = loopEndSample = -1;
	}

	// if there is no correct endsample, try to guess it
	if (endSample < 0 && atracBytesPerFrame != 0) {
		int atracSamplesPerFrame = (codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
		endSample = (first.filesize / atracBytesPerFrame) * atracSamplesPerFrame;
	}

	return 0;
}

int Atrac::AnalyzeAA3() {
	AnalyzeReset();

	if (first.size < 10) {
		return ATRAC_ERROR_AA3_SIZE_TOO_SMALL;
	}

	// TODO: Make sure this validation is correct, more testing.

	const u8 *buffer = Memory::GetPointer(first.addr);
	if (buffer[0] != 'e' || buffer[1] != 'a' || buffer[2] != '3') {
		return ATRAC_ERROR_AA3_INVALID_DATA;
	}

	// It starts with an id3 header (replaced with ea3.)  This is the size.
	u32 tagSize = buffer[9] | (buffer[8] << 7) | (buffer[7] << 14) | (buffer[6] << 21);
	if (first.size < tagSize + 36) {
		return ATRAC_ERROR_AA3_SIZE_TOO_SMALL;
	}

	// EA3 header starts at id3 header (10) + tagSize.
	buffer = Memory::GetPointer(first.addr + 10 + tagSize);
	if (buffer[0] != 'E' || buffer[1] != 'A' || buffer[2] != '3') {
		return ATRAC_ERROR_AA3_INVALID_DATA;
	}

	// Based on FFmpeg's code.
	u32 codecParams = buffer[35] | (buffer[34] << 8) | (buffer[35] << 16);
	const u32 at3SampleRates[8] = { 32000, 44100, 48000, 88200, 96000, 0 };

	switch (buffer[32]) {
	case 0:
		codecType = PSP_MODE_AT_3;
		atracBytesPerFrame = (codecParams & 0x03FF) * 8;
		atracBitrate = at3SampleRates[(codecParams >> 13) & 7] * atracBytesPerFrame * 8 / 1024;
		atracChannels = 2;
		break;
	case 1:
		codecType = PSP_MODE_AT_3_PLUS;
		atracBytesPerFrame = ((codecParams & 0x03FF) * 8) + 8;
		atracBitrate = at3SampleRates[(codecParams >> 13) & 7] * atracBytesPerFrame * 8 / 2048;
		atracChannels = (codecParams >> 10) & 7;
		break;
	case 3:
	case 4:
	case 5:
		ERROR_LOG_REPORT(ME, "OMA header contains unsupported codec type: %d", buffer[32]);
		return ATRAC_ERROR_AA3_INVALID_DATA;
	default:
		return ATRAC_ERROR_AA3_INVALID_DATA;
	}

	dataOff = 0;
	firstSampleoffset = 0;
	if (endSample < 0 && atracBytesPerFrame != 0) {
		int atracSamplesPerFrame = (codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
		endSample = (first.filesize / atracBytesPerFrame) * atracSamplesPerFrame;
	}
	return 0;
}

static u32 sceAtracGetAtracID(int codecType) {
	if (codecType != PSP_MODE_AT_3 && codecType != PSP_MODE_AT_3_PLUS) {
		ERROR_LOG_REPORT(ME, "sceAtracGetAtracID(%i): invalid codecType", codecType);
		return ATRAC_ERROR_INVALID_CODECTYPE;
	}

	Atrac *atrac = new Atrac();
	atrac->codecType = codecType;
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		ERROR_LOG(ME, "sceAtracGetAtracID(%i): no free ID", codecType);
		delete atrac;
		return atracID;
	}

	INFO_LOG(ME, "%d=sceAtracGetAtracID(%i)", atracID, codecType);
	return atracID;
}

u32 _AtracAddStreamData(int atracID, u32 bufPtr, u32 bytesToAdd) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac)
		return 0;
	int addbytes = std::min(bytesToAdd, atrac->first.filesize - atrac->first.fileoffset);
	Memory::Memcpy(atrac->data_buf + atrac->first.fileoffset, bufPtr, addbytes);
	atrac->first.size += bytesToAdd;
	if (atrac->first.size > atrac->first.filesize)
		atrac->first.size = atrac->first.filesize;
	atrac->first.fileoffset = atrac->first.size;
	atrac->first.writableBytes = 0;
	if (atrac->atracContext.IsValid()) {
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
static u32 sceAtracAddStreamData(int atracID, u32 bytesToAdd) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	} else if (!atrac->data_buf) {
		return hleLogError(ME, ATRAC_ERROR_NO_DATA, "no data");
	} else {
		if (atrac->first.size >= atrac->first.filesize) {
			// Let's avoid spurious warnings.  Some games call this with 0 which is pretty harmless.
			if (bytesToAdd == 0)
				return hleLogDebug(ME, ATRAC_ERROR_ALL_DATA_LOADED, "stream entirely loaded");
			return hleLogWarning(ME, ATRAC_ERROR_ALL_DATA_LOADED, "stream entirely loaded");
		}
		if (bytesToAdd > atrac->first.writableBytes)
			return hleLogWarning(ME, ATRAC_ERROR_ADD_DATA_IS_TOO_BIG, "too many bytes");

		if (bytesToAdd > 0) {
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
	return hleLogSuccessI(ME, 0);
}

u32 _AtracDecodeData(int atracID, u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	Atrac *atrac = getAtrac(atracID);

	u32 ret = 0;
	if (atrac == NULL) {
		ret = ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ret = ATRAC_ERROR_NO_DATA;
	} else {
		// We already passed the end - return an error (many games check for this.)
		if (atrac->currentSample >= atrac->endSample && atrac->loopNum == 0) {
			*SamplesNum = 0;
			*finish = 1;
			*remains = 0;
			ret = ATRAC_ERROR_ALL_DATA_DECODED;
		} else {
			// TODO: This isn't at all right, but at least it makes the music "last" some time.
			u32 numSamples = 0;
			u32 atracSamplesPerFrame = (atrac->codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);

			// Some kind of header size?
			u32 firstOffsetExtra = atrac->codecType == PSP_CODEC_AT3PLUS ? 368 : 69;
			// It seems like the PSP aligns the sample position to 0x800...?
			int offsetSamples = atrac->firstSampleoffset + firstOffsetExtra;
			int skipSamples = 0;
			u32 maxSamples = atrac->endSample - atrac->currentSample;
			u32 unalignedSamples = (offsetSamples + atrac->currentSample) % atracSamplesPerFrame;
			if (unalignedSamples != 0) {
				// We're off alignment, possibly due to a loop.  Force it back on.
				maxSamples = atracSamplesPerFrame - unalignedSamples;
				skipSamples = unalignedSamples;
			}

#ifdef USE_FFMPEG
			if (!atrac->failedDecode && (atrac->codecType == PSP_MODE_AT_3 || atrac->codecType == PSP_MODE_AT_3_PLUS) && atrac->pCodecCtx) {
				atrac->SeekToSample(atrac->currentSample);

				AtracDecodeResult res = ATDECODE_FEEDME;
				while (atrac->FillPacket()) {
					res = atrac->DecodePacket();
					if (res == ATDECODE_FAILED) {
						*SamplesNum = 0;
						*finish = 1;
						*remains = 0;
						return ATRAC_ERROR_ALL_DATA_DECODED;
					}

					if (res == ATDECODE_GOTFRAME) {
						// got a frame
						// Use a small buffer and keep overwriting it with file data constantly
						atrac->first.writableBytes += atrac->atracBytesPerFrame;
						int skipped = std::min(skipSamples, atrac->pFrame->nb_samples);
						skipSamples -= skipped;
						numSamples = atrac->pFrame->nb_samples - skipped;

						// If we're at the end, clamp to samples we want.  It always returns a full chunk.
						numSamples = std::min(maxSamples, numSamples);

						if (skipped > 0 && numSamples == 0) {
							// Wait for the next one.
							res = ATDECODE_FEEDME;
						}

						if (outbuf != NULL && numSamples != 0) {
							int inbufOffset = 0;
							if (skipped != 0) {
								AVSampleFormat fmt = (AVSampleFormat)atrac->pFrame->format;
								// We want the offset per channel.
								inbufOffset = av_samples_get_buffer_size(NULL, 1, skipped, fmt, 1);
							}

							u8 *out = outbuf;
							const u8 *inbuf[2] = {
								atrac->pFrame->extended_data[0] + inbufOffset,
								atrac->pFrame->extended_data[1] + inbufOffset,
							};
							int avret = swr_convert(atrac->pSwrCtx, &out, numSamples, inbuf, numSamples);
							if (outbufPtr != 0) {
								u32 outBytes = numSamples * atrac->atracOutputChannels * sizeof(s16);
								CBreakPoints::ExecMemCheck(outbufPtr, true, outBytes, currentMIPS->pc);
							}
							if (avret < 0) {
								ERROR_LOG(ME, "swr_convert: Error while converting %d", avret);
							}
						}
					}
					if (res == ATDECODE_GOTFRAME) {
						// We only want one frame per call, let's continue the next time.
						break;
					}
				}

				if (res != ATDECODE_GOTFRAME && atrac->currentSample < atrac->endSample) {
					// Never got a frame.  We may have dropped a GHA frame or otherwise have a bug.
					// For now, let's try to provide an extra "frame" if possible so games don't infinite loop.
					numSamples = std::min(maxSamples, atracSamplesPerFrame);
					u32 outBytes = numSamples * atrac->atracOutputChannels * sizeof(s16);
					memset(outbuf, 0, outBytes);
					CBreakPoints::ExecMemCheck(outbufPtr, true, outBytes, currentMIPS->pc);
				}
			}
#endif // USE_FFMPEG

			*SamplesNum = numSamples;
			// update current sample and decodePos
			atrac->currentSample += numSamples;
			atrac->decodePos = atrac->getDecodePosBySample(atrac->currentSample);

			int finishFlag = 0;
			if (atrac->loopNum != 0 && (atrac->currentSample > atrac->loopEndSample ||
				(numSamples == 0 && atrac->first.size >= atrac->first.filesize))) {
				atrac->SeekToSample(atrac->loopStartSample);
				if (atrac->loopNum > 0)
					atrac->loopNum --;
			} else if (atrac->currentSample >= atrac->endSample ||
				(numSamples == 0 && atrac->first.size >= atrac->first.filesize)) {
				finishFlag = 1;
			}

			*finish = finishFlag;
			*remains = atrac->getRemainFrames();
		}
		if (atrac->atracContext.IsValid()) {
			// refresh atracContext
			_AtracGenarateContext(atrac, atrac->atracContext);
		}
	}

	return ret;
}

static u32 sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr) {
	int ret = -1;

	// Note that outAddr being null is completely valid here, used to skip data.

	u32 numSamples = 0;
	u32 finish = 0;
	int remains = 0;
	ret = _AtracDecodeData(atracID, Memory::GetPointer(outAddr), outAddr, &numSamples, &finish, &remains);
	if (ret != (int)ATRAC_ERROR_BAD_ATRACID && ret != (int)ATRAC_ERROR_NO_DATA) {
		if (Memory::IsValidAddress(numSamplesAddr))
			Memory::Write_U32(numSamples, numSamplesAddr);
		if (Memory::IsValidAddress(finishFlagAddr))
			Memory::Write_U32(finish, finishFlagAddr);
		if (Memory::IsValidAddress(remainAddr))
			Memory::Write_U32(remains, remainAddr);
	}
	DEBUG_LOG(ME, "%08x=sceAtracDecodeData(%i, %08x, %08x[%08x], %08x[%08x], %08x[%d])", ret, atracID, outAddr, 
			  numSamplesAddr, numSamples,
			  finishFlagAddr, finish,
			  remainAddr, remains);
	if (!ret) {
		// decode data successfully, delay thread
		return hleDelayResult(ret, "atrac decode data", atracDecodeDelay);
	}
	return ret;
}

static u32 sceAtracEndEntry() {
	ERROR_LOG_REPORT(ME, "UNIMPL sceAtracEndEntry()");
	return 0;
}

static u32 sceAtracGetBufferInfoForResetting(int atracID, int sample, u32 bufferInfoAddr) {
	auto bufferInfo = PSPPointer<AtracResetBufferInfo>::Create(bufferInfoAddr);

	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		WARN_LOG(ME, "sceAtracGetBufferInfoForResetting(%i, %i, %08x): invalid id", atracID, sample, bufferInfoAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetBufferInfoForResetting(%i, %i, %08x): no data", atracID, sample, bufferInfoAddr);
		return ATRAC_ERROR_NO_DATA;
	} else if (!bufferInfo.IsValid()) {
		ERROR_LOG_REPORT(ME, "sceAtracGetBufferInfoForResetting(%i, %i, %08x): invalid buffer, should crash", atracID, sample, bufferInfoAddr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	} else {
		u32 atracSamplesPerFrame = (atrac->codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
		if ((u32)sample + atracSamplesPerFrame > (u32)atrac->endSample) {
			WARN_LOG(ME, "sceAtracGetBufferInfoForResetting(%i, %i, %08x): invalid sample position", atracID, sample, bufferInfoAddr);
			return ATRAC_ERROR_BAD_SAMPLE;
		}

		int Sampleoffset = atrac->getDecodePosBySample(sample);
		int minWritebytes = std::max(Sampleoffset - (int)atrac->first.size, 0);
		// Reset temp buf for adding more stream data and set full filled buffer 
		atrac->first.writableBytes = std::min(atrac->first.filesize - atrac->first.size, atrac->atracBufSize);
		atrac->first.offset = 0;
		// minWritebytes should not be bigger than writeablebytes
		minWritebytes = std::min(minWritebytes, (int)atrac->first.writableBytes);

		if (atrac->first.fileoffset <= 2*atrac->atracBufSize){
			Sampleoffset = atrac->first.fileoffset;
		}

		// If we've already loaded everything, the answer is 0.
		if (atrac->first.size >= atrac->first.filesize) {
			Sampleoffset = 0;
		}

		bufferInfo->first.writePosPtr = atrac->first.addr;
		bufferInfo->first.writableBytes = atrac->first.writableBytes;
		bufferInfo->first.minWriteBytes = minWritebytes;
		bufferInfo->first.filePos = Sampleoffset;

		// TODO: It seems like this is always the same as the first buffer's pos?
		bufferInfo->second.writePosPtr = atrac->first.addr;
		bufferInfo->second.writableBytes = atrac->second.writableBytes;
		bufferInfo->second.minWriteBytes = atrac->second.neededBytes;
		bufferInfo->second.filePos = atrac->second.fileoffset;

		INFO_LOG(ME, "0=sceAtracGetBufferInfoForResetting(%i, %i, %08x)",atracID, sample, bufferInfoAddr);
		return 0;
	}
}

static u32 sceAtracGetBitrate(int atracID, u32 outBitrateAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetBitrate(%i, %08x): bad atrac ID", atracID, outBitrateAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetBitrate(%i, %08x): no data", atracID, outBitrateAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		atrac->atracBitrate = ( atrac->atracBytesPerFrame * 352800 ) / 1000;
		if (atrac->codecType == PSP_MODE_AT_3_PLUS)
			atrac->atracBitrate = ((atrac->atracBitrate >> 11) + 8) & 0xFFFFFFF0;
		else
			atrac->atracBitrate = (atrac->atracBitrate + 511) >> 10;
		if (Memory::IsValidAddress(outBitrateAddr)) {
			Memory::Write_U32(atrac->atracBitrate, outBitrateAddr);
			DEBUG_LOG(ME, "sceAtracGetBitrate(%i, %08x[%d])", atracID, outBitrateAddr, atrac->atracBitrate);
		}
		else
			DEBUG_LOG_REPORT(ME, "sceAtracGetBitrate(%i, %08x[%d]) invalid address", atracID, outBitrateAddr, atrac->atracBitrate);
	}
	return 0;
}

static u32 sceAtracGetChannel(int atracID, u32 channelAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetChannel(%i, %08x): bad atrac ID", atracID, channelAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetChannel(%i, %08x): no data", atracID, channelAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (Memory::IsValidAddress(channelAddr)){
			Memory::Write_U32(atrac->atracChannels, channelAddr);
			DEBUG_LOG(ME, "sceAtracGetChannel(%i, %08x[%d])", atracID, channelAddr, atrac->atracChannels);
		}
		else
			DEBUG_LOG_REPORT(ME, "sceAtracGetChannel(%i, %08x[%d]) invalid address", atracID, channelAddr, atrac->atracChannels);
	}
	return 0;
}

static u32 sceAtracGetLoopStatus(int atracID, u32 loopNumAddr, u32 statusAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetLoopStatus(%i, %08x, %08x): bad atrac ID", atracID, loopNumAddr, statusAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetLoopStatus(%i, %08x, %08x): no data", atracID, loopNumAddr, statusAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetLoopStatus(%i, %08x, %08x)", atracID, loopNumAddr, statusAddr);
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

static u32 sceAtracGetInternalErrorInfo(int atracID, u32 errorAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetInternalErrorInfo(%i, %08x): bad atrac ID", atracID, errorAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		WARN_LOG(ME, "sceAtracGetInternalErrorInfo(%i, %08x): no data", atracID, errorAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		ERROR_LOG(ME, "UNIMPL sceAtracGetInternalErrorInfo(%i, %08x)", atracID, errorAddr);
		if (Memory::IsValidAddress(errorAddr))
			Memory::Write_U32(0, errorAddr);
	}
	return 0;
}

static u32 sceAtracGetMaxSample(int atracID, u32 maxSamplesAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetMaxSample(%i, %08x): bad atrac ID", atracID, maxSamplesAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetMaxSample(%i, %08x): no data", atracID, maxSamplesAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetMaxSample(%i, %08x)", atracID, maxSamplesAddr);
		if (Memory::IsValidAddress(maxSamplesAddr)) {
			int atracSamplesPerFrame = (atrac->codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
			Memory::Write_U32(atracSamplesPerFrame, maxSamplesAddr);
		}
	}
	return 0;
}

static u32 sceAtracGetNextDecodePosition(int atracID, u32 outposAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetNextDecodePosition(%i, %08x): bad atrac ID", atracID, outposAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetNextDecodePosition(%i, %08x): no data", atracID, outposAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetNextDecodePosition(%i, %08x)", atracID, outposAddr);
		if (atrac->currentSample >= atrac->endSample) {
			if (Memory::IsValidAddress(outposAddr))
				Memory::Write_U32(0, outposAddr);
			return ATRAC_ERROR_ALL_DATA_DECODED;
		} else {
			if (Memory::IsValidAddress(outposAddr))
			Memory::Write_U32(atrac->currentSample, outposAddr);
		}
	}
	return 0;
}

static u32 sceAtracGetNextSample(int atracID, u32 outNAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetNextSample(%i, %08x): bad atrac ID", atracID, outNAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetNextSample(%i, %08x): no data", atracID, outNAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (atrac->currentSample >= atrac->endSample) {
			if (Memory::IsValidAddress(outNAddr))
				Memory::Write_U32(0, outNAddr);
			DEBUG_LOG(ME, "sceAtracGetNextSample(%i, %08x): 0 samples left", atracID, outNAddr);
			return 0;
		} else {
			u32 atracSamplesPerFrame = (atrac->codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
			// Some kind of header size?
			u32 firstOffsetExtra = atrac->codecType == PSP_CODEC_AT3PLUS ? 368 : 69;
			// It seems like the PSP aligns the sample position to 0x800...?
			u32 skipSamples = atrac->firstSampleoffset + firstOffsetExtra;
			u32 firstSamples = (atracSamplesPerFrame - skipSamples) % atracSamplesPerFrame;
			u32 numSamples = atrac->endSample - atrac->currentSample;
			if (atrac->currentSample == 0 && firstSamples != 0) {
				numSamples = firstSamples;
			}
			u32 unalignedSamples = (skipSamples + atrac->currentSample) % atracSamplesPerFrame;
			if (unalignedSamples != 0) {
				// We're off alignment, possibly due to a loop.  Force it back on.
				numSamples = atracSamplesPerFrame - unalignedSamples;
			}
			if (numSamples > atracSamplesPerFrame)
				numSamples = atracSamplesPerFrame;
			if (Memory::IsValidAddress(outNAddr))
				Memory::Write_U32(numSamples, outNAddr);
			DEBUG_LOG(ME, "sceAtracGetNextSample(%i, %08x): %d samples left", atracID, outNAddr, numSamples);
		}
	}
	return 0;
}

static u32 sceAtracGetRemainFrame(int atracID, u32 remainAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetRemainFrame(%i, %08x): bad atrac ID", atracID, remainAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetRemainFrame(%i, %08x): no data", atracID, remainAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (Memory::IsValidAddress(remainAddr)) {
			Memory::Write_U32(atrac->getRemainFrames(), remainAddr);
			DEBUG_LOG(ME, "sceAtracGetRemainFrame(%i, %08x[%d])", atracID, remainAddr, atrac->getRemainFrames());
		}
		else
			DEBUG_LOG_REPORT(ME, "sceAtracGetRemainFrame(%i, %08x[%d]) invalid address", atracID, remainAddr, atrac->getRemainFrames());
		// Let sceAtracGetStreamDataInfo() know to set the full filled buffer .
		atrac->resetBuffer = true;

	}
	return 0;
}

static u32 sceAtracGetSecondBufferInfo(int atracID, u32 outposAddr, u32 outBytesAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetSecondBufferInfo(%i, %08x, %08x): bad atrac ID", atracID, outposAddr, outBytesAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetSecondBufferInfo(%i, %08x, %08x): no data", atracID, outposAddr, outBytesAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		ERROR_LOG(ME, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)", atracID, outposAddr, outBytesAddr);
		if (Memory::IsValidAddress(outposAddr) && atrac)
			Memory::Write_U32(atrac->second.fileoffset, outposAddr);
		if (Memory::IsValidAddress(outBytesAddr) && atrac)
			Memory::Write_U32(atrac->second.writableBytes, outBytesAddr);
	}
	// TODO: Maybe don't write the above?
	return ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED;
}

static u32 sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetSoundSample(%i, %08x, %08x, %08x): bad atrac ID", atracID, outEndSampleAddr, outLoopStartSampleAddr, outLoopEndSampleAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetSoundSample(%i, %08x, %08x, %08x): no data", atracID, outEndSampleAddr, outLoopStartSampleAddr, outLoopEndSampleAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (Memory::IsValidAddress(outEndSampleAddr))
			Memory::Write_U32(atrac->endSample - 1, outEndSampleAddr);
		if (Memory::IsValidAddress(outLoopStartSampleAddr))
			Memory::Write_U32(atrac->loopStartSample, outLoopStartSampleAddr);
		if (Memory::IsValidAddress(outLoopEndSampleAddr))
			Memory::Write_U32(atrac->loopEndSample, outLoopEndSampleAddr);
		if (Memory::IsValidAddress(outEndSampleAddr) && (Memory::IsValidAddress(outLoopStartSampleAddr)) && (Memory::IsValidAddress(outLoopEndSampleAddr)))
			DEBUG_LOG(ME, "sceAtracGetSoundSample(%i, %08x[%08x], %08x[%d], %08x[%d])", atracID, outEndSampleAddr, atrac->endSample - 1, outLoopStartSampleAddr, atrac->loopStartSample, outLoopEndSampleAddr, atrac->loopEndSample);
		else
			DEBUG_LOG_REPORT(ME, "sceAtracGetSoundSample(%i, %08x[%08x], %08x[%d], %08x[%d]) invalid address", atracID, outEndSampleAddr, atrac->endSample - 1, outLoopStartSampleAddr, atrac->loopStartSample, outLoopEndSampleAddr, atrac->loopEndSample);

	}
	return 0;
}

// Games call this function to get some info for add more stream data,
// such as where the data read from, where the data add to,
// and how many bytes are allowed to add.
static u32 sceAtracGetStreamDataInfo(int atracID, u32 writeAddr, u32 writableBytesAddr, u32 readOffsetAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x): bad atrac ID", atracID, writeAddr, writableBytesAddr, readOffsetAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x): no data", atracID, writeAddr, writableBytesAddr, readOffsetAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		// TODO: Is this check even needed?  More testing is needed on writableBytes.
		if (atrac->resetBuffer) {
			// Reset temp buf for adding more stream data and set full filled buffer.
			atrac->first.writableBytes = std::min(atrac->first.filesize - atrac->first.size, atrac->atracBufSize);
		} else {
			atrac->first.writableBytes = std::min(atrac->first.filesize - atrac->first.size, atrac->first.writableBytes);
		}

		atrac->first.offset = 0;
		if (Memory::IsValidAddress(writeAddr))
			Memory::Write_U32(atrac->first.addr, writeAddr);
		if (Memory::IsValidAddress(writableBytesAddr))
			Memory::Write_U32(atrac->first.writableBytes, writableBytesAddr);
		if (Memory::IsValidAddress(readOffsetAddr))
			Memory::Write_U32(atrac->first.fileoffset, readOffsetAddr);
		if ((Memory::IsValidAddress(writeAddr)) && (Memory::IsValidAddress(writableBytesAddr)) && (Memory::IsValidAddress(readOffsetAddr)))
			DEBUG_LOG(ME, "sceAtracGetStreamDataInfo(%i, %08x[%08x], %08x[%08x], %08x[%08x])", atracID, 
				  writeAddr, atrac->first.addr,
				  writableBytesAddr, atrac->first.writableBytes,
				  readOffsetAddr, atrac->first.fileoffset);
		else
			//TODO:Use JPCSPtrace to correct
			DEBUG_LOG_REPORT(ME, "sceAtracGetStreamDataInfo(%i, %08x[%08x], %08x[%08x], %08x[%08x]) invalid address", atracID,
			writeAddr, atrac->first.addr,
			writableBytesAddr, atrac->first.writableBytes,
			readOffsetAddr, atrac->first.fileoffset);
	}
	return 0;
}

static u32 sceAtracReleaseAtracID(int atracID) {
	INFO_LOG(ME, "sceAtracReleaseAtracID(%i)", atracID);
	return deleteAtrac(atracID);
}

static u32 sceAtracResetPlayPosition(int atracID, int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracResetPlayPosition(%i, %i, %i, %i): bad atrac ID", atracID, sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracResetPlayPosition(%i, %i, %i, %i): no data", atracID, sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
		return ATRAC_ERROR_NO_DATA;
	} else {
		INFO_LOG(ME, "sceAtracResetPlayPosition(%i, %i, %i, %i)", atracID, sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
		if (bytesWrittenFirstBuf > 0)
			sceAtracAddStreamData(atracID, bytesWrittenFirstBuf);
#ifdef USE_FFMPEG
		if ((atrac->codecType == PSP_MODE_AT_3 || atrac->codecType == PSP_MODE_AT_3_PLUS) && atrac->pCodecCtx) {
			atrac->SeekToSample(sample);
		} else
#endif // USE_FFMPEG
		{
			atrac->currentSample = sample;
			atrac->decodePos = atrac->getDecodePosBySample(sample);
		}
	}
	return 0;
}

#ifdef USE_FFMPEG
static int _AtracReadbuffer(void *opaque, uint8_t *buf, int buf_size) {
	Atrac *atrac = (Atrac *)opaque;
	if (atrac->bufferPos > atrac->first.filesize)
		return -1;
	int size = std::min((int)atrac->atracBufSize, buf_size);
	size = std::max(std::min(((int)atrac->first.size - (int)atrac->bufferPos), size), 0);
	if (size > 0)
		memcpy(buf, atrac->data_buf + atrac->bufferPos, size);
	atrac->bufferPos += size;
	return size;
}

static int64_t _AtracSeekbuffer(void *opaque, int64_t offset, int whence) {
	Atrac *atrac = (Atrac*)opaque;
	if (offset > atrac->first.filesize)
		return -1;

	switch (whence) {
	case SEEK_SET:
		atrac->bufferPos = (u32)offset;
		break;
	case SEEK_CUR:
		atrac->bufferPos += (u32)offset;
		break;
	case SEEK_END:
		atrac->bufferPos = atrac->first.filesize - (u32)offset;
		break;
#ifdef USE_FFMPEG
	case AVSEEK_SIZE:
		return atrac->first.filesize;
#endif
	}
	return atrac->bufferPos;
}

#endif // USE_FFMPEG

#ifdef USE_FFMPEG
static int __AtracUpdateOutputMode(Atrac *atrac, int wanted_channels) {
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
		ERROR_LOG(ME, "swr_alloc_set_opts: Could not allocate resampler context");
		return -1;
	}
	if (swr_init(atrac->pSwrCtx) < 0) {
		ERROR_LOG(ME, "swr_init: Failed to initialize the resampling context");
		return -1;
	}
	return 0;
}
#endif // USE_FFMPEG

int __AtracSetContext(Atrac *atrac) {
#ifdef USE_FFMPEG
	InitFFmpeg();

	u8* tempbuf = (u8*)av_malloc(atrac->atracBufSize);

	atrac->pFormatCtx = avformat_alloc_context();
	atrac->pAVIOCtx = avio_alloc_context(tempbuf, atrac->atracBufSize, 0, (void*)atrac, _AtracReadbuffer, NULL, _AtracSeekbuffer);
	atrac->pFormatCtx->pb = atrac->pAVIOCtx;

	int ret;
	// Load audio buffer
	if((ret = avformat_open_input((AVFormatContext**)&atrac->pFormatCtx, NULL, NULL, NULL)) != 0) {
		ERROR_LOG(ME, "avformat_open_input: Cannot open input %d", ret);
		// TODO: This is not exactly correct, but if the header is right and there's not enough data
		// (which is likely the case here), this is the correct error.
		return ATRAC_ERROR_ALL_DATA_DECODED;
	}

	if((ret = avformat_find_stream_info(atrac->pFormatCtx, NULL)) < 0) {
		ERROR_LOG(ME, "avformat_find_stream_info: Cannot find stream information %d", ret);
		return -1;
	}

	AVCodec *pCodec;
	// select the audio stream
	ret = av_find_best_stream(atrac->pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &pCodec, 0);
	if (ret < 0) {
		if (ret == AVERROR_DECODER_NOT_FOUND) {
			ERROR_LOG(HLE, "av_find_best_stream: No appropriate decoder found");
		} else {
			ERROR_LOG(HLE, "av_find_best_stream: Cannot find an audio stream in the input file %d", ret);
		}
		return -1;
	}
	atrac->audio_stream_index = ret;
	atrac->pCodecCtx = atrac->pFormatCtx->streams[atrac->audio_stream_index]->codec;

	// Appears we need to force mono in some cases. (See CPkmn's comments in issue #4248)
	if (atrac->atracChannels == 1)
		atrac->pCodecCtx->channel_layout = AV_CH_LAYOUT_MONO;

	// Explicitly set the block_align value (needed by newer FFmpeg versions, see #5772.)
	if (atrac->pCodecCtx->block_align == 0) {
		atrac->pCodecCtx->block_align = atrac->atracBytesPerFrame;
	}

	atrac->pCodecCtx->request_sample_fmt = AV_SAMPLE_FMT_S16;
	if ((ret = avcodec_open2(atrac->pCodecCtx, pCodec, NULL)) < 0) {
		ERROR_LOG(ME, "avcodec_open2: Cannot open audio decoder %d", ret);
		return -1;
	}

	if ((ret = __AtracUpdateOutputMode(atrac, atrac->atracOutputChannels)) < 0)
		return ret;

	// alloc audio frame
	atrac->pFrame = av_frame_alloc();
	atrac->packet = new AVPacket;
	av_init_packet(atrac->packet);
	atrac->packet->data = nullptr;
	atrac->packet->size = 0;
	// reinit decodePos, because ffmpeg had changed it.
	atrac->decodePos = 0;
#endif

	return 0;
}

static int _AtracSetData(Atrac *atrac, u32 buffer, u32 bufferSize) {
	if (atrac->first.size > atrac->first.filesize)
		atrac->first.size = atrac->first.filesize;
	atrac->first.fileoffset = atrac->first.size;

	// got the size of temp buf, and calculate writableBytes and offset
	atrac->atracBufSize = bufferSize;
	atrac->first.writableBytes = (u32)std::max((int)bufferSize - (int)atrac->first.size, 0);
	atrac->first.offset = atrac->first.size;

	// some games may reuse an atracID for playing sound
	atrac->CleanStuff();


	if (atrac->codecType == PSP_MODE_AT_3) {
		if (atrac->atracChannels == 1) {
			WARN_LOG(ME, "This is an atrac3 mono audio");
		} else {
			WARN_LOG(ME, "This is an atrac3 stereo audio");
		}

#ifdef USE_FFMPEG
		atrac->data_buf = new u8[atrac->first.filesize];
		u32 copybytes = std::min(bufferSize, atrac->first.filesize);
		Memory::Memcpy(atrac->data_buf, buffer, copybytes);
		return __AtracSetContext(atrac);
#endif // USE_FFMPEG

	} else if (atrac->codecType == PSP_MODE_AT_3_PLUS) {
		if (atrac->atracChannels == 1) {
			WARN_LOG(ME, "This is an atrac3+ mono audio");
		} else {
			WARN_LOG(ME, "This is an atrac3+ stereo audio");
		}
		atrac->data_buf = new u8[atrac->first.filesize];
		u32 copybytes = std::min(bufferSize, atrac->first.filesize);
		Memory::Memcpy(atrac->data_buf, buffer, copybytes);
		return __AtracSetContext(atrac);
	}


	return 0;
}

static int _AtracSetData(int atracID, u32 buffer, u32 bufferSize, bool needReturnAtracID = false) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac)
		return -1;
	int ret = _AtracSetData(atrac, buffer, bufferSize);
	if (needReturnAtracID && ret >= 0)
		ret = atracID;
	// not sure the real delay time
	return hleDelayResult(ret, "atrac set data", 100);
}

static u32 sceAtracSetHalfwayBuffer(int atracID, u32 halfBuffer, u32 readSize, u32 halfBufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracSetHalfwayBuffer(%i, %08x, %8x, %8x): bad atrac ID", atracID, halfBuffer, readSize, halfBufferSize);
		return ATRAC_ERROR_BAD_ATRACID;
	}

	INFO_LOG(ME, "sceAtracSetHalfwayBuffer(%i, %08x, %8x, %8x)", atracID, halfBuffer, readSize, halfBufferSize);
	if (readSize > halfBufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;

	int ret = 0;
	if (atrac != NULL) {
		atrac->first.addr = halfBuffer;
		atrac->first.size = readSize;
		ret = atrac->Analyze();
		if (ret < 0) {
			ERROR_LOG_REPORT(ME, "sceAtracSetHalfwayBuffer(%i, %08x, %8x, %8x): bad data", atracID, halfBuffer, readSize, halfBufferSize);
			return ret;
		}
		atrac->atracOutputChannels = 2;
		ret = _AtracSetData(atracID, halfBuffer, halfBufferSize);
	}
	return ret;
}

static u32 sceAtracSetSecondBuffer(int atracID, u32 secondBuffer, u32 secondBufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracSetSecondBuffer(%i, %08x, %8x): bad atrac ID", atracID, secondBuffer, secondBufferSize);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracSetSecondBuffer(%i, %08x, %8x): no data", atracID, secondBuffer, secondBufferSize);
		return ATRAC_ERROR_NO_DATA;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceAtracSetSecondBuffer(%i, %08x, %8x)", atracID, secondBuffer, secondBufferSize);
	return 0;
}

static u32 sceAtracSetData(int atracID, u32 buffer, u32 bufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (atrac != NULL) {
		INFO_LOG(ME, "sceAtracSetData(%i, %08x, %08x)", atracID, buffer, bufferSize);
		atrac->first.addr = buffer;
		atrac->first.size = bufferSize;
		int ret = atrac->Analyze();
		if (ret < 0) {
			ERROR_LOG_REPORT(ME, "sceAtracSetData(%i, %08x, %08x): bad data", atracID, buffer, bufferSize);
		} else if (atrac->codecType != atracIDTypes[atracID]) {
			ERROR_LOG_REPORT(ME, "sceAtracSetData(%i, %08x, %08x): atracID uses different codec type than data", atracID, buffer, bufferSize);
			ret = ATRAC_ERROR_WRONG_CODECTYPE;
		} else {
			atrac->atracOutputChannels = 2;
			ret = _AtracSetData(atracID, buffer, bufferSize);
		}
		return ret;
	} else {
		ERROR_LOG(ME, "sceAtracSetData(%i, %08x, %08x): bad atrac ID", atracID, buffer, bufferSize);
		return ATRAC_ERROR_BAD_ATRACID;
	}
}

static int sceAtracSetDataAndGetID(u32 buffer, int bufferSize) {
	// A large value happens in Tales of VS, and isn't handled somewhere properly as a u32.
	// It's impossible for it to be that big anyway, so cap it.
	if (bufferSize < 0) {
		WARN_LOG(ME, "sceAtracSetDataAndGetID(%08x, %08x): negative bufferSize", buffer, bufferSize);
		bufferSize = 0x10000000;
	}
	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	int ret = atrac->Analyze();
	if (ret < 0) {
		ERROR_LOG_REPORT(ME, "sceAtracSetDataAndGetID(%08x, %08x): bad data", buffer, bufferSize);
		delete atrac;
		return ret;
	}
	atrac->atracOutputChannels = 2;
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		ERROR_LOG(ME, "sceAtracSetDataAndGetID(%08x, %08x): no free ID", buffer, bufferSize);
		delete atrac;
		return atracID;
	}
	INFO_LOG(ME, "%d=sceAtracSetDataAndGetID(%08x, %08x)", atracID, buffer, bufferSize);
	return _AtracSetData(atracID, buffer, bufferSize, true);
}

static int sceAtracSetHalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize) {
	if (readSize > halfBufferSize) {
		ERROR_LOG(ME, "sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x): incorrect read size", halfBuffer, readSize, halfBufferSize);
		return ATRAC_ERROR_INCORRECT_READ_SIZE;
	}
	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = readSize;
	int ret = atrac->Analyze();
	if (ret < 0) {
		ERROR_LOG_REPORT(ME, "sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x): bad data", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return ret;
	}
	atrac->atracOutputChannels = 2;
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		ERROR_LOG(ME, "sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x): no free ID", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return atracID;
	}
	INFO_LOG(ME, "%d=sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	return _AtracSetData(atracID, halfBuffer, halfBufferSize, true);
}

static u32 sceAtracStartEntry() {
	ERROR_LOG_REPORT(ME, "UNIMPL sceAtracStartEntry()");
	return 0;
}

static u32 sceAtracSetLoopNum(int atracID, int loopNum) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracSetLoopNum(%i, %i): bad atrac ID", atracID, loopNum);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracSetLoopNum(%i, %i):no data", atracID, loopNum);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (atrac->loopinfoNum == 0) {
			ERROR_LOG(ME, "sceAtracSetLoopNum(%i, %i):no loop information", atracID, loopNum);
			return ATRAC_ERROR_NO_LOOP_INFORMATION;
		}
		// Spammed in MHU
		DEBUG_LOG(ME, "sceAtracSetLoopNum(%i, %i)", atracID, loopNum);
		atrac->loopNum = loopNum;
		if (loopNum != 0 && atrac->loopinfoNum == 0) {
			// Just loop the whole audio
			atrac->loopStartSample = 0;
			atrac->loopEndSample = atrac->endSample;
		}
	}
	return 0;
}

static int sceAtracReinit(int at3Count, int at3plusCount) {
	for (int i = 0; i < PSP_NUM_ATRAC_IDS; ++i) {
		if (atracIDs[i] != NULL) {
			ERROR_LOG_REPORT(ME, "sceAtracReinit(%d, %d): cannot reinit while IDs in use", at3Count, at3plusCount);
			return SCE_KERNEL_ERROR_BUSY;
		}
	}

	memset(atracIDTypes, 0, sizeof(atracIDTypes));
	int next = 0;
	int space = PSP_NUM_ATRAC_IDS;

	// This seems to deinit things.  Mostly, it cause a reschedule on next deinit (but -1, -1 does not.)
	if (at3Count == 0 && at3plusCount == 0) {
		INFO_LOG(ME, "sceAtracReinit(%d, %d): deinit", at3Count, at3plusCount);
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
	int result = space >= 0 ? 0 : (int)SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	if (atracInited || next == 0) {
		INFO_LOG(ME, "sceAtracReinit(%d, %d)", at3Count, at3plusCount);
		atracInited = true;
		return result;
	} else {
		INFO_LOG(ME, "sceAtracReinit(%d, %d): init", at3Count, at3plusCount);
		atracInited = true;
		return hleDelayResult(result, "atrac reinit", 400);
	}
}

static int sceAtracGetOutputChannel(int atracID, u32 outputChanPtr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetOutputChannel(%i, %08x): bad atrac ID", atracID, outputChanPtr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracGetOutputChannel(%i, %08x): no data", atracID, outputChanPtr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetOutputChannel(%i, %08x)", atracID, outputChanPtr);
		if (Memory::IsValidAddress(outputChanPtr))
			Memory::Write_U32(atrac->atracOutputChannels, outputChanPtr);
	}
	return 0;
}

static int sceAtracIsSecondBufferNeeded(int atracID) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracIsSecondBufferNeeded(%i): bad atrac ID", atracID);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->data_buf) {
		ERROR_LOG(ME, "sceAtracIsSecondBufferNeeded(%i): no data", atracID);
		return ATRAC_ERROR_NO_DATA;
	} 
	WARN_LOG(ME, "UNIMPL sceAtracIsSecondBufferNeeded(%i)", atracID);
	return 0;
}

static int sceAtracSetMOutHalfwayBuffer(int atracID, u32 buffer, u32 readSize, u32 bufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracSetMOutHalfwayBuffer(%i, %08x, %08x, %08x): bad atrac ID", atracID, buffer, readSize, bufferSize);
		return ATRAC_ERROR_BAD_ATRACID;
	}

	INFO_LOG(ME, "sceAtracSetMOutHalfwayBuffer(%i, %08x, %08x, %08x)", atracID, buffer, readSize, bufferSize);
	if (readSize > bufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;

	int ret = 0;
	if (atrac != NULL) {
		atrac->first.addr = buffer;
		atrac->first.size = readSize;
		ret = atrac->Analyze();
		if (ret < 0) {
			ERROR_LOG_REPORT(ME, "sceAtracSetMOutHalfwayBuffer(%i, %08x, %08x, %08x): bad data", atracID, buffer, readSize, bufferSize);
			return ret;
		}
		if (atrac->atracChannels != 1) {
			ERROR_LOG_REPORT(ME, "sceAtracSetMOutHalfwayBuffer(%i, %08x, %08x, %08x): not mono data", atracID, buffer, readSize, bufferSize);
			ret = ATRAC_ERROR_NOT_MONO;
			// It seems it still sets the data.
			atrac->atracOutputChannels = 2;
			_AtracSetData(atrac, buffer, bufferSize);
			return ret;
		} else {
			atrac->atracOutputChannels = 1;
			ret = _AtracSetData(atracID, buffer, bufferSize);
		}
	}
	return ret;
}

static u32 sceAtracSetMOutData(int atracID, u32 buffer, u32 bufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracSetMOutData(%i, %08x, %08x): bad atrac ID", atracID, buffer, bufferSize);
		return ATRAC_ERROR_BAD_ATRACID;
	}

	// This doesn't seem to be part of any available libatrac3plus library.
	WARN_LOG_REPORT(ME, "sceAtracSetMOutData(%i, %08x, %08x)", atracID, buffer, bufferSize);

	// TODO: What is the proper error code here?
	int ret = 0;
	if (atrac != NULL) {
		atrac->first.addr = buffer;
		atrac->first.size = bufferSize;
		ret = atrac->Analyze();
		if (ret < 0) {
			ERROR_LOG_REPORT(ME, "sceAtracSetMOutData(%i, %08x, %08x): bad data", atracID, buffer, bufferSize);
			return ret;
		}
		if (atrac->atracChannels != 1) {
			ERROR_LOG_REPORT(ME, "sceAtracSetMOutData(%i, %08x, %08x): not mono data", atracID, buffer, bufferSize);
			ret = ATRAC_ERROR_NOT_MONO;
			// It seems it still sets the data.
			atrac->atracOutputChannels = 2;
			_AtracSetData(atrac, buffer, bufferSize);
			// Not sure of the real delay time.
			return ret;
		} else {
			atrac->atracOutputChannels = 1;
			ret = _AtracSetData(atracID, buffer, bufferSize);
		}
	}
	return ret;
}

static int sceAtracSetMOutDataAndGetID(u32 buffer, u32 bufferSize) {
	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	int ret = atrac->Analyze();
	if (ret < 0) {
		ERROR_LOG_REPORT(ME, "sceAtracSetMOutDataAndGetID(%08x, %08x): bad data", buffer, bufferSize);
		delete atrac;
		return ret;
	}
	if (atrac->atracChannels != 1) {
		ERROR_LOG_REPORT(ME, "sceAtracSetMOutDataAndGetID(%08x, %08x): not mono data", buffer, bufferSize);
		delete atrac;
		return ATRAC_ERROR_NOT_MONO;
	}
	atrac->atracOutputChannels = 1;
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		ERROR_LOG(ME, "sceAtracSetMOutDataAndGetID(%08x, %08x): no free ID", buffer, bufferSize);
		delete atrac;
		return atracID;
	}
	// This doesn't seem to be part of any available libatrac3plus library.
	WARN_LOG_REPORT(ME, "%d=sceAtracSetMOutDataAndGetID(%08x, %08x)", atracID, buffer, bufferSize);
	return _AtracSetData(atracID, buffer, bufferSize, true);
}

static int sceAtracSetMOutHalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize) {
	if (readSize > halfBufferSize) {
		ERROR_LOG(ME, "sceAtracSetMOutHalfwayBufferAndGetID(%08x, %08x, %08x): incorrect read size", halfBuffer, readSize, halfBufferSize);
		return ATRAC_ERROR_INCORRECT_READ_SIZE;
	}
	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = readSize;
	int ret = atrac->Analyze();
	if (ret < 0) {
		ERROR_LOG_REPORT(ME, "sceAtracSetMOutHalfwayBufferAndGetID(%08x, %08x, %08x): bad data", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return ret;
	}
	if (atrac->atracChannels != 1) {
		ERROR_LOG_REPORT(ME, "sceAtracSetMOutHalfwayBufferAndGetID(%08x, %08x, %08x): not mono data", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return ATRAC_ERROR_NOT_MONO;
	}
	atrac->atracOutputChannels = 1;
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		ERROR_LOG(ME, "sceAtracSetMOutHalfwayBufferAndGetID(%08x, %08x, %08x): no free ID", halfBuffer, readSize, halfBufferSize);
		delete atrac;
		return atracID;
	}
	INFO_LOG(ME, "%d=sceAtracSetMOutHalfwayBufferAndGetID(%08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	return _AtracSetData(atracID, halfBuffer, halfBufferSize, true);
}

static int sceAtracSetAA3DataAndGetID(u32 buffer, u32 bufferSize, u32 fileSize, u32 metadataSizeAddr) {
	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	atrac->first.filesize = fileSize;
	int ret = atrac->AnalyzeAA3();
	if (ret < 0) {
		ERROR_LOG(ME, "sceAtracSetAA3DataAndGetID(%08x, %i, %i, %08x): bad data", buffer, bufferSize, fileSize, metadataSizeAddr);
		delete atrac;
		return ret;
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		ERROR_LOG(ME, "sceAtracSetAA3DataAndGetID(%08x, %i, %i, %08x): no free ID", buffer, bufferSize, fileSize, metadataSizeAddr);
		delete atrac;
		return atracID;
	}
	WARN_LOG(ME, "%d=sceAtracSetAA3DataAndGetID(%08x, %i, %i, %08x)", atracID, buffer, bufferSize, fileSize, metadataSizeAddr);
	return _AtracSetData(atracID, buffer, bufferSize, true);
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
	context->info.codec = atrac->codecType;
	context->info.loopNum = atrac->loopNum;
	context->info.loopStart = atrac->loopStartSample > 0 ? atrac->loopStartSample : 0;
	context->info.loopEnd = atrac->loopEndSample > 0 ? atrac->loopEndSample : 0;
	if (context->info.endSample > 0) {
		// do not change info.state if this was not called at first time
		// In Sol Trigger, it would set info.state = 0x10 outside
		// TODO: Should we just keep this in PSP ram then, or something?
	} else if (!atrac->data_buf) {
		// State 1, no buffer yet.
		context->info.state = 1;
	} else if (atrac->first.size >= atrac->first.filesize) {
		// state 2, all data loaded
		context->info.state = 2;
	} else if (atrac->loopinfoNum == 0) {
		// state 3, lack some data, no loop info
		context->info.state = 3;
	} else {
		// state 6, lack some data, has loop info
		context->info.state = 6;
	}
	context->info.samplesPerChan = (atrac->codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
	context->info.sampleSize = atrac->atracBytesPerFrame;
	context->info.numChan = atrac->atracChannels;
	context->info.dataOff = atrac->dataOff;
	context->info.endSample = atrac->endSample;
	context->info.dataEnd = atrac->first.filesize;
	context->info.curOff = atrac->first.size;
	context->info.decodePos = atrac->getDecodePosBySample(atrac->currentSample);
	context->info.streamDataByte = atrac->first.size - atrac->firstSampleoffset;

	u8* buf = (u8*)context;
	*(u32*)(buf + 0xfc) = atrac->atracID;
}

static int _sceAtracGetContextAddress(int atracID) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "_sceAtracGetContextAddress(%i): bad atrac id", atracID);
		return 0;
	}
	if (!atrac->atracContext.IsValid()) {
		// allocate a new atracContext
		u32 contextsize = 256;
		atrac->atracContext = kernelMemory.Alloc(contextsize, false, "Atrac Context");
		if (atrac->atracContext.IsValid())
			Memory::Memset(atrac->atracContext.ptr, 0, 256);

		WARN_LOG(ME, "%08x=_sceAtracGetContextAddress(%i): allocated new context", atrac->atracContext.ptr, atracID);
	}
	else
		WARN_LOG(ME, "%08x=_sceAtracGetContextAddress(%i)", atrac->atracContext.ptr, atracID);
	if (atrac->atracContext.IsValid())
		_AtracGenarateContext(atrac, atrac->atracContext);
	return atrac->atracContext.ptr;
}

struct At3HeaderMap {
	u16 bytes;
	u16 channels;
	u8 headerVal1;
	u8 headerVal2;

	bool Matches(const Atrac *at) const {
		return bytes == at->atracBytesPerFrame && channels == at->atracChannels;
	}
};

static const u8 at3HeaderTemplate[] ={0x52,0x49,0x46,0x46,0x3b,0xbe,0x00,0x00,0x57,0x41,0x56,0x45,0x66,0x6d,0x74,0x20,0x20,0x00,0x00,0x00,0x70,0x02,0x02,0x00,0x44,0xac,0x00,0x00,0x4d,0x20,0x00,0x00,0xc0,0x00,0x00,0x00,0x0e,0x00,0x01,0x00,0x00,0x10,0x00,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x64,0x61,0x74,0x61,0xc0,0xbd,0x00,0x00};
static const At3HeaderMap at3HeaderMap[] = {
    { 0x00C0, 0x1, 0x8,  0x00 },
    { 0x0098, 0x1, 0x8,  0x00 },
    { 0x0180, 0x2, 0x10, 0x00 },
    { 0x0130, 0x2, 0x10, 0x00 },
    { 0x00C0, 0x2, 0x10, 0x01 }
};

struct At3plusHeaderMap {
	u16 bytes;
	u16 channels;
	u16 headerVal;

	bool Matches(const Atrac *at) const {
		return bytes == at->atracBytesPerFrame && channels == at->atracChannels;
	}
};

static const u8 at3plusHeaderTemplate[] = { 0x52, 0x49, 0x46, 0x46, 0x00, 0xb5, 0xff, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20, 0x34, 0x00, 0x00, 0x00, 0xfe, 0xff, 0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0xa0, 0x1f, 0x00, 0x00, 0xe8, 0x02, 0x00, 0x00, 0x22, 0x00, 0x00, 0x08, 0x03, 0x00, 0x00, 0x00, 0xbf, 0xaa, 0x23, 0xe9, 0x58, 0xcb, 0x71, 0x44, 0xa1, 0x19, 0xff, 0xfa, 0x01, 0xe4, 0xce, 0x62, 0x01, 0x00, 0x28, 0x5c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x61, 0x63, 0x74, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x08, 0x00, 0x00, 0x64, 0x61, 0x74, 0x61, 0xa8, 0xb4, 0xff, 0x00 };
static const At3plusHeaderMap at3plusHeaderMap[] = {
	{ 0x00C0, 0x1, 0x1724 },
	{ 0x0180, 0x1, 0x2224 },
	{ 0x0178, 0x1, 0x2E24 },

	{ 0x0230, 0x1, 0x4524 },
	{ 0x02E8, 0x1, 0x5C24 },
	{ 0x0118, 0x1, 0x2224 },

	{ 0x0118, 0x2, 0x2228 },
	{ 0x0178, 0x2, 0x2E28 },

	{ 0x0230, 0x2, 0x4528 },
	{ 0x02E8, 0x2, 0x5C28 },

	{ 0x03A8, 0x2, 0x7428 },
	{ 0x0460, 0x2, 0x8B28 },

	{ 0x05D0, 0x2, 0xB928 },
	{ 0x0748, 0x2, 0xE828 },

	{ 0x0800, 0x2, 0xFF28 },
};

static bool initAT3Decoder(Atrac *atrac, u8 *at3Header, u32 dataSize = 0xffb4a8) {
	for (size_t i = 0; i < ARRAY_SIZE(at3HeaderMap); ++i) {
		if (at3HeaderMap[i].Matches(atrac)) {
			*(u32 *)(at3Header + 0x04) = dataSize + sizeof(at3HeaderTemplate) - 8;
			*(u16 *)(at3Header + 0x16) = atrac->atracChannels;
			*(u16 *)(at3Header + 0x20) = atrac->atracBytesPerFrame;
			atrac->atracBitrate = ( atrac->atracBytesPerFrame * 352800 ) / 1000;
			atrac->atracBitrate = (atrac->atracBitrate + 511) >> 10;
			*(u32 *)(at3Header + 0x1c) = atrac->atracBitrate * 1000 / 8;
			at3Header[0x29] = at3HeaderMap[i].headerVal1;
			at3Header[0x2c] = at3HeaderMap[i].headerVal2;
			at3Header[0x2e] = at3HeaderMap[i].headerVal2;
			*(u32 *)(at3Header + sizeof(at3HeaderTemplate) - 4) = dataSize;
			return true;
		}
	}
	return false;
}

static bool initAT3plusDecoder(Atrac *atrac, u8 *at3plusHeader, u32 dataSize = 0xffb4a8) {
	for (size_t i = 0; i < ARRAY_SIZE(at3plusHeaderMap); ++i) {
		if (at3plusHeaderMap[i].Matches(atrac)) {
			*(u32 *)(at3plusHeader + 0x04) = dataSize + sizeof(at3plusHeaderTemplate) - 8;
			*(u16 *)(at3plusHeader + 0x16) = atrac->atracChannels;
			*(u16 *)(at3plusHeader + 0x20) = atrac->atracBytesPerFrame;
			atrac->atracBitrate = ( atrac->atracBytesPerFrame * 352800 ) / 1000;
			atrac->atracBitrate = ((atrac->atracBitrate >> 11) + 8) & 0xFFFFFFF0;
			*(u32 *)(at3plusHeader + 0x1c) = atrac->atracBitrate * 1000 / 8;
			*(u16 *)(at3plusHeader + 0x3e) = at3plusHeaderMap[i].headerVal;
			*(u32 *)(at3plusHeader + sizeof(at3plusHeaderTemplate) - 4) = dataSize;
			return true;
		}
	}
	return false;
}

static int sceAtracLowLevelInitDecoder(int atracID, u32 paramsAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracLowLevelInitDecoder(%i, %08x): bad atrac ID", atracID, paramsAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	}

	INFO_LOG(ME, "sceAtracLowLevelInitDecoder(%i, %08x)", atracID, paramsAddr);
	if (Memory::IsValidAddress(paramsAddr)) {
		atrac->atracChannels = Memory::Read_U32(paramsAddr);
		atrac->atracOutputChannels = Memory::Read_U32(paramsAddr + 4);
		atrac->atracBufSize = Memory::Read_U32(paramsAddr + 8);
		atrac->atracBytesPerFrame = atrac->atracBufSize;
		atrac->first.writableBytes = atrac->atracBytesPerFrame;
		atrac->CleanStuff();
		INFO_LOG(ME, "Channels: %i outputChannels: %i bytesperFrame: %x", 
			atrac->atracChannels, atrac->atracOutputChannels, atrac->atracBytesPerFrame);
#ifdef USE_FFMPEG
		if (atrac->codecType == PSP_MODE_AT_3) {
			if (atrac->atracChannels == 1) {
				WARN_LOG(ME, "This is an atrac3 mono audio (low level)");
			} else {
				WARN_LOG(ME, "This is an atrac3 stereo audio (low level)");
			}
			const int headersize = sizeof(at3HeaderTemplate);
			u8 at3Header[headersize];
			memcpy(at3Header, at3HeaderTemplate, headersize);
			if (!initAT3Decoder(atrac, at3Header)) {
				ERROR_LOG_REPORT(ME, "AT3 header map lacks entry for bpf: %i  channels: %i", atrac->atracBytesPerFrame, atrac->atracChannels);
				// TODO: What to do, if anything?
			}

			atrac->firstSampleoffset = headersize;
			atrac->dataOff = headersize;
			atrac->first.size = headersize;
			atrac->first.filesize = headersize + atrac->atracBytesPerFrame;
			atrac->data_buf = new u8[atrac->first.filesize];
			memcpy(atrac->data_buf, at3Header, headersize);
			atrac->currentSample = 0;
			__AtracSetContext(atrac);
			return 0;
		}

		if (atrac->codecType == PSP_MODE_AT_3_PLUS){
			if (atrac->atracChannels == 1) {
				WARN_LOG(ME, "This is an atrac3+ mono audio (low level)");
			} else {
				WARN_LOG(ME, "This is an atrac3+ stereo audio (low level)");
			}
			const int headersize = sizeof(at3plusHeaderTemplate);
			u8 at3plusHeader[headersize];
			memcpy(at3plusHeader, at3plusHeaderTemplate, headersize);
			if (!initAT3plusDecoder(atrac, at3plusHeader)) {
				ERROR_LOG_REPORT(ME, "AT3plus header map lacks entry for bpf: %i  channels: %i", atrac->atracBytesPerFrame, atrac->atracChannels);
				// TODO: What to do, if anything?
			}

			atrac->firstSampleoffset = headersize;
			atrac->dataOff = headersize;
			atrac->first.size = headersize;
			atrac->first.filesize = headersize + atrac->atracBytesPerFrame;
			atrac->data_buf = new u8[atrac->first.filesize];
			memcpy(atrac->data_buf, at3plusHeader, headersize);
			atrac->currentSample = 0;
			__AtracSetContext(atrac);
			return 0;
		}
#endif // USE_FFMPEG
	}
	return 0;
}

static int sceAtracLowLevelDecode(int atracID, u32 sourceAddr, u32 sourceBytesConsumedAddr, u32 samplesAddr, u32 sampleBytesAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracLowLevelDecode(%i, %08x, %08x, %08x, %08x): bad atrac ID", atracID, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	}

	DEBUG_LOG(ME, "UNIMPL sceAtracLowLevelDecode(%i, %08x, %08x, %08x, %08x)", atracID, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);
#ifdef USE_FFMPEG
	if (atrac && atrac->pCodecCtx && Memory::IsValidAddress(sourceAddr) && Memory::IsValidAddress(sourceBytesConsumedAddr) &&
  		Memory::IsValidAddress(samplesAddr) && Memory::IsValidAddress(sampleBytesAddr)) {
		u32 sourcebytes = atrac->first.writableBytes;
		if (sourcebytes > 0) {
			Memory::Memcpy(atrac->data_buf + atrac->first.size, sourceAddr, sourcebytes);
			if (atrac->bufferPos >= atrac->first.size) {
				atrac->bufferPos = atrac->first.size;
			}
			atrac->first.size += sourcebytes;
		}

		int numSamples = 0;
		atrac->ForceSeekToSample(atrac->currentSample);

		if (!atrac->failedDecode) {
			AtracDecodeResult res;
			while (atrac->FillPacket()) {
				res = atrac->DecodePacket();
				if (res == ATDECODE_FAILED) {
					break;
				}

				if (res == ATDECODE_GOTFRAME) {
					// got a frame
					u8 *out = Memory::GetPointer(samplesAddr);
					numSamples = atrac->pFrame->nb_samples;
					int avret = swr_convert(atrac->pSwrCtx, &out, numSamples,
						(const u8**)atrac->pFrame->extended_data, numSamples);
					u32 outBytes = numSamples * atrac->atracOutputChannels * sizeof(s16);
					CBreakPoints::ExecMemCheck(samplesAddr, true, outBytes, currentMIPS->pc);
					if (avret < 0) {
						ERROR_LOG(ME, "swr_convert: Error while converting %d", avret);
					}
					break;
				}
			}
		}

		atrac->currentSample += numSamples;
		numSamples = (atrac->codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
		Memory::Write_U32(numSamples * sizeof(s16) * atrac->atracOutputChannels, sampleBytesAddr);

		if (atrac->bufferPos >= atrac->first.size) {
			atrac->first.writableBytes = atrac->atracBytesPerFrame;
			atrac->first.size = atrac->firstSampleoffset;
			atrac->ForceSeekToSample(0);
		}
		else
			atrac->first.writableBytes = 0;
		Memory::Write_U32(atrac->first.writableBytes, sourceBytesConsumedAddr);
		return hleDelayResult(0, "low level atrac decode data", atracDecodeDelay);
	}
#endif // USE_FFMPEG

	return 0;
}

static int sceAtracSetAA3HalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize, u32 fileSize) {
	if (readSize > halfBufferSize) {
		ERROR_LOG(ME, "sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x, %08x): invalid read size", halfBuffer, readSize, halfBufferSize, fileSize);
		return ATRAC_ERROR_INCORRECT_READ_SIZE;
	}

	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = halfBufferSize;
	atrac->first.filesize = fileSize;
	int ret = atrac->AnalyzeAA3();
	if (ret < 0) {
		ERROR_LOG(ME, "sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x, %08x): bad data", halfBuffer, readSize, halfBufferSize, fileSize);
		delete atrac;
		return ret;
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		ERROR_LOG(ME, "sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x, %08x): no free ID", halfBuffer, readSize, halfBufferSize, fileSize);
		delete atrac;
		return atracID;
	}
	ERROR_LOG(ME, "UNIMPL %d=sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	return _AtracSetData(atracID, halfBuffer, halfBufferSize, true);
}

const HLEFunction sceAtrac3plus[] = {
	{0X7DB31251, &WrapU_IU<sceAtracAddStreamData>,                 "sceAtracAddStreamData",                'x', "ix"   },
	{0X6A8C3CD5, &WrapU_IUUUU<sceAtracDecodeData>,                 "sceAtracDecodeData",                   'x', "ixxxx"},
	{0XD5C28CC0, &WrapU_V<sceAtracEndEntry>,                       "sceAtracEndEntry",                     'x', ""     },
	{0X780F88D1, &WrapU_I<sceAtracGetAtracID>,                     "sceAtracGetAtracID",                   'x', "i"    },
	{0XCA3CA3D2, &WrapU_IIU<sceAtracGetBufferInfoForResetting>,    "sceAtracGetBufferInfoForReseting",     'x', "iix"  },
	{0XA554A158, &WrapU_IU<sceAtracGetBitrate>,                    "sceAtracGetBitrate",                   'x', "ix"   },
	{0X31668BAA, &WrapU_IU<sceAtracGetChannel>,                    "sceAtracGetChannel",                   'x', "ix"   },
	{0XFAA4F89B, &WrapU_IUU<sceAtracGetLoopStatus>,                "sceAtracGetLoopStatus",                'x', "ixx"  },
	{0XE88F759B, &WrapU_IU<sceAtracGetInternalErrorInfo>,          "sceAtracGetInternalErrorInfo",         'x', "ix"   },
	{0XD6A5F2F7, &WrapU_IU<sceAtracGetMaxSample>,                  "sceAtracGetMaxSample",                 'x', "ix"   },
	{0XE23E3A35, &WrapU_IU<sceAtracGetNextDecodePosition>,         "sceAtracGetNextDecodePosition",        'x', "ix"   },
	{0X36FAABFB, &WrapU_IU<sceAtracGetNextSample>,                 "sceAtracGetNextSample",                'x', "ix"   },
	{0X9AE849A7, &WrapU_IU<sceAtracGetRemainFrame>,                "sceAtracGetRemainFrame",               'x', "ix"   },
	{0X83E85EA0, &WrapU_IUU<sceAtracGetSecondBufferInfo>,          "sceAtracGetSecondBufferInfo",          'x', "ixx"  },
	{0XA2BBA8BE, &WrapU_IUUU<sceAtracGetSoundSample>,              "sceAtracGetSoundSample",               'x', "ixxx" },
	{0X5D268707, &WrapU_IUUU<sceAtracGetStreamDataInfo>,           "sceAtracGetStreamDataInfo",            'x', "ixxx" },
	{0X61EB33F5, &WrapU_I<sceAtracReleaseAtracID>,                 "sceAtracReleaseAtracID",               'x', "i"    },
	{0X644E5607, &WrapU_IIII<sceAtracResetPlayPosition>,           "sceAtracResetPlayPosition",            'x', "iiii" },
	{0X3F6E26B5, &WrapU_IUUU<sceAtracSetHalfwayBuffer>,            "sceAtracSetHalfwayBuffer",             'x', "ixxx" },
	{0X83BF7AFD, &WrapU_IUU<sceAtracSetSecondBuffer>,              "sceAtracSetSecondBuffer",              'x', "ixx"  },
	{0X0E2A73AB, &WrapU_IUU<sceAtracSetData>,                      "sceAtracSetData",                      'x', "ixx"  },
	{0X7A20E7AF, &WrapI_UI<sceAtracSetDataAndGetID>,               "sceAtracSetDataAndGetID",              'i', "xi"   },
	{0XD1F59FDB, &WrapU_V<sceAtracStartEntry>,                     "sceAtracStartEntry",                   'x', ""     },
	{0X868120B5, &WrapU_II<sceAtracSetLoopNum>,                    "sceAtracSetLoopNum",                   'x', "ii"   },
	{0X132F1ECA, &WrapI_II<sceAtracReinit>,                        "sceAtracReinit",                       'i', "ii"   },
	{0XECA32A99, &WrapI_I<sceAtracIsSecondBufferNeeded>,           "sceAtracIsSecondBufferNeeded",         'i', "i"    },
	{0X0FAE370E, &WrapI_UUU<sceAtracSetHalfwayBufferAndGetID>,     "sceAtracSetHalfwayBufferAndGetID",     'i', "xxx"  },
	{0X2DD3E298, &WrapU_IIU<sceAtracGetBufferInfoForResetting>,    "sceAtracGetBufferInfoForResetting",    'x', "iix"  },
	{0X5CF9D852, &WrapI_IUUU<sceAtracSetMOutHalfwayBuffer>,        "sceAtracSetMOutHalfwayBuffer",         'i', "ixxx" },
	{0XF6837A1A, &WrapU_IUU<sceAtracSetMOutData>,                  "sceAtracSetMOutData",                  'x', "ixx"  },
	{0X472E3825, &WrapI_UU<sceAtracSetMOutDataAndGetID>,           "sceAtracSetMOutDataAndGetID",          'i', "xx"   },
	{0X9CD7DE03, &WrapI_UUU<sceAtracSetMOutHalfwayBufferAndGetID>, "sceAtracSetMOutHalfwayBufferAndGetID", 'i', "xxx"  },
	{0XB3B5D042, &WrapI_IU<sceAtracGetOutputChannel>,              "sceAtracGetOutputChannel",             'i', "ix"   },
	{0X5622B7C1, &WrapI_UUUU<sceAtracSetAA3DataAndGetID>,          "sceAtracSetAA3DataAndGetID",           'i', "xxxx" },
	{0X5DD66588, &WrapI_UUUU<sceAtracSetAA3HalfwayBufferAndGetID>, "sceAtracSetAA3HalfwayBufferAndGetID",  'i', "xxxx" },
	{0X231FC6B7, &WrapI_I<_sceAtracGetContextAddress>,             "_sceAtracGetContextAddress",           'i', "i"    },
	{0X1575D64B, &WrapI_IU<sceAtracLowLevelInitDecoder>,           "sceAtracLowLevelInitDecoder",          'i', "ix"   },
	{0X0C116E1B, &WrapI_IUUUU<sceAtracLowLevelDecode>,             "sceAtracLowLevelDecode",               'i', "ixxxx"},
};

void Register_sceAtrac3plus() {
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
