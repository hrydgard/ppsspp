// Copyright (c) 2013- PPSSPP Project.

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

#include "Core/Config.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"

#ifdef USE_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}

#endif  // USE_FFMPEG

bool SimpleAudio::GetAudioCodecID(int audioType) {
#ifdef USE_FFMPEG
	switch (audioType) {
	case PSP_CODEC_AAC:
		audioCodecId = AV_CODEC_ID_AAC;
		break;
	case PSP_CODEC_AT3:
		audioCodecId = AV_CODEC_ID_ATRAC3;
		break;
	case PSP_CODEC_AT3PLUS:
		audioCodecId = AV_CODEC_ID_ATRAC3P;
		break;
	case PSP_CODEC_MP3:
		audioCodecId = AV_CODEC_ID_MP3;
		break;
	default:
		audioType = 0;
		break;
	}
	if (audioType != 0) {
		return true;
	}
	return false;
#else
	return false;
#endif // USE_FFMPEG
}

SimpleAudio::SimpleAudio(int audioType, int sample_rate, int channels)
: ctxPtr(0xFFFFFFFF), audioType(audioType), sample_rate_(sample_rate), channels_(channels), codec_(0), codecCtx_(0), swrCtx_(0), outSamples(0), srcPos(0), wanted_resample_freq(44100), extradata_(0) {
	Init();
}

void SimpleAudio::Init() {
#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
	InitFFmpeg();

	frame_ = av_frame_alloc();

	// Get Audio Codec ctx
	if (!GetAudioCodecID(audioType)){
		ERROR_LOG(ME, "This version of FFMPEG does not support Audio codec type: %08x. Update your submodule.", audioType);
		return;
	}
	// Find decoder
	codec_ = avcodec_find_decoder((AVCodecID)audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ctx for audio (%s). Update your submodule.", GetCodecName(audioType));
		return;
	}
	// Allocate codec context
	codecCtx_ = avcodec_alloc_context3(codec_);
	if (!codecCtx_) {
		ERROR_LOG(ME, "Failed to allocate a codec context");
		return;
	}
	codecCtx_->channels = channels_;
	codecCtx_->channel_layout = channels_ == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
	codecCtx_->sample_rate = sample_rate_;
	OpenCodec();
#endif  // USE_FFMPEG
}

bool SimpleAudio::OpenCodec() {
#ifdef USE_FFMPEG
	AVDictionary *opts = 0;
	int retval = avcodec_open2(codecCtx_, codec_, &opts);
	if (retval < 0) {
		ERROR_LOG(ME, "Failed to open codec: retval = %i", retval);
	}
	av_dict_free(&opts);
#endif  // USE_FFMPEG
	return retval >= 0;
}

bool SimpleAudio::ResetCodecCtx(int channels, int samplerate){
#ifdef USE_FFMPEG
	if (codecCtx_)
		avcodec_close(codecCtx_);

	// Find decoder
	codec_ = avcodec_find_decoder((AVCodecID)audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ctx for audio (%s). Update your submodule.", GetCodecName(audioType));
		return false;
	}

	codecCtx_->channels = channels;
	codecCtx_->channel_layout = channels==2?AV_CH_LAYOUT_STEREO:AV_CH_LAYOUT_MONO;
	codecCtx_->sample_rate = samplerate;
	OpenCodec();
	return true;
#endif
	return false;
}

void SimpleAudio::SetExtraData(u8 *data, int size, int wav_bytes_per_packet) {
	delete [] extradata_;
	extradata_ = 0;

	if (data != 0) {
		extradata_ = new u8[size];
		memcpy(extradata_, data, size);
	}

#ifdef USE_FFMPEG
	if (codecCtx_) {
		codecCtx_->extradata = extradata_;
		codecCtx_->extradata_size = size;
		codecCtx_->block_align = wav_bytes_per_packet;
		OpenCodec();
	}
#endif
}

SimpleAudio::~SimpleAudio() {
#ifdef USE_FFMPEG
	if (swrCtx_)
		swr_free(&swrCtx_);
	if (frame_)
		av_frame_free(&frame_);
	if (codecCtx_)
		avcodec_close(codecCtx_);
	frame_ = 0;
	codecCtx_ = 0;
	codec_ = 0;
#endif  // USE_FFMPEG
	delete [] extradata_;
	extradata_ = 0;
}

bool SimpleAudio::IsOK() const {
#ifdef USE_FFMPEG
	return codec_ != 0;
#else
	return 0;
#endif
}

void SaveAudio(const char filename[], uint8_t *outbuf, int size){
	FILE * pf;
	pf = fopen(filename, "ab+");

	fwrite(outbuf, size, 1, pf);
	fclose(pf);
}

