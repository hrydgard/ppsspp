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

bool SimpleAudio::GetAudioCodecID(int audioType){
#ifdef USE_FFMPEG

	switch (audioType)
	{
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
	if (audioType != 0){
		return true;
	}
	return false;
#else
	return false;
#endif // USE_FFMPEG
}

SimpleAudio::SimpleAudio(int audioType)
: codec_(0), codecCtx_(0), swrCtx_(0), audioType(audioType), outSamples(0), wanted_resample_freq(44100){
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
	codec_ = avcodec_find_decoder(audioCodecId);
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
	codecCtx_->channels = 2;
	codecCtx_->channel_layout = AV_CH_LAYOUT_STEREO;
	codecCtx_->sample_rate = 44100;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		return;
	}

	av_dict_free(&opts);
#endif  // USE_FFMPEG
}


SimpleAudio::SimpleAudio(u32 ctxPtr, int audioType)
: codec_(0), codecCtx_(0), swrCtx_(0), ctxPtr(ctxPtr), audioType(audioType), outSamples(0), wanted_resample_freq(44100){
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
	codec_ = avcodec_find_decoder(audioCodecId);
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
	codecCtx_->channels = 2;
	codecCtx_->channel_layout = AV_CH_LAYOUT_STEREO;
	codecCtx_->sample_rate = 44100;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		av_dict_free(&opts);
		return;
	}

	av_dict_free(&opts);
#endif  // USE_FFMPEG
}

bool SimpleAudio::ResetCodecCtx(int channels, int samplerate){
#ifdef USE_FFMPEG
	if (codecCtx_)
		avcodec_close(codecCtx_);

	// Find decoder
	codec_ = avcodec_find_decoder(audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ctx for audio (%s). Update your submodule.", GetCodecName(audioType));
		return false;
	}

	codecCtx_->channels = channels;
	codecCtx_->channel_layout = channels==2?AV_CH_LAYOUT_STEREO:AV_CH_LAYOUT_MONO;
	codecCtx_->sample_rate = samplerate;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		av_dict_free(&opts);
		return false;
	}
	av_dict_free(&opts);
	return true;
#endif
	return false;
}

SimpleAudio::~SimpleAudio() {
#ifdef USE_FFMPEG
	if (frame_)
		av_frame_free(&frame_);
	if (codecCtx_)
		avcodec_close(codecCtx_);
	frame_ = 0;
	codecCtx_ = 0;
	codec_ = 0;
#endif  // USE_FFMPEG
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
		ERROR_LOG(ME, "Error decoding Audio frame");
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
		// convert audio to AV_SAMPLE_FMT_S16
		int swrRet = swr_convert(swrCtx_, &outbuf, frame_->nb_samples, (const u8 **)frame_->extended_data, frame_->nb_samples);
		if (swrRet < 0) {
			ERROR_LOG(ME, "swr_convert: Error while converting %d", swrRet);
			return false;
		}
		swr_free(&swrCtx_);
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

int SimpleAudio::getOutSamples(){
	return outSamples;
}

int SimpleAudio::getSourcePos(){
	return srcPos;
}

void SimpleAudio::setResampleFrequency(int freq){
	wanted_resample_freq = freq;
}

void AudioClose(SimpleAudio **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}

bool isValidCodec(int codec){
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return true;
	}
	return false;
}


// sceAu module starts from here

// return output pcm size, <0 error
u32 AuCtx::sceAuDecode(u32 pcmAddr)
{
	if (!Memory::IsValidAddress(pcmAddr)){
		ERROR_LOG(ME, "%s: output bufferAddress %08x is invalctx", __FUNCTION__, pcmAddr);
		return -1;
	}

	auto inbuff = Memory::GetPointer(AuBuf);
	auto outbuf = Memory::GetPointer(PCMBuf);
	u32 outpcmbufsize = 0;

	// move inbuff to writePos of buffer
	inbuff += writePos;

	// decode frames in AuBuf and output into PCMBuf if it is not exceed
	if (AuBufAvailable > 0 && outpcmbufsize < PCMBufSize){
		int pcmframesize;
		// decode
		decoder->Decode(inbuff, AuBufAvailable, outbuf, &pcmframesize);
		if (pcmframesize == 0){
			// no output pcm, we have either no data or no enough data to decode
			// move back audio source readPos to the begin of the last incomplete frame if we not start looping and reset available AuBuf
			if (readPos > startPos) { // this means we are not begin to loop yet
				readPos -= AuBufAvailable;
			}
			AuBufAvailable = 0;
		}
		// count total output pcm size 
		outpcmbufsize += pcmframesize;
		// count total output samples
		SumDecodedSamples += decoder->getOutSamples();
		// move inbuff position to next frame
		int srcPos = decoder->getSourcePos();
		inbuff += srcPos;
		// decrease available AuBuf
		AuBufAvailable -= srcPos;
		// modify the writePos value
		writePos += srcPos;
		// move outbuff position to the current end of output 
		outbuf += pcmframesize;
	}

	Memory::Write_U32(PCMBuf, pcmAddr);

	// if we got zero pcm, and we still haven't reach endPos. 
	// some game like "Miku" will stop playing if we return 0, but some others will recharge buffer.
	// so we did a hack here, clear output buff and just return a nonzero value to continue 
	if (outpcmbufsize == 0 && readPos < endPos){
		// clear output buffer will avoid noise
		memset(outbuf, 0, PCMBufSize);
		return FF_INPUT_BUFFER_PADDING_SIZE; // return a padding size seems very good and almost unsensible latency.
	}

	return outpcmbufsize;
}

u32 AuCtx::sceAuGetLoopNum()
{
	return LoopNum;
}

u32 AuCtx::sceAuSetLoopNum(int loop)
{
	LoopNum = loop;
	return 0;
}

// return 1 to read more data stream, 0 don't read
int AuCtx::sceAuCheckStreamDataNeeded()
{
	// if we have no available Au buffer, and the current read position in source file is not the end of stream, then we can read
	if (AuBufAvailable == 0 && readPos < endPos){
		return 1;
	}
	return 0;
}

// check how many bytes we have read from source file
u32 AuCtx::sceAuNotifyAddStreamData(int size)
{
	readPos += size;
	AuBufAvailable += size;
	writePos = 0;

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
u32 AuCtx::sceAuGetInfoToAddStreamData(u32 buff, u32 size, u32 srcPos)
{
	// we can recharge AuBuf from its begining
	if (Memory::IsValidAddress(buff))
		Memory::Write_U32(AuBuf, buff);
	if (Memory::IsValidAddress(size))
		Memory::Write_U32(AuBufSize, size);
	if (Memory::IsValidAddress(srcPos))
		Memory::Write_U32(readPos, srcPos);

	return 0;
}

u32 AuCtx::sceAuGetMaxOutputSample()
{
	return MaxOutputSample;
}

u32 AuCtx::sceAuGetSumDecodedSample()
{
	return SumDecodedSamples;
}

u32 AuCtx::sceAuResetPlayPosition()
{
	readPos = startPos;
	return 0;
}

int AuCtx::sceAuGetChannelNum(){
	return Channels;
}

int AuCtx::sceAuGetBitRate(){
	return BitRate;
}

int AuCtx::sceAuGetSamplingRate(){
	return SamplingRate;
}

u32 AuCtx::sceAuResetPlayPositionByFrame(int position){
	readPos = position;
	return 0;
}

int AuCtx::sceAuGetVersion(){
	return Version;
}
