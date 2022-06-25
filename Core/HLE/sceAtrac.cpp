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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"

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
//
// This buffer is generally filled by sceAtracAddStreamData, and where to fill it is given by
// either sceAtracGetStreamDataInfo when continuing to move forwards in the stream of audio data,
// or sceAtracGetBufferInfoForResetting when seeking to a specific location in the audio stream.
//
// State 6 indicates a second buffer is needed.  This buffer is used to manage looping correctly.
// To determine how to fill it, the game will call sceAtracGetSecondBufferInfo, then after filling
// the buffer it will call sceAtracSetSecondBuffer.
// The second buffer will just contain the data for the end of loop.  The "first" buffer may manage
// only the looped portion, or some of the part after the loop (depending on second buf size.)
//
// Most files will be in RIFF format.  It's also possible to load in an OMA/AA3 format file, but
// ultimately this will share the same buffer - it's just offset a bit more.
//
// Low level decoding doesn't use the buffer, and decodes only a single packet at a time.
//
// Lastly, sceSas has some integration with sceAtrac, which allows setting an Atrac id as
// a voice for an SAS core.  In this mode, the game will directly modify some of the context,
// but will largely only interact using sceSas.
//
// Note that this buffer is THE view of the audio stream.  On a PSP, the firmware does not manage
// any cache or separate version of the buffer - at most it manages decode state from earlier in
// the buffer.

#define ATRAC_ERROR_API_FAIL                 0x80630002
#define ATRAC_ERROR_NO_ATRACID               0x80630003
#define ATRAC_ERROR_INVALID_CODECTYPE        0x80630004
#define ATRAC_ERROR_BAD_ATRACID              0x80630005
#define ATRAC_ERROR_UNKNOWN_FORMAT           0x80630006
#define ATRAC_ERROR_WRONG_CODECTYPE          0x80630007
#define ATRAC_ERROR_BAD_CODEC_PARAMS         0x80630008
#define ATRAC_ERROR_ALL_DATA_LOADED          0x80630009
#define ATRAC_ERROR_NO_DATA                  0x80630010
#define ATRAC_ERROR_SIZE_TOO_SMALL           0x80630011
#define ATRAC_ERROR_SECOND_BUFFER_NEEDED     0x80630012
#define ATRAC_ERROR_INCORRECT_READ_SIZE      0x80630013
#define ATRAC_ERROR_BAD_SAMPLE               0x80630015
#define ATRAC_ERROR_BAD_FIRST_RESET_SIZE     0x80630016
#define ATRAC_ERROR_BAD_SECOND_RESET_SIZE    0x80630017
#define ATRAC_ERROR_ADD_DATA_IS_TOO_BIG      0x80630018
#define ATRAC_ERROR_NOT_MONO                 0x80630019
#define ATRAC_ERROR_NO_LOOP_INFORMATION      0x80630021
#define ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED 0x80630022
#define ATRAC_ERROR_BUFFER_IS_EMPTY          0x80630023
#define ATRAC_ERROR_ALL_DATA_DECODED         0x80630024
#define ATRAC_ERROR_IS_LOW_LEVEL             0x80630031
#define ATRAC_ERROR_IS_FOR_SCESAS            0x80630040
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
	ATDECODE_BADFRAME = 2,
};

struct InputBuffer {
	// Address of the buffer.
	u32 addr;
	// Size of data read so far into dataBuf_ (to be removed.)
	u32 size;
	// Offset into addr at which new data is added.
	u32 offset;
	// Last writableBytes number (to be removed.)
	u32 writableBytes;
	// Unused, always 0.
	u32 neededBytes;
	// Total size of the entire file data.
	u32 filesize;
	// Offset into the file at which new data is read.
	u32 fileoffset;
};

struct Atrac;
int __AtracSetContext(Atrac *atrac);
void _AtracGenerateContext(Atrac *atrac);

struct AtracLoopInfo {
	int cuePointID;
	int type;
	int startSample;
	int endSample;
	int fraction;
	int playCount;
};

#ifndef USE_FFMPEG
struct AVPacket {
	uint8_t *data;
	int size;
	int64_t pos;
};
#endif

struct Atrac {
	Atrac() : atracID_(-1), dataBuf_(0), decodePos_(0), bufferPos_(0),
		channels_(0), outputChannels_(2), bitrate_(64), bytesPerFrame_(0), bufferMaxSize_(0), jointStereo_(0),
		currentSample_(0), endSample_(0), firstSampleOffset_(0), dataOff_(0),
		loopStartSample_(-1), loopEndSample_(-1), loopNum_(0),
		failedDecode_(false), ignoreDataBuf_(false), codecType_(0),
		bufferState_(ATRAC_STATUS_NO_DATA) {
		memset(&first_, 0, sizeof(first_));
		memset(&second_, 0, sizeof(second_));
#ifdef USE_FFMPEG
		codecCtx_ = nullptr;
		swrCtx_ = nullptr;
		frame_ = nullptr;
		packet_ = nullptr;
#endif // USE_FFMPEG
		context_ = 0;
	}

	~Atrac() {
		ResetData();
	}

	void ResetData() {
#ifdef USE_FFMPEG
		ReleaseFFMPEGContext();
#endif // USE_FFMPEG

		if (dataBuf_)
			delete [] dataBuf_;
		dataBuf_ = 0;
		ignoreDataBuf_ = false;
		bufferState_ = ATRAC_STATUS_NO_DATA;

		if (context_.IsValid())
			kernelMemory.Free(context_.ptr);

		// Clean slate time.
		failedDecode_ = false;
	}

	void SetBufferState() {
		if (bufferMaxSize_ >= first_.filesize) {
			if (first_.size < first_.filesize) {
				// The buffer is big enough, but we don't have all the data yet.
				bufferState_ = ATRAC_STATUS_HALFWAY_BUFFER;
			} else {
				bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
			}
		} else {
			if (loopEndSample_ <= 0) {
				// There's no looping, but we need to stream the data in our buffer.
				bufferState_ = ATRAC_STATUS_STREAMED_WITHOUT_LOOP;
			} else if (loopEndSample_ == endSample_ + firstSampleOffset_ + (int)FirstOffsetExtra()) {
				bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
			} else {
				bufferState_ = ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
			}
		}
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("Atrac", 1, 9);
		if (!s)
			return;

		Do(p, channels_);
		Do(p, outputChannels_);
		if (s >= 5) {
			Do(p, jointStereo_);
		}

		Do(p, atracID_);
		Do(p, first_);
		Do(p, bufferMaxSize_);
		Do(p, codecType_);

		Do(p, currentSample_);
		Do(p, endSample_);
		Do(p, firstSampleOffset_);
		if (s >= 3) {
			Do(p, dataOff_);
		} else {
			dataOff_ = firstSampleOffset_;
		}

		u32 hasDataBuf = dataBuf_ != nullptr;
		Do(p, hasDataBuf);
		if (hasDataBuf) {
			if (p.mode == p.MODE_READ) {
				if (dataBuf_)
					delete [] dataBuf_;
				dataBuf_ = new u8[first_.filesize];
			}
			DoArray(p, dataBuf_, first_.filesize);
		}
		Do(p, second_);

		Do(p, decodePos_);
		if (s < 9) {
			u32 oldDecodeEnd = 0;
			Do(p, oldDecodeEnd);
		}
		if (s >= 4) {
			Do(p, bufferPos_);
		} else {
			bufferPos_ = decodePos_;
		}

		Do(p, bitrate_);
		Do(p, bytesPerFrame_);

		Do(p, loopinfo_);
		if (s < 9) {
			int oldLoopInfoNum = 42;
			Do(p, oldLoopInfoNum);
		}

		Do(p, loopStartSample_);
		Do(p, loopEndSample_);
		Do(p, loopNum_);

		Do(p, context_);
		if (s >= 6) {
			Do(p, bufferState_);
		} else {
			if (dataBuf_ == nullptr) {
				bufferState_ = ATRAC_STATUS_NO_DATA;
			} else {
				SetBufferState();
			}
		}

		if (s >= 7) {
			Do(p, ignoreDataBuf_);
		} else {
			ignoreDataBuf_ = false;
		}

		if (s >= 9) {
			Do(p, bufferValidBytes_);
			Do(p, bufferHeaderSize_);
		} else {
			bufferHeaderSize_ = dataOff_;
			bufferValidBytes_ = std::min(first_.size - dataOff_, StreamBufferEnd() - dataOff_);
			if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
				bufferPos_ = dataOff_;
			}
		}

		if (s < 8 && bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
			// We didn't actually allow the second buffer to be set this far back.
			// Pretend it's a regular loop, we'll just try our best.
			bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
		}

		// Make sure to do this late; it depends on things like bytesPerFrame_.
		if (p.mode == p.MODE_READ && bufferState_ != ATRAC_STATUS_NO_DATA) {
			__AtracSetContext(this);
		}
		
		if (s >= 2 && s < 9) {
			bool oldResetBuffer = false;
			Do(p, oldResetBuffer);
		}
	}

	int Analyze(u32 addr, u32 size);
	int AnalyzeAA3(u32 addr, u32 size, u32 filesize);

	u32 SamplesPerFrame() const {
		return codecType_ == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES;
	}

	u32 FirstOffsetExtra() const {
		return codecType_ == PSP_CODEC_AT3PLUS ? 368 : 69;
	}

	u32 DecodePosBySample(int sample) const {
		return (u32)(firstSampleOffset_ + sample / (int)SamplesPerFrame() * bytesPerFrame_);
	}

	u32 FileOffsetBySample(int sample) const {
		int offsetSample = sample + firstSampleOffset_;
		int frameOffset = offsetSample / (int)SamplesPerFrame();
		return (u32)(dataOff_ + bytesPerFrame_ + frameOffset * bytesPerFrame_);
	}

	int RemainingFrames() const {
		if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
			// Meaning, infinite I guess?  We've got it all.
			return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;
		}

		u32 currentFileOffset = FileOffsetBySample(currentSample_ - SamplesPerFrame() + FirstOffsetExtra());
		if (first_.fileoffset >= first_.filesize) {
			if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
				return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
			}
			int loopEndAdjusted = loopEndSample_ - FirstOffsetExtra() - firstSampleOffset_;
			if (bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && currentSample_ > loopEndAdjusted) {
				// No longer looping in this case, outside the loop.
				return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
			}
			if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK && loopNum_ == 0) {
				return PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY;
			}
		}

		if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
			// Since we're streaming, the remaining frames are what's valid in the buffer.
			return bufferValidBytes_ / bytesPerFrame_;
		}

		// Since the first frame is shorter by this offset, add to round up at this offset.
		const int remainingBytes = first_.fileoffset - currentFileOffset;
		if (remainingBytes < 0) {
			// Just in case.  Shouldn't happen, but once did by mistake.
			return 0;
		}
		return remainingBytes / bytesPerFrame_;
	}

	int atracID_;
	u8 *dataBuf_;

	u32 decodePos_;
	// Used by low-level decoding and to track streaming.
	u32 bufferPos_;
	u32 bufferValidBytes_;
	u32 bufferHeaderSize_;

	u16 channels_;
	u16 outputChannels_;
	u32 bitrate_;
	u16 bytesPerFrame_;
	u32 bufferMaxSize_;
	int jointStereo_;

	int currentSample_;
	int endSample_;
	int firstSampleOffset_;
	// Offset of the first sample in the input buffer
	int dataOff_;

	std::vector<AtracLoopInfo> loopinfo_;

	int loopStartSample_;
	int loopEndSample_;
	int loopNum_;

	bool failedDecode_;
	// Indicates that the dataBuf_ array should not be used.
	bool ignoreDataBuf_;

	u32 codecType_;
	AtracStatus bufferState_;

	InputBuffer first_;
	InputBuffer second_;

	PSPPointer<SceAtracId> context_;