bool SimpleAudio::Decode(void* inbuf, int inbytes, uint8_t *outbuf, int *outbytes) {
#ifdef USE_FFMPEG
	AVPacket packet;
	av_init_packet(&packet);
	packet.data = static_cast<uint8_t *>(inbuf);
	packet.size = inbytes;

	int got_frame = 0;
	av_frame_unref(frame_);

	*outbytes = 0;
	srcPos = 0;
	int len = avcodec_decode_audio4(codecCtx_, frame_, &got_frame, &packet);
	if (len < 0) {
		ERROR_LOG(ME, "Error decoding Audio frame (%i bytes): %i (%08x)", inbytes, len, len);
		// TODO: cleanup
		return false;
	}
	av_free_packet(&packet);
	
	// get bytes consumed in source
	srcPos = len;

	if (got_frame) {
		// Initializing the sample rate convert. We will use it to convert float output into int.
		int64_t wanted_channel_layout = AV_CH_LAYOUT_STEREO; // we want stereo output layout
		int64_t dec_channel_layout = frame_->channel_layout; // decoded channel layout

		if (!swrCtx_) {
			swrCtx_ = swr_alloc_set_opts(
				swrCtx_,
				wanted_channel_layout,
				AV_SAMPLE_FMT_S16,
				wanted_resample_freq,
				dec_channel_layout,
				codecCtx_->sample_fmt,
				codecCtx_->sample_rate,
				0,
				NULL);

			if (!swrCtx_ || swr_init(swrCtx_) < 0) {
				ERROR_LOG(ME, "swr_init: Failed to initialize the resampling context");
				avcodec_close(codecCtx_);
				codec_ = 0;
				return false;
			}
		}

		// convert audio to AV_SAMPLE_FMT_S16
		int swrRet = swr_convert(swrCtx_, &outbuf, frame_->nb_samples, (const u8 **)frame_->extended_data, frame_->nb_samples);
		if (swrRet < 0) {
			ERROR_LOG(ME, "swr_convert: Error while converting: %d", swrRet);
			return false;
		}
		// output samples per frame, we should *2 since we have two channels
		outSamples = swrRet * 2;

		// each sample occupies 2 bytes
		*outbytes = outSamples * 2;
		// We always convert to stereo.
		__AdjustBGMVolume((s16 *)outbuf, frame_->nb_samples * 2);

		// Save outbuf into pcm audio, you can uncomment this line to save and check the decoded audio into pcm file.
		// SaveAudio("dump.pcm", outbuf, *outbytes);
	}
	return true;
#else
	// Zero bytes output. No need to memset.
	*outbytes = 0;
	return true;
#endif  // USE_FFMPEG
}

int SimpleAudio::GetOutSamples(){
	return outSamples;
}

int SimpleAudio::GetSourcePos(){
	return srcPos;
}

void AudioClose(SimpleAudio **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}


static const char *const codecNames[4] = {
	"AT3+", "AT3", "MP3", "AAC",
};

const char *GetCodecName(int codec) {
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return codecNames[codec - PSP_CODEC_AT3PLUS];
	} else {
		return "(unk)";
	}
};

bool IsValidCodec(int codec){
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return true;
	}
	return false;
}


// sceAu module starts from here

AuCtx::AuCtx() {
	decoder = NULL;
	startPos = 0;
	endPos = 0;
	LoopNum = -1;
	AuBuf = 0;
	AuBufSize = 2048;
	PCMBuf = 0;
	PCMBufSize = 2048;
	AuBufAvailable = 0;
	SamplingRate = 44100;
	freq = SamplingRate;
	BitRate = 0;
	Channels = 2;
	Version = 0;
	SumDecodedSamples = 0;
	MaxOutputSample = 0;
	askedReadSize = 0;
	realReadSize = 0;
	audioType = 0;
	FrameNum = 0;
};

AuCtx::~AuCtx(){
	if (decoder){
		AudioClose(&decoder);
		decoder = NULL;
	}
};