#ifdef USE_FFMPEG
	AVCodecContext  *codecCtx_ = nullptr;
	SwrContext      *swrCtx_ = nullptr;
	AVFrame         *frame_ = nullptr;
	AVPacket        *packet_ = nullptr;
#endif // USE_FFMPEG

#ifdef USE_FFMPEG
	void ReleaseFFMPEGContext() {
		// All of these allow null pointers.
		av_freep(&frame_);
		swr_free(&swrCtx_);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 52, 0)
		// If necessary, extradata is automatically freed.
		avcodec_free_context(&codecCtx_);
#else
		// Future versions may add other things to free, but avcodec_free_context didn't exist yet here.
		// Some old versions crash when we try to free extradata and subtitle_header, so let's not. A minor
		// leak is better than a segfualt.
		// av_freep(&codecCtx_->extradata);
		// av_freep(&codecCtx_->subtitle_header);
		avcodec_close(codecCtx_);
		av_freep(&codecCtx_);
#endif
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
		av_packet_free(&packet_);
#else
		av_free_packet(packet_);
		delete packet_;
		packet_ = nullptr;
#endif
	}
#endif // USE_FFMPEG

	void ForceSeekToSample(int sample) {
#ifdef USE_FFMPEG
		avcodec_flush_buffers(codecCtx_);

		// Discard any pending packet data.
		packet_->size = 0;
#endif

		currentSample_ = sample;
	}

	u8 *BufferStart() {
		return ignoreDataBuf_ ? Memory::GetPointer(first_.addr) : dataBuf_;
	}

	void SeekToSample(int sample) {
#ifdef USE_FFMPEG
		// Discard any pending packet data.
		packet_->size = 0;

		// It seems like the PSP aligns the sample position to 0x800...?
		const u32 offsetSamples = firstSampleOffset_ + FirstOffsetExtra();
		const u32 unalignedSamples = (offsetSamples + sample) % SamplesPerFrame();
		int seekFrame = sample + offsetSamples - unalignedSamples;

		if ((sample != currentSample_ || sample == 0) && codecCtx_ != nullptr) {
			// Prefill the decode buffer with packets before the first sample offset.
			avcodec_flush_buffers(codecCtx_);

			int adjust = 0;
			if (sample == 0) {
				int offsetSamples = firstSampleOffset_ + FirstOffsetExtra();
				adjust = -(int)(offsetSamples % SamplesPerFrame());
			}
			const u32 off = FileOffsetBySample(sample + adjust);
			const u32 backfill = bytesPerFrame_ * 2;
			const u32 start = off - dataOff_ < backfill ? dataOff_ : off - backfill;
			for (u32 pos = start; pos < off; pos += bytesPerFrame_) {
				av_init_packet(packet_);
				packet_->data = BufferStart() + pos;
				packet_->size = bytesPerFrame_;
				packet_->pos = pos;

				// Process the packet, we don't care about success.
				DecodePacket();
			}
		}
#endif // USE_FFMPEG

		currentSample_ = sample;
	}

	uint32_t CurBufferAddress(int adjust = 0) {
		u32 off = FileOffsetBySample(currentSample_ + adjust);
		if (off < first_.size && ignoreDataBuf_) {
			return first_.addr + off;
		}
		// If it's in dataBug, it's not in PSP memory.
		return 0;
	}

	bool FillPacket(int adjust = 0) {
		u32 off = FileOffsetBySample(currentSample_ + adjust);
		if (off < first_.size) {
#ifdef USE_FFMPEG
			av_init_packet(packet_);
			packet_->data = BufferStart() + off;
			packet_->size = std::min((u32)bytesPerFrame_, first_.size - off);
			packet_->pos = off;
#endif // USE_FFMPEG

			return true;
		} else {
			return false;
		}

		return true;
	}

	bool FillLowLevelPacket(u8 *ptr) {
#ifdef USE_FFMPEG
		av_init_packet(packet_);

		packet_->data = ptr;
		packet_->size = bytesPerFrame_;
		packet_->pos = 0;
#endif // USE_FFMPEG
		return true;
	}

	AtracDecodeResult DecodePacket() {
#ifdef USE_FFMPEG
		if (codecCtx_ == nullptr) {
			return ATDECODE_FAILED;
		}

		int got_frame = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
		if (packet_->size != 0) {
			int err = avcodec_send_packet(codecCtx_, packet_);
			if (err < 0) {
				ERROR_LOG_REPORT(ME, "avcodec_send_packet: Error decoding audio %d / %08x", err, err);
				failedDecode_ = true;
				return ATDECODE_FAILED;
			}
		}

		int err = avcodec_receive_frame(codecCtx_, frame_);
		int bytes_read = 0;
		if (err >= 0) {
			bytes_read = frame_->pkt_size;
			got_frame = 1;
		} else if (err != AVERROR(EAGAIN)) {
			bytes_read = err;
		}
#else
		int bytes_read = avcodec_decode_audio4(codecCtx_, frame_, &got_frame, packet_);
#endif
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
		av_packet_unref(packet_);
#else
		av_free_packet(packet_);
#endif
		if (bytes_read == AVERROR_PATCHWELCOME) {
			ERROR_LOG(ME, "Unsupported feature in ATRAC audio.");
			// Let's try the next packet.
			packet_->size = 0;
			return ATDECODE_BADFRAME;
		} else if (bytes_read < 0) {
			ERROR_LOG_REPORT(ME, "avcodec_decode_audio4: Error decoding audio %d / %08x", bytes_read, bytes_read);
			failedDecode_ = true;
			return ATDECODE_FAILED;
		}

		return got_frame ? ATDECODE_GOTFRAME : ATDECODE_FEEDME;
#else
		return ATDECODE_BADFRAME;
#endif // USE_FFMPEG
	}

	void CalculateStreamInfo(u32 *readOffset);

	u32 StreamBufferEnd() const {
		// The buffer is always aligned to a frame in size, not counting an optional header.
		// The header will only initially exist after the data is first set.
		u32 framesAfterHeader = (bufferMaxSize_ - bufferHeaderSize_) / bytesPerFrame_;
		return framesAfterHeader * bytesPerFrame_ + bufferHeaderSize_;
	}

	void ConsumeFrame() {
		bufferPos_ += bytesPerFrame_;
		if (bufferValidBytes_ > bytesPerFrame_) {
			bufferValidBytes_ -= bytesPerFrame_;
		} else {
			bufferValidBytes_ = 0;
		}
		if (bufferPos_ >= StreamBufferEnd()) {
			// Wrap around... theoretically, this should only happen at exactly StreamBufferEnd.
			bufferPos_ -= StreamBufferEnd();
			bufferHeaderSize_ = 0;
		}
	}

private:
	void AnalyzeReset();
};

struct AtracSingleResetBufferInfo {
	u32_le writePosPtr;
	u32_le writableBytes;
	u32_le minWriteBytes;
	u32_le filePos;
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
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 18, 100)
	avcodec_register_all();
#endif
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 12, 100)
	av_register_all();
#endif
#endif // USE_FFMPEG
}

void __AtracDoState(PointerWrap &p) {
	auto s = p.Section("sceAtrac", 1);
	if (!s)
		return;

	Do(p, atracInited);
	for (int i = 0; i < PSP_NUM_ATRAC_IDS; ++i) {
		bool valid = atracIDs[i] != NULL;
		Do(p, valid);
		if (valid) {
			Do(p, atracIDs[i]);
		} else {
			delete atracIDs[i];
			atracIDs[i] = NULL;
		}
	}
	DoArray(p, atracIDTypes, PSP_NUM_ATRAC_IDS);
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
	Atrac *atrac = atracIDs[atracID];

	if (atrac && atrac->context_.IsValid()) {
		// Read in any changes from the game to the context.
		// TODO: Might be better to just always track in RAM.
		atrac->bufferState_ = atrac->context_->info.state;
		// This value is actually abused by games to store the SAS voice number.
		atrac->loopNum_ = atrac->context_->info.loopNum;
	}

	return atrac;
}

static int createAtrac(Atrac *atrac) {
	for (int i = 0; i < (int)ARRAY_SIZE(atracIDs); ++i) {
		if (atracIDTypes[i] == atrac->codecType_ && atracIDs[i] == 0) {
			atracIDs[i] = atrac;
			atrac->atracID_ = i;
			return i;
		}
	}

	return ATRAC_ERROR_NO_ATRACID;
}

static int deleteAtrac(int atracID) {
	if (atracID >= 0 && atracID < PSP_NUM_ATRAC_IDS) {
		if (atracIDs[atracID] != nullptr) {
			delete atracIDs[atracID];
			atracIDs[atracID] = nullptr;

			return 0;
		}
	}

	return ATRAC_ERROR_BAD_ATRACID;
}

void Atrac::AnalyzeReset() {
	// Reset some values.
	codecType_ = 0;
	currentSample_ = 0;
	endSample_ = -1;
	loopNum_ = 0;
	loopinfo_.clear();
	loopStartSample_ = -1;
	loopEndSample_ = -1;
	decodePos_ = 0;
	bufferPos_ = 0;
	channels_ = 2;
}

struct RIFFFmtChunk {
	u16_le fmtTag;
	u16_le channels;
	u32_le samplerate;
	u32_le avgBytesPerSec;
	u16_le blockAlign;
};

int Atrac::Analyze(u32 addr, u32 size) {
	first_.addr = addr;
	first_.size = size;

	AnalyzeReset();

	// 72 is about the size of the minimum required data to even be valid.
	if (first_.size < 72) {
		return hleReportError(ME, ATRAC_ERROR_SIZE_TOO_SMALL, "buffer too small");
	}

	if (!Memory::IsValidAddress(first_.addr)) {
		return hleReportWarning(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "invalid buffer address");
	}

	// TODO: Validate stuff.

	if (Memory::Read_U32(first_.addr) != RIFF_CHUNK_MAGIC) {
		return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "invalid RIFF header");
	}

	u32 offset = 8;
	firstSampleOffset_ = 0;

	while (Memory::Read_U32(first_.addr + offset) != RIFF_WAVE_MAGIC) {
		// Get the size preceding the magic.
		int chunk = Memory::Read_U32(first_.addr + offset - 4);
		// Round the chunk size up to the nearest 2.
		offset += chunk + (chunk & 1);
		if (offset + 12 > first_.size) {
			return hleReportError(ME, ATRAC_ERROR_SIZE_TOO_SMALL, "too small for WAVE chunk at %d", offset);
		}
		if (Memory::Read_U32(first_.addr + offset) != RIFF_CHUNK_MAGIC) {
			return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "RIFF chunk did not contain WAVE");
		}
		offset += 8;
	}
	offset += 4;

	if (offset != 12) {
		WARN_LOG_REPORT(ME, "RIFF chunk at offset: %d", offset);
	}

	// RIFF size excluding chunk header.
	first_.filesize = Memory::Read_U32(first_.addr + offset - 8) + 8;
	// Even if the RIFF size is too low, it may simply be incorrect.  This works on real firmware.
	u32 maxSize = std::max(first_.filesize, first_.size);

	bool bfoundData = false;
	u32 dataChunkSize = 0;
	int sampleOffsetAdjust = 0;
	while (maxSize >= offset + 8 && !bfoundData) {
		int chunkMagic = Memory::Read_U32(first_.addr + offset);
		u32 chunkSize = Memory::Read_U32(first_.addr + offset + 4);
		// Account for odd sized chunks.
		if (chunkSize & 1) {
			WARN_LOG_REPORT_ONCE(oddchunk, ME, "RIFF chunk had uneven size");
		}
		chunkSize += (chunkSize & 1);
		offset += 8;
		if (chunkSize > maxSize - offset)
			break;
		switch (chunkMagic) {
		case FMT_CHUNK_MAGIC:
			{
				if (codecType_ != 0) {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "multiple fmt definitions");
				}

				auto at3fmt = PSPPointer<const RIFFFmtChunk>::Create(first_.addr + offset);
				if (chunkSize < 32 || (at3fmt->fmtTag == AT3_PLUS_MAGIC && chunkSize < 52)) {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "fmt definition too small (%d)", chunkSize);
				}

				if (at3fmt->fmtTag == AT3_MAGIC)
					codecType_ = PSP_MODE_AT_3;
				else if (at3fmt->fmtTag == AT3_PLUS_MAGIC)
					codecType_ = PSP_MODE_AT_3_PLUS;
				else {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "invalid fmt magic: %04x", at3fmt->fmtTag);
				}
				channels_ = at3fmt->channels;
				if (channels_ != 1 && channels_ != 2) {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "invalid channel count: %d", channels_);
				}
				if (at3fmt->samplerate != 44100) {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "unsupported sample rate: %d", at3fmt->samplerate);
				}
				bitrate_ = at3fmt->avgBytesPerSec * 8;
				bytesPerFrame_ = at3fmt->blockAlign;
				if (bytesPerFrame_ == 0) {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "invalid bytes per frame: %d", bytesPerFrame_);
				}

				// TODO: There are some format specific bytes here which seem to have fixed values?
				// Probably don't need them.

				if (at3fmt->fmtTag == AT3_MAGIC) {
					// This is the offset to the jointStereo_ field.
					jointStereo_ = Memory::Read_U32(first_.addr + offset + 24);
				}
			}
			break;
		case FACT_CHUNK_MAGIC:
			{
				endSample_ = Memory::Read_U32(first_.addr + offset);
				if (chunkSize >= 8) {
					firstSampleOffset_ = Memory::Read_U32(first_.addr + offset + 4);
				}
				if (chunkSize >= 12) {
					u32 largerOffset = Memory::Read_U32(first_.addr + offset + 8);
					sampleOffsetAdjust = firstSampleOffset_ - largerOffset;
				}
			}
			break;
		case SMPL_CHUNK_MAGIC:
			{
				if (chunkSize < 32) {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "smpl chunk too small (%d)", chunkSize);
				}
				int checkNumLoops = Memory::Read_U32(first_.addr + offset + 28);
				if (checkNumLoops != 0 && chunkSize < 36 + 20) {
					return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "smpl chunk too small for loop (%d)", chunkSize);
				}

				loopinfo_.resize(checkNumLoops);
				u32 loopinfoAddr = first_.addr + offset + 36;
				// The PSP only cares about the first loop start and end, it seems.
				// Most likely can skip the rest of this data, but it's not hurting anyone.
				for (int i = 0; i < checkNumLoops && 36 + (u32)i < chunkSize; i++, loopinfoAddr += 24) {
					loopinfo_[i].cuePointID = Memory::Read_U32(loopinfoAddr);
					loopinfo_[i].type = Memory::Read_U32(loopinfoAddr + 4);
					loopinfo_[i].startSample = Memory::Read_U32(loopinfoAddr + 8);
					loopinfo_[i].endSample = Memory::Read_U32(loopinfoAddr + 12);
					loopinfo_[i].fraction = Memory::Read_U32(loopinfoAddr + 16);
					loopinfo_[i].playCount = Memory::Read_U32(loopinfoAddr + 20);

					if (loopinfo_[i].startSample >= loopinfo_[i].endSample) {
						return hleReportError(ME, ATRAC_ERROR_BAD_CODEC_PARAMS, "loop starts after it ends");
					}
				}
			}
			break;
		case DATA_CHUNK_MAGIC:
			{
				bfoundData = true;
				dataOff_ = offset;
				dataChunkSize = chunkSize;
				if (first_.filesize < offset + chunkSize) {
					WARN_LOG_REPORT(ME, "Atrac data chunk extends beyond riff chunk");
					first_.filesize = offset + chunkSize;
				}
			}
			break;
		}
		offset += chunkSize;
	}

	if (codecType_ == 0) {
		return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "could not detect codec");
	}

	if (!bfoundData) {
		return hleReportError(ME, ATRAC_ERROR_SIZE_TOO_SMALL, "no data chunk");
	}

	// set the loopStartSample_ and loopEndSample_ by loopinfo_
	if (loopinfo_.size() > 0) {
		loopStartSample_ = loopinfo_[0].startSample + FirstOffsetExtra() + sampleOffsetAdjust;
		loopEndSample_ = loopinfo_[0].endSample + FirstOffsetExtra() + sampleOffsetAdjust;
	} else {
		loopStartSample_ = -1;
		loopEndSample_ = -1;
	}

	// if there is no correct endsample, try to guess it
	if (endSample_ <= 0 && bytesPerFrame_ != 0) {
		endSample_ = (dataChunkSize / bytesPerFrame_) * SamplesPerFrame();
		endSample_ -= firstSampleOffset_ + FirstOffsetExtra();
	}
	endSample_ -= 1;

	if (loopEndSample_ != -1 && loopEndSample_ > endSample_ + firstSampleOffset_ + (int)FirstOffsetExtra()) {
		return hleReportError(ME, ATRAC_ERROR_BAD_CODEC_PARAMS, "loop after end of data");
	}

	return 0;
}

int Atrac::AnalyzeAA3(u32 addr, u32 size, u32 filesize) {
	first_.addr = addr;
	first_.size = size;
	first_.filesize = filesize;

	AnalyzeReset();

	if (first_.size < 10) {
		return hleReportError(ME, ATRAC_ERROR_AA3_SIZE_TOO_SMALL, "buffer too small");
	}

	// TODO: Make sure this validation is correct, more testing.

	const u8 *buffer = Memory::GetPointer(first_.addr);
	if (buffer[0] != 'e' || buffer[1] != 'a' || buffer[2] != '3') {
		return hleReportError(ME, ATRAC_ERROR_AA3_INVALID_DATA, "invalid ea3 magic bytes");
	}

	// It starts with an id3 header (replaced with ea3.)  This is the size.
	u32 tagSize = buffer[9] | (buffer[8] << 7) | (buffer[7] << 14) | (buffer[6] << 21);
	if (first_.size < tagSize + 36) {
		return hleReportError(ME, ATRAC_ERROR_AA3_SIZE_TOO_SMALL, "truncated before id3 end");
	}

	// EA3 header starts at id3 header (10) + tagSize.
	buffer = Memory::GetPointer(first_.addr + 10 + tagSize);
	if (buffer[0] != 'E' || buffer[1] != 'A' || buffer[2] != '3') {
		return hleReportError(ME, ATRAC_ERROR_AA3_INVALID_DATA, "invalid EA3 magic bytes");
	}

	// Based on FFmpeg's code.
	u32 codecParams = buffer[35] | (buffer[34] << 8) | (buffer[35] << 16);
	const u32 at3SampleRates[8] = { 32000, 44100, 48000, 88200, 96000, 0 };

	switch (buffer[32]) {
	case 0:
		codecType_ = PSP_MODE_AT_3;
		bytesPerFrame_ = (codecParams & 0x03FF) * 8;
		bitrate_ = at3SampleRates[(codecParams >> 13) & 7] * bytesPerFrame_ * 8 / 1024;
		channels_ = 2;
		jointStereo_ = (codecParams >> 17) & 1;
		break;
	case 1:
		codecType_ = PSP_MODE_AT_3_PLUS;
		bytesPerFrame_ = ((codecParams & 0x03FF) * 8) + 8;
		bitrate_ = at3SampleRates[(codecParams >> 13) & 7] * bytesPerFrame_ * 8 / 2048;
		channels_ = (codecParams >> 10) & 7;
		break;
	case 3:
	case 4:
	case 5:
		return hleReportError(ME, ATRAC_ERROR_AA3_INVALID_DATA, "unsupported codec type %d", buffer[32]);
	default:
		return hleReportError(ME, ATRAC_ERROR_AA3_INVALID_DATA, "invalid codec type %d", buffer[32]);
	}

	dataOff_ = 10 + tagSize + 96;
	firstSampleOffset_ = 0;
	if (endSample_ < 0 && bytesPerFrame_ != 0) {
		endSample_ = ((first_.filesize - dataOff_) / bytesPerFrame_) * SamplesPerFrame();
	}
	endSample_ -= 1;

	return 0;
}

static u32 sceAtracGetAtracID(int codecType) {
	if (codecType != PSP_MODE_AT_3 && codecType != PSP_MODE_AT_3_PLUS) {
		return hleReportError(ME, ATRAC_ERROR_INVALID_CODECTYPE, "invalid codecType");
	}

	Atrac *atrac = new Atrac();
	atrac->codecType_ = codecType;
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(ME, atracID, "no free ID");
	}

	return hleLogSuccessInfoI(ME, atracID);
}

u32 _AtracAddStreamData(int atracID, u32 bufPtr, u32 bytesToAdd) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac)
		return 0;
	int addbytes = std::min(bytesToAdd, atrac->first_.filesize - atrac->first_.fileoffset);
	Memory::Memcpy(atrac->dataBuf_ + atrac->first_.fileoffset, bufPtr, addbytes, "AtracAddStreamData");
	atrac->first_.size += bytesToAdd;
	if (atrac->first_.size >= atrac->first_.filesize) {
		atrac->first_.size = atrac->first_.filesize;
		if (atrac->bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER)
			atrac->bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	}
	atrac->first_.fileoffset += addbytes;
	if (atrac->context_.IsValid()) {
		// refresh context_
		_AtracGenerateContext(atrac);
	}
	return 0;
}

static u32 AtracValidateManaged(const Atrac *atrac) {
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	} else if (atrac->bufferState_ == ATRAC_STATUS_NO_DATA) {
		return hleLogError(ME, ATRAC_ERROR_NO_DATA, "no data");
	} else if (atrac->bufferState_ == ATRAC_STATUS_LOW_LEVEL) {
		return hleLogError(ME, ATRAC_ERROR_IS_LOW_LEVEL, "cannot use for low level stream");
	} else if (atrac->bufferState_ == ATRAC_STATUS_FOR_SCESAS) {
		return hleLogError(ME, ATRAC_ERROR_IS_FOR_SCESAS, "cannot use for SAS stream");
	} else {
		return 0;
	}
}

void Atrac::CalculateStreamInfo(u32 *outReadOffset) {
	u32 readOffset = first_.fileoffset;
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Nothing to write.
		readOffset = 0;
		first_.offset = 0;
		first_.writableBytes = 0;
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// If we're buffering the entire file, just give the same as readOffset.
		first_.offset = readOffset;
		// In this case, the bytes writable are just the remaining bytes, always.
		first_.writableBytes = first_.filesize - readOffset;
	} else {
		u32 bufferEnd = StreamBufferEnd();
		u32 bufferValidExtended = bufferPos_ + bufferValidBytes_;
		if (bufferValidExtended < bufferEnd) {
			first_.offset = bufferValidExtended;
			first_.writableBytes = bufferEnd - bufferValidExtended;
		} else {
			u32 bufferStartUsed = bufferValidExtended - bufferEnd;
			first_.offset = bufferStartUsed;
			first_.writableBytes = bufferPos_ - bufferStartUsed;
		}

		if (readOffset >= first_.filesize) {
			if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
				// We don't need anything more, so all 0s.
				readOffset = 0;
				first_.offset = 0;
				first_.writableBytes = 0;
			} else {
				readOffset = FileOffsetBySample(loopStartSample_ - FirstOffsetExtra() - firstSampleOffset_ - SamplesPerFrame() * 2);
			}
		}

		if (readOffset + first_.writableBytes > first_.filesize) {
			// Never ask for past the end of file, even when the space is free.
			first_.writableBytes = first_.filesize - readOffset;
		}

		// If you don't think this should be here, remove it.  It's just a temporary safety check.
		if (first_.offset + first_.writableBytes > bufferMaxSize_) {
			ERROR_LOG_REPORT(ME, "Somehow calculated too many writable bytes: %d + %d > %d", first_.offset, first_.writableBytes, bufferMaxSize_);
			first_.offset = 0;
			first_.writableBytes = bufferMaxSize_;
		}
	}

	if (outReadOffset) {
		*outReadOffset = readOffset;
	}
}