// return output pcm size, <0 error
u32 AuCtx::AuDecode(u32 pcmAddr)
{
	if (!Memory::IsValidAddress(pcmAddr)){
		ERROR_LOG(ME, "%s: output bufferAddress %08x is invalctx", __FUNCTION__, pcmAddr);
		return -1;
	}

	auto outbuf = Memory::GetPointer(PCMBuf);
	memset(outbuf, 0, PCMBufSize); // important! empty outbuf to avoid noise
	u32 outpcmbufsize = 0;

	int repeat = 1;
	if (g_Config.bSoundSpeedHack){
		repeat = 2;
	}
	int i = 0;
	// decode frames in sourcebuff and output into PCMBuf (each time, we decode one or two frames)
	// some games as Miku like one frame each time, some games like DOA like two frames each time
	while (sourcebuff.size() > 0 && outpcmbufsize < PCMBufSize && i < repeat){
		i++;
		int pcmframesize;
		// decode
		decoder->Decode((void*)sourcebuff.c_str(), (int)sourcebuff.size(), outbuf, &pcmframesize);
		if (pcmframesize == 0){
			// no output pcm, we are at the end of the stream
			AuBufAvailable = 0;
			sourcebuff.clear();
			if (LoopNum != 0){
				// if we loop, reset readPos
				readPos = startPos;
			}
			break;
		}
		// count total output pcm size 
		outpcmbufsize += pcmframesize;
		// count total output samples
		SumDecodedSamples += decoder->GetOutSamples();
		// get consumed source length
		int srcPos = decoder->GetSourcePos();
		// remove the consumed source
		sourcebuff.erase(0, srcPos);
		// reduce the available Aubuff size
		// (the available buff size is now used to know if we can read again from file and how many to read)
		AuBufAvailable -= srcPos;
		// move outbuff position to the current end of output 
		outbuf += pcmframesize;
		// increase FrameNum count
		FrameNum++;
	}
	Memory::Write_U32(PCMBuf, pcmAddr);
	return outpcmbufsize;
}

u32 AuCtx::AuGetLoopNum()
{
	return LoopNum;
}

u32 AuCtx::AuSetLoopNum(int loop)
{
	LoopNum = loop;
	return 0;
}

// return 1 to read more data stream, 0 don't read
int AuCtx::AuCheckStreamDataNeeded()
{
	// if we have no available Au buffer, and the current read position in source file is not the end of stream, then we can read
	if (AuBufAvailable < (int)AuBufSize && readPos < (int)endPos){
		return 1;
	}
	return 0;
}

// check how many bytes we have read from source file
u32 AuCtx::AuNotifyAddStreamData(int size)
{
	realReadSize = size;
	int diffszie = realReadSize - askedReadSize;
	// Notify the real read size
	if (diffszie != 0){
		readPos += diffszie;
		AuBufAvailable += diffszie;
	}

	// append AuBuf into sourcebuff
	sourcebuff.append((const char*)Memory::GetPointer(AuBuf), size);

	if (readPos >= endPos && LoopNum != 0){
		// if we need loop, reset readPos
		readPos = startPos;
		// reset LoopNum
		if (LoopNum > 0){
			LoopNum--;
		}
	}

	return 0;
}

// read from stream position srcPos of size bytes into buff
// buff, size and srcPos are all pointers
u32 AuCtx::AuGetInfoToAddStreamData(u32 buff, u32 size, u32 srcPos)
{
	// you can not read beyond file size and the buffer size
	int readsize = std::min((int)AuBufSize - AuBufAvailable, (int)endPos - readPos);

	// we can recharge AuBuf from its beginning
	if (Memory::IsValidAddress(buff))
		Memory::Write_U32(AuBuf, buff);
	if (Memory::IsValidAddress(size))
		Memory::Write_U32(readsize, size);
	if (Memory::IsValidAddress(srcPos))
		Memory::Write_U32(readPos, srcPos);

	// preset the readPos and available size, they will be notified later in NotifyAddStreamData.
	askedReadSize = readsize;
	readPos += askedReadSize;
	AuBufAvailable += askedReadSize;

	return 0;
}

u32 AuCtx::AuResetPlayPositionByFrame(int position) {
	readPos = position;
	return 0;
}

u32 AuCtx::AuResetPlayPosition() {
	readPos = startPos;
	return 0;
}

void AuCtx::DoState(PointerWrap &p) {
	auto s = p.Section("AuContext", 0, 1);
	if (!s)
		return;

	p.Do(startPos);
	p.Do(endPos);
	p.Do(AuBuf);
	p.Do(AuBufSize);
	p.Do(PCMBuf);
	p.Do(PCMBufSize);
	p.Do(freq);
	p.Do(SumDecodedSamples);
	p.Do(LoopNum);
	p.Do(Channels);
	p.Do(MaxOutputSample);
	p.Do(readPos);
	p.Do(audioType);
	p.Do(BitRate);
	p.Do(SamplingRate);
	p.Do(askedReadSize);
	p.Do(realReadSize);
	p.Do(FrameNum);

	if (p.mode == p.MODE_READ) {
		decoder = new SimpleAudio(audioType);
		AuBufAvailable = 0; // reset to read from file at position readPos
	}
}