// Notifies that more data is (OR will be very soon) available in the buffer.
// This implies it has been added to whatever position sceAtracGetStreamDataInfo would indicate.
//
// The total size of the buffer is atrac->bufferMaxSize_.
static u32 sceAtracAddStreamData(int atracID, u32 bytesToAdd) {
	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	if (atrac->bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Let's avoid spurious warnings.  Some games call this with 0 which is pretty harmless.
		if (bytesToAdd == 0)
			return hleLogDebug(ME, ATRAC_ERROR_ALL_DATA_LOADED, "stream entirely loaded");
		return hleLogWarning(ME, ATRAC_ERROR_ALL_DATA_LOADED, "stream entirely loaded");
	}

	u32 readOffset;
	atrac->CalculateStreamInfo(&readOffset);

	if (bytesToAdd > atrac->first_.writableBytes)
		return hleLogWarning(ME, ATRAC_ERROR_ADD_DATA_IS_TOO_BIG, "too many bytes");

	if (bytesToAdd > 0) {
		atrac->first_.fileoffset = readOffset;
		int addbytes = std::min(bytesToAdd, atrac->first_.filesize - atrac->first_.fileoffset);
		if (!atrac->ignoreDataBuf_) {
			Memory::Memcpy(atrac->dataBuf_ + atrac->first_.fileoffset, atrac->first_.addr + atrac->first_.offset, addbytes, "AtracAddStreamData");
		}
		atrac->first_.fileoffset += addbytes;
	}
	atrac->first_.size += bytesToAdd;
	if (atrac->first_.size >= atrac->first_.filesize) {
		atrac->first_.size = atrac->first_.filesize;
		if (atrac->bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER)
			atrac->bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		if (atrac->context_.IsValid()) {
			_AtracGenerateContext(atrac);
		}
	}

	atrac->first_.offset += bytesToAdd;
	atrac->bufferValidBytes_ += bytesToAdd;

	return hleLogSuccessI(ME, 0);
}

u32 _AtracDecodeData(int atracID, u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	Atrac *atrac = getAtrac(atracID);

	u32 ret = 0;
	if (atrac == NULL) {
		ret = ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->dataBuf_) {
		ret = ATRAC_ERROR_NO_DATA;
	} else {
		int loopNum = atrac->loopNum_;
		if (atrac->bufferState_ == ATRAC_STATUS_FOR_SCESAS) {
			// TODO: Might need more testing.
			loopNum = 0;
		}

		// We already passed the end - return an error (many games check for this.)
		if (atrac->currentSample_ >= atrac->endSample_ && loopNum == 0) {
			*SamplesNum = 0;
			*finish = 1;
			ret = ATRAC_ERROR_ALL_DATA_DECODED;
		} else {
			// TODO: This isn't at all right, but at least it makes the music "last" some time.
			u32 numSamples = 0;

			// It seems like the PSP aligns the sample position to 0x800...?
			int offsetSamples = atrac->firstSampleOffset_ + atrac->FirstOffsetExtra();
			int skipSamples = 0;
			u32 maxSamples = atrac->endSample_ + 1 - atrac->currentSample_;
			u32 unalignedSamples = (offsetSamples + atrac->currentSample_) % atrac->SamplesPerFrame();
			if (unalignedSamples != 0) {
				// We're off alignment, possibly due to a loop.  Force it back on.
				maxSamples = atrac->SamplesPerFrame() - unalignedSamples;
				skipSamples = unalignedSamples;
			}

			if (skipSamples != 0 && atrac->bufferHeaderSize_ == 0) {
				// Skip the initial frame used to load state for the looped frame.
				// TODO: We will want to actually read this in.
				atrac->ConsumeFrame();
			}

			if (!atrac->failedDecode_ && (atrac->codecType_ == PSP_MODE_AT_3 || atrac->codecType_ == PSP_MODE_AT_3_PLUS)) {
				atrac->SeekToSample(atrac->currentSample_);

				AtracDecodeResult res = ATDECODE_FEEDME;
				while (atrac->FillPacket(-skipSamples)) {
					uint32_t packetAddr = atrac->CurBufferAddress(-skipSamples);
#ifdef USE_FFMPEG
					int packetSize = atrac->packet_->size;
#endif // USE_FFMPEG
					res = atrac->DecodePacket();
					if (res == ATDECODE_FAILED) {
						*SamplesNum = 0;
						*finish = 1;
						return ATRAC_ERROR_ALL_DATA_DECODED;
					}

					if (res == ATDECODE_GOTFRAME) {
#ifdef USE_FFMPEG
						// got a frame
						int skipped = std::min(skipSamples, atrac->frame_->nb_samples);
						skipSamples -= skipped;
						numSamples = atrac->frame_->nb_samples - skipped;

						// If we're at the end, clamp to samples we want.  It always returns a full chunk.
						numSamples = std::min(maxSamples, numSamples);

						if (skipped > 0 && numSamples == 0) {
							// Wait for the next one.
							res = ATDECODE_FEEDME;
						}

						if (outbuf != NULL && numSamples != 0) {
							int inbufOffset = 0;
							if (skipped != 0) {
								AVSampleFormat fmt = (AVSampleFormat)atrac->frame_->format;
								// We want the offset per channel.
								inbufOffset = av_samples_get_buffer_size(NULL, 1, skipped, fmt, 1);
							}

							u8 *out = outbuf;
							const u8 *inbuf[2] = {
								atrac->frame_->extended_data[0] + inbufOffset,
								atrac->frame_->extended_data[1] + inbufOffset,
							};
							int avret = swr_convert(atrac->swrCtx_, &out, numSamples, inbuf, numSamples);
							if (outbufPtr != 0) {
								u32 outBytes = numSamples * atrac->outputChannels_ * sizeof(s16);
								if (packetAddr != 0 && MemBlockInfoDetailed()) {
									const std::string tag = "AtracDecode/" + GetMemWriteTagAt(packetAddr, packetSize);
									NotifyMemInfo(MemBlockFlags::READ, packetAddr, packetSize, tag.c_str(), tag.size());
									NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, tag.c_str(), tag.size());
								} else {
									NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, "AtracDecode");
								}
							}
							if (avret < 0) {
								ERROR_LOG(ME, "swr_convert: Error while converting %d", avret);
							}
						}
#endif // USE_FFMPEG
					}
					if (res == ATDECODE_GOTFRAME || res == ATDECODE_BADFRAME) {
						// We only want one frame per call, let's continue the next time.
						break;
					}
				}

				if (res != ATDECODE_GOTFRAME && atrac->currentSample_ < atrac->endSample_) {
					// Never got a frame.  We may have dropped a GHA frame or otherwise have a bug.
					// For now, let's try to provide an extra "frame" if possible so games don't infinite loop.
					if (atrac->FileOffsetBySample(atrac->currentSample_) < atrac->first_.filesize) {
						numSamples = std::min(maxSamples, atrac->SamplesPerFrame());
						u32 outBytes = numSamples * atrac->outputChannels_ * sizeof(s16);
						if (outbuf != nullptr) {
							memset(outbuf, 0, outBytes);
							NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, "AtracDecode");
						}
					}
				}
			}

			*SamplesNum = numSamples;
			// update current sample and decodePos
			atrac->currentSample_ += numSamples;
			atrac->decodePos_ = atrac->DecodePosBySample(atrac->currentSample_);

			atrac->ConsumeFrame();

			int finishFlag = 0;
			// TODO: Verify.
			bool hitEnd = atrac->currentSample_ >= atrac->endSample_ || (numSamples == 0 && atrac->first_.size >= atrac->first_.filesize);
			int loopEndAdjusted = atrac->loopEndSample_ - atrac->FirstOffsetExtra() - atrac->firstSampleOffset_;
			if ((hitEnd || atrac->currentSample_ > loopEndAdjusted) && loopNum != 0) {
				atrac->SeekToSample(atrac->loopStartSample_ - atrac->FirstOffsetExtra() - atrac->firstSampleOffset_);
				if (atrac->bufferState_ != ATRAC_STATUS_FOR_SCESAS) {
					if (atrac->loopNum_ > 0)
						atrac->loopNum_--;
				}
				if ((atrac->bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
					// Whatever bytes we have left were added from the loop.
					u32 loopOffset = atrac->FileOffsetBySample(atrac->loopStartSample_ - atrac->FirstOffsetExtra() - atrac->firstSampleOffset_ - atrac->SamplesPerFrame() * 2);
					// TODO: Hmm, need to manage the buffer better.  But don't move fileoffset if we already have valid data.
					if (loopOffset > atrac->first_.fileoffset || loopOffset + atrac->bufferValidBytes_ < atrac->first_.fileoffset) {
						// Skip the initial frame at the start.
						atrac->first_.fileoffset = atrac->FileOffsetBySample(atrac->loopStartSample_ - atrac->FirstOffsetExtra() - atrac->firstSampleOffset_ - atrac->SamplesPerFrame() * 2);
					}
				}
			} else if (hitEnd) {
				finishFlag = 1;

				// Still move forward, so we know that we've read everything.
				// This seems to be reflected in the context as well.
				atrac->currentSample_ += atrac->SamplesPerFrame() - numSamples;
			}

			*finish = finishFlag;
			*remains = atrac->RemainingFrames();
		}
		if (atrac->context_.IsValid()) {
			// refresh context_
			_AtracGenerateContext(atrac);
		}
	}

	return ret;
}

static u32 sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr) {
	// Note that outAddr being null is completely valid here, used to skip data.

	u32 numSamples = 0;
	u32 finish = 0;
	int remains = 0;
	int ret = _AtracDecodeData(atracID, Memory::GetPointer(outAddr), outAddr, &numSamples, &finish, &remains);
	if (ret != (int)ATRAC_ERROR_BAD_ATRACID && ret != (int)ATRAC_ERROR_NO_DATA) {
		if (Memory::IsValidAddress(numSamplesAddr))
			Memory::Write_U32(numSamples, numSamplesAddr);
		if (Memory::IsValidAddress(finishFlagAddr))
			Memory::Write_U32(finish, finishFlagAddr);
		// On error, no remaining frame value is written.
		if (ret == 0 && Memory::IsValidAddress(remainAddr))
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

static void AtracGetResetBufferInfo(Atrac *atrac, AtracResetBufferInfo *bufferInfo, int sample) {
	if (atrac->bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		bufferInfo->first.writePosPtr = atrac->first_.addr;
		// Everything is loaded, so nothing needs to be read.
		bufferInfo->first.writableBytes = 0;
		bufferInfo->first.minWriteBytes = 0;
		bufferInfo->first.filePos = 0;
	} else if (atrac->bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// Here the message is: you need to read at least this many bytes to get to that position.
		// This is because we're filling the buffer start to finish, not streaming.
		bufferInfo->first.writePosPtr = atrac->first_.addr + atrac->first_.size;
		bufferInfo->first.writableBytes = atrac->first_.filesize - atrac->first_.size;
		int minWriteBytes = atrac->FileOffsetBySample(sample) - atrac->first_.size;
		if (minWriteBytes > 0) {
			bufferInfo->first.minWriteBytes = minWriteBytes;
		} else {
			bufferInfo->first.minWriteBytes = 0;
		}
		bufferInfo->first.filePos = atrac->first_.size;
	} else {
		// This is without the sample offset.  The file offset also includes the previous batch of samples?
		int sampleFileOffset = atrac->FileOffsetBySample(sample - atrac->firstSampleOffset_ - atrac->SamplesPerFrame());

		// Update the writable bytes.  When streaming, this is just the number of bytes until the end.
		const u32 bufSizeAligned = (atrac->bufferMaxSize_ / atrac->bytesPerFrame_) * atrac->bytesPerFrame_;
		const int needsMoreFrames = atrac->FirstOffsetExtra();

		bufferInfo->first.writePosPtr = atrac->first_.addr;
		bufferInfo->first.writableBytes = std::min(atrac->first_.filesize - sampleFileOffset, bufSizeAligned);
		if (((sample + atrac->firstSampleOffset_) % (int)atrac->SamplesPerFrame()) >= (int)atrac->SamplesPerFrame() - needsMoreFrames) {
			// Not clear why, but it seems it wants a bit extra in case the sample is late?
			bufferInfo->first.minWriteBytes = atrac->bytesPerFrame_ * 3;
		} else {
			bufferInfo->first.minWriteBytes = atrac->bytesPerFrame_ * 2;
		}
		if ((u32)sample < (u32)atrac->firstSampleOffset_ && sampleFileOffset != atrac->dataOff_) {
			sampleFileOffset -= atrac->bytesPerFrame_;
		}
		bufferInfo->first.filePos = sampleFileOffset;

		if (atrac->second_.size != 0) {
			// TODO: We have a second buffer.  Within it, minWriteBytes should be zero.
			// The filePos should be after the end of the second buffer (or zero.)
			// We actually need to ensure we READ from the second buffer before implementing that.
		}
	}

	// It seems like this is always the same as the first buffer's pos, weirdly.
	bufferInfo->second.writePosPtr = atrac->first_.addr;
	// Reset never needs a second buffer write, since the loop is in a fixed place.
	bufferInfo->second.writableBytes = 0;
	bufferInfo->second.minWriteBytes = 0;
	bufferInfo->second.filePos = 0;
}

// Obtains information about what needs to be in the buffer to seek (or "reset")
// Generally called by games right before calling sceAtracResetPlayPosition().
static u32 sceAtracGetBufferInfoForResetting(int atracID, int sample, u32 bufferInfoAddr) {
	auto bufferInfo = PSPPointer<AtracResetBufferInfo>::Create(bufferInfoAddr);

	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	if (!bufferInfo.IsValid()) {
		return hleReportError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid buffer, should crash");
	} else if (atrac->bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && atrac->second_.size == 0) {
		return hleReportError(ME, ATRAC_ERROR_SECOND_BUFFER_NEEDED, "no second buffer");
	} else if ((u32)sample + atrac->firstSampleOffset_ > (u32)atrac->endSample_ + atrac->firstSampleOffset_) {
		return hleLogWarning(ME, ATRAC_ERROR_BAD_SAMPLE, "invalid sample position");
	} else {
		AtracGetResetBufferInfo(atrac, bufferInfo, sample);

		return hleLogSuccessInfoI(ME, 0);
	}
}

static u32 sceAtracGetBitrate(int atracID, u32 outBitrateAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetBitrate(%i, %08x): bad atrac ID", atracID, outBitrateAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracGetBitrate(%i, %08x): no data", atracID, outBitrateAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		atrac->bitrate_ = (atrac->bytesPerFrame_ * 352800) / 1000;
		if (atrac->codecType_ == PSP_MODE_AT_3_PLUS)
			atrac->bitrate_ = ((atrac->bitrate_ >> 11) + 8) & 0xFFFFFFF0;
		else
			atrac->bitrate_ = (atrac->bitrate_ + 511) >> 10;
		if (Memory::IsValidAddress(outBitrateAddr)) {
			Memory::Write_U32(atrac->bitrate_, outBitrateAddr);
			DEBUG_LOG(ME, "sceAtracGetBitrate(%i, %08x[%d])", atracID, outBitrateAddr, atrac->bitrate_);
		}
		else
			DEBUG_LOG_REPORT(ME, "sceAtracGetBitrate(%i, %08x[%d]) invalid address", atracID, outBitrateAddr, atrac->bitrate_);
	}
	return 0;
}

static u32 sceAtracGetChannel(int atracID, u32 channelAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetChannel(%i, %08x): bad atrac ID", atracID, channelAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracGetChannel(%i, %08x): no data", atracID, channelAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (Memory::IsValidAddress(channelAddr)){
			Memory::Write_U32(atrac->channels_, channelAddr);
			DEBUG_LOG(ME, "sceAtracGetChannel(%i, %08x[%d])", atracID, channelAddr, atrac->channels_);
		}
		else
			DEBUG_LOG_REPORT(ME, "sceAtracGetChannel(%i, %08x[%d]) invalid address", atracID, channelAddr, atrac->channels_);
	}
	return 0;
}

static u32 sceAtracGetLoopStatus(int atracID, u32 loopNumAddr, u32 statusAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetLoopStatus(%i, %08x, %08x): bad atrac ID", atracID, loopNumAddr, statusAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracGetLoopStatus(%i, %08x, %08x): no data", atracID, loopNumAddr, statusAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetLoopStatus(%i, %08x, %08x)", atracID, loopNumAddr, statusAddr);
		if (Memory::IsValidAddress(loopNumAddr))
			Memory::Write_U32(atrac->loopNum_, loopNumAddr);
		// return audio's loopinfo in at3 file
		if (Memory::IsValidAddress(statusAddr)) {
			if (atrac->loopinfo_.size() > 0)
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
	} else if (!atrac->dataBuf_) {
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
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracGetMaxSample(%i, %08x): no data", atracID, maxSamplesAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetMaxSample(%i, %08x)", atracID, maxSamplesAddr);
		if (Memory::IsValidAddress(maxSamplesAddr)) {
			Memory::Write_U32(atrac->SamplesPerFrame(), maxSamplesAddr);
		}
	}
	return 0;
}

static u32 sceAtracGetNextDecodePosition(int atracID, u32 outposAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetNextDecodePosition(%i, %08x): bad atrac ID", atracID, outposAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracGetNextDecodePosition(%i, %08x): no data", atracID, outposAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetNextDecodePosition(%i, %08x)", atracID, outposAddr);
		if (atrac->currentSample_ >= atrac->endSample_) {
			if (Memory::IsValidAddress(outposAddr))
				Memory::Write_U32(0, outposAddr);
			return ATRAC_ERROR_ALL_DATA_DECODED;
		} else {
			if (Memory::IsValidAddress(outposAddr))
			Memory::Write_U32(atrac->currentSample_, outposAddr);
		}
	}
	return 0;
}

static u32 sceAtracGetNextSample(int atracID, u32 outNAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "sceAtracGetNextSample(%i, %08x): bad atrac ID", atracID, outNAddr);
		return ATRAC_ERROR_BAD_ATRACID;
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracGetNextSample(%i, %08x): no data", atracID, outNAddr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (atrac->currentSample_ >= atrac->endSample_) {
			if (Memory::IsValidAddress(outNAddr))
				Memory::Write_U32(0, outNAddr);
			DEBUG_LOG(ME, "sceAtracGetNextSample(%i, %08x): 0 samples left", atracID, outNAddr);
			return 0;
		} else {
			// It seems like the PSP aligns the sample position to 0x800...?
			u32 skipSamples = atrac->firstSampleOffset_ + atrac->FirstOffsetExtra();
			u32 firstSamples = (atrac->SamplesPerFrame() - skipSamples) % atrac->SamplesPerFrame();
			u32 numSamples = atrac->endSample_ + 1 - atrac->currentSample_;
			if (atrac->currentSample_ == 0 && firstSamples != 0) {
				numSamples = firstSamples;
			}
			u32 unalignedSamples = (skipSamples + atrac->currentSample_) % atrac->SamplesPerFrame();
			if (unalignedSamples != 0) {
				// We're off alignment, possibly due to a loop.  Force it back on.
				numSamples = atrac->SamplesPerFrame() - unalignedSamples;
			}
			if (numSamples > atrac->SamplesPerFrame())
				numSamples = atrac->SamplesPerFrame();
			if (Memory::IsValidAddress(outNAddr))
				Memory::Write_U32(numSamples, outNAddr);
			DEBUG_LOG(ME, "sceAtracGetNextSample(%i, %08x): %d samples left", atracID, outNAddr, numSamples);
		}
	}
	return 0;
}

// Obtains the number of frames remaining in the buffer which can be decoded.
// When no more data would be needed, this returns a negative number.
static u32 sceAtracGetRemainFrame(int atracID, u32 remainAddr) {
	auto remainingFrames = PSPPointer<u32_le>::Create(remainAddr);

	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	if (!remainingFrames.IsValid()) {
		// Would crash.
		return hleReportError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid remainingFrames pointer");
	}

	*remainingFrames = atrac->RemainingFrames();
	return hleLogSuccessI(ME, 0);
}

static u32 sceAtracGetSecondBufferInfo(int atracID, u32 fileOffsetAddr, u32 desiredSizeAddr) {
	auto fileOffset = PSPPointer<u32_le>::Create(fileOffsetAddr);
	auto desiredSize = PSPPointer<u32_le>::Create(desiredSizeAddr);

	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	if (!fileOffset.IsValid() || !desiredSize.IsValid()) {
		// Would crash.
		return hleReportError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid addresses");
	}

	if (atrac->bufferState_ != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		// Writes zeroes in this error case.
		*fileOffset = 0;
		*desiredSize = 0;
		return hleLogWarning(ME, ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED, "not needed");
	}

	*fileOffset = atrac->FileOffsetBySample(atrac->loopEndSample_ - atrac->firstSampleOffset_);
	*desiredSize = atrac->first_.filesize - *fileOffset;

	return hleLogSuccessI(ME, 0);
}

static u32 sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr) {
	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	auto outEndSample = PSPPointer<u32_le>::Create(outEndSampleAddr);
	if (outEndSample.IsValid())
		*outEndSample = atrac->endSample_;
	auto outLoopStart = PSPPointer<u32_le>::Create(outLoopStartSampleAddr);
	if (outLoopStart.IsValid())
		*outLoopStart = atrac->loopStartSample_ == -1 ? -1 : atrac->loopStartSample_ - atrac->firstSampleOffset_ - atrac->FirstOffsetExtra();
	auto outLoopEnd = PSPPointer<u32_le>::Create(outLoopEndSampleAddr);
	if (outLoopEnd.IsValid())
		*outLoopEnd = atrac->loopEndSample_ == -1 ? -1 : atrac->loopEndSample_ - atrac->firstSampleOffset_ - atrac->FirstOffsetExtra();

	if (!outEndSample.IsValid() || !outLoopStart.IsValid() || !outLoopEnd.IsValid()) {
		return hleReportError(ME, 0, "invalid address");
	}
	return hleLogSuccessI(ME, 0);
}

// Games call this function to get some info for add more stream data,
// such as where the data read from, where the data add to,
// and how many bytes are allowed to add.
static u32 sceAtracGetStreamDataInfo(int atracID, u32 writePtrAddr, u32 writableBytesAddr, u32 readOffsetAddr) {
	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	u32 readOffset;
	atrac->CalculateStreamInfo(&readOffset);

	if (Memory::IsValidAddress(writePtrAddr))
		Memory::Write_U32(atrac->first_.addr + atrac->first_.offset, writePtrAddr);
	if (Memory::IsValidAddress(writableBytesAddr))
		Memory::Write_U32(atrac->first_.writableBytes, writableBytesAddr);
	if (Memory::IsValidAddress(readOffsetAddr))
		Memory::Write_U32(readOffset, readOffsetAddr);

	return hleLogSuccessI(ME, 0);
}

static u32 sceAtracReleaseAtracID(int atracID) {
	int result = deleteAtrac(atracID);
	if (result < 0) {
		return hleLogError(ME, result, "did not exist");
	}
	return hleLogSuccessInfoI(ME, result);
}

// This is called when a game wants to seek (or "reset") to a specific position in the audio data.
// Normally, sceAtracGetBufferInfoForResetting() is called to determine how to buffer.
// The game must add sufficient packets to the buffer in order to complete the seek.
static u32 sceAtracResetPlayPosition(int atracID, int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	if (atrac->bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && atrac->second_.size == 0) {
		return hleReportError(ME, ATRAC_ERROR_SECOND_BUFFER_NEEDED, "no second buffer");
	} else if ((u32)sample + atrac->firstSampleOffset_ > (u32)atrac->endSample_ + atrac->firstSampleOffset_) {
		return hleLogWarning(ME, ATRAC_ERROR_BAD_SAMPLE, "invalid sample position");
	} else {
		// Reuse the same calculation as before.
		AtracResetBufferInfo bufferInfo;
		AtracGetResetBufferInfo(atrac, &bufferInfo, sample);

		if ((u32)bytesWrittenFirstBuf < bufferInfo.first.minWriteBytes || (u32)bytesWrittenFirstBuf > bufferInfo.first.writableBytes) {
			return hleLogError(ME, ATRAC_ERROR_BAD_FIRST_RESET_SIZE, "first byte count not in valid range");
		}
		if ((u32)bytesWrittenSecondBuf < bufferInfo.second.minWriteBytes || (u32)bytesWrittenSecondBuf > bufferInfo.second.writableBytes) {
			return hleLogError(ME, ATRAC_ERROR_BAD_SECOND_RESET_SIZE, "second byte count not in valid range");
		}

		if (atrac->bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
			// Always adds zero bytes.
		} else if (atrac->bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
			// Okay, it's a valid number of bytes.  Let's set them up.
			if (bytesWrittenFirstBuf != 0) {
				if (!atrac->ignoreDataBuf_) {
					Memory::Memcpy(atrac->dataBuf_ + atrac->first_.size, atrac->first_.addr + atrac->first_.size, bytesWrittenFirstBuf, "AtracResetPlayPosition");
				}
				atrac->first_.fileoffset += bytesWrittenFirstBuf;
				atrac->first_.size += bytesWrittenFirstBuf;
				atrac->first_.offset += bytesWrittenFirstBuf;
			}

			// Did we transition to a full buffer?
			if (atrac->first_.size >= atrac->first_.filesize) {
				atrac->first_.size = atrac->first_.filesize;
				if (atrac->bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER)
					atrac->bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
			}
		} else {
			if (bufferInfo.first.filePos > atrac->first_.filesize) {
				return hleDelayResult(hleLogError(ME, ATRAC_ERROR_API_FAIL, "invalid file position"), "reset play pos", 200);
			}

			// Move the offset to the specified position.
			atrac->first_.fileoffset = bufferInfo.first.filePos;

			if (bytesWrittenFirstBuf != 0) {
				if (!atrac->ignoreDataBuf_) {
					Memory::Memcpy(atrac->dataBuf_ + atrac->first_.fileoffset, atrac->first_.addr, bytesWrittenFirstBuf, "AtracResetPlayPosition");
				}
				atrac->first_.fileoffset += bytesWrittenFirstBuf;
			}
			atrac->first_.size = atrac->first_.fileoffset;
			atrac->first_.offset = bytesWrittenFirstBuf;

			atrac->bufferHeaderSize_ = 0;
			atrac->bufferPos_ = atrac->bytesPerFrame_;
			atrac->bufferValidBytes_ = bytesWrittenFirstBuf - atrac->bufferPos_;
		}

		if (atrac->codecType_ == PSP_MODE_AT_3 || atrac->codecType_ == PSP_MODE_AT_3_PLUS) {
			atrac->SeekToSample(sample);
		}

		if (atrac->context_.IsValid()) {
			_AtracGenerateContext(atrac);
		}

		return hleDelayResult(hleLogSuccessInfoI(ME, 0), "reset play pos", 3000);
	}
}

#ifdef USE_FFMPEG
static int __AtracUpdateOutputMode(Atrac *atrac, int wanted_channels) {
	if (atrac->swrCtx_ && atrac->outputChannels_ == wanted_channels)
		return 0;
	atrac->outputChannels_ = wanted_channels;
	int64_t wanted_channel_layout = av_get_default_channel_layout(wanted_channels);
	int64_t dec_channel_layout = av_get_default_channel_layout(atrac->channels_);

	atrac->swrCtx_ =
		swr_alloc_set_opts
		(
			atrac->swrCtx_,
			wanted_channel_layout,
			AV_SAMPLE_FMT_S16,
			atrac->codecCtx_->sample_rate,
			dec_channel_layout,
			atrac->codecCtx_->sample_fmt,
			atrac->codecCtx_->sample_rate,
			0,
			NULL
		);
	if (!atrac->swrCtx_) {
		ERROR_LOG(ME, "swr_alloc_set_opts: Could not allocate resampler context");
		return -1;
	}
	if (swr_init(atrac->swrCtx_) < 0) {
		ERROR_LOG(ME, "swr_init: Failed to initialize the resampling context");
		return -1;
	}
	return 0;
}
#endif // USE_FFMPEG

int __AtracSetContext(Atrac *atrac) {
#ifdef USE_FFMPEG
	InitFFmpeg();

	AVCodecID ff_codec;
	if (atrac->codecType_ == PSP_MODE_AT_3) {
		ff_codec = AV_CODEC_ID_ATRAC3;
	} else if (atrac->codecType_ == PSP_MODE_AT_3_PLUS) {
		ff_codec = AV_CODEC_ID_ATRAC3P;
	} else {
		return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "unknown codec type in set context");
	}

	const AVCodec *codec = avcodec_find_decoder(ff_codec);
	atrac->codecCtx_ = avcodec_alloc_context3(codec);

	if (atrac->codecType_ == PSP_MODE_AT_3) {
		// For ATRAC3, we need the "extradata" in the RIFF header.
		atrac->codecCtx_->extradata = (uint8_t *)av_mallocz(14);
		atrac->codecCtx_->extradata_size = 14;

		// We don't pull this from the RIFF so that we can support OMA also.
		// The only thing that changes are the jointStereo_ values.
		atrac->codecCtx_->extradata[0] = 1;
		atrac->codecCtx_->extradata[3] = atrac->channels_ << 3;
		atrac->codecCtx_->extradata[6] = atrac->jointStereo_;
		atrac->codecCtx_->extradata[8] = atrac->jointStereo_;
		atrac->codecCtx_->extradata[10] = 1;
	}

	// Appears we need to force mono in some cases. (See CPkmn's comments in issue #4248)
	if (atrac->channels_ == 1) {
		atrac->codecCtx_->channels = 1;
		atrac->codecCtx_->channel_layout = AV_CH_LAYOUT_MONO;
	} else if (atrac->channels_ == 2) {
		atrac->codecCtx_->channels = 2;
		atrac->codecCtx_->channel_layout = AV_CH_LAYOUT_STEREO;
	} else {
		return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "unknown channel layout in set context");
	}

	// Explicitly set the block_align value (needed by newer FFmpeg versions, see #5772.)
	if (atrac->codecCtx_->block_align == 0) {
		atrac->codecCtx_->block_align = atrac->bytesPerFrame_;
	}
	// Only one supported, it seems?
	atrac->codecCtx_->sample_rate = 44100;

	atrac->codecCtx_->request_sample_fmt = AV_SAMPLE_FMT_S16;
	int ret;
	if ((ret = avcodec_open2(atrac->codecCtx_, codec, nullptr)) < 0) {
		// This can mean that the frame size is wrong or etc.
		return hleLogError(ME, ATRAC_ERROR_BAD_CODEC_PARAMS, "failed to open decoder %d", ret);
	}

	if ((ret = __AtracUpdateOutputMode(atrac, atrac->outputChannels_)) < 0)
		return hleLogError(ME, ret, "failed to set the output mode");

	// alloc audio frame
	atrac->frame_ = av_frame_alloc();
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
	atrac->packet_ = av_packet_alloc();
#else
	atrac->packet_ = new AVPacket;
	av_init_packet(atrac->packet_);
	atrac->packet_->data = nullptr;
	atrac->packet_->size = 0;
#endif
	// reinit decodePos, because ffmpeg had changed it.
	atrac->decodePos_ = 0;
#endif

	return 0;
}

static int _AtracSetData(Atrac *atrac, u32 buffer, u32 readSize, u32 bufferSize, int successCode = 0) {
	atrac->first_.addr = buffer;
	atrac->first_.size = readSize;

	if (atrac->first_.size > atrac->first_.filesize)
		atrac->first_.size = atrac->first_.filesize;
	atrac->first_.fileoffset = atrac->first_.size;

	// got the size of temp buf, and calculate offset
	atrac->bufferMaxSize_ = bufferSize;
	atrac->first_.offset = atrac->first_.size;

	// some games may reuse an atracID for playing sound
	atrac->ResetData();
	atrac->SetBufferState();

	if (atrac->codecType_ != PSP_MODE_AT_3 && atrac->codecType_ != PSP_MODE_AT_3_PLUS) {
		// Shouldn't have gotten here, Analyze() checks this.
		atrac->bufferState_ = ATRAC_STATUS_NO_DATA;
		return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "unexpected codec type in set data");
	}

	if (atrac->bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED || atrac->bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// This says, don't use the dataBuf_ array, use the PSP RAM.
		// This way, games can load data async into the buffer, and it still works.
		// TODO: Support this always, even for streaming.
		atrac->ignoreDataBuf_ = true;
	}
	if (atrac->bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP || atrac->bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END || atrac->bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		atrac->bufferHeaderSize_ = atrac->dataOff_;
		atrac->bufferPos_ = atrac->dataOff_ + atrac->bytesPerFrame_;
		atrac->bufferValidBytes_ = atrac->first_.size - atrac->bufferPos_;
	}

	const char *codecName = atrac->codecType_ == PSP_MODE_AT_3 ? "atrac3" : "atrac3+";
	const char *channelName = atrac->channels_ == 1 ? "mono" : "stereo";

	atrac->dataBuf_ = new u8[atrac->first_.filesize];
	if (!atrac->ignoreDataBuf_) {
		u32 copybytes = std::min(bufferSize, atrac->first_.filesize);
		Memory::Memcpy(atrac->dataBuf_, buffer, copybytes, "AtracSetData");
	}
	int ret = __AtracSetContext(atrac);
	if (ret < 0) {
		// Already logged.
		return ret;
	}

	return hleLogSuccessInfoI(ME, successCode, "%s %s audio", codecName, channelName);
}

static int _AtracSetData(int atracID, u32 buffer, u32 readSize, u32 bufferSize, bool needReturnAtracID = false) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac)
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "invalid atrac ID");
	int ret = _AtracSetData(atrac, buffer, readSize, bufferSize, needReturnAtracID ? atracID : 0);
	// not sure the real delay time
	return hleDelayResult(ret, "atrac set data", 100);
}

static u32 sceAtracSetHalfwayBuffer(int atracID, u32 buffer, u32 readSize, u32 bufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	}
	if (readSize > bufferSize) {
		return hleLogError(ME, ATRAC_ERROR_INCORRECT_READ_SIZE, "read size too large");
	}

	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		// Already logged.
		return ret;
	}

	atrac->outputChannels_ = 2;
	return _AtracSetData(atracID, buffer, readSize, bufferSize);
}

static u32 sceAtracSetSecondBuffer(int atracID, u32 secondBuffer, u32 secondBufferSize) {
	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	u32 secondFileOffset = atrac->FileOffsetBySample(atrac->loopEndSample_ - atrac->firstSampleOffset_);
	u32 desiredSize = atrac->first_.filesize - secondFileOffset;

	// 3 seems to be the number of frames required to handle a loop.
	if (secondBufferSize < desiredSize && secondBufferSize < (u32)atrac->bytesPerFrame_ * 3) {
		return hleReportError(ME, ATRAC_ERROR_SIZE_TOO_SMALL, "too small");
	}
	if (atrac->bufferState_ != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		return hleReportError(ME, ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED, "not needed");
	}

	atrac->second_.addr = secondBuffer;
	atrac->second_.size = secondBufferSize;
	atrac->second_.fileoffset = secondFileOffset;

	return 0;
}

static u32 sceAtracSetData(int atracID, u32 buffer, u32 bufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	}

	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		// Already logged.
		return ret;
	}

	if (atrac->codecType_ != atracIDTypes[atracID]) {
		// TODO: Should this not change the buffer size?
		return hleReportError(ME, ATRAC_ERROR_WRONG_CODECTYPE, "atracID uses different codec type than data");
	}

	atrac->outputChannels_ = 2;
	return _AtracSetData(atracID, buffer, bufferSize, bufferSize);
}

static int sceAtracSetDataAndGetID(u32 buffer, int bufferSize) {
	// A large value happens in Tales of VS, and isn't handled somewhere properly as a u32.
	// It's impossible for it to be that big anyway, so cap it.
	if (bufferSize < 0) {
		WARN_LOG(ME, "sceAtracSetDataAndGetID(%08x, %08x): negative bufferSize", buffer, bufferSize);
		bufferSize = 0x10000000;
	}

	Atrac *atrac = new Atrac();
	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		// Already logged.
		delete atrac;
		return ret;
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(ME, atracID, "no free ID");
	}

	atrac->outputChannels_ = 2;
	return _AtracSetData(atracID, buffer, bufferSize, bufferSize, true);
}

static int sceAtracSetHalfwayBufferAndGetID(u32 buffer, u32 readSize, u32 bufferSize) {
	if (readSize > bufferSize) {
		return hleLogError(ME, ATRAC_ERROR_INCORRECT_READ_SIZE, "read size too large");
	}
	Atrac *atrac = new Atrac();
	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		// Already logged.
		delete atrac;
		return ret;
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(ME, atracID, "no free ID");
	}

	atrac->outputChannels_ = 2;
	return _AtracSetData(atracID, buffer, readSize, bufferSize, true);
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
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracSetLoopNum(%i, %i): no data", atracID, loopNum);
		return ATRAC_ERROR_NO_DATA;
	} else {
		if (atrac->loopinfo_.size() == 0) {
			DEBUG_LOG(ME, "sceAtracSetLoopNum(%i, %i): error: no loop information", atracID, loopNum);
			return ATRAC_ERROR_NO_LOOP_INFORMATION;
		}
		// Spammed in MHU
		DEBUG_LOG(ME, "sceAtracSetLoopNum(%i, %i)", atracID, loopNum);
		atrac->loopNum_ = loopNum;
		if (loopNum != 0 && atrac->loopinfo_.size() == 0) {
			// Just loop the whole audio
			atrac->loopStartSample_ = atrac->firstSampleOffset_ + atrac->FirstOffsetExtra();
			atrac->loopEndSample_ = atrac->endSample_ + atrac->firstSampleOffset_ + atrac->FirstOffsetExtra();
		}
		if (atrac->context_.IsValid()) {
			_AtracGenerateContext(atrac);
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
	} else if (!atrac->dataBuf_) {
		ERROR_LOG(ME, "sceAtracGetOutputChannel(%i, %08x): no data", atracID, outputChanPtr);
		return ATRAC_ERROR_NO_DATA;
	} else {
		DEBUG_LOG(ME, "sceAtracGetOutputChannel(%i, %08x)", atracID, outputChanPtr);
		if (Memory::IsValidAddress(outputChanPtr))
			Memory::Write_U32(atrac->outputChannels_, outputChanPtr);
	}
	return 0;
}

static int sceAtracIsSecondBufferNeeded(int atracID) {
	Atrac *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		// Already logged.
		return err;
	}

	// Note that this returns true whether the buffer is already set or not.
	int needed = atrac->bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER ? 1 : 0;
	return hleLogSuccessI(ME, needed);
}

static int sceAtracSetMOutHalfwayBuffer(int atracID, u32 buffer, u32 readSize, u32 bufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	}
	if (readSize > bufferSize) {
		return hleLogError(ME, ATRAC_ERROR_INCORRECT_READ_SIZE, "read size too large");
	}

	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		// Already logged.
		return ret;
	}
	if (atrac->channels_ != 1) {
		// It seems it still sets the data.
		atrac->outputChannels_ = 2;
		_AtracSetData(atrac, buffer, readSize, bufferSize);
		return hleReportError(ME, ATRAC_ERROR_NOT_MONO, "not mono data");
	} else {
		atrac->outputChannels_ = 1;
		ret = _AtracSetData(atracID, buffer, readSize, bufferSize);
	}
	return ret;
}

// Note: This doesn't seem to be part of any available libatrac3plus library.
static u32 sceAtracSetMOutData(int atracID, u32 buffer, u32 bufferSize) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	}

	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		// Already logged.
		return ret;
	}
	if (atrac->channels_ != 1) {
		// It seems it still sets the data.
		atrac->outputChannels_ = 2;
		_AtracSetData(atrac, buffer, bufferSize, bufferSize);
		return hleReportError(ME, ATRAC_ERROR_NOT_MONO, "not mono data");
	} else {
		atrac->outputChannels_ = 1;
		ret = _AtracSetData(atracID, buffer, bufferSize, bufferSize);
	}
	return ret;
}

// Note: This doesn't seem to be part of any available libatrac3plus library.
static int sceAtracSetMOutDataAndGetID(u32 buffer, u32 bufferSize) {
	Atrac *atrac = new Atrac();
	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		// Already logged.
		delete atrac;
		return ret;
	}
	if (atrac->channels_ != 1) {
		delete atrac;
		return hleReportError(ME, ATRAC_ERROR_NOT_MONO, "not mono data");
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(ME, atracID, "no free ID");
	}

	atrac->outputChannels_ = 1;
	return _AtracSetData(atracID, buffer, bufferSize, bufferSize, true);
}

static int sceAtracSetMOutHalfwayBufferAndGetID(u32 buffer, u32 readSize, u32 bufferSize) {
	if (readSize > bufferSize) {
		return hleLogError(ME, ATRAC_ERROR_INCORRECT_READ_SIZE, "read size too large");
	}
	Atrac *atrac = new Atrac();
	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		// Already logged.
		delete atrac;
		return ret;
	}
	if (atrac->channels_ != 1) {
		delete atrac;
		return hleReportError(ME, ATRAC_ERROR_NOT_MONO, "not mono data");
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(ME, atracID, "no free ID");
	}

	atrac->outputChannels_ = 1;
	return _AtracSetData(atracID, buffer, readSize, bufferSize, true);
}

static int sceAtracSetAA3DataAndGetID(u32 buffer, u32 bufferSize, u32 fileSize, u32 metadataSizeAddr) {
	Atrac *atrac = new Atrac();
	int ret = atrac->AnalyzeAA3(buffer, bufferSize, fileSize);
	if (ret < 0) {
		// Already logged.
		delete atrac;
		return ret;
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(ME, atracID, "no free ID");
	}

	atrac->outputChannels_ = 2;
	return _AtracSetData(atracID, buffer, bufferSize, bufferSize, true);
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

void _AtracGenerateContext(Atrac *atrac) {
	SceAtracId *context = atrac->context_;
	context->info.buffer = atrac->first_.addr;
	context->info.bufferByte = atrac->bufferMaxSize_;
	context->info.secondBuffer = atrac->second_.addr;
	context->info.secondBufferByte = atrac->second_.size;
	context->info.codec = atrac->codecType_;
	context->info.loopNum = atrac->loopNum_;
	context->info.loopStart = atrac->loopStartSample_ > 0 ? atrac->loopStartSample_ : 0;
	context->info.loopEnd = atrac->loopEndSample_ > 0 ? atrac->loopEndSample_ : 0;

	// Note that we read in the state when loading the atrac object, so it's safe
	// to update it back here all the time.  Some games, like Sol Trigger, change it.
	// TODO: Should we just keep this in PSP ram then, or something?
	context->info.state = atrac->bufferState_;
	if (atrac->firstSampleOffset_ != 0) {
		context->info.samplesPerChan = atrac->firstSampleOffset_ + atrac->FirstOffsetExtra();
	} else {
		context->info.samplesPerChan = (atrac->codecType_ == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
	}
	context->info.sampleSize = atrac->bytesPerFrame_;
	context->info.numChan = atrac->channels_;
	context->info.dataOff = atrac->dataOff_;
	context->info.endSample = atrac->endSample_ + atrac->firstSampleOffset_ + atrac->FirstOffsetExtra();
	context->info.dataEnd = atrac->first_.filesize;
	context->info.curOff = atrac->first_.fileoffset;
	context->info.decodePos = atrac->DecodePosBySample(atrac->currentSample_);
	context->info.streamDataByte = atrac->first_.size - atrac->dataOff_;

	u8 *buf = (u8 *)context;
	*(u32_le *)(buf + 0xfc) = atrac->atracID_;

	NotifyMemInfo(MemBlockFlags::WRITE, atrac->context_.ptr, sizeof(SceAtracId), "AtracContext");
}

static u32 _sceAtracGetContextAddress(int atracID) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		ERROR_LOG(ME, "_sceAtracGetContextAddress(%i): bad atrac id", atracID);
		return 0;
	}
	if (!atrac->context_.IsValid()) {
		// allocate a new context_
		u32 contextsize = 256;
		atrac->context_ = kernelMemory.Alloc(contextsize, false, "Atrac Context");
		if (atrac->context_.IsValid())
			Memory::Memset(atrac->context_.ptr, 0, 256, "AtracContextClear");

		WARN_LOG(ME, "%08x=_sceAtracGetContextAddress(%i): allocated new context", atrac->context_.ptr, atracID);
	}
	else
		WARN_LOG(ME, "%08x=_sceAtracGetContextAddress(%i)", atrac->context_.ptr, atracID);
	if (atrac->context_.IsValid())
		_AtracGenerateContext(atrac);
	return atrac->context_.ptr;
}

struct At3HeaderMap {
	u16 bytes;
	u16 channels;
	u8 jointStereo;

	bool Matches(const Atrac *at) const {
		return bytes == at->bytesPerFrame_ && channels == at->channels_;
	}
};

// These should represent all possible supported bitrates (66, 104, and 132 for stereo.)
static const At3HeaderMap at3HeaderMap[] = {
	{ 0x00C0, 1, 0 }, // 132/2 (66) kbps mono
	{ 0x0098, 1, 0 }, // 105/2 (52.5) kbps mono
	{ 0x0180, 2, 0 }, // 132 kbps stereo
	{ 0x0130, 2, 0 }, // 105 kbps stereo
	// At this size, stereo can only use joint stereo.
	{ 0x00C0, 2, 1 }, // 66 kbps stereo
};

static bool initAT3Decoder(Atrac *atrac) {
	atrac->bitrate_ = (atrac->bytesPerFrame_ * 352800) / 1000;
	atrac->bitrate_ = (atrac->bitrate_ + 511) >> 10;
	atrac->jointStereo_ = false;

	// See if we can match the actual jointStereo value.
	for (size_t i = 0; i < ARRAY_SIZE(at3HeaderMap); ++i) {
		if (at3HeaderMap[i].Matches(atrac)) {
			atrac->jointStereo_ = at3HeaderMap[i].jointStereo;
			return true;
		}
	}
	return false;
}

static void initAT3plusDecoder(Atrac *atrac) {
	atrac->bitrate_ = (atrac->bytesPerFrame_ * 352800) / 1000;
	atrac->bitrate_ = ((atrac->bitrate_ >> 11) + 8) & 0xFFFFFFF0;
	atrac->jointStereo_ = false;
}

static int sceAtracLowLevelInitDecoder(int atracID, u32 paramsAddr) {
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	}

	if (atrac->codecType_ != PSP_MODE_AT_3 && atrac->codecType_ != PSP_MODE_AT_3_PLUS) {
		// TODO: Error code?  Was silently 0 before, and just didn't work.  Shouldn't ever happen...
		return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "bad codec type");
	}

	if (!Memory::IsValidAddress(paramsAddr)) {
		// TODO: Returning zero as code was before.  Needs testing.
		return hleReportError(ME, 0, "invalid pointers");
	}

	atrac->channels_ = Memory::Read_U32(paramsAddr);
	atrac->outputChannels_ = Memory::Read_U32(paramsAddr + 4);
	atrac->bufferMaxSize_ = Memory::Read_U32(paramsAddr + 8);
	atrac->bytesPerFrame_ = atrac->bufferMaxSize_;
	atrac->first_.writableBytes = atrac->bytesPerFrame_;
	atrac->ResetData();

	const char *codecName = atrac->codecType_ == PSP_MODE_AT_3 ? "atrac3" : "atrac3+";
	const char *channelName = atrac->channels_ == 1 ? "mono" : "stereo";

	if (atrac->codecType_ == PSP_MODE_AT_3) {
		if (!initAT3Decoder(atrac)) {
			ERROR_LOG_REPORT(ME, "AT3 header map lacks entry for bpf: %i  channels: %i", atrac->bytesPerFrame_, atrac->channels_);
			// TODO: Should we return an error code for these values?
		}
	} else if (atrac->codecType_ == PSP_MODE_AT_3_PLUS) {
		initAT3plusDecoder(atrac);
	}

	atrac->dataOff_ = 0;
	atrac->first_.size = 0;
	atrac->first_.filesize = atrac->bytesPerFrame_;
	atrac->bufferState_ = ATRAC_STATUS_LOW_LEVEL;
	atrac->dataBuf_ = new u8[atrac->first_.filesize];
	atrac->currentSample_ = 0;
	int ret = __AtracSetContext(atrac);

	if (atrac->context_.IsValid()) {
		_AtracGenerateContext(atrac);
	}

	if (ret < 0) {
		// Already logged.
		return ret;
	}
	return hleLogSuccessInfoI(ME, ret, "%s %s audio", codecName, channelName);
}

static int sceAtracLowLevelDecode(int atracID, u32 sourceAddr, u32 sourceBytesConsumedAddr, u32 samplesAddr, u32 sampleBytesAddr) {
	auto srcp = PSPPointer<u8>::Create(sourceAddr);
	auto srcConsumed = PSPPointer<u32_le>::Create(sourceBytesConsumedAddr);
	auto outp = PSPPointer<u8>::Create(samplesAddr);
	auto outWritten = PSPPointer<u32_le>::Create(sampleBytesAddr);

	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(ME, ATRAC_ERROR_BAD_ATRACID, "bad atrac ID");
	}

	if (!srcp.IsValid() || !srcConsumed.IsValid() || !outp.IsValid() || !outWritten.IsValid()) {
		// TODO: Returning zero as code was before.  Needs testing.
		return hleReportError(ME, 0, "invalid pointers");
	}

	int numSamples = (atrac->codecType_ == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);

	if (!atrac->failedDecode_) {
		atrac->FillLowLevelPacket(srcp);

		AtracDecodeResult res = atrac->DecodePacket();
		if (res == ATDECODE_GOTFRAME) {
#ifdef USE_FFMPEG
			// got a frame
			numSamples = atrac->frame_->nb_samples;

			u8 *out = outp;
			int avret = swr_convert(atrac->swrCtx_, &out, numSamples,
				(const u8**)atrac->frame_->extended_data, numSamples);
			u32 outBytes = numSamples * atrac->outputChannels_ * sizeof(s16);
			NotifyMemInfo(MemBlockFlags::WRITE, samplesAddr, outBytes, "AtracLowLevelDecode");
			if (avret < 0) {
				ERROR_LOG(ME, "swr_convert: Error while converting %d", avret);
			}
#endif // USE_FFMPEG
		} else {
			// TODO: Error code otherwise?
		}
	}

	*outWritten = numSamples * atrac->outputChannels_ * sizeof(s16);
	*srcConsumed = atrac->bytesPerFrame_;

	return hleLogDebug(ME, hleDelayResult(0, "low level atrac decode data", atracDecodeDelay));
}

static int sceAtracSetAA3HalfwayBufferAndGetID(u32 buffer, u32 readSize, u32 bufferSize, u32 fileSize) {
	if (readSize > bufferSize) {
		return hleLogError(ME, ATRAC_ERROR_INCORRECT_READ_SIZE, "read size too large");
	}

	Atrac *atrac = new Atrac();
	int ret = atrac->AnalyzeAA3(buffer, readSize, fileSize);
	if (ret < 0) {
		// Already logged.
		delete atrac;
		return ret;
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(ME, atracID, "no free ID");
	}

	atrac->outputChannels_ = 2;
	return _AtracSetData(atracID, buffer, readSize, bufferSize, true);
}

const HLEFunction sceAtrac3plus[] = {
	{0X7DB31251, &WrapU_IU<sceAtracAddStreamData>,                 "sceAtracAddStreamData",                'x', "ix"   },
	{0X6A8C3CD5, &WrapU_IUUUU<sceAtracDecodeData>,                 "sceAtracDecodeData",                   'x', "ixppp"},
	{0XD5C28CC0, &WrapU_V<sceAtracEndEntry>,                       "sceAtracEndEntry",                     'x', ""     },
	{0X780F88D1, &WrapU_I<sceAtracGetAtracID>,                     "sceAtracGetAtracID",                   'i', "x"    },
	{0XCA3CA3D2, &WrapU_IIU<sceAtracGetBufferInfoForResetting>,    "sceAtracGetBufferInfoForReseting",     'x', "iix"  },
	{0XA554A158, &WrapU_IU<sceAtracGetBitrate>,                    "sceAtracGetBitrate",                   'x', "ip"   },
	{0X31668BAA, &WrapU_IU<sceAtracGetChannel>,                    "sceAtracGetChannel",                   'x', "ip"   },
	{0XFAA4F89B, &WrapU_IUU<sceAtracGetLoopStatus>,                "sceAtracGetLoopStatus",                'x', "ipp"  },
	{0XE88F759B, &WrapU_IU<sceAtracGetInternalErrorInfo>,          "sceAtracGetInternalErrorInfo",         'x', "ip"   },
	{0XD6A5F2F7, &WrapU_IU<sceAtracGetMaxSample>,                  "sceAtracGetMaxSample",                 'x', "ip"   },
	{0XE23E3A35, &WrapU_IU<sceAtracGetNextDecodePosition>,         "sceAtracGetNextDecodePosition",        'x', "ip"   },
	{0X36FAABFB, &WrapU_IU<sceAtracGetNextSample>,                 "sceAtracGetNextSample",                'x', "ip"   },
	{0X9AE849A7, &WrapU_IU<sceAtracGetRemainFrame>,                "sceAtracGetRemainFrame",               'x', "ip"   },
	{0X83E85EA0, &WrapU_IUU<sceAtracGetSecondBufferInfo>,          "sceAtracGetSecondBufferInfo",          'x', "ipp"  },
	{0XA2BBA8BE, &WrapU_IUUU<sceAtracGetSoundSample>,              "sceAtracGetSoundSample",               'x', "ippp" },
	{0X5D268707, &WrapU_IUUU<sceAtracGetStreamDataInfo>,           "sceAtracGetStreamDataInfo",            'x', "ippp" },
	{0X61EB33F5, &WrapU_I<sceAtracReleaseAtracID>,                 "sceAtracReleaseAtracID",               'x', "i"    },
	{0X644E5607, &WrapU_IIII<sceAtracResetPlayPosition>,           "sceAtracResetPlayPosition",            'x', "iiii" },
	{0X3F6E26B5, &WrapU_IUUU<sceAtracSetHalfwayBuffer>,            "sceAtracSetHalfwayBuffer",             'x', "ixxx" },
	{0X83BF7AFD, &WrapU_IUU<sceAtracSetSecondBuffer>,              "sceAtracSetSecondBuffer",              'x', "ixx"  },
	{0X0E2A73AB, &WrapU_IUU<sceAtracSetData>,                      "sceAtracSetData",                      'x', "ixx"  },
	{0X7A20E7AF, &WrapI_UI<sceAtracSetDataAndGetID>,               "sceAtracSetDataAndGetID",              'i', "xx"   },
	{0XD1F59FDB, &WrapU_V<sceAtracStartEntry>,                     "sceAtracStartEntry",                   'x', ""     },
	{0X868120B5, &WrapU_II<sceAtracSetLoopNum>,                    "sceAtracSetLoopNum",                   'x', "ii"   },
	{0X132F1ECA, &WrapI_II<sceAtracReinit>,                        "sceAtracReinit",                       'x', "ii"   },
	{0XECA32A99, &WrapI_I<sceAtracIsSecondBufferNeeded>,           "sceAtracIsSecondBufferNeeded",         'i', "i"    },
	{0X0FAE370E, &WrapI_UUU<sceAtracSetHalfwayBufferAndGetID>,     "sceAtracSetHalfwayBufferAndGetID",     'i', "xxx"  },
	{0X2DD3E298, &WrapU_IIU<sceAtracGetBufferInfoForResetting>,    "sceAtracGetBufferInfoForResetting",    'x', "iix"  },
	{0X5CF9D852, &WrapI_IUUU<sceAtracSetMOutHalfwayBuffer>,        "sceAtracSetMOutHalfwayBuffer",         'x', "ixxx" },
	{0XF6837A1A, &WrapU_IUU<sceAtracSetMOutData>,                  "sceAtracSetMOutData",                  'x', "ixx"  },
	{0X472E3825, &WrapI_UU<sceAtracSetMOutDataAndGetID>,           "sceAtracSetMOutDataAndGetID",          'i', "xx"   },
	{0X9CD7DE03, &WrapI_UUU<sceAtracSetMOutHalfwayBufferAndGetID>, "sceAtracSetMOutHalfwayBufferAndGetID", 'i', "xxx"  },
	{0XB3B5D042, &WrapI_IU<sceAtracGetOutputChannel>,              "sceAtracGetOutputChannel",             'x', "ip"   },
	{0X5622B7C1, &WrapI_UUUU<sceAtracSetAA3DataAndGetID>,          "sceAtracSetAA3DataAndGetID",           'i', "xxxp" },
	{0X5DD66588, &WrapI_UUUU<sceAtracSetAA3HalfwayBufferAndGetID>, "sceAtracSetAA3HalfwayBufferAndGetID",  'i', "xxxx" },
	{0X231FC6B7, &WrapU_I<_sceAtracGetContextAddress>,             "_sceAtracGetContextAddress",           'x', "i"    },
	{0X1575D64B, &WrapI_IU<sceAtracLowLevelInitDecoder>,           "sceAtracLowLevelInitDecoder",          'x', "ix"   },
	{0X0C116E1B, &WrapI_IUUUU<sceAtracLowLevelDecode>,             "sceAtracLowLevelDecode",               'x', "ixpxp"},
};

void Register_sceAtrac3plus() {
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
